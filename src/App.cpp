// GEX - Graphical Explorer
// Core application logic: scanning, navigation, selection, file actions, settings.
#include "App.h"
#include "IconCache.h"
#include "Icons.h"
#include "Util.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <cwctype>

namespace gex
{
    // ===================================================================== lifecycle
    void App::Init(void* hwnd, const std::wstring& startPath)
    {
        m_hwnd = hwnd;
        shell::Init(hwnd);
        m_elevated = IsProcessElevated();
        const std::wstring dir = AppDataDirectory();
        m_settingsPath = JoinPath(dir, L"gex_settings.ini");
        m_iniPath = ToUtf8(JoinPath(dir, L"gex_layout.ini"));
        ImGui::GetIO().IniFilename = m_iniPath.c_str();
        LoadSettings();
        SetupStyle();
        m_drives = shell::ListDrives();
        m_drivesRefreshed = 0.0;
        m_tmMain.showLabels = m_tmExplorer.showLabels = m_set.treemapLabels;
        m_tmMain.cushion = m_tmExplorer.cushion = m_set.treemapCushion;
        snprintf(m_liveExcludeBuf, sizeof(m_liveExcludeBuf), "%s", m_set.liveExclude.c_str());
        shell::RegisterDropTarget(this);
        if (!startPath.empty()) StartScan(startPath);
    }

    void App::Shutdown()
    {
        shell::RevokeDropTarget();
        m_set.liveExclude = m_liveExcludeBuf;
        SaveSettings();
        m_watcher.Stop();
        if (m_rbFuture.valid()) m_rbFuture.wait();
        m_scanner.Cancel();
        m_subScanner.Cancel();
        m_scanner.Wait();
        m_subScanner.Wait();
    }

    bool App::NeedsFastRefresh() const
    {
        return m_scanner.IsRunning() || m_subScanner.IsRunning() || !m_subQueue.empty() || m_deferred != Deferred::None || m_boxSelecting || m_extDrag || !m_pendingDrops.empty();
    }

    bool App::NeedsSlowRefresh() const
    {
        return m_watcher.IsRunning() || !m_scheduled.empty();
    }

    // ===================================================================== settings
    void App::LoadSettings()
    {
        FILE* f = _wfopen(m_settingsPath.c_str(), L"rb");
        if (!f) return;
        char line[4096];
        while (fgets(line, sizeof(line), f))
        {
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            std::string key = line, val = eq + 1;
            while (!val.empty() && (val.back() == '\n' || val.back() == '\r')) val.pop_back();
            auto b = [&](bool& v) { v = val == "1"; };
            if (key == "showHidden") b(m_set.showHidden);
            else if (key == "showSystem") b(m_set.showSystem);
            else if (key == "threads") m_set.threads = atoi(val.c_str());
            else if (key == "nativeMenu") b(m_set.nativeMenuByDefault);
            else if (key == "treemapInExplorer") b(m_set.treemapInExplorer);
            else if (key == "treemapSplit") m_set.treemapSplit = (float)atof(val.c_str());
            else if (key == "treemapLabels") b(m_set.treemapLabels);
            else if (key == "treemapCushion") b(m_set.treemapCushion);
            else if (key == "liveUpdateTree") b(m_set.liveUpdateTree);
            else if (key == "liveFilesOnly") b(m_set.liveFilesOnly);
            else if (key == "recentWindow") m_set.recentWindow = atoi(val.c_str());
            else if (key == "lastPath") m_set.lastPath = ToWide(val);
            else if (key == "liveExclude") m_set.liveExclude = val;
            else if (key == "fastNtfs") b(m_set.fastNtfs);
            else if (key == "treeStyle") m_set.treeStyle = atoi(val.c_str()) == 1 ? 1 : 0;
            else if (key == "treeSizesLeft") b(m_set.treeSizesFirst);
            else if (key.rfind("cat", 0) == 0 || key == "folderColor")
            {
                float r, g, bl;
                if (sscanf(val.c_str(), "%f,%f,%f", &r, &g, &bl) == 3)
                {
                    float* c = key == "folderColor" ? FolderColor() : GetCategoryInfo(atoi(key.c_str() + 3)).color;
                    c[0] = r; c[1] = g; c[2] = bl;
                }
            }
        }
        fclose(f);
        m_set.treemapSplit = std::clamp(m_set.treemapSplit, 0.1f, 0.9f);
    }

    void App::SaveSettings()
    {
        FILE* f = _wfopen(m_settingsPath.c_str(), L"wb");
        if (!f) return;
        fprintf(f, "showHidden=%d\nshowSystem=%d\nthreads=%d\nnativeMenu=%d\ntreemapInExplorer=%d\ntreemapSplit=%.3f\n",
            m_set.showHidden, m_set.showSystem, m_set.threads, m_set.nativeMenuByDefault, m_set.treemapInExplorer, m_set.treemapSplit);
        fprintf(f, "treemapLabels=%d\ntreemapCushion=%d\nliveUpdateTree=%d\nliveFilesOnly=%d\nrecentWindow=%d\n",
            m_set.treemapLabels, m_set.treemapCushion, m_set.liveUpdateTree, m_set.liveFilesOnly, m_set.recentWindow);
        fprintf(f, "lastPath=%s\nliveExclude=%s\n", ToUtf8(m_set.lastPath).c_str(), m_set.liveExclude.c_str());
        fprintf(f, "treeStyle=%d\ntreeSizesLeft=%d\nfastNtfs=%d\n", m_set.treeStyle, m_set.treeSizesFirst, m_set.fastNtfs);
        for (int i = 0; i < Cat_COUNT; ++i)
        {
            const float* c = GetCategoryInfo(i).color;
            fprintf(f, "cat%d=%.3f,%.3f,%.3f\n", i, c[0], c[1], c[2]);
        }
        const float* fc = FolderColor();
        fprintf(f, "folderColor=%.3f,%.3f,%.3f\n", fc[0], fc[1], fc[2]);
        fclose(f);
    }

