// GEX - Graphical Explorer
// Windows shell integration: the same operations Explorer uses (IFileOperation, IContextMenu,
// clipboard CF_HDROP, drag & drop, properties). Header is free of <windows.h>.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gex::shell
{
    void Init(void* hwnd);           // HWND of the main window (owner of dialogs/menus)
    void* MainWindow();

    // --- simple file system helpers ---
    bool Exists(const std::wstring& path);
    struct Stat { uint64_t size = 0, mtime = 0; uint32_t attrs = 0; bool isDir = false; };
    bool GetStat(const std::wstring& path, Stat& out);
    struct DirEntry { std::wstring name; Stat st; };
    bool ListDirectory(const std::wstring& dir, std::vector<DirEntry>& out); // non recursive

    // --- opening ---
    bool OpenItem(const std::wstring& path);                    // default verb (like double click in Explorer)
    bool OpenWith(const std::wstring& path);                    // "Open with..." dialog
    bool ShowInExplorer(const std::vector<std::wstring>& paths); // open containing folder(s) and select items
    bool ExploreFolder(const std::wstring& dir);                 // open the folder itself in Explorer
    bool OpenTerminal(const std::wstring& dir);                  // Windows Terminal, falls back to cmd.exe
    void ShowProperties(const std::vector<std::wstring>& paths);

    // --- file operations (show the standard Windows progress/conflict UI) ---
    // Returns true if the operation ran (it may still have been partially cancelled by the user).
    bool DeleteItems(const std::vector<std::wstring>& paths, bool toRecycleBin);
    bool RenameItem(const std::wstring& path, const std::wstring& newName);
    bool CopyOrMoveItems(const std::vector<std::wstring>& paths, const std::wstring& destDir, bool move);
    bool CreateNewFolder(const std::wstring& parentDir, std::wstring* createdPath);

    // --- clipboard (interoperable with Explorer copy/cut/paste) ---
    bool SetClipboardFiles(const std::vector<std::wstring>& paths, bool cut);
    bool GetClipboardFiles(std::vector<std::wstring>& paths, bool& cut);
    bool ClipboardHasFiles();
    void ClearClipboard();
    bool SetClipboardText(const std::string& utf8);

    // --- native Windows context menu ---
    enum class MenuResult { None, Invoked, Rename, Delete, Failed };
    // Shows the real Explorer context menu for the given items (all items should share one parent folder;
    // if not, only the first one is used). Coordinates are screen pixels.
    MenuResult ShowNativeContextMenu(const std::vector<std::wstring>& paths, int screenX, int screenY, bool extended);
    // Folder background menu (New, Paste, View...)
    MenuResult ShowNativeBackgroundMenu(const std::wstring& dir, int screenX, int screenY, bool extended);
    // Must be called from WndProc first; forwards owner-draw/submenu messages to the active shell menu.
    bool HandleMenuMessage(unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);

    // --- drag & drop out of GEX (into Explorer, other apps, desktop) ---
    // Blocks until the drop finishes. Returns the DROPEFFECT (0 = none/cancelled).
    uint32_t DragOut(const std::vector<std::wstring>& paths);

    // --- misc ---
    bool PickFolder(std::wstring& out);
    bool RelaunchAsAdmin(const std::wstring& args);
    void MessageBoxError(const std::string& utf8Text);
    void GetCursorScreenPos(int& x, int& y);
    bool IsShiftDown();

    struct DriveInfo
    {
        std::wstring root;      // "C:\"
        std::string  label;     // "Windows (C:)"
        uint64_t total = 0, free = 0;
        unsigned type = 0;      // DRIVE_FIXED etc.
        bool ready = false;
    };
    std::vector<DriveInfo> ListDrives();
}
