// GEX - Graphical Explorer
#include "Watcher.h"
#include "Util.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gex
{
    const char* FsActionName(uint32_t a)
    {
        switch (a)
        {
        case Fs_Added:      return "Created";
        case Fs_Removed:    return "Deleted";
        case Fs_Modified:   return "Modified";
        case Fs_RenamedOld: return "Renamed (old)";
        case Fs_RenamedNew: return "Renamed";
        default:            return "?";
        }
    }

    Watcher::~Watcher() { Stop(); }

    void Watcher::SetError(const std::string& e)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_error = e;
    }

    std::string Watcher::LastError()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_error;
    }

    bool Watcher::Start(const std::wstring& rootPath)
    {
        Stop();
        m_root = NormalizeDirPath(rootPath);
        SetError("");
        HANDLE h = ::CreateFileW(m_root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            SetError("Cannot open folder for monitoring (error " + std::to_string(::GetLastError()) + ")");
            return false;
        }
        m_dirHandle = h;
        m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_overflows = 0; m_dropped = 0; m_total = 0;
        m_running = true;
        m_thread = std::thread(&Watcher::Thread, this);
        return true;
    }

    void Watcher::Stop()
    {
        if (m_stopEvent) ::SetEvent((HANDLE)m_stopEvent);
        if (m_thread.joinable()) m_thread.join();
        if (m_dirHandle) { ::CloseHandle((HANDLE)m_dirHandle); m_dirHandle = nullptr; }
        if (m_stopEvent) { ::CloseHandle((HANDLE)m_stopEvent); m_stopEvent = nullptr; }
        m_running = false;
    }

    size_t Watcher::Drain(std::vector<FsEvent>& out, size_t max)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        size_t n = 0;
        while (!m_queue.empty() && n < max)
        {
            out.push_back(std::move(m_queue.front()));
            m_queue.pop_front();
            ++n;
        }
        return n;
    }

    size_t Watcher::Queued()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_queue.size();
    }

    static void StatPath(FsEvent& ev)
    {
        WIN32_FILE_ATTRIBUTE_DATA d;
        std::wstring p = ev.path;
        if (p.size() >= MAX_PATH && p.compare(0, 4, L"\\\\?\\") != 0) p = L"\\\\?\\" + p;
        if (::GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d))
        {
            ev.exists = true;
            ev.attrs = d.dwFileAttributes;
            ev.isDir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            ev.size = ev.isDir ? 0 : (((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow);
            ev.mtime = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
        }
    }

    void Watcher::Thread()
    {
        // DWORD aligned buffer. 64KB is the limit for network shares.
        std::vector<DWORD> buffer(64 * 1024 / sizeof(DWORD));
        OVERLAPPED ov{};
        ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                             FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
        HANDLE dir = (HANDLE)m_dirHandle;
        HANDLE waits[2] = { ov.hEvent, (HANDLE)m_stopEvent };

        for (;;)
        {
            ::ResetEvent(ov.hEvent);
            if (!::ReadDirectoryChangesW(dir, buffer.data(), (DWORD)(buffer.size() * sizeof(DWORD)), TRUE, filter, nullptr, &ov, nullptr))
            {
                SetError("ReadDirectoryChangesW failed (error " + std::to_string(::GetLastError()) + ")");
                break;
            }
            const DWORD w = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0)
            {
                ::CancelIoEx(dir, &ov);
                DWORD dummy; ::GetOverlappedResult(dir, &ov, &dummy, TRUE);
                break;
            }
            DWORD bytes = 0;
            if (!::GetOverlappedResult(dir, &ov, &bytes, FALSE))
            {
                const DWORD err = ::GetLastError();
                if (err == ERROR_NOTIFY_ENUM_DIR) { ++m_overflows; continue; }
                SetError("Monitoring stopped (error " + std::to_string(err) + ")");
                break;
            }
            if (bytes == 0) { ++m_overflows; continue; } // buffer overflow: events were lost

            const uint64_t now = NowFileTime();
            std::vector<FsEvent> batch;
            const BYTE* p = (const BYTE*)buffer.data();
            for (;;)
            {
                const FILE_NOTIFY_INFORMATION* fni = (const FILE_NOTIFY_INFORMATION*)p;
                FsEvent ev;
                ev.action = fni->Action;
                ev.time = now;
                ev.path = JoinPath(m_root, std::wstring(fni->FileName, fni->FileNameLength / sizeof(WCHAR)));
                if (ev.action != Fs_Removed && ev.action != Fs_RenamedOld) StatPath(ev);
                batch.push_back(std::move(ev));
                if (!fni->NextEntryOffset) break;
                p += fni->NextEntryOffset;
            }
            m_total += batch.size();
            std::lock_guard<std::mutex> lk(m_mutex);
            for (auto& e : batch)
            {
                if (m_queue.size() >= kMaxQueue) { m_queue.pop_front(); ++m_dropped; }
                m_queue.push_back(std::move(e));
            }
        }
        ::CloseHandle(ov.hEvent);
        m_running = false;
    }
}
