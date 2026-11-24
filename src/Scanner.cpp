// GEX - Graphical Explorer
#include "Scanner.h"
#include "Util.h"
#include "MftReader.h"
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

    bool Scanner::Start(const std::wstring& rootPath, int threads, bool allowMft)
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
        m_allowMft = allowMft; m_mftActive = false; m_mftFraction = 0.0f;
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
        p.mft = m_mftActive;
        p.fraction = m_mftActive ? m_mftFraction.load() : -1.0f;
        std::lock_guard<std::mutex> lk(m_curMutex);
        p.current = m_current;
        return p;
    }

    void Scanner::Coordinator()
    {
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        if (m_allowMft && ScanMft())
        {
            m_finalizing = true;
            m_tree->scanErrors = 0;
            m_tree->partial = false;
            m_tree->usedMft = true;
            m_tree->Finalize();
            m_endTicks = Ticks();
            m_tree->scanSeconds = (double)(m_endTicks - m_startTicks) / 1000.0;
            m_tree->scanTime = NowFileTime();
            m_result = std::move(m_tree);
            m_done = true;
            m_running = false;
            return;
        }
        if (m_cancel)
        {
            // stopped while reading the MFT: there is nothing useful to show, keep the previous results
            m_endTicks = Ticks();
            m_result.reset();
            m_done = true;
            m_running = false;
            return;
        }
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

namespace gex
{
    // ---------------------------------------------------------------- NTFS MFT fast path
    bool Scanner::ScanMft()
    {
        // local drive letter paths only
        if (m_root.size() < 3 || m_root[1] != L':' || m_root[2] != L'\\') return false;
        if (!IsProcessElevated()) return false;
        const std::wstring driveRoot = m_root.substr(0, 3);
        wchar_t fsName[MAX_PATH + 1] = {};
        if (!::GetVolumeInformationW(driveRoot.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH) || _wcsicmp(fsName, L"NTFS") != 0)
            return false;
        const std::wstring volPath = std::wstring(L"\\\\.\\") + m_root[0] + L":";
        HANDLE vol = ::CreateFileW(volPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (vol == INVALID_HANDLE_VALUE) return false;

        NTFS_VOLUME_DATA_BUFFER nv{};
        DWORD got = 0;
        if (!::DeviceIoControl(vol, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &nv, sizeof(nv), &got, nullptr))
        {
            ::CloseHandle(vol);
            return false;
        }
        mft::VolumeInfo vi;
        vi.bytesPerSector = nv.BytesPerSector;
        vi.bytesPerCluster = nv.BytesPerCluster;
        vi.recordSize = nv.BytesPerFileRecordSegment;
        vi.mftStartLcn = (uint64_t)nv.MftStartLcn.QuadPart;
        vi.mftValidLength = (uint64_t)nv.MftValidDataLength.QuadPart;

        {
            std::lock_guard<std::mutex> lk(m_curMutex);
            m_current = L"Reading the NTFS master file table of " + driveRoot + L" ...";
        }
        m_mftActive = true;

        // Raw volume reads: page-aligned bounce buffer, sector-aligned offsets (the reader asks for whole clusters).
        const size_t bounceSize = 16u << 20;
        uint8_t* bounce = (uint8_t*)::VirtualAlloc(nullptr, bounceSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        mft::ReadFn read = [&](uint64_t offset, void* dst, uint32_t size) -> bool
        {
            uint8_t* out = (uint8_t*)dst;
            while (size > 0)
            {
                const DWORD n = (DWORD)std::min<size_t>(size, bounceSize);
                LARGE_INTEGER li; li.QuadPart = (LONGLONG)offset;
                DWORD rd = 0;
                if (!::SetFilePointerEx(vol, li, nullptr, FILE_BEGIN) || !::ReadFile(vol, bounce, n, &rd, nullptr) || rd != n) return false;
                memcpy(out, bounce, n);
                out += n; offset += n; size -= n;
            }
            return true;
        };

        // Build the whole volume; if a sub folder was requested, re-root the tree there afterwards.
        std::unique_ptr<FileTree> vt(new FileTree(driveRoot));
        mft::Counters c{ &m_files, &m_dirs, &m_bytes, &m_mftFraction, &m_cancel };
        std::string err;
        const bool ok = bounce && mft::BuildTree(vi, read, *vt, c, err);
        if (bounce) ::VirtualFree(bounce, 0, MEM_RELEASE);
        ::CloseHandle(vol);
        m_mftActive = false;
        if (!ok)
        {
            m_files = 0; m_dirs = 0; m_bytes = 0;
            return false;
        }
        if (!PathEquals(m_root, driveRoot))
        {
            Node* sub = vt->Find(m_root);
            if (!sub || !sub->IsDir()) return false;
            sub->parent = nullptr;
            sub->name = ToUtf8(m_root);
            vt->root = sub;
            vt->rootPath = m_root;
        }
        m_tree = std::move(vt);
        return true;
    }
}
