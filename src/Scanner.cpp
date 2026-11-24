// GEX - Graphical Explorer
#include "Scanner.h"
#include "Util.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <algorithm>

namespace gex
{
    static uint64_t Ticks() { return ::GetTickCount64(); }

    Scanner::~Scanner()
    {
        Cancel();
        Join();
    }

    void Scanner::Join()
    {
        if (m_coordinator.joinable()) m_coordinator.join();
    }

    bool Scanner::IsSpinningDisk(const std::wstring& rootPath)
    {
        // Query the volume's seek penalty (true = HDD). Works without admin rights.
        std::wstring p = NormalizeDirPath(rootPath);
        if (p.size() < 2 || p[1] != L':') return false; // network paths etc.
        std::wstring vol = L"\\\\.\\";
        vol += p[0];
        vol += L':';
        HANDLE h = ::CreateFileW(vol.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        STORAGE_PROPERTY_QUERY q{};
        q.PropertyId = StorageDeviceSeekPenaltyProperty;
        q.QueryType = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR d{};
        DWORD bytes = 0;
        BOOL ok = ::DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &d, sizeof(d), &bytes, nullptr);
        ::CloseHandle(h);
        return ok && bytes >= sizeof(d) && d.IncursSeekPenalty;
    }

    int Scanner::SuggestThreads(const std::wstring& rootPath)
    {
        int hw = (int)std::thread::hardware_concurrency();
        if (hw <= 0) hw = 4;
        if (IsSpinningDisk(rootPath)) return 3;            // a few threads let NTFS/the disk queue reorder seeks
        return std::clamp(hw * 2, 4, 24);                  // SSD/NVMe: metadata is cached, more parallelism helps
    }

    bool Scanner::Start(const std::wstring& rootPath, int threads)
    {
        if (m_running) return false;
        Join();
        m_root = NormalizeDirPath(rootPath);
        const DWORD attr = ::GetFileAttributesW(m_root.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return false;

        m_tree.reset(new FileTree(m_root));
        m_result.reset();
        m_threadCount = threads > 0 ? threads : SuggestThreads(m_root);
        m_cancel = false; m_done = false; m_finalizing = false;
        m_files = 0; m_dirs = 0; m_bytes = 0; m_errors = 0;
        m_queue.clear();
        m_queue.push_back(m_tree->root);
        m_pending = 1;
        m_startTicks = Ticks(); m_endTicks = 0;
        m_running = true;
        m_coordinator = std::thread(&Scanner::Coordinator, this);
        return true;
    }

    void Scanner::Cancel()
    {
        if (!m_running) return;
        m_cancel = true;
        std::lock_guard<std::mutex> lk(m_qmutex);
        m_qcv.notify_all();
    }

    std::unique_ptr<FileTree> Scanner::TakeResult()
    {
        if (!m_done) return nullptr;
        Join();
        return std::move(m_result);
    }

    Scanner::Progress Scanner::GetProgress()
    {
        Progress p;
        p.files = m_files; p.dirs = m_dirs; p.bytes = m_bytes; p.errors = m_errors;
        const uint64_t end = m_endTicks ? m_endTicks : Ticks();
        p.seconds = (double)(end - m_startTicks) / 1000.0;
        p.threads = m_threadCount;
        p.finalizing = m_finalizing;
        std::lock_guard<std::mutex> lk(m_curMutex);
        p.current = m_current;
        return p;
    }

    void Scanner::Coordinator()
    {
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::vector<std::thread> workers;
        std::vector<NodeArena*> arenas;
        for (int i = 0; i < m_threadCount; ++i) arenas.push_back(m_tree->NewArena());
        for (int i = 0; i < m_threadCount; ++i) workers.emplace_back(&Scanner::Worker, this, arenas[(size_t)i]);
        for (auto& t : workers) t.join();

        // Anything still queued was never enumerated (cancelled)
        for (Node* n : m_queue) n->flags |= NF_Partial;
        m_queue.clear();

        m_finalizing = true;
        m_tree->scanErrors = m_errors;
        m_tree->partial = m_cancel.load();
        m_tree->Finalize();
        m_endTicks = Ticks();
        m_tree->scanSeconds = (double)(m_endTicks - m_startTicks) / 1000.0;
        m_tree->scanTime = NowFileTime();
        m_result = std::move(m_tree);
        m_done = true;
        m_running = false;
    }

    void Scanner::Worker(NodeArena* arena)
    {
        std::vector<Node*> subdirs;
        subdirs.reserve(256);
        uint32_t counter = 0;
        for (;;)
        {
            Node* dir = nullptr;
            {
                std::unique_lock<std::mutex> lk(m_qmutex);
                m_qcv.wait(lk, [&] { return m_cancel || !m_queue.empty() || m_pending == 0; });
                if (m_cancel || (m_queue.empty() && m_pending == 0)) return;
                dir = m_queue.back();      // LIFO -> depth-first, keeps the queue small
                m_queue.pop_back();
            }
            subdirs.clear();
            ScanDirectory(dir, arena, subdirs);
            if ((++counter & 63) == 0)
            {
                std::wstring path = m_tree->FullPath(dir);
                std::lock_guard<std::mutex> lk(m_curMutex);
                m_current = std::move(path);
            }
            {
                std::lock_guard<std::mutex> lk(m_qmutex);
                m_queue.insert(m_queue.end(), subdirs.rbegin(), subdirs.rend());
                m_pending += (int64_t)subdirs.size() - 1;
                if (m_pending == 0 || subdirs.size() > 1) m_qcv.notify_all();
                else if (!subdirs.empty()) m_qcv.notify_one();
            }
        }
    }

    void Scanner::ScanDirectory(Node* dir, NodeArena* arena, std::vector<Node*>& subdirs)
    {
        const std::wstring path = m_tree->FullPath(dir);
        std::wstring pattern;
        if (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\')
            pattern = L"\\\\?\\UNC\\" + path.substr(2);   // \\server\share -> \\?\UNC\server\share
        else
            pattern = L"\\\\?\\" + path;                   // long path support
        if (pattern.back() != L'\\') pattern += L'\\';
        pattern += L'*';

        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE)
        {
            const DWORD err = ::GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
            {
                dir->flags |= NF_Error;
                ++m_errors;
            }
            return;
        }
        uint64_t localBytes = 0, localFiles = 0, localDirs = 0;
        do
        {
            const wchar_t* nm = fd.cFileName;
            if (nm[0] == L'.' && (nm[1] == 0 || (nm[1] == L'.' && nm[2] == 0))) continue;

            Node* n = arena->Alloc();
            n->parent = dir;
            n->name = ToUtf8(nm);
            n->attrs = fd.dwFileAttributes;
            n->mtime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                n->flags = NF_Dir;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    n->flags |= NF_Reparse;          // do not follow junctions/symlinks
                else
                    subdirs.push_back(n);
                ++localDirs;
            }
            else
            {
                n->size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                n->category = (uint8_t)ClassifyName(n->name);
                localBytes += n->size;
                ++localFiles;
            }
            dir->children.push_back(n);
        } while (!m_cancel && ::FindNextFileW(h, &fd));
        ::FindClose(h);
        m_files += localFiles;
        m_dirs += localDirs;
        m_bytes += localBytes;
    }
}
