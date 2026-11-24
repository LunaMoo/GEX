// GEX - Graphical Explorer
#include "FileTree.h"
#include "Util.h"
#include <algorithm>

namespace gex
{
    bool NodeBiggerFirst(const Node* a, const Node* b)
    {
        if (a->size != b->size) return a->size > b->size;
        return a->name < b->name;
    }

    Node* NodeArena::Alloc()
    {
        if (m_used >= kBlock)
        {
            m_blocks.emplace_back(new Node[kBlock]);
            m_used = 0;
        }
        ++m_count;
        return &m_blocks.back()[m_used++];
    }

    FileTree::FileTree(const std::wstring& path)
    {
        rootPath = NormalizeDirPath(path);
        root = MainArena()->Alloc();
        root->flags = NF_Dir;
        root->name = ToUtf8(rootPath);
    }

    NodeArena* FileTree::NewArena()
    {
        m_arenas.emplace_back(new NodeArena());
        return m_arenas.back().get();
    }

    NodeArena* FileTree::MainArena()
    {
        if (!m_mainArena) m_mainArena = NewArena();
        return m_mainArena;
    }

    void FileTree::Finalize()
    {
        // Iterative post-order: compute totals, sort children.
        struct Frame { Node* n; size_t next; };
        std::vector<Frame> stack;
        stack.push_back({ root, 0 });
        m_allFiles.clear();
        m_allDirs.clear();
        while (!stack.empty())
        {
            Frame& f = stack.back();
            Node* n = f.n;
            if (f.next < n->children.size())
            {
                Node* c = n->children[f.next++];
                if (c->IsDir())
                {
                    c->size = 0; c->fileCount = 0; c->dirCount = 0;
                    stack.push_back({ c, 0 });
                }
                else
                    m_allFiles.push_back(c);
                continue;
            }
            // all children done
            uint64_t size = 0; uint32_t files = 0, dirs = 0;
            for (Node* c : n->children)
            {
                size += c->size;
                if (c->IsDir()) { files += c->fileCount; dirs += c->dirCount + 1; }
                else files += 1;
            }
            n->size = size; n->fileCount = files; n->dirCount = dirs;
            std::sort(n->children.begin(), n->children.end(), NodeBiggerFirst);
            if (n != root) m_allDirs.push_back(n);
            stack.pop_back();
        }
        std::sort(m_allFiles.begin(), m_allFiles.end(), NodeBiggerFirst);
        std::sort(m_allDirs.begin(), m_allDirs.end(), NodeBiggerFirst);
        m_filesDirty = m_dirsDirty = m_needsCompact = false;
        ++version;
    }

    std::wstring FileTree::FullPath(const Node* n) const
    {
        if (!n) return {};
        if (n == root || !n->parent) return rootPath;
        const Node* chain[512];
        int depth = 0;
        std::vector<const Node*> deep; // fallback for absurdly deep trees
        for (const Node* p = n; p && p != root; p = p->parent)
        {
            if (depth < 512) chain[depth++] = p; else deep.push_back(p);
        }
        std::string utf8;
        utf8.reserve(260);
        for (int i = (int)deep.size() - 1; i >= 0; --i) { utf8 += deep[(size_t)i]->name; utf8 += '\\'; }
        for (int i = depth - 1; i >= 0; --i) { utf8 += chain[i]->name; if (i) utf8 += '\\'; }
        // reorder: deep entries are further from n (closer to root) -> they come first, done above
        return JoinPath(rootPath, ToWide(utf8));
    }

    std::string FileTree::FullPathUtf8(const Node* n) const { return ToUtf8(FullPath(n)); }

    static bool NameEq(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    }

    Node* FileTree::Find(const std::wstring& pathIn) const
    {
        std::wstring path = NormalizeDirPath(pathIn);
        if (!PathStartsWith(path, rootPath)) return nullptr;
        if (path.size() == rootPath.size()) return root;
        size_t pos = rootPath.size();
        if (path[pos] == L'\\') ++pos;
        Node* cur = root;
        while (pos < path.size() && cur)
        {
            size_t end = path.find(L'\\', pos);
            if (end == std::wstring::npos) end = path.size();
            const std::string comp = ToUtf8(path.substr(pos, end - pos));
            Node* next = nullptr;
            for (Node* c : cur->children)
                if (NameEq(c->name, comp)) { next = c; break; }
            cur = next;
            pos = end + 1;
        }
        return cur;
    }

    bool FileTree::Contains(const std::wstring& path) const { return PathStartsWith(path, rootPath); }

    void FileTree::AdjustUp(Node* from, int64_t sizeDelta, int64_t fileDelta, int64_t dirDelta)
    {
        for (Node* p = from; p; p = p->parent)
        {
            p->size = (uint64_t)((int64_t)p->size + sizeDelta);
            p->fileCount = (uint32_t)((int64_t)p->fileCount + fileDelta);
            p->dirCount = (uint32_t)((int64_t)p->dirCount + dirDelta);
            if (sizeDelta != 0) FixOrder(p);
        }
        if (sizeDelta != 0) m_dirsDirty = true;
        ++version;
    }

    void FileTree::FixOrder(Node* n)
    {
        Node* parent = n->parent;
        if (!parent) return;
        auto& v = parent->children;
        auto it = std::find(v.begin(), v.end(), n);
        if (it == v.end()) return;
        size_t i = (size_t)(it - v.begin());
        while (i > 0 && NodeBiggerFirst(v[i], v[i - 1])) { std::swap(v[i], v[i - 1]); --i; }
        while (i + 1 < v.size() && NodeBiggerFirst(v[i + 1], v[i])) { std::swap(v[i], v[i + 1]); ++i; }
    }