    // ===================================================================== scanning
    void App::StartScan(const std::wstring& pathIn)
    {
        const std::wstring path = NormalizeDirPath(pathIn);
        m_scanner.Cancel();
        m_scanner.Wait();
        m_scanner.TakeResult();
        if (!m_scanner.Start(path, m_set.threads, m_set.fastNtfs))
        {
            m_errorText = "Cannot open \"" + ToUtf8(path) + "\".\nThe folder does not exist or access was denied.";
            m_openError = true;
            return;
        }
        m_set.lastPath = path;
    }

    void App::PollScanner()
    {
        if (!m_scanner.HasResult()) return;
        std::unique_ptr<FileTree> tree = m_scanner.TakeResult();
        if (!tree) return;
        // Remember where the user was, to restore it in the new tree
        std::wstring curPath = (m_tree && m_cur) ? m_tree->FullPath(m_cur) : std::wstring();
        m_tree = std::move(tree);
        m_back.clear(); m_fwd.clear();
        ClearSelection();
        m_cur = m_tree->root;
        if (!curPath.empty())
            if (Node* n = m_tree->Find(curPath)) if (n->IsDir()) m_cur = n;
        m_tmRoot = m_cur;
        m_tmHover = nullptr; m_reveal = nullptr; m_ctxBgDir = nullptr;
        m_treeOpen.clear();
        m_tmMain.Invalidate(); m_tmExplorer.Invalidate();
        m_explorerList = ListState(); m_topList = ListState(); m_typeFilesList = ListState(); m_recentScanList = ListState();
        m_typeStatsKey = ~0ull;
        m_syncTreeToCur = true;
        m_subQueue.clear();
        m_drivesRefreshed = -100.0;
        if (m_watchPathBuf[0] == 0) snprintf(m_watchPathBuf, sizeof(m_watchPathBuf), "%s", ToUtf8(m_tree->rootPath).c_str());
    }

    void App::QueueSubtreeScan(const std::wstring& path)
    {
        for (auto& q : m_subQueue) if (PathEquals(q, path)) return;
        if (!m_subPath.empty() && PathEquals(m_subPath, path) && m_subScanner.IsRunning()) return;
        m_subQueue.push_back(path);
    }

    void App::PollSubScans()
    {
        if (m_subScanner.HasResult())
        {
            std::unique_ptr<FileTree> sub = m_subScanner.TakeResult();
            if (m_tree && sub)
                if (Node* n = m_tree->Find(m_subPath))
                    if (n->IsDir()) m_tree->Graft(n, std::move(sub));
            m_subPath.clear();
        }
        if (!m_subScanner.IsRunning() && !m_subScanner.HasResult() && !m_subQueue.empty() && m_tree)
        {
            m_subPath = m_subQueue.front();
            m_subQueue.pop_front();
            if (!m_tree->Find(m_subPath) || !m_subScanner.Start(m_subPath, m_set.threads)) m_subPath.clear();
        }
    }

    void App::ShallowRefresh(Node* dir)
    {
        if (!m_tree || !dir || !dir->IsDir() || dir->IsRemoved()) return;
        const std::wstring dirPath = m_tree->FullPath(dir);
        std::vector<shell::DirEntry> entries;
        if (!shell::ListDirectory(dirPath, entries))
        {
            if (!shell::Exists(dirPath)) m_tree->RemoveNode(dir);
            return;
        }
        std::unordered_map<std::string, Node*> existing;
        for (Node* c : dir->children) existing[ToLowerAscii(c->name)] = c;
        std::vector<Node*> keep;
        for (auto& e : entries)
        {
            const std::string name = ToUtf8(e.name);
            auto it = existing.find(ToLowerAscii(name));
            if (it != existing.end())
            {
                Node* n = it->second;
                existing.erase(it);
                if (n->IsDir() != e.st.isDir) m_tree->RemoveNode(n);  // file became folder or vice versa
                else
                {
                    if (n->name != name) m_tree->Rename(n, name);
                    if (!n->IsDir() && (n->size != e.st.size || n->mtime != e.st.mtime)) m_tree->UpdateFile(n, e.st.size, e.st.mtime, e.st.attrs);
                    if (n->IsDir()) { n->attrs = e.st.attrs; n->mtime = e.st.mtime; }
                    continue;
                }
            }
            if (e.st.isDir)
            {
                Node* n = m_tree->AddDir(dir, name, e.st.mtime, e.st.attrs);
                if (n && !(e.st.attrs & 0x400 /*FILE_ATTRIBUTE_REPARSE_POINT*/)) QueueSubtreeScan(JoinPath(dirPath, e.name));
                else if (n) n->flags |= NF_Reparse;
            }
            else
                m_tree->AddFile(dir, name, e.st.size, e.st.mtime, e.st.attrs);
        }
        for (auto& kv : existing) m_tree->RemoveNode(kv.second);  // vanished
        dir->flags &= (uint8_t)~NF_Error;
        PruneSelection();
    }

