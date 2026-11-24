// GEX - Graphical Explorer
#include "ShellOps.h"
#include "Util.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace gex::shell
{
    static HWND g_hwnd = nullptr;
    static IContextMenu2* g_cm2 = nullptr;   // active native menu (for owner drawn submenus)
    static IContextMenu3* g_cm3 = nullptr;

    void  Init(void* hwnd) { g_hwnd = (HWND)hwnd; }
    void* MainWindow() { return g_hwnd; }

    // ------------------------------------------------------------------ helpers
    struct Pidl
    {
        PIDLIST_ABSOLUTE p = nullptr;
        Pidl() = default;
        explicit Pidl(const std::wstring& path) { ::SHParseDisplayName(path.c_str(), nullptr, &p, 0, nullptr); }
        ~Pidl() { if (p) ::CoTaskMemFree(p); }
        Pidl(const Pidl&) = delete;
        Pidl& operator=(const Pidl&) = delete;
        Pidl(Pidl&& o) noexcept : p(o.p) { o.p = nullptr; }
    };

    static std::wstring LongPathIfNeeded(const std::wstring& path)
    {
        if (path.size() >= MAX_PATH && path.compare(0, 4, L"\\\\?\\") != 0)
        {
            if (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\') return L"\\\\?\\UNC\\" + path.substr(2);
            return L"\\\\?\\" + path;
        }
        return path;
    }

    // Returns parent IShellFolder + child ids for items sharing the first item's parent folder.
    struct ShellSelection
    {
        ComPtr<IShellFolder>            folder;
        std::vector<Pidl>               abs;
        std::vector<PCUITEMID_CHILD>    children;
        bool Valid() const { return folder && !children.empty(); }
    };

    static ShellSelection BuildSelection(const std::vector<std::wstring>& paths)
    {
        ShellSelection sel;
        if (paths.empty()) return sel;
        const std::wstring parent0 = ParentPath(paths[0]);
        for (const auto& path : paths)
        {
            if (!sel.abs.empty() && !PathEquals(ParentPath(path), parent0)) continue; // different folder: skip
            Pidl pidl(path);
            if (!pidl.p) continue;
            ComPtr<IShellFolder> folder;
            PCUITEMID_CHILD child = nullptr;
            if (FAILED(::SHBindToParent(pidl.p, IID_PPV_ARGS(&folder), &child))) continue;
            if (!sel.folder) sel.folder = folder;
            sel.children.push_back(child);
            sel.abs.push_back(std::move(pidl));
        }
        return sel;
    }

    static ComPtr<IShellItem> ItemFromPath(const std::wstring& path)
    {
        ComPtr<IShellItem> item;
        ::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
        return item;
    }

    // ------------------------------------------------------------------ file system
    bool Exists(const std::wstring& path)
    {
        return ::GetFileAttributesW(LongPathIfNeeded(path).c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool GetStat(const std::wstring& path, Stat& out)
    {
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!::GetFileAttributesExW(LongPathIfNeeded(path).c_str(), GetFileExInfoStandard, &d)) return false;
        out.attrs = d.dwFileAttributes;
        out.isDir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.size = out.isDir ? 0 : (((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow);
        out.mtime = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
        return true;
    }

    bool ListDirectory(const std::wstring& dir, std::vector<DirEntry>& out)
    {
        std::wstring pattern;
        if (dir.size() > 2 && dir[0] == L'\\' && dir[1] == L'\\') pattern = L"\\\\?\\UNC\\" + dir.substr(2);
        else pattern = L"\\\\?\\" + dir;
        if (pattern.back() != L'\\') pattern += L'\\';
        pattern += L'*';
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return ::GetLastError() == ERROR_FILE_NOT_FOUND;
        do
        {
            const wchar_t* nm = fd.cFileName;
            if (nm[0] == L'.' && (nm[1] == 0 || (nm[1] == L'.' && nm[2] == 0))) continue;
            DirEntry e;
            e.name = nm;
            e.st.attrs = fd.dwFileAttributes;
            e.st.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e.st.size = e.st.isDir ? 0 : (((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
            e.st.mtime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            out.push_back(std::move(e));
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
        return true;
    }

    // ------------------------------------------------------------------ opening
    bool OpenItem(const std::wstring& path)
    {
        const std::wstring dir = ParentPath(path);
        HINSTANCE r = ::ShellExecuteW(g_hwnd, nullptr, path.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
        return (INT_PTR)r > 32;
    }

    bool OpenWith(const std::wstring& path)
    {
        OPENASINFO info{};
        info.pcszFile = path.c_str();
        info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_EXEC;
        return SUCCEEDED(::SHOpenWithDialog(g_hwnd, &info));
    }

    bool ExploreFolder(const std::wstring& dir)
    {
        HINSTANCE r = ::ShellExecuteW(g_hwnd, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (INT_PTR)r > 32;
    }

    bool ShowInExplorer(const std::vector<std::wstring>& paths)
    {
        // Group by parent folder, open each (max 8 windows) with its items selected.
        std::vector<std::wstring> remaining = paths;
        int windows = 0;
        while (!remaining.empty() && windows < 8)
        {
            const std::wstring parent = ParentPath(remaining[0]);
            if (PathEquals(parent, remaining[0])) // drive root
            {
                ExploreFolder(remaining[0]);
                remaining.erase(remaining.begin());
                ++windows;
                continue;
            }
            std::vector<Pidl> items;
            std::vector<PCUITEMID_CHILD> children;
            std::vector<std::wstring> rest;
            for (auto& p : remaining)
            {
                if (!PathEquals(ParentPath(p), parent)) { rest.push_back(p); continue; }
                Pidl pidl(p);
                if (!pidl.p) continue;
                children.push_back(::ILFindLastID(pidl.p));
                items.push_back(std::move(pidl));
            }
            Pidl folder(parent);
            if (folder.p)
                ::SHOpenFolderAndSelectItems(folder.p, (UINT)children.size(), children.data(), 0);
            remaining.swap(rest);
            ++windows;
        }
        return true;
    }

    bool OpenTerminal(const std::wstring& dir)
    {
        std::wstring args = L"-d \"" + dir + (dir.back() == L'\\' ? L"\\" : L"") + L"\"";
        HINSTANCE r = ::ShellExecuteW(g_hwnd, nullptr, L"wt.exe", args.c_str(), dir.c_str(), SW_SHOWNORMAL);
        if ((INT_PTR)r > 32) return true;
        r = ::ShellExecuteW(g_hwnd, nullptr, L"cmd.exe", nullptr, dir.c_str(), SW_SHOWNORMAL);
        return (INT_PTR)r > 32;
    }

    void ShowProperties(const std::vector<std::wstring>& paths)
    {
        if (paths.empty()) return;
        if (paths.size() == 1)
        {
            ::SHObjectProperties(g_hwnd, SHOP_FILEPATH, paths[0].c_str(), nullptr);
            return;
        }
        ShellSelection sel = BuildSelection(paths);
        if (!sel.Valid()) return;
        ComPtr<IDataObject> data;
        if (SUCCEEDED(sel.folder->GetUIObjectOf(g_hwnd, (UINT)sel.children.size(), sel.children.data(), IID_IDataObject, nullptr, (void**)data.GetAddressOf())))
            ::SHMultiFileProperties(data.Get(), 0);
    }

    // ------------------------------------------------------------------ file operations
    static ComPtr<IFileOperation> NewFileOperation(DWORD flags)
    {
        ComPtr<IFileOperation> op;
        if (FAILED(::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) return nullptr;
        op->SetOwnerWindow(g_hwnd);
        op->SetOperationFlags(flags);
        return op;
    }

    bool DeleteItems(const std::vector<std::wstring>& paths, bool toRecycleBin)
    {
        DWORD flags = FOF_NOCONFIRMATION; // GEX already asked the user
        if (toRecycleBin) flags |= FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_WANTNUKEWARNING;
        auto op = NewFileOperation(flags);
        if (!op) return false;
        int queued = 0;
        for (auto& p : paths)
            if (auto item = ItemFromPath(p)) { if (SUCCEEDED(op->DeleteItem(item.Get(), nullptr))) ++queued; }
        if (!queued) return false;
        return SUCCEEDED(op->PerformOperations());
    }

    bool RenameItem(const std::wstring& path, const std::wstring& newName)
    {
        auto op = NewFileOperation(FOF_ALLOWUNDO);
        auto item = ItemFromPath(path);
        if (!op || !item) return false;
        if (FAILED(op->RenameItem(item.Get(), newName.c_str(), nullptr))) return false;
        if (FAILED(op->PerformOperations())) return false;
        BOOL aborted = FALSE; op->GetAnyOperationsAborted(&aborted);
        return !aborted;
    }

    bool CopyOrMoveItems(const std::vector<std::wstring>& paths, const std::wstring& destDir, bool move)
    {
        bool sameFolder = false;
        for (auto& p : paths) if (PathEquals(ParentPath(p), destDir)) sameFolder = true;
        DWORD flags = FOF_ALLOWUNDO;
        if (sameFolder && !move) flags |= FOF_RENAMEONCOLLISION; // "file - Copy.txt" like Explorer
        auto op = NewFileOperation(flags);
        auto dest = ItemFromPath(destDir);
        if (!op || !dest) return false;
        int queued = 0;
        for (auto& p : paths)
        {
            if (move && PathEquals(ParentPath(p), destDir)) continue; // moving onto itself
            auto item = ItemFromPath(p);
            if (!item) continue;
            HRESULT hr = move ? op->MoveItem(item.Get(), dest.Get(), nullptr, nullptr)
                              : op->CopyItem(item.Get(), dest.Get(), nullptr, nullptr);
            if (SUCCEEDED(hr)) ++queued;
        }
        if (!queued) return false;
        return SUCCEEDED(op->PerformOperations());
    }

    bool CreateNewFolder(const std::wstring& parentDir, std::wstring* createdPath)
    {
        for (int i = 1; i < 1000; ++i)
        {
            std::wstring name = i == 1 ? L"New folder" : L"New folder (" + std::to_wstring(i) + L")";
            std::wstring path = JoinPath(parentDir, name);
            if (::CreateDirectoryW(LongPathIfNeeded(path).c_str(), nullptr))
            {
                if (createdPath) *createdPath = path;
                ::SHChangeNotify(SHCNE_MKDIR, SHCNF_PATHW, path.c_str(), nullptr);
                return true;
            }
            if (::GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
        return false;
    }

    // ------------------------------------------------------------------ clipboard
    bool SetClipboardFiles(const std::vector<std::wstring>& paths, bool cut)
    {
        if (paths.empty()) return false;
        size_t chars = 1;
        for (auto& p : paths) chars += p.size() + 1;
        HGLOBAL hDrop = ::GlobalAlloc(GHND, sizeof(DROPFILES) + chars * sizeof(wchar_t));
        if (!hDrop) return false;
        DROPFILES* df = (DROPFILES*)::GlobalLock(hDrop);
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        wchar_t* dst = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
        for (auto& p : paths) { memcpy(dst, p.c_str(), p.size() * sizeof(wchar_t)); dst += p.size(); *dst++ = 0; }
        *dst = 0;
        ::GlobalUnlock(hDrop);

        HGLOBAL hEffect = ::GlobalAlloc(GHND, sizeof(DWORD));
        if (hEffect)
        {
            DWORD* e = (DWORD*)::GlobalLock(hEffect);
            *e = cut ? DROPEFFECT_MOVE : (DROPEFFECT_COPY | DROPEFFECT_LINK);
            ::GlobalUnlock(hEffect);
        }
        if (!::OpenClipboard(g_hwnd)) { ::GlobalFree(hDrop); if (hEffect) ::GlobalFree(hEffect); return false; }
        ::EmptyClipboard();
        ::SetClipboardData(CF_HDROP, hDrop);
        if (hEffect) ::SetClipboardData(::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), hEffect);
        ::CloseClipboard();
        return true;
    }

    bool ClipboardHasFiles() { return ::IsClipboardFormatAvailable(CF_HDROP) != FALSE; }

    void ClearClipboard()
    {
        if (::OpenClipboard(g_hwnd)) { ::EmptyClipboard(); ::CloseClipboard(); }
    }

    bool GetClipboardFiles(std::vector<std::wstring>& paths, bool& cut)
    {
        cut = false;
        if (!::OpenClipboard(g_hwnd)) return false;
        HDROP hDrop = (HDROP)::GetClipboardData(CF_HDROP);
        if (hDrop)
        {
            const UINT n = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i)
            {
                const UINT len = ::DragQueryFileW(hDrop, i, nullptr, 0);
                std::wstring s(len, L'\0');
                ::DragQueryFileW(hDrop, i, s.data(), len + 1);
                paths.push_back(s);
            }
            HGLOBAL hEffect = ::GetClipboardData(::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
            if (hEffect)
            {
                DWORD* e = (DWORD*)::GlobalLock(hEffect);
                if (e) { cut = (*e & DROPEFFECT_MOVE) != 0; ::GlobalUnlock(hEffect); }
            }
        }
        ::CloseClipboard();
        return !paths.empty();
    }

    bool SetClipboardText(const std::string& utf8)
    {
        const std::wstring w = ToWide(utf8);
        HGLOBAL h = ::GlobalAlloc(GHND, (w.size() + 1) * sizeof(wchar_t));
        if (!h) return false;
        memcpy(::GlobalLock(h), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        ::GlobalUnlock(h);
        if (!::OpenClipboard(g_hwnd)) { ::GlobalFree(h); return false; }
        ::EmptyClipboard();
        ::SetClipboardData(CF_UNICODETEXT, h);
        ::CloseClipboard();
        return true;
    }

    // ------------------------------------------------------------------ native context menu
    static MenuResult RunContextMenu(IContextMenu* cm, int x, int y, bool extended)
    {
        HMENU menu = ::CreatePopupMenu();
        if (!menu) return MenuResult::Failed;
        UINT flags = CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME;
        if (extended) flags |= CMF_EXTENDEDVERBS;
        const UINT kFirst = 1, kLast = 0x7FFF;
        if (FAILED(cm->QueryContextMenu(menu, 0, kFirst, kLast, flags))) { ::DestroyMenu(menu); return MenuResult::Failed; }

        cm->QueryInterface(IID_PPV_ARGS(&g_cm3));
        if (!g_cm3) cm->QueryInterface(IID_PPV_ARGS(&g_cm2));
        const int cmd = (int)::TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, g_hwnd, nullptr);
        if (g_cm3) { g_cm3->Release(); g_cm3 = nullptr; }
        if (g_cm2) { g_cm2->Release(); g_cm2 = nullptr; }

        MenuResult result = MenuResult::None;
        if (cmd >= (int)kFirst)
        {
            const UINT id = (UINT)cmd - kFirst;
            wchar_t verb[256] = {};
            if (SUCCEEDED(cm->GetCommandString(id, GCS_VERBW, nullptr, (LPSTR)verb, 255)))
            {
                if (_wcsicmp(verb, L"rename") == 0) { ::DestroyMenu(menu); return MenuResult::Rename; }
                if (_wcsicmp(verb, L"delete") == 0) { ::DestroyMenu(menu); return MenuResult::Delete; }
            }
            CMINVOKECOMMANDINFOEX ici{};
            ici.cbSize = sizeof(ici);
            ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
            if (::GetKeyState(VK_CONTROL) < 0) ici.fMask |= CMIC_MASK_CONTROL_DOWN;
            if (::GetKeyState(VK_SHIFT) < 0) ici.fMask |= CMIC_MASK_SHIFT_DOWN;
            ici.hwnd = g_hwnd;
            ici.lpVerb = MAKEINTRESOURCEA(id);
            ici.lpVerbW = MAKEINTRESOURCEW(id);
            ici.nShow = SW_SHOWNORMAL;
            ici.ptInvoke.x = x; ici.ptInvoke.y = y;
            result = SUCCEEDED(cm->InvokeCommand((LPCMINVOKECOMMANDINFO)&ici)) ? MenuResult::Invoked : MenuResult::Failed;
        }
        ::DestroyMenu(menu);
        return result;
    }

    MenuResult ShowNativeContextMenu(const std::vector<std::wstring>& paths, int x, int y, bool extended)
    {
        ShellSelection sel = BuildSelection(paths);
        if (!sel.Valid()) return MenuResult::Failed;
        ComPtr<IContextMenu> cm;
        if (FAILED(sel.folder->GetUIObjectOf(g_hwnd, (UINT)sel.children.size(), sel.children.data(), IID_IContextMenu, nullptr, (void**)cm.GetAddressOf())))
            return MenuResult::Failed;
        return RunContextMenu(cm.Get(), x, y, extended);
    }

    MenuResult ShowNativeBackgroundMenu(const std::wstring& dir, int x, int y, bool extended)
    {
        Pidl pidl(dir);
        if (!pidl.p) return MenuResult::Failed;
        ComPtr<IShellFolder> desktop, folder;
        if (FAILED(::SHGetDesktopFolder(&desktop))) return MenuResult::Failed;
        if (ILIsEmpty(pidl.p)) folder = desktop;
        else if (FAILED(desktop->BindToObject(pidl.p, nullptr, IID_PPV_ARGS(&folder)))) return MenuResult::Failed;
        ComPtr<IContextMenu> cm;
        if (FAILED(folder->CreateViewObject(g_hwnd, IID_PPV_ARGS(&cm)))) return MenuResult::Failed;
        return RunContextMenu(cm.Get(), x, y, extended);
    }

    bool HandleMenuMessage(unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result)
    {
        if (msg != WM_INITMENUPOPUP && msg != WM_DRAWITEM && msg != WM_MEASUREITEM && msg != WM_MENUCHAR) return false;
        if (g_cm3)
        {
            LRESULT r = 0;
            if (SUCCEEDED(g_cm3->HandleMenuMsg2(msg, (WPARAM)wParam, (LPARAM)lParam, &r))) { if (result) *result = r; return true; }
        }
        else if (g_cm2)
        {
            if (SUCCEEDED(g_cm2->HandleMenuMsg(msg, (WPARAM)wParam, (LPARAM)lParam))) { if (result) *result = 0; return true; }
        }
        return false;
    }

    // ------------------------------------------------------------------ drag & drop
    uint32_t DragOut(const std::vector<std::wstring>& paths)
    {
        ShellSelection sel = BuildSelection(paths);
        if (!sel.Valid()) return 0;
        ComPtr<IDataObject> data;
        if (FAILED(sel.folder->GetUIObjectOf(g_hwnd, (UINT)sel.children.size(), sel.children.data(), IID_IDataObject, nullptr, (void**)data.GetAddressOf())))
            return 0;
        DWORD effect = 0;
        // SHDoDragDrop provides the drag image and a default drop source.
        HRESULT hr = ::SHDoDragDrop(g_hwnd, data.Get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
        return hr == DRAGDROP_S_DROP ? (uint32_t)effect : 0u;
    }

    // ------------------------------------------------------------------ misc
    bool PickFolder(std::wstring& out)
    {
        ComPtr<IFileOpenDialog> dlg;
        if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(L"GEX - choose a folder to analyze");
        if (FAILED(dlg->Show(g_hwnd))) return false;
        ComPtr<IShellItem> item;
        if (FAILED(dlg->GetResult(&item))) return false;
        PWSTR p = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) return false;
        out = p;
        ::CoTaskMemFree(p);
        return true;
    }

    bool RelaunchAsAdmin(const std::wstring& args)
    {
        wchar_t exe[MAX_PATH * 4];
        ::GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = exe;
        sei.lpParameters = args.c_str();
        sei.hwnd = g_hwnd;
        sei.nShow = SW_SHOWNORMAL;
        return ::ShellExecuteExW(&sei) != FALSE;
    }

    void MessageBoxError(const std::string& utf8Text)
    {
        ::MessageBoxW(g_hwnd, ToWide(utf8Text).c_str(), L"GEX", MB_OK | MB_ICONWARNING);
    }

    void GetCursorScreenPos(int& x, int& y)
    {
        POINT pt{}; ::GetCursorPos(&pt);
        x = pt.x; y = pt.y;
    }

    bool IsShiftDown() { return ::GetKeyState(VK_SHIFT) < 0; }

    std::vector<DriveInfo> ListDrives()
    {
        std::vector<DriveInfo> out;
        const DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if (!(mask & (1u << i))) continue;
            DriveInfo d;
            d.root = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
            d.type = ::GetDriveTypeW(d.root.c_str());
            if (d.type == DRIVE_NO_ROOT_DIR) continue;
            wchar_t label[MAX_PATH + 1] = {};
            // Avoid "insert disk" popups for empty card readers
            const UINT oldMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
            const BOOL hasVol = ::GetVolumeInformationW(d.root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
            ULARGE_INTEGER freeAvail{}, total{}, totalFree{};
            if (hasVol && ::GetDiskFreeSpaceExW(d.root.c_str(), &freeAvail, &total, &totalFree))
            {
                d.total = total.QuadPart;
                d.free = totalFree.QuadPart;
                d.ready = true;
            }
            ::SetErrorMode(oldMode);
            std::wstring name = label[0] ? label : (d.type == DRIVE_REMOVABLE ? L"Removable" : d.type == DRIVE_REMOTE ? L"Network" : d.type == DRIVE_CDROM ? L"Optical" : L"Local Disk");
            d.label = ToUtf8(name) + " (" + (char)('A' + i) + ":)";
            out.push_back(std::move(d));
        }
        return out;
    }
}