    void FileTree::MarkRemovedRecursive(Node* n)
    {
        std::vector<Node*> stack{ n };
        while (!stack.empty())
        {
            Node* c = stack.back(); stack.pop_back();
            c->flags |= NF_Removed;
            for (Node* k : c->children) stack.push_back(k);
        }
        m_needsCompact = true;
    }

    void FileTree::RemoveNode(Node* n)
    {
        if (!n || n == root || n->IsRemoved()) return;
        Node* parent = n->parent;
        auto& v = parent->children;
        v.erase(std::remove(v.begin(), v.end(), n), v.end());
        const int64_t files = n->IsDir() ? (int64_t)n->fileCount : 1;
        const int64_t dirs = n->IsDir() ? (int64_t)n->dirCount + 1 : 0;
        MarkRemovedRecursive(n);
        AdjustUp(parent, -(int64_t)n->size, -files, -dirs);
    }

    Node* FileTree::AddFile(Node* dir, const std::string& name, uint64_t size, uint64_t mtime, uint32_t attrs)
    {
        if (!dir || !dir->IsDir()) return nullptr;
        Node* n = MainArena()->Alloc();
        n->parent = dir; n->name = name; n->size = 0; n->mtime = mtime; n->attrs = attrs;
        n->category = (uint8_t)ClassifyName(name);
        dir->children.push_back(n);
        m_allFiles.push_back(n);
        m_filesDirty = true;
        AdjustUp(dir, 0, 1, 0);
        UpdateFile(n, size, mtime, attrs);
        return n;
    }

    Node* FileTree::AddDir(Node* dir, const std::string& name, uint64_t mtime, uint32_t attrs)
    {
        if (!dir || !dir->IsDir()) return nullptr;
        Node* n = MainArena()->Alloc();
        n->parent = dir; n->name = name; n->mtime = mtime; n->attrs = attrs; n->flags = NF_Dir;
        dir->children.push_back(n);
        m_allDirs.push_back(n);
        m_dirsDirty = true;
        AdjustUp(dir, 0, 0, 1);
        FixOrder(n);
        return n;
    }

    void FileTree::UpdateFile(Node* n, uint64_t size, uint64_t mtime, uint32_t attrs)
    {
        if (!n || n->IsDir() || n->IsRemoved()) return;
        const int64_t delta = (int64_t)size - (int64_t)n->size;
        n->size = size; n->mtime = mtime; n->attrs = attrs;
        if (delta != 0)
        {
            FixOrder(n);
            m_filesDirty = true;
            AdjustUp(n->parent, delta, 0, 0);
        }
        else
            ++version;
    }

    void FileTree::Rename(Node* n, const std::string& newName)
    {
        if (!n || n == root) return;
        n->name = newName;
        if (!n->IsDir()) n->category = (uint8_t)ClassifyName(newName);
        ++version;
    }

    void FileTree::Graft(Node* dir, std::unique_ptr<FileTree> sub)
    {
        if (!dir || !sub || !dir->IsDir() || dir->IsRemoved()) return;
        // Detach old contents
        for (Node* c : dir->children) MarkRemovedRecursive(c);
        const int64_t sizeDelta = (int64_t)sub->root->size - (int64_t)dir->size;
        const int64_t fileDelta = (int64_t)sub->root->fileCount - (int64_t)dir->fileCount;
        const int64_t dirDelta = (int64_t)sub->root->dirCount - (int64_t)dir->dirCount;

        dir->children = std::move(sub->root->children);
        for (Node* c : dir->children) c->parent = dir;
        dir->size = sub->root->size;
        dir->fileCount = sub->root->fileCount;
        dir->dirCount = sub->root->dirCount;
        dir->flags = (uint8_t)((dir->flags & ~(NF_Error | NF_Partial)) | (sub->root->flags & (NF_Error | NF_Partial)));
        scanErrors += sub->scanErrors;

        m_allFiles.insert(m_allFiles.end(), sub->m_allFiles.begin(), sub->m_allFiles.end());
        m_allDirs.insert(m_allDirs.end(), sub->m_allDirs.begin(), sub->m_allDirs.end());
        for (auto& a : sub->m_arenas) m_arenas.push_back(std::move(a));
        sub->m_arenas.clear();
        sub->m_mainArena = nullptr;
        m_filesDirty = m_dirsDirty = true;

        FixOrder(dir);
        AdjustUp(dir->parent, sizeDelta, fileDelta, dirDelta);
        ++version;
    }

    void FileTree::CompactLists()
    {
        if (!m_needsCompact) return;
        auto removed = [](const Node* n) { return n->IsRemoved(); };
        m_allFiles.erase(std::remove_if(m_allFiles.begin(), m_allFiles.end(), removed), m_allFiles.end());
        m_allDirs.erase(std::remove_if(m_allDirs.begin(), m_allDirs.end(), removed), m_allDirs.end());
        m_needsCompact = false;
    }

    const std::vector<Node*>& FileTree::AllFiles()
    {
        CompactLists();
        if (m_filesDirty) { std::sort(m_allFiles.begin(), m_allFiles.end(), NodeBiggerFirst); m_filesDirty = false; }
        return m_allFiles;
    }

    const std::vector<Node*>& FileTree::AllDirs()
    {
        CompactLists();
        if (m_dirsDirty) { std::sort(m_allDirs.begin(), m_allDirs.end(), NodeBiggerFirst); m_dirsDirty = false; }
        return m_allDirs;
    }
}