    void App::ValidatePaths(const std::vector<std::wstring>& paths)
    {
        if (!m_tree) return;
        for (auto& p : paths)
        {
            Node* n = m_tree->Find(p);
            if (!n) continue;
            shell::Stat st;
            if (!shell::GetStat(p, st)) m_tree->RemoveNode(n);
            else if (!n->IsDir()) m_tree->UpdateFile(n, st.size, st.mtime, st.attrs);
        }
        for (auto& r : m_recent)
            if (r.selected && !shell::Exists(r.path)) r.exists = false;
        PruneSelection();
    }

    void App::ScheduleRefresh(const std::wstring& dir, double delay)
    {
        m_scheduled.push_back({ dir, ImGui::GetTime() + delay });
    }

    void App::PollScheduledRefresh()
    {
        if (m_scheduled.empty() || !m_tree) { if (!m_tree) m_scheduled.clear(); return; }
        const double now = ImGui::GetTime();
        std::vector<std::wstring> due;
        for (size_t i = 0; i < m_scheduled.size();)
        {
            if (m_scheduled[i].at <= now) { due.push_back(m_scheduled[i].dir); m_scheduled.erase(m_scheduled.begin() + (ptrdiff_t)i); }
            else ++i;
        }
        for (auto& d : due)
            if (Node* n = m_tree->Find(d)) ShallowRefresh(n);
    }

    void App::AfterExternalChange(const std::vector<std::wstring>& paths)
    {
        if (!m_tree) return;
        ValidatePaths(paths);
        std::vector<std::wstring> parents;
        for (auto& p : paths)
        {
            std::wstring parent = ParentPath(p);
            bool dup = false;
            for (auto& q : parents) if (PathEquals(q, parent)) dup = true;
            if (!dup) parents.push_back(parent);
        }
        for (auto& p : parents)
        {
            if (Node* n = m_tree->Find(p)) ShallowRefresh(n);
            ScheduleRefresh(p, 1.5);   // shell verbs (extract, compress...) often run asynchronously
            ScheduleRefresh(p, 6.0);
        }
    }

    // ===================================================================== navigation / selection
    void App::Navigate(Node* dir, bool addHistory)
    {
        if (!dir || !dir->IsDir() || dir->IsRemoved() || dir == m_cur) return;
        if (addHistory && m_cur) { m_back.push_back(m_cur); m_fwd.clear(); }
        m_cur = dir;
        m_explorerList.focus = -1; m_explorerList.anchor = -1;
        m_filter[0] = 0;
        m_syncTreeToCur = true;
        m_tmRoot = dir;
        m_editingPath = false;
    }

    void App::GoBack()
    {
        while (!m_back.empty())
        {
            Node* n = m_back.back(); m_back.pop_back();
            if (n->IsRemoved()) continue;
            if (m_cur) m_fwd.push_back(m_cur);
            Navigate(n, false);
            return;
        }
    }

    void App::GoForward()
    {
        while (!m_fwd.empty())
        {
            Node* n = m_fwd.back(); m_fwd.pop_back();
            if (n->IsRemoved()) continue;
            if (m_cur) m_back.push_back(m_cur);
            Navigate(n, false);
            return;
        }
    }

    void App::GoUp()
    {
        if (m_cur && m_cur->parent)
        {
            Node* child = m_cur;
            Navigate(m_cur->parent);
            SelectOnly(child);
            m_reveal = child;
        }
    }

    void App::Activate(Node* n)
    {
        if (!n || n->IsRemoved()) return;
        if (n->IsDir())
        {
            if (n->flags & NF_Reparse) { shell::OpenItem(m_tree->FullPath(n)); return; }
            Navigate(n);
            m_requestTab = Tab_Explorer;
        }
        else
            shell::OpenItem(m_tree->FullPath(n));
    }

    void App::Reveal(Node* n)
    {
        if (!n || n->IsRemoved()) return;
        Navigate(n->IsDir() && n == m_tree->root ? n : n->parent);
        SelectOnly(n);
        m_reveal = n;
        m_requestTab = Tab_Explorer;
    }

    void App::SelectOnly(Node* n)
    {
        m_sel.clear(); m_selSet.clear();
        if (n) { m_sel.push_back(n); m_selSet.insert(n); }
    }

    void App::ToggleSelect(Node* n)
    {
        if (!n) return;
        if (m_selSet.count(n)) { m_selSet.erase(n); m_sel.erase(std::remove(m_sel.begin(), m_sel.end(), n), m_sel.end()); }
        else { m_selSet.insert(n); m_sel.push_back(n); }
    }

    void App::ClearSelection() { m_sel.clear(); m_selSet.clear(); }

