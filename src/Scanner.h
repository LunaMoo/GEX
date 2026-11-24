// GEX - Graphical Explorer
// Multithreaded directory scanner (FindFirstFileExW with large fetch, includes hidden/system files).
#pragma once
#include "FileTree.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gex
{
    class Scanner
    {
    public:
        Scanner() = default;
        ~Scanner();
        Scanner(const Scanner&) = delete;
        Scanner& operator=(const Scanner&) = delete;

        // threads <= 0 : choose automatically (fewer threads on spinning disks)
        bool Start(const std::wstring& rootPath, int threads = 0);
        void Cancel();                        // stops; partial results are still finalized and returned
        bool IsRunning() const { return m_running.load(); }
        bool HasResult() const { return m_done.load() && m_result != nullptr; }
        std::unique_ptr<FileTree> TakeResult(); // joins threads; valid once HasResult()
        const std::wstring& Root() const { return m_root; }
        void Wait() { Join(); }                // block until the scan threads have finished

        struct Progress
        {
            uint64_t files = 0, dirs = 0, bytes = 0, errors = 0;
            double   seconds = 0.0;
            int      threads = 0;
            bool     finalizing = false;
            std::wstring current;
        };
        Progress GetProgress();

        static int  SuggestThreads(const std::wstring& rootPath);
        static bool IsSpinningDisk(const std::wstring& rootPath);

    private:
        void Coordinator();
        void Worker(NodeArena* arena);
        void ScanDirectory(Node* dir, NodeArena* arena, std::vector<Node*>& subdirs);
        void Join();

        std::wstring                m_root;
        std::unique_ptr<FileTree>   m_tree;
        std::unique_ptr<FileTree>   m_result;
        int                         m_threadCount = 1;
        std::thread                 m_coordinator;

        std::mutex                  m_qmutex;
        std::condition_variable     m_qcv;
        std::vector<Node*>          m_queue;
        int64_t                     m_pending = 0;

        std::atomic<bool>           m_cancel{ false };
        std::atomic<bool>           m_running{ false };
        std::atomic<bool>           m_done{ false };
        std::atomic<bool>           m_finalizing{ false };
        std::atomic<uint64_t>       m_files{ 0 }, m_dirs{ 0 }, m_bytes{ 0 }, m_errors{ 0 };
        uint64_t                    m_startTicks = 0, m_endTicks = 0;

        std::mutex                  m_curMutex;
        std::wstring                m_current;
    };
}
