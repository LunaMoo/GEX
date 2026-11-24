// GEX - Graphical Explorer
// Real-time file system change monitor (ReadDirectoryChangesW, recursive).
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gex
{
    enum FsAction : uint32_t
    {
        Fs_Added = 1,        // == FILE_ACTION_ADDED
        Fs_Removed = 2,
        Fs_Modified = 3,
        Fs_RenamedOld = 4,
        Fs_RenamedNew = 5,
    };
    const char* FsActionName(uint32_t a);

    struct FsEvent
    {
        std::wstring path;
        uint32_t action = 0;
        uint64_t time = 0;       // FILETIME when observed
        uint64_t size = 0;       // after the change (if it still exists)
        uint64_t mtime = 0;
        uint32_t attrs = 0;
        bool     exists = false;
        bool     isDir = false;
    };

    class Watcher
    {
    public:
        ~Watcher();
        bool Start(const std::wstring& rootPath);
        void Stop();
        bool IsRunning() const { return m_running.load(); }
        const std::wstring& Root() const { return m_root; }

        // Move up to 'max' queued events into 'out'. Returns number moved.
        size_t Drain(std::vector<FsEvent>& out, size_t max = 100000);
        size_t Queued();
        uint64_t Overflows() const { return m_overflows.load(); }
        uint64_t Dropped() const { return m_dropped.load(); }
        uint64_t TotalEvents() const { return m_total.load(); }
        std::string LastError();

    private:
        void Thread();
        void SetError(const std::string& e);

        std::wstring        m_root;
        std::thread         m_thread;
        void*               m_stopEvent = nullptr;  // HANDLE
        void*               m_dirHandle = nullptr;  // HANDLE
        std::atomic<bool>   m_running{ false };
        std::atomic<uint64_t> m_overflows{ 0 }, m_dropped{ 0 }, m_total{ 0 };
        std::mutex          m_mutex;
        std::deque<FsEvent> m_queue;
        std::string         m_error;
        static constexpr size_t kMaxQueue = 500000;
    };
}