    void App::SetSelection(const std::vector<Node*>& nodes)
    {
        m_sel = nodes;
        m_selSet.clear();
        m_selSet.insert(nodes.begin(), nodes.end());
    }

    void App::PruneSelection()
    {
        bool any = false;
        for (Node* n : m_sel) if (n->IsRemoved()) { any = true; break; }
        if (any)
        {
            std::vector<Node*> keep;
            for (Node* n : m_sel) if (!n->IsRemoved()) keep.push_back(n);
            SetSelection(keep);
        }
        if (m_cur && m_cur->IsRemoved())
        {
            Node* p = m_cur;
            while (p && p->IsRemoved()) p = p->parent;
            m_cur = p ? p : (m_tree ? m_tree->root : nullptr);
        }
        if (m_tmRoot && m_tmRoot->IsRemoved()) m_tmRoot = m_cur;
    }

    std::vector<std::wstring> App::PathsOf(const std::vector<Node*>& nodes) const
    {
        std::vector<std::wstring> out;
        if (!m_tree) return out;
        for (Node* n : nodes) if (!n->IsRemoved()) out.push_back(m_tree->FullPath(n));
        return out;
    }

    uint64_t App::SelectionSize() const
    {
        uint64_t s = 0;
        for (Node* n : m_sel) s += n->size;
        return s;
    }

    // ===================================================================== actions
    void App::OpenItemMenu(const std::vector<std::wstring>& paths, bool forceNative)
    {
        if (paths.empty()) return;
        m_ctxPaths = paths;
        if (forceNative || m_set.nativeMenuByDefault)
        {
            m_deferred = Deferred::NativeMenu;
            m_deferredPaths = paths;
            m_deferredExtended = shell::IsShiftDown();
        }
        else
            m_openItemCtx = true;
    }

    void App::OpenBackgroundMenu(Node* dir, bool forceNative)
    {
        if (!dir) return;
        m_ctxBgDir = dir;
        if (forceNative || m_set.nativeMenuByDefault)
        {
            m_deferred = Deferred::NativeBgMenu;
            m_deferredDir = m_tree->FullPath(dir);
            m_deferredExtended = shell::IsShiftDown();
        }
        else
            m_openBgCtx = true;
    }

    void App::RequestDelete(const std::vector<std::wstring>& paths, bool permanent)
    {
        if (paths.empty()) return;
        m_delPaths = paths;
        m_delPermanent = permanent;
        m_delSize = 0;
        for (auto& p : paths)
        {
            if (Node* n = m_tree ? m_tree->Find(p) : nullptr) { m_delSize += n->size; continue; }
            shell::Stat st;
            if (shell::GetStat(p, st)) m_delSize += st.size;
        }
        m_openDelete = true;
    }

    void App::RequestRename(const std::wstring& path)
    {
        m_renamePath = path;
        snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", ToUtf8(FileNameOf(path)).c_str());
        m_openRename = true;
    }

    void App::DoCopy(const std::vector<std::wstring>& paths, bool cut)
    {
        shell::SetClipboardFiles(paths, cut);
    }

    void App::DoPaste(Node* destDir)
    {
        if (!m_tree || !destDir || !destDir->IsDir()) return;
        std::vector<std::wstring> files;
        bool cut = false;
        if (!shell::GetClipboardFiles(files, cut)) return;
        const std::wstring dest = m_tree->FullPath(destDir);
        if (shell::CopyOrMoveItems(files, dest, cut))
        {
            if (cut) { shell::ClearClipboard(); ValidatePaths(files); AfterExternalChange(files); }
            ShallowRefresh(destDir);
            ScheduleRefresh(dest, 1.0);
        }
    }

    void App::DoNewFolder(Node* dir)
    {
        if (!m_tree || !dir) return;
        std::wstring created;
        if (shell::CreateNewFolder(m_tree->FullPath(dir), &created))
        {
            ShallowRefresh(dir);
            if (Node* n = m_tree->Find(created)) { SelectOnly(n); m_reveal = n; }
            RequestRename(created); // like Explorer: type the name right away
        }
        else
        {
            m_errorText = "Could not create a folder here (access denied?).";
            m_openError = true;
        }
    }

    void App::DoOpen(const std::vector<std::wstring>& paths)
    {
        if (paths.size() == 1 && m_tree)
            if (Node* n = m_tree->Find(paths[0])) { Activate(n); return; }
        for (size_t i = 0; i < paths.size() && i < 16; ++i) shell::OpenItem(paths[i]);
    }

    void App::ExportCsv(const std::vector<Node*>& rows)
    {
        if (!m_tree || rows.empty()) return;
        const std::wstring path = JoinPath(AppDataDirectory(), L"gex_export.csv");
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (!f) return;
        fputs("\xEF\xBB\xBF" "Path,Size (bytes),Size,Type,Modified\r\n", f);
        for (Node* n : rows)
        {
            std::string p = m_tree->FullPathUtf8(n);
            std::string q;
            for (char c : p) { if (c == '"') q += '"'; q += c; }
            fprintf(f, "\"%s\",%llu,%s,%s,%s\r\n", q.c_str(), (unsigned long long)n->size, FormatSize(n->size).c_str(),
                CategoryName(n->ColorCategory()), FormatFileTime(n->mtime).c_str());
        }
        fclose(f);
        shell::ShowInExplorer({ path });
    }

