// GEX - Graphical Explorer
// Application state + Dear ImGui user interface.
#pragma once
#include "FileTree.h"
#include "Scanner.h"
#include "ShellOps.h"
#include "Treemap.h"
#include "Watcher.h"
#include "imgui.h"
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gex
{
    struct Settings
    {
        bool  showHidden = true;          // hidden files are always counted; this only affects lists
        bool  showSystem = true;
        int   threads = 0;                // 0 = auto
        bool  fastNtfs = true;            // read the NTFS MFT directly when running as administrator
        bool  nativeMenuByDefault = false;// right click shows the Windows menu directly
        bool  treemapInExplorer = true;
        float treemapSplit = 0.42f;       // fraction of explorer height used by the treemap
        bool  treemapLabels = true;
        bool  treemapCushion = true;
        bool  liveUpdateTree = true;      // apply monitor events to scan results
        bool  liveFilesOnly = false;
        int   recentWindow = 1;           // index into recent-window table
        float leftPaneWidth = 330.0f;
        int   treeStyle = 0;              // folder tree: 0 classic lines with [+]/[-], 1 arrows
        bool  treeSizesFirst = false;     // folder tree: size columns on the left instead of pinned on the right
        std::wstring lastPath;
        std::string  liveExclude = "";    // ';' separated substrings
    };

    // A table of nodes (explorer folder contents, flat search results, ...)
    struct ListState
    {
        std::vector<Node*> rows;
        uint64_t key = ~0ull;
        uint64_t version = 0;            // tree version the rows were built from
        double   builtAt = -1.0;
        int      sortCol = -1;
        bool     sortAsc = false;
        bool     sortDirty = true;
        int      anchor = -1;            // shift-click anchor (row index)
        int      focus = -1;             // keyboard focus (row index)
        bool     scrollToFocus = false;
        uint64_t totalSize = 0;          // sum of rows
        // rubber-band selection
        bool     boxPending = false;     // mouse down, waiting for the drag threshold
        bool     boxActive = false;
        ImVec2   boxStart = ImVec2(0, 0);// content space of the table's scrolling window
        std::vector<Node*> boxBase;      // selection kept when Ctrl was held
        // row geometry (content space), measured while drawing
        float    rowY0 = -1e30f;
        float    pitch = 0.0f;
        int      measuredRow = -1;
        float    measuredY = 0.0f;
    };

    enum ListMode { List_Explorer, List_Flat, List_FlatRecent };

    struct RecentEntry
    {
        std::wstring path;
        std::string  nameUtf8, dirUtf8;
        uint64_t firstTime = 0, lastTime = 0;
        uint64_t size = 0, prevSize = 0;
        uint32_t lastAction = 0;
        uint32_t count = 0;
        uint32_t attrs = 0;
        uint8_t  category = Cat_Other;
        bool     isDir = false;
        bool     exists = true;
        bool     selected = false;
    };

    struct TypeStat
    {
        std::string key;          // category name or extension
        int      cat = Cat_Other;
        int      catIndex = -1;   // >= 0 when grouping by category
        uint64_t size = 0;
        uint64_t count = 0;
    };

    class App final : public shell::DropHandler
    {
    public:
        void Init(void* hwnd, const std::wstring& startPath);
        void Shutdown();
        void Frame();                 // build the UI (between NewFrame and Render)
        void PostFrame();             // run blocking native UI (menus, drag & drop, dialogs) after Present
        bool NeedsFastRefresh() const;// scanning / animations -> render continuously
        bool NeedsSlowRefresh() const;// live monitor running -> refresh a few times per second
        bool WantsQuit() const { return m_quit; }
        void SetUiScale(float s) { m_uiScale = s; }

        // shell::DropHandler: files dragged onto the GEX window
        uint32_t OnDragOver(const std::vector<std::wstring>& paths, int x, int y, uint32_t keys, uint32_t allowed) override;
        void     OnDragLeave() override;
        void     OnDrop(const std::vector<std::wstring>& paths, int x, int y, uint32_t keys, uint32_t effect) override;

    private:
        // ---- scanning
        void StartScan(const std::wstring& path);
        void PollScanner();
        void QueueSubtreeScan(const std::wstring& path);
        void PollSubScans();
        void ShallowRefresh(Node* dir);
        void ValidatePaths(const std::vector<std::wstring>& paths);
        void ScheduleRefresh(const std::wstring& dir, double delay);
        void PollScheduledRefresh();

        // ---- navigation / selection
        void Navigate(Node* dir, bool addHistory = true);
        void GoBack();
        void GoForward();
        void GoUp();
        void Activate(Node* n);       // double click / Enter
        void Reveal(Node* n);         // navigate to parent and select n
        bool IsSelected(const Node* n) const { return m_selSet.count(n) != 0; }
        void SelectOnly(Node* n);
        void ToggleSelect(Node* n);
        void ClearSelection();
        void SetSelection(const std::vector<Node*>& nodes);
        void PruneSelection();
        std::vector<std::wstring> PathsOf(const std::vector<Node*>& nodes) const;
        std::vector<std::wstring> SelectionPaths() const { return PathsOf(m_sel); }
        uint64_t SelectionSize() const;

        // ---- actions (operate on paths so they work for every view)
        void OpenItemMenu(const std::vector<std::wstring>& paths, bool forceNative);
        void OpenBackgroundMenu(Node* dir, bool forceNative);
        void RequestDelete(const std::vector<std::wstring>& paths, bool permanent);
        void RequestRename(const std::wstring& path);
        void DoCopy(const std::vector<std::wstring>& paths, bool cut);
        void DoPaste(Node* destDir);
        void DoNewFolder(Node* dir);
        void DoOpen(const std::vector<std::wstring>& paths);
        void AfterExternalChange(const std::vector<std::wstring>& paths);
        void HandleListShortcuts(ListState& st, ListMode mode);
        void HandleGlobalShortcuts();
        void ExportCsv(const std::vector<Node*>& rows);

        // ---- UI pieces
        void SetupStyle();
        void DrawMenuBar();
        void DrawToolbar();
        void DrawDriveStrip();
        void DrawScanProgress();
        void DrawWelcome();
        void DrawStatusBar();
        void DrawFolderTree();
        void DrawFolderTreeNode(Node* n, int depth, std::vector<uint8_t>& more);
        void DrawTabs();
        void DrawExplorerTab();
        void DrawTreemapTab();
        void DrawTopTab();
        void DrawTypesTab();
        void DrawRecentTab();
        void DrawLiveMonitor();
        void DrawRecentFromScan();
        void DrawPopups();
        void DrawItemContextMenu();
        void DrawBackgroundContextMenu();
        void DrawNodeTooltip(Node* n, uint64_t referenceSize);
        void DrawTreemapView(Treemap& tm, const char* id, Node* root, ImVec2 size, bool navigateOnDouble);
        void DrawNodeList(const char* id, ListState& st, ListMode mode, uint64_t key,
                          const std::function<void(std::vector<Node*>&)>& build, ImVec2 size,
                          uint64_t percentOf, int defaultSortCol = 1);
        void DrawNameCell(Node* n, const std::wstring* fullPathHint);
        void DrawSizeBar(uint64_t size, uint64_t total, int colorCat);
        void SortRows(ListState& st);
        void BeginBoxSelect(ListState& st, bool additive);
        void UpdateBoxSelect(ListState& st);
        bool PassesFilter(const Node* n) const;

        // ---- drop targets (rebuilt every frame from what is visible)
        enum DropKind { Drop_Folder, Drop_Analyze, Drop_Treemap };
        struct DropZone { ImVec2 min, max; Node* dir; int kind; Treemap* tm; };
        void AddDropZone(ImVec2 a, ImVec2 b, Node* dir, int kind, Treemap* tm = nullptr);
        bool ResolveDrop(const std::vector<std::wstring>& paths, ImVec2 p, uint32_t keys, uint32_t allowed,
                         uint32_t& effect, Node*& dir, int& kind, const DropZone*& zone);
        void DrawDropOverlay();
        void RunPendingDrops();

        // ---- Recycle Bin
        void RefreshRecycleBin();
        void PollRecycleBin();
        void RequestEmptyRecycleBin(const std::wstring& root);
        void AfterRecycleBinChanged();
        void DrawRecycleBinCard(ImVec2 size);

        // ---- live monitor
        void StartWatcher(const std::wstring& path);
        void PollWatcher();
        void ApplyEventToTree(const FsEvent& ev);
        void ComputeTypeStats();

        // ---- settings
        void LoadSettings();
        void SaveSettings();

        // ---------------------------------------------------------------- state
        void*        m_hwnd = nullptr;
        bool         m_quit = false;
        bool         m_elevated = false;
        Settings     m_set;
        std::wstring m_settingsPath;
        std::string  m_iniPath;

        Scanner                   m_scanner;
        std::unique_ptr<FileTree> m_tree;
        std::vector<shell::DriveInfo> m_drives;
        double                    m_drivesRefreshed = -100.0;

        Scanner                   m_subScanner;
        std::deque<std::wstring>  m_subQueue;
        std::wstring              m_subPath;
        struct Scheduled { std::wstring dir; double at; };
        std::vector<Scheduled>    m_scheduled;

        Node*              m_cur = nullptr;      // current explorer folder
        std::vector<Node*> m_back, m_fwd;
        std::vector<Node*> m_sel;
        std::unordered_set<const Node*> m_selSet;
        bool               m_syncTreeToCur = false;
        Node*              m_reveal = nullptr;   // scroll the explorer list to this node
        std::unordered_set<const Node*> m_treeOpen;   // expanded folders in the folder tree
        float              m_treeNameWidth = 0.0f, m_treeNameWidthNext = 0.0f; // folder tree: name column content width
        float              m_treeScrollX = 0.0f, m_treeColX = 0.0f, m_treeColW = 1.0f, m_treeGrabOffset = 0.0f; // name column's own scrolling
        bool               m_editingPath = false;
        char               m_pathBuf[2048] = {};
        char               m_filter[256] = {};   // explorer filter
        bool               m_focusFilter = false;

        int  m_activeTab = 0;
        int  m_requestTab = -1;
        enum { Tab_Explorer, Tab_Treemap, Tab_Top, Tab_Types, Tab_Recent };

        ListState m_explorerList, m_topList, m_typeFilesList, m_recentScanList;
        Treemap   m_tmExplorer, m_tmMain;
        Node*     m_tmRoot = nullptr;
        Node*     m_tmHover = nullptr;

        // Top files tab
        int   m_topMode = 0;          // 0 files, 1 folders
        char  m_topSearch[256] = {};
        int   m_topCat = -1;          // -1 all
        int   m_topMinSize = 0;
        bool  m_topScopeCur = false;

        // Types tab
        int   m_typeGroupMode = 0;    // 0 categories, 1 extensions
        bool  m_typeScopeCur = false;
        std::vector<TypeStat> m_typeStats;
        uint64_t m_typeStatsKey = ~0ull;
        double   m_typeStatsAt = -1.0;
        uint64_t m_typeStatsVersion = 0;
        uint64_t m_typeTotal = 0;
        std::string m_typeSelKey;

        // Live monitor
        Watcher  m_watcher;
        bool     m_watchPaused = false;
        char     m_watchPathBuf[1024] = {};
        char     m_liveFilter[256] = {};
        char     m_liveExcludeBuf[512] = {};
        std::vector<RecentEntry> m_recent;
        std::unordered_map<std::wstring, size_t> m_recentIndex;
        std::vector<int> m_recentOrder;
        bool     m_recentDirty = true;
        uint64_t m_recentFilterKey = 0;
        double   m_recentSortedAt = -1.0;
        int      m_recentSortCol = 0;
        bool     m_recentSortAsc = false;
        int      m_recentAnchor = -1;
        uint64_t m_liveTreeUpdates = 0;

        // Context menu / modal state
        std::vector<std::wstring> m_ctxPaths;
        Node*        m_ctxBgDir = nullptr;
        bool         m_openItemCtx = false, m_openBgCtx = false;
        std::vector<std::wstring> m_delPaths;
        bool         m_delPermanent = false;
        uint64_t     m_delSize = 0;
        bool         m_openDelete = false;
        std::wstring m_renamePath;
        char         m_renameBuf[512] = {};
        bool         m_openRename = false;
        bool         m_openAbout = false, m_openShortcuts = false;
        std::string  m_errorText;
        bool         m_openError = false;

        // Drag & drop into GEX
        std::vector<DropZone> m_dropZones, m_dropZonesBuild;
        bool         m_extDrag = false;
        ImVec2       m_dropPos = ImVec2(0, 0);
        uint32_t     m_dropEffect = 0;
        Node*        m_dropDir = nullptr;
        int          m_dropKind = -1;
        ImVec2       m_dropMin = ImVec2(0, 0), m_dropMax = ImVec2(0, 0);
        size_t       m_dropCount = 0;
        struct PendingDrop { std::vector<std::wstring> paths; std::wstring dest; bool move; bool analyze; };
        std::vector<PendingDrop> m_pendingDrops;

        // Recycle Bin
        std::future<shell::RecycleBinInfo> m_rbFuture;
        shell::RecycleBinInfo m_rb;
        bool         m_rbKnown = false;
        double       m_rbQueriedAt = -100.0;
        bool         m_rbRefreshSoon = false;
        std::wstring m_rbEmptyRoot;          // "" = all drives
        bool         m_openEmptyRb = false;

        // Deferred blocking actions, executed in PostFrame()
        enum class Deferred { None, NativeMenu, NativeBgMenu, DragOut, PickFolder, PickWatchFolder, Properties, EmptyRecycleBin };
        Deferred     m_deferred = Deferred::None;
        std::vector<std::wstring> m_deferredPaths;
        std::wstring m_deferredDir;
        bool         m_deferredExtended = false;
        bool         m_dragArmed = false;
        bool         m_boxSelecting = false;   // a selection box is being dragged (keep rendering)
        float        m_uiScale = 1.0f;
    };
}
