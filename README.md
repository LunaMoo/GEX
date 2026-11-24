# GEX - Graphical Explorer

A fast, colorful disk space analyzer and file manager for Windows, built with C++17, Win32, Direct3D 11 and [Dear ImGui](https://github.com/ocornut/imgui).

GEX scans a whole drive or folder (hidden and system files included), sorts everything by size, and lets you clean up with the same file operations Windows Explorer uses.

## Download

Prebuilt `GEX.exe` (Windows 10/11, x64) is attached to each [release](https://github.com/LunaMoo/GEX/releases). No installer: unzip and run.

## Build

1. Open `GEX.sln` in Visual Studio 2022 or 2026 (workload: *Desktop development with C++*).
2. Select **Release | x64** and build (Ctrl+Shift+B).
3. Run `bin\Release\GEX.exe`.

Dear ImGui **v1.93.0** is already bundled in `third_party\imgui`, so no download is needed.
To update it later, run `setup.bat` (needs git): `setup.bat v1.93.0`, or `setup.bat master` for the latest version.

If Visual Studio asks to retarget the project (different Windows SDK or toolset), accept it.

Optional command line: `GEX.exe "D:\Some\Folder"` starts scanning that folder.

## Features

**Scanning**
- Multithreaded scanner (`FindFirstFileExW` with large fetch). Uses about 3 threads on spinning hard drives and more on SSD/NVMe (auto-detected, adjustable in *View*).
- Counts hidden and system files, `pagefile.sys`, `hiberfil.sys` and so on. Junctions and symlinks are shown but not followed, so nothing is counted twice.
- Progress bar shows real progress for drive scans (bytes found vs. used space). *Stop* keeps the partial result.
- Long path support (`\\?\`) and network shares (`\\server\share`).
- *File > Restart as administrator* to include folders that need admin rights.

**Explorer tab** (works like Windows Explorer)
- Back / forward / up, clickable breadcrumb path (click empty space or Ctrl+L to type a path), folder tree on the left sorted by size with % bars. The tree's *View* button switches between a classic tree with lines and [+]/[-] boxes and Dear ImGui arrows, and between sizes pinned on the right (default) or on the left. Long folder names scroll with their own scrollbar under the name column (or Shift + mouse wheel) while sizes stay visible.
- Real Windows file icons, a size bar per row, sortable and reorderable columns, per-folder filter.
- Multi-select (Ctrl / Shift / Ctrl+A) and Explorer-style box selection: drag anywhere except on a name (other columns, empty space below the list) to draw a selection box. Hold Ctrl to add to the selection; the list auto-scrolls at the edges. Keyboard navigation, double click to open.
- Optional treemap under the list (drag the splitter to resize).

**Treemap tab**: a squarified, nested, cushion-shaded map of every file in the folder, colored by file type. Click to select, double click to zoom into a folder, right click for the menu.

**Largest tab**: every file (or folder) on the scanned drive, biggest first. Filter by name, type group, minimum size, or current folder only. *Export CSV* saves the list.

**File types tab**: group by media type (Videos, Music & Audio, Images, Archives, Documents, Source Code, Programs, Disc Images, Temp & Logs, Other) or by extension. Shows a donut chart and a table, and lists the files of the selected group. Click a color swatch to change a group's color.

**Recent changes tab**
- *Live monitor*: real-time list of created, modified, deleted and renamed files under any folder (`ReadDirectoryChangesW`). Shows size, growth and change count, with fresh changes highlighted. **Start / Pause / Resume / Stop / Clear**. Pause freezes the list while still recording changes. It can also update the scan results live, has a filter, and takes exclusions (e.g. `\AppData\Local\Temp; .log`).
- *Recently modified (from scan)*: files changed in the last 15 min / hour / day / week / month / year.

**File operations** (all lists, the tree and the treemap)
- Right click opens the GEX menu: Open, Open with, Show in Windows Explorer, Open in Treemap, Open terminal here, Cut / Copy / Paste, Copy path / name, Rename, Delete (Recycle Bin), Delete permanently, Rescan, Properties.
- *Show more options* or **Shift+F10** opens the real Windows context menu, including shell extensions like 7-Zip, Send to, antivirus scans and so on. Shift + right click shows the extended verbs. *View > Right click opens the Windows menu* makes it the default.
- Copy, cut and paste use the Windows clipboard, so they work both ways with Explorer.
- Drag files out of GEX (grab them by the name) into Explorer, the desktop or other apps.
- Copying, moving, deleting and renaming go through `IFileOperation`, with the usual Windows progress, conflict and undo behavior. GEX always asks before deleting.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Enter / double click | Open |
| Backspace, Alt+Up | Parent folder |
| Alt+Left / Alt+Right, mouse buttons 4/5 | Back / forward |
| Del / Shift+Del | Recycle Bin / delete permanently |
| F2 | Rename |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste |
| Ctrl+Shift+C | Copy full path |
| Ctrl+A | Select all |
| Ctrl+Shift+N | New folder |
| Alt+Enter | Properties |
| Shift+F10, Menu key | Windows context menu |
| F5 / Ctrl+F5 | Rescan current folder / everything |
| Ctrl+F | Filter current folder |
| Ctrl+L, F4, Alt+D | Type a path |
| Ctrl+1 to Ctrl+5 | Switch tabs |
| Drag outside the names | Box select (Ctrl adds) |

## Notes

- Sizes are logical file sizes, as in Explorer's "Size" column, not "size on disk". Hard-linked files (e.g. in `C:\Windows\WinSxS`) are counted once per link, so that folder looks bigger than its real usage. OneDrive "online-only" files show their full size and are marked *Cloud* in the tooltip.
- Settings, window layout and CSV exports are stored in `%APPDATA%\GEX`.
- Not implemented yet: dropping files *into* GEX, and reading the NTFS MFT directly (a WizTree-style instant scan that needs admin rights).

## Project layout

```
GEX.sln / GEX.vcxproj     Visual Studio solution (x64 Debug/Release)
src/main.cpp              Win32 window, D3D11, fonts, idle-friendly main loop
src/App*.cpp, App.h       UI and application logic (AppUI = chrome/dialogs, AppViews = tabs)
src/Scanner.*             multithreaded scanner
src/FileTree.*            in-memory tree (arena allocated) + live mutations
src/Watcher.*             ReadDirectoryChangesW live monitor
src/ShellOps.*            IFileOperation, IContextMenu, clipboard, drag & drop, properties
src/Treemap.*             squarified treemap renderer
src/IconCache.*           shell icons -> D3D11 textures
src/FileTypes.*           extension -> media group table and colors
third_party/imgui         Dear ImGui v1.93.0 (MIT license)
res/                      icon, manifest (PerMonitorV2 DPI, long paths), version info
```

## License

GEX is free software, released under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

It bundles [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut and contributors, which is under the MIT license (`third_party/imgui/LICENSE.txt`).