    // ===================================================================== live monitor
    void App::StartWatcher(const std::wstring& path)
    {
        m_watchPaused = false;
        if (!m_watcher.Start(path))
        {
            m_errorText = m_watcher.LastError();
            m_openError = true;
        }
    }

    void App::PollWatcher()
    {
        if (m_watchPaused) return;
        std::vector<FsEvent> events;
        m_watcher.Drain(events, 20000);
        if (events.empty()) return;

        std::vector<std::string> excludes;
        {
            std::string all = ToLowerAscii(m_liveExcludeBuf);
            size_t pos = 0;
            while (pos <= all.size())
            {
                size_t end = all.find(';', pos);
                if (end == std::string::npos) end = all.size();
                std::string token = all.substr(pos, end - pos);
                while (!token.empty() && token.front() == ' ') token.erase(token.begin());
                while (!token.empty() && token.back() == ' ') token.pop_back();
                if (!token.empty()) excludes.push_back(token);
                pos = end + 1;
            }
        }

        for (const FsEvent& ev : events)
        {
            if (m_set.liveUpdateTree && m_tree) ApplyEventToTree(ev);
            if (m_set.liveFilesOnly && ev.isDir) continue;
            const std::string pathUtf8 = ToUtf8(ev.path);
            bool excluded = false;
            for (auto& ex : excludes) if (ContainsNoCase(pathUtf8, ex)) { excluded = true; break; }
            if (excluded) continue;

            auto it = m_recentIndex.find(ev.path);
            RecentEntry* e = nullptr;
            if (it == m_recentIndex.end())
            {
                m_recentIndex[ev.path] = m_recent.size();
                m_recent.emplace_back();
                e = &m_recent.back();
                e->path = ev.path;
                e->nameUtf8 = ToUtf8(FileNameOf(ev.path));
                e->dirUtf8 = ToUtf8(ParentPath(ev.path));
                e->firstTime = ev.time;
                e->category = (uint8_t)ClassifyName(e->nameUtf8);
                e->prevSize = ev.size;
            }
            else
            {
                e = &m_recent[it->second];
                e->prevSize = e->size;
            }
            e->lastTime = ev.time;
            e->lastAction = ev.action;
            e->count++;
            if (ev.action == Fs_Removed || ev.action == Fs_RenamedOld) e->exists = false;
            else
            {
                e->exists = ev.exists;
                if (ev.exists) { e->size = ev.size; e->attrs = ev.attrs; e->isDir = ev.isDir; }
            }
        }
        // Keep the list bounded
        const size_t kMax = 60000;
        if (m_recent.size() > kMax)
        {
            std::sort(m_recent.begin(), m_recent.end(), [](const RecentEntry& a, const RecentEntry& b) { return a.lastTime > b.lastTime; });
            m_recent.resize(kMax / 2);
            m_recentIndex.clear();
            for (size_t i = 0; i < m_recent.size(); ++i) m_recentIndex[m_recent[i].path] = i;
            m_recentAnchor = -1;
        }
        m_recentDirty = true;
    }

    void App::ApplyEventToTree(const FsEvent& ev)
    {
        if (!m_tree->Contains(ev.path)) return;
        Node* n = m_tree->Find(ev.path);
        if (ev.action == Fs_Removed || ev.action == Fs_RenamedOld)
        {
            if (n && n != m_tree->root) { m_tree->RemoveNode(n); ++m_liveTreeUpdates; }
            return;
        }
        if (!ev.exists) return;
        if (n)
        {
            if (!n->IsDir() && !ev.isDir && (n->size != ev.size || n->mtime != ev.mtime))
            {
                m_tree->UpdateFile(n, ev.size, ev.mtime, ev.attrs);
                ++m_liveTreeUpdates;
            }
            return;
        }
        Node* parent = m_tree->Find(ParentPath(ev.path));
        if (!parent || !parent->IsDir()) return;
        const std::string name = ToUtf8(FileNameOf(ev.path));
        if (ev.isDir)
        {
            Node* d = m_tree->AddDir(parent, name, ev.mtime, ev.attrs);
            if (d && !(ev.attrs & 0x400)) QueueSubtreeScan(ev.path); // moved-in folders arrive as one event
        }
        else
            m_tree->AddFile(parent, name, ev.size, ev.mtime, ev.attrs);
        ++m_liveTreeUpdates;
    }

