// GEX - Graphical Explorer
// In-memory file system tree produced by the Scanner and mutated by the UI
// (delete, rename, live monitor updates, folder rescans).
//
// Threading: a FileTree is built by the Scanner threads, then handed over to the
// UI thread which is the only one touching it afterwards.
#pragma once
#include "FileTypes.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gex
{
    enum NodeFlags : uint8_t
    {
        NF_Dir     = 1 << 0,
        NF_Reparse = 1 << 1,   // junction / symlink: not followed (avoids loops & double counting)
        NF_Error   = 1 << 2,   // could not enumerate (access denied, ...)
        NF_Removed = 1 << 3,   // detached from the tree; pointer stays valid until the tree dies
        NF_Partial = 1 << 4,   // scan was cancelled before this folder was enumerated
    };

    struct Node
    {
        Node*              parent = nullptr;
        std::vector<Node*> children;      // files and folders, sorted by size (desc) after Finalize()
        std::string        name;          // UTF-8
        uint64_t           size = 0;      // file size, or recursive total for folders
        uint64_t           mtime = 0;     // last write time (FILETIME, UTC)
        uint32_t           attrs = 0;     // FILE_ATTRIBUTE_*
        uint32_t           fileCount = 0; // folders: recursive number of files
        uint32_t           dirCount = 0;  // folders: recursive number of sub folders
        uint8_t            category = Cat_Other;
        uint8_t            flags = 0;

        bool IsDir() const     { return (flags & NF_Dir) != 0; }
        bool IsRemoved() const { return (flags & NF_Removed) != 0; }
        bool IsHidden() const  { return (attrs & 0x2) != 0; }  // FILE_ATTRIBUTE_HIDDEN
        bool IsSystem() const  { return (attrs & 0x4) != 0; }  // FILE_ATTRIBUTE_SYSTEM
        int  ColorCategory() const { return IsDir() ? (int)Cat_Folder : (int)category; }
    };

    // Simple block allocator: nodes never move, freed all at once with the tree.
    class NodeArena
    {
    public:
        Node* Alloc();
        size_t Count() const { return m_count; }
    private:
        static constexpr size_t kBlock = 8192;
        std::vector<std::unique_ptr<Node[]>> m_blocks;
        size_t m_used = kBlock;
        size_t m_count = 0;
    };

    class FileTree
    {
    public:
        FileTree(const std::wstring& rootPath);

        Node*               root = nullptr;
        std::wstring        rootPath;             // normalized, e.g. "C:\" or "D:\Games"
        uint64_t            version = 1;          // bumped on every structural / size change
        uint64_t            scanErrors = 0;
        double              scanSeconds = 0.0;
        uint64_t            scanTime = 0;         // FILETIME when scan finished
        bool                partial = false;      // scan was cancelled
        bool                usedMft = false;      // built from the NTFS master file table

        NodeArena* NewArena();                    // for scanner threads
        NodeArena* MainArena();                   // for UI thread insertions

        // Compute recursive totals, sort children, build flat lists. Called once by the scanner.
        void Finalize();

        std::wstring FullPath(const Node* n) const;
        std::string  FullPathUtf8(const Node* n) const;
        Node*        Find(const std::wstring& path) const;   // nullptr when not in tree
        bool         Contains(const std::wstring& path) const;

        // Mutations (UI thread)
        void  RemoveNode(Node* n);
        Node* AddFile(Node* dir, const std::string& name, uint64_t size, uint64_t mtime, uint32_t attrs);
        Node* AddDir(Node* dir, const std::string& name, uint64_t mtime, uint32_t attrs);
        void  UpdateFile(Node* n, uint64_t size, uint64_t mtime, uint32_t attrs);
        void  Rename(Node* n, const std::string& newName);
        // Replace the contents of 'dir' with a freshly scanned subtree of the same folder.
        void  Graft(Node* dir, std::unique_ptr<FileTree> sub);

        // Flat lists (sorted by size desc). Lazily re-sorted after mutations.
        const std::vector<Node*>& AllFiles();
        const std::vector<Node*>& AllDirs();
        size_t FileCount() const { return m_allFiles.size(); }

    private:
        void AdjustUp(Node* from, int64_t sizeDelta, int64_t fileDelta, int64_t dirDelta);
        void FixOrder(Node* n);                    // keep parent's children sorted after a size change
        void MarkRemovedRecursive(Node* n);
        void CompactLists();

        std::vector<std::unique_ptr<NodeArena>> m_arenas;
        NodeArena*         m_mainArena = nullptr;
        std::vector<Node*> m_allFiles;
        std::vector<Node*> m_allDirs;
        bool m_filesDirty = false;
        bool m_dirsDirty = false;
        bool m_needsCompact = false;
    };

    // Sort helper used everywhere: biggest first, then by name.
    bool NodeBiggerFirst(const Node* a, const Node* b);
}