    // ===================================================================== deferred native UI
    void App::PostFrame()
    {
        const Deferred d = m_deferred;
        m_deferred = Deferred::None;
        ImGuiIO& io = ImGui::GetIO();
        switch (d)
        {
        case Deferred::None: break;
        case Deferred::NativeMenu:
        {
            int x, y; shell::GetCursorScreenPos(x, y);
            const shell::MenuResult r = shell::ShowNativeContextMenu(m_deferredPaths, x, y, m_deferredExtended);
            io.AddMouseButtonEvent(1, false);
            if (r == shell::MenuResult::Rename && !m_deferredPaths.empty()) RequestRename(m_deferredPaths[0]);
            else if (r == shell::MenuResult::Delete) RequestDelete(m_deferredPaths, shell::IsShiftDown());
            else if (r == shell::MenuResult::Invoked) AfterExternalChange(m_deferredPaths);
            break;
        }
        case Deferred::NativeBgMenu:
        {
            int x, y; shell::GetCursorScreenPos(x, y);
            const shell::MenuResult r = shell::ShowNativeBackgroundMenu(m_deferredDir, x, y, m_deferredExtended);
            io.AddMouseButtonEvent(1, false);
            if (r == shell::MenuResult::Invoked && m_tree)
            {
                if (Node* n = m_tree->Find(m_deferredDir)) ShallowRefresh(n);
                ScheduleRefresh(m_deferredDir, 1.0);
                ScheduleRefresh(m_deferredDir, 4.0);
            }
            break;
        }
        case Deferred::DragOut:
        {
            shell::DragOut(m_deferredPaths);
            io.AddMouseButtonEvent(0, false);
            AfterExternalChange(m_deferredPaths); // a move removes the sources
            break;
        }
        case Deferred::PickFolder:
        {
            std::wstring out;
            if (shell::PickFolder(out)) StartScan(out);
            break;
        }
        case Deferred::PickWatchFolder:
        {
            std::wstring out;
            if (shell::PickFolder(out)) snprintf(m_watchPathBuf, sizeof(m_watchPathBuf), "%s", ToUtf8(out).c_str());
            break;
        }
        case Deferred::Properties:
            shell::ShowProperties(m_deferredPaths);
            break;
        case Deferred::EmptyRecycleBin:
            shell::EmptyRecycleBin(m_deferredDir);
            io.AddMouseButtonEvent(0, false);
            AfterRecycleBinChanged();
            break;
        }
        RunPendingDrops();
    }

    // ===================================================================== frame
    void App::Frame()
    {
        icons::NewFrame();
        m_tmHover = nullptr;   // set again by the treemap while hovered
        m_dropZonesBuild.clear();
        PollScanner();
        PollSubScans();
        PollScheduledRefresh();
        PollWatcher();
        PollRecycleBin();
        PruneSelection();
        if (ImGui::GetTime() - m_drivesRefreshed > 10.0)
        {
            m_drives = shell::ListDrives();
            m_drivesRefreshed = ImGui::GetTime();
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_dragArmed = false;
        m_boxSelecting = false;   // set again by an active selection box this frame

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("##GEX", nullptr, flags);
        ImGui::PopStyleVar(2);

        DrawMenuBar();
        DrawToolbar();
        DrawDriveStrip();
        if (m_scanner.IsRunning()) DrawScanProgress();

        const float statusH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        if (!m_tree)
        {
            ImGui::BeginChild("##welcome", ImVec2(0, -statusH));
            DrawWelcome();
            ImGui::EndChild();
        }
        else
        {
            ImGui::BeginChild("##left", ImVec2(m_set.leftPaneWidth * m_uiScale, -statusH), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
            DrawFolderTree();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##right", ImVec2(0, -statusH));
            DrawTabs();
            ImGui::EndChild();
        }
        DrawStatusBar();
        HandleGlobalShortcuts();
        DrawPopups();
        DrawDropOverlay();
        ImGui::End();
        m_dropZones.swap(m_dropZonesBuild);   // used by OLE drag callbacks until the next frame
    }

    // ===================================================================== shortcuts
    void App::HandleGlobalShortcuts()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
        const bool text = io.WantTextInput;
        if ((io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle + 1)) GoBack();
        if ((io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle + 2)) GoForward();
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) GoUp();
        if (text) return;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) m_focusFilter = true;
        if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L)) || ImGui::IsKeyPressed(ImGuiKey_F4) || (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_D)))
        {
            if (m_tree && m_cur) { m_editingPath = true; snprintf(m_pathBuf, sizeof(m_pathBuf), "%s", m_tree->FullPathUtf8(m_cur).c_str()); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F5) && m_tree)
        {
            if (io.KeyCtrl) StartScan(m_tree->rootPath);
            else if (m_cur) QueueSubtreeScan(m_tree->FullPath(m_cur));
        }
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N) && m_cur) DoNewFolder(m_cur);
        if (io.KeyCtrl && !io.KeyShift)
        {
            for (int i = 0; i < 5; ++i) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i))) m_requestTab = i;
        }
    }

    void App::HandleListShortcuts(ListState& st, ListMode mode)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) return;
        const int n = (int)st.rows.size();

        // Keyboard movement
        int move = 0;
        const int page = std::max(1, (int)(ImGui::GetContentRegionAvail().y / ImGui::GetFrameHeightWithSpacing()) - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) move = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) move = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) move = page;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) move = -page;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) move = -1000000000;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) move = 1000000000;
        if (move != 0 && n > 0 && !io.KeyAlt)
        {
            int f = st.focus < 0 ? (move > 0 ? -1 : n) : st.focus;
            f = (int)std::clamp((long long)f + move, 0LL, (long long)n - 1);
            st.focus = f;
            st.scrollToFocus = true;
            if (io.KeyShift && st.anchor >= 0)
            {
                std::vector<Node*> range;
                const int a = std::min(st.anchor, f), b = std::max(st.anchor, f);
                for (int i = a; i <= b; ++i) range.push_back(st.rows[(size_t)i]);
                SetSelection(range);
            }
            else if (!io.KeyCtrl)
            {
                SelectOnly(st.rows[(size_t)f]);
                st.anchor = f;
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space) && st.focus >= 0 && st.focus < n) ToggleSelect(st.rows[(size_t)st.focus]);

        const auto paths = SelectionPaths();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
        {
            if (io.KeyAlt) { m_deferred = Deferred::Properties; m_deferredPaths = paths; }
            else if (!m_sel.empty())
            {
                if (m_sel.size() == 1) Activate(m_sel[0]);
                else DoOpen(paths);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && mode == List_Explorer) GoUp();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !paths.empty()) RequestDelete(paths, io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && paths.size() == 1) RequestRename(paths[0]);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
        {
            std::vector<Node*> all(st.rows.begin(), st.rows.end());
            SetSelection(all);
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C) && !paths.empty()) DoCopy(paths, false);
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C) && !paths.empty())
        {
            std::string all;
            for (auto& p : paths) { if (!all.empty()) all += "\r\n"; all += ToUtf8(p); }
            shell::SetClipboardText(all);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X) && !paths.empty()) DoCopy(paths, true);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && mode == List_Explorer) DoPaste(m_cur);
        if ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F10)) || ImGui::IsKeyPressed(ImGuiKey_Menu))
        {
            if (!paths.empty()) OpenItemMenu(paths, io.KeyShift);
            else if (mode == List_Explorer) OpenBackgroundMenu(m_cur, io.KeyShift);
        }
    }
}

namespace gex
{
    // ===================================================================== Recycle Bin
    void App::RefreshRecycleBin()
    {
        if (m_rbFuture.valid()) { m_rbRefreshSoon = true; return; }   // one query at a time
        std::vector<std::wstring> roots;
        for (auto& d : m_drives)
            if (d.ready && (d.type == 3 /*DRIVE_FIXED*/ || d.type == 2 /*DRIVE_REMOVABLE*/)) roots.push_back(d.root);
        m_rbQueriedAt = ImGui::GetTime();
        // SHQueryRecycleBin walks the bins; do it off the UI thread
        m_rbFuture = std::async(std::launch::async, [roots]() { return shell::QueryRecycleBin(roots); });
    }

    void App::PollRecycleBin()
    {
        if (m_rbFuture.valid() && m_rbFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            m_rb = m_rbFuture.get();
            m_rbKnown = true;
        }
        const double now = ImGui::GetTime();
        if (!m_rbFuture.valid() && (m_rbRefreshSoon || now - m_rbQueriedAt > 15.0) && !m_drives.empty())
        {
            m_rbRefreshSoon = false;
            RefreshRecycleBin();
        }
    }

    void App::RequestEmptyRecycleBin(const std::wstring& root)
    {
        m_rbEmptyRoot = root;
        m_openEmptyRb = true;
        m_rbRefreshSoon = true;   // show up-to-date numbers in the dialog
    }

    void App::AfterRecycleBinChanged()
    {
        m_rbRefreshSoon = true;
        m_drivesRefreshed = -100.0;   // free space changed
        // keep the scan results honest: the hidden $Recycle.Bin folders hold the deleted files
        if (m_tree)
            for (auto& d : m_drives)
            {
                const std::wstring bin = JoinPath(d.root, L"$Recycle.Bin");
                if (m_tree->Find(bin)) QueueSubtreeScan(bin);
            }
    }
}

namespace gex
{
    // ===================================================================== drag & drop into GEX
    void App::AddDropZone(ImVec2 a, ImVec2 b, Node* dir, int kind, Treemap* tm)
    {
        // only the part visible in the current (scrolling) window counts
        const ImVec2 cmin = ImGui::GetWindowPos();
        const ImVec2 cmax(cmin.x + ImGui::GetWindowWidth(), cmin.y + ImGui::GetWindowHeight());
        a.x = std::max(a.x, cmin.x); a.y = std::max(a.y, cmin.y);
        b.x = std::min(b.x, cmax.x); b.y = std::min(b.y, cmax.y);
        if (b.x <= a.x || b.y <= a.y) return;
        if (kind != Drop_Analyze && (!dir || dir->IsRemoved() || !dir->IsDir() || (dir->flags & NF_Reparse))) return;
        m_dropZonesBuild.push_back({ a, b, dir, kind, tm });
    }

    bool App::ResolveDrop(const std::vector<std::wstring>& paths, ImVec2 p, uint32_t keys, uint32_t allowed,
                          uint32_t& effect, Node*& dir, int& kind, const DropZone*& zone)
    {
        effect = shell::kDropNone; dir = nullptr; kind = -1; zone = nullptr;
        // the smallest zone under the cursor wins (a folder row beats the list background)
        float best = 3.4e38f;
        for (const DropZone& z : m_dropZones)
            if (p.x >= z.min.x && p.x < z.max.x && p.y >= z.min.y && p.y < z.max.y)
            {
                const float area = (z.max.x - z.min.x) * (z.max.y - z.min.y);
                if (area < best) { best = area; zone = &z; }
            }
        if (!zone) return false;
        kind = zone->kind;
        if (kind == Drop_Analyze)
        {
            effect = (allowed & shell::kDropCopy) ? shell::kDropCopy : (allowed & shell::kDropLink);
            return effect != 0;
        }
        dir = zone->dir;
        if (kind == Drop_Treemap && zone->tm)
            if (Node* n = zone->tm->HitTest(p))
                dir = n->IsDir() && !(n->flags & NF_Reparse) ? n : n->parent;
        if (!m_tree || !dir || dir->IsRemoved()) return false;

        const std::wstring dest = m_tree->FullPath(dir);
        bool allHere = true;
        for (const auto& s : paths)
        {
            if (PathEquals(s, dest) || PathStartsWith(dest, s)) return false;   // a folder into itself
            if (!PathEquals(ParentPath(s), dest)) allHere = false;
        }
        // Explorer rules: same drive = move, other drive = copy; Shift forces move, Ctrl forces copy.
        bool move;
        if (keys & shell::kKeyCtrl) move = false;
        else if (keys & shell::kKeyShift) move = true;
        else
        {
            const std::wstring& s = paths[0];
            move = s.size() > 1 && dest.size() > 1 && s[1] == L':' && dest[1] == L':' && towupper(s[0]) == towupper(dest[0]);
        }
        if (allHere && move) return false;   // already there
        effect = move ? shell::kDropMove : shell::kDropCopy;
        if (!(effect & allowed)) effect = (allowed & shell::kDropCopy) ? shell::kDropCopy : shell::kDropNone;
        return effect != shell::kDropNone;
    }

    uint32_t App::OnDragOver(const std::vector<std::wstring>& paths, int x, int y, uint32_t keys, uint32_t allowed)
    {
        // OLE owns the mouse during a drag: feed its position to Dear ImGui so rows highlight under the cursor
        m_dropPos = ImVec2((float)x, (float)y);
        ImGui::GetIO().AddMousePosEvent(m_dropPos.x, m_dropPos.y);
        m_extDrag = true;
        m_dropCount = paths.size();
        const DropZone* zone = nullptr;
        ResolveDrop(paths, m_dropPos, keys, allowed, m_dropEffect, m_dropDir, m_dropKind, zone);
        if (zone) { m_dropMin = zone->min; m_dropMax = zone->max; }
        return m_dropEffect;
    }

    void App::OnDragLeave()
    {
        m_extDrag = false;
        m_dropEffect = 0;
        m_dropDir = nullptr;
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    void App::OnDrop(const std::vector<std::wstring>& paths, int x, int y, uint32_t keys, uint32_t allowed)
    {
        m_extDrag = false;
        uint32_t effect; Node* dir; int kind; const DropZone* zone;
        if (!ResolveDrop(paths, ImVec2((float)x, (float)y), keys, allowed, effect, dir, kind, zone)) return;
        PendingDrop d;
        d.paths = paths;
        d.analyze = kind == Drop_Analyze;
        d.move = effect == shell::kDropMove;
        if (d.analyze)
        {
            shell::Stat st;
            d.dest = (shell::GetStat(paths[0], st) && st.isDir) ? paths[0] : ParentPath(paths[0]);
        }
        else d.dest = m_tree->FullPath(dir);
        // the file operation runs after the drop returns (it shows its own progress UI)
        m_pendingDrops.push_back(std::move(d));
    }

    void App::RunPendingDrops()
    {
        std::vector<PendingDrop> drops;
        drops.swap(m_pendingDrops);
        for (auto& d : drops)
        {
            if (d.analyze) { StartScan(d.dest); continue; }
            if (shell::CopyOrMoveItems(d.paths, d.dest, d.move))
            {
                if (d.move) AfterExternalChange(d.paths);
                if (m_tree)
                    if (Node* n = m_tree->Find(d.dest)) ShallowRefresh(n);
                ScheduleRefresh(d.dest, 1.5);
            }
        }
    }

    void App::DrawDropOverlay()
    {
        if (!m_extDrag) return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const bool ok = m_dropEffect != 0;
        const ImU32 col = ok ? IM_COL32(61, 220, 151, 255) : IM_COL32(255, 92, 92, 200);
        if (m_dropKind >= 0) dl->AddRect(m_dropMin, m_dropMax, col, 4.0f, 0, 2.0f);
        std::string label;
        if (!ok) label = "Can't drop here";
        else if (m_dropKind == Drop_Analyze) label = "Analyze dropped folder";
        else
        {
            label = std::string(m_dropEffect == shell::kDropMove ? "Move " : "Copy ") + std::to_string(m_dropCount) +
                    (m_dropCount == 1 ? " item to " : " items to ") + (m_dropDir ? m_dropDir->name : std::string());
            label += m_dropEffect == shell::kDropMove ? "   (Ctrl = copy)" : "   (Shift = move)";
        }
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const ImVec2 p(m_dropPos.x + 22 * m_uiScale, m_dropPos.y + 26 * m_uiScale);
        dl->AddRectFilled(ImVec2(p.x - 6, p.y - 4), ImVec2(p.x + ts.x + 6, p.y + ts.y + 4), IM_COL32(28, 32, 48, 235), 5.0f);
        dl->AddRect(ImVec2(p.x - 6, p.y - 4), ImVec2(p.x + ts.x + 6, p.y + ts.y + 4), col, 5.0f);
        dl->AddText(p, IM_COL32(235, 238, 245, 255), label.c_str());
    }
}
