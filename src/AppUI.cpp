// GEX - Graphical Explorer
// UI chrome: theme, menu bar, toolbar, drive cards, status bar, popups and context menus.
#include "App.h"
#include "IconCache.h"
#include "Icons.h"
#include "Util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gex
{
    bool g_haveIconFont = false;

    // "<icon> text" or "text" when the icon font is missing
    static std::string IL(const char* icon, const char* text)
    {
        if (!g_haveIconFont) return text;
        return std::string(icon) + "  " + text;
    }

    static ImVec4 Hex(uint32_t rgb, float a = 1.0f)
    {
        return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
    }

    void App::SetupStyle()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 6.0f;
        s.ChildRounding = 6.0f;
        s.FrameRounding = 5.0f;
        s.PopupRounding = 6.0f;
        s.GrabRounding = 4.0f;
        s.TabRounding = 5.0f;
        s.ScrollbarRounding = 8.0f;
        s.ScrollbarSize = 14.0f;
        s.FramePadding = ImVec2(8, 5);
        s.ItemSpacing = ImVec2(8, 6);
        s.ItemInnerSpacing = ImVec2(6, 4);
        s.CellPadding = ImVec2(6, 3);
        s.WindowPadding = ImVec2(10, 8);
        s.IndentSpacing = 16.0f;
        s.ChildBorderSize = 1.0f;
        s.PopupBorderSize = 1.0f;
        s.TabBarBorderSize = 2.0f;

        ImVec4* c = s.Colors;
        const ImVec4 accent = Hex(0x5B7CFF);
        const ImVec4 accent2 = Hex(0xB06BFF);
        c[ImGuiCol_Text] = Hex(0xE6E9F2);
        c[ImGuiCol_TextDisabled] = Hex(0x7D8499);
        c[ImGuiCol_WindowBg] = Hex(0x12141B);
        c[ImGuiCol_ChildBg] = Hex(0x161922);
        c[ImGuiCol_PopupBg] = Hex(0x1C2030, 0.98f);
        c[ImGuiCol_Border] = Hex(0x2A3042);
        c[ImGuiCol_FrameBg] = Hex(0x222738);
        c[ImGuiCol_FrameBgHovered] = Hex(0x2C3350);
        c[ImGuiCol_FrameBgActive] = Hex(0x353E62);
        c[ImGuiCol_TitleBg] = Hex(0x12141B);
        c[ImGuiCol_TitleBgActive] = Hex(0x1C2030);
        c[ImGuiCol_MenuBarBg] = Hex(0x181B26);
        c[ImGuiCol_ScrollbarBg] = Hex(0x12141B, 0.6f);
        c[ImGuiCol_ScrollbarGrab] = Hex(0x343B52);
        c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x47507A);
        c[ImGuiCol_ScrollbarGrabActive] = accent;
        c[ImGuiCol_CheckMark] = Hex(0x7FE0FF);
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accent2;
        c[ImGuiCol_Button] = Hex(0x262C40);
        c[ImGuiCol_ButtonHovered] = Hex(0x3A4570);
        c[ImGuiCol_ButtonActive] = accent;
        c[ImGuiCol_Header] = Hex(0x3352C8, 0.55f);           // selected rows
        c[ImGuiCol_HeaderHovered] = Hex(0x4060E0, 0.35f);
        c[ImGuiCol_HeaderActive] = Hex(0x4D6BFF, 0.70f);
        c[ImGuiCol_Separator] = Hex(0x2A3042);
        c[ImGuiCol_SeparatorHovered] = accent;
        c[ImGuiCol_SeparatorActive] = accent2;
        c[ImGuiCol_ResizeGrip] = Hex(0x3A4570, 0.4f);
        c[ImGuiCol_ResizeGripHovered] = accent;
        c[ImGuiCol_ResizeGripActive] = accent2;
        c[ImGuiCol_Tab] = Hex(0x1C2030);
        c[ImGuiCol_TabHovered] = Hex(0x3A4570);
        c[ImGuiCol_TabSelected] = Hex(0x2E3A6E);
        c[ImGuiCol_TabSelectedOverline] = accent2;
        c[ImGuiCol_TabDimmed] = Hex(0x181B26);
        c[ImGuiCol_TabDimmedSelected] = Hex(0x262E52);
        c[ImGuiCol_TableHeaderBg] = Hex(0x1E2334);
        c[ImGuiCol_TableBorderStrong] = Hex(0x2A3042);
        c[ImGuiCol_TableBorderLight] = Hex(0x222738);
        c[ImGuiCol_TableRowBg] = Hex(0x000000, 0.0f);
        c[ImGuiCol_TableRowBgAlt] = Hex(0xFFFFFF, 0.025f);
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered] = accent2;
        c[ImGuiCol_TextSelectedBg] = Hex(0x5B7CFF, 0.45f);
        c[ImGuiCol_NavCursor] = accent2;
        c[ImGuiCol_ModalWindowDimBg] = Hex(0x000000, 0.55f);
    }

    // ===================================================================== menu bar
    void App::DrawMenuBar()
    {
        if (!ImGui::BeginMenuBar()) return;
        ImGui::TextColored(Hex(0xB06BFF), "GEX");
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem(IL(ICON_FOLDER_OPEN, "Analyze folder...").c_str())) m_deferred = Deferred::PickFolder;
            if (ImGui::BeginMenu(IL(ICON_DRIVE, "Analyze drive").c_str()))
            {
                for (auto& d : m_drives)
                    if (ImGui::MenuItem(d.label.c_str(), nullptr, false, d.ready)) StartScan(d.root);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(IL(ICON_REFRESH, "Rescan everything").c_str(), "Ctrl+F5", false, m_tree != nullptr)) StartScan(m_tree->rootPath);
            if (ImGui::MenuItem(IL(ICON_STOP, "Stop scan").c_str(), nullptr, false, m_scanner.IsRunning())) m_scanner.Cancel();
            ImGui::Separator();
            {
                std::string label = IL(ICON_DELETE, "Empty Recycle Bin...");
                if (m_rbKnown) label += m_rb.count ? "  (" + FormatSize(m_rb.size) + ")" : "  (empty)";
                if (ImGui::MenuItem(label.c_str(), nullptr, false, !m_rbKnown || m_rb.count > 0)) RequestEmptyRecycleBin(L"");
            }
            if (ImGui::MenuItem("Open Recycle Bin")) shell::OpenRecycleBin();
            ImGui::Separator();
            if (ImGui::MenuItem(IL(ICON_SHIELD, "Restart as administrator").c_str(), nullptr, false, !m_elevated))
            {
                std::wstring args = m_tree ? L"\"" + m_tree->rootPath + (m_tree->rootPath.back() == L'\\' ? L"\\" : L"") + L"\"" : L"";
                if (shell::RelaunchAsAdmin(args)) m_quit = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) m_quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            const auto paths = SelectionPaths();
            const bool any = !paths.empty();
            if (ImGui::MenuItem(IL(ICON_CUT, "Cut").c_str(), "Ctrl+X", false, any)) DoCopy(paths, true);
            if (ImGui::MenuItem(IL(ICON_COPY, "Copy").c_str(), "Ctrl+C", false, any)) DoCopy(paths, false);
            if (ImGui::MenuItem(IL(ICON_PASTE, "Paste").c_str(), "Ctrl+V", false, m_cur && shell::ClipboardHasFiles())) DoPaste(m_cur);
            ImGui::Separator();
            if (ImGui::MenuItem(IL(ICON_RENAME, "Rename").c_str(), "F2", false, paths.size() == 1)) RequestRename(paths[0]);
            if (ImGui::MenuItem(IL(ICON_DELETE, "Delete").c_str(), "Del", false, any)) RequestDelete(paths, false);
            if (ImGui::MenuItem("Delete permanently", "Shift+Del", false, any)) RequestDelete(paths, true);
            ImGui::Separator();
            if (ImGui::MenuItem(IL(ICON_NEW_FOLDER, "New folder").c_str(), "Ctrl+Shift+N", false, m_cur != nullptr)) DoNewFolder(m_cur);
            if (ImGui::MenuItem("Clear selection", nullptr, false, any)) ClearSelection();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            bool dirty = false;
            dirty |= ImGui::MenuItem("Show hidden files", nullptr, &m_set.showHidden);
            dirty |= ImGui::MenuItem("Show system files", nullptr, &m_set.showSystem);
            ImGui::Separator();
            dirty |= ImGui::MenuItem("Treemap below the file list", nullptr, &m_set.treemapInExplorer);
            if (ImGui::MenuItem("Treemap labels", nullptr, &m_set.treemapLabels)) { m_tmMain.showLabels = m_tmExplorer.showLabels = m_set.treemapLabels; m_tmMain.Invalidate(); m_tmExplorer.Invalidate(); dirty = true; }
            if (ImGui::MenuItem("Treemap cushion shading", nullptr, &m_set.treemapCushion)) { m_tmMain.cushion = m_tmExplorer.cushion = m_set.treemapCushion; dirty = true; }
            ImGui::Separator();
            dirty |= ImGui::MenuItem("Right click opens the Windows menu", nullptr, &m_set.nativeMenuByDefault);
            ImGui::Separator();
            dirty |= ImGui::MenuItem("Fast NTFS scan (read the MFT)", nullptr, &m_set.fastNtfs);
            ImGui::SetItemTooltip("Reads the NTFS master file table directly, like WizTree: a whole drive in seconds.\n"
                                  "Needs administrator rights (File > Restart as administrator); otherwise the normal scan is used.\n"
                                  "Hard-linked files are counted once.");
            ImGui::SetNextItemWidth(160 * m_uiScale);
            if (ImGui::SliderInt("Scan threads", &m_set.threads, 0, 32, m_set.threads == 0 ? "Auto" : "%d")) dirty = true;
            ImGui::SetItemTooltip("Auto uses ~3 threads on spinning hard drives and more on SSD/NVMe.");
            if (dirty) { SaveSettings(); m_explorerList.key = ~0ull; m_topList.key = ~0ull; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard shortcuts")) m_openShortcuts = true;
            if (ImGui::MenuItem("About GEX")) m_openAbout = true;
            ImGui::EndMenu();
        }
        if (m_elevated)
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 110 * m_uiScale);
            ImGui::TextColored(Hex(0xFFC857), "%s", IL(ICON_SHIELD, "Admin").c_str());
        }
        ImGui::EndMenuBar();
    }

    // ===================================================================== toolbar + breadcrumbs
    void App::DrawToolbar()
    {
        const bool haveTree = m_tree && m_cur;
        ImGui::BeginDisabled(m_back.empty());
        if (ImGui::ArrowButton("##back", ImGuiDir_Left)) GoBack();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Back (Alt+Left)");
        ImGui::SameLine(0, 4);
        ImGui::BeginDisabled(m_fwd.empty());
        if (ImGui::ArrowButton("##fwd", ImGuiDir_Right)) GoForward();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Forward (Alt+Right)");
        ImGui::SameLine(0, 4);
        ImGui::BeginDisabled(!haveTree || !m_cur->parent);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) GoUp();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Up one level (Alt+Up / Backspace)");
        ImGui::SameLine(0, 4);
        ImGui::BeginDisabled(!haveTree);
        if (ImGui::Button(g_haveIconFont ? ICON_REFRESH "##rescan" : "Rescan##rescan") && haveTree) QueueSubtreeScan(m_tree->FullPath(m_cur));
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Rescan this folder (F5). Ctrl+F5 rescans everything.");
        ImGui::SameLine();

        const float filterW = 280.0f * m_uiScale;
        const float crumbsW = std::max(100.0f, ImGui::GetContentRegionAvail().x - filterW - ImGui::GetStyle().ItemSpacing.x);
        if (m_editingPath)
        {
            ImGui::SetNextItemWidth(crumbsW);
            if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##path", m_pathBuf, sizeof(m_pathBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                const std::wstring p = NormalizeDirPath(ToWide(m_pathBuf));
                m_editingPath = false;
                Node* n = m_tree ? m_tree->Find(p) : nullptr;
                if (n && n->IsDir()) Navigate(n);
                else if (n) Reveal(n);
                else
                {
                    shell::Stat st;
                    if (shell::GetStat(p, st) && st.isDir) StartScan(p);
                    else { m_errorText = "Path not found:\n" + std::string(m_pathBuf); m_openError = true; }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit())) m_editingPath = false;
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding);
            ImGui::BeginChild("##crumbs", ImVec2(crumbsW, ImGui::GetFrameHeight()), ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar(2);
            if (haveTree)
            {
                std::vector<Node*> chain;
                for (Node* p = m_cur; p; p = p->parent) chain.push_back(p);
                std::reverse(chain.begin(), chain.end());
                // Drop leading segments when too long
                float total = 0;
                std::vector<float> widths;
                for (Node* p : chain) { widths.push_back(ImGui::CalcTextSize(p->name.c_str()).x + 26.0f * m_uiScale); total += widths.back(); }
                size_t first = 0;
                while (first + 1 < chain.size() && total > crumbsW - 40.0f * m_uiScale) total -= widths[first++];
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, ImGui::GetStyle().FramePadding.y));
                if (first > 0) { ImGui::TextDisabled("..."); ImGui::SameLine(0, 2); }
                for (size_t i = first; i < chain.size(); ++i)
                {
                    ImGui::PushID((int)i);
                    const bool last = i + 1 == chain.size();
                    if (last) ImGui::PushStyleColor(ImGuiCol_Text, Hex(0x9FD8FF));
                    if (ImGui::Button(chain[i]->name.c_str())) Navigate(chain[i]);
                    AddDropZone(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), chain[i], Drop_Folder);
                    if (last) ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                        OpenItemMenu({ m_tree->FullPath(chain[i]) }, shell::IsShiftDown());
                    if (!last) { ImGui::SameLine(0, 0); ImGui::TextDisabled(">"); ImGui::SameLine(0, 0); }
                    ImGui::PopID();
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                // click on empty area -> edit path
                ImGui::SameLine();
                if (ImGui::InvisibleButton("##editpath", ImVec2(std::max(4.0f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight())))
                {
                    m_editingPath = true;
                    snprintf(m_pathBuf, sizeof(m_pathBuf), "%s", m_tree->FullPathUtf8(m_cur).c_str());
                }
                ImGui::SetItemTooltip("Click to type a path (Ctrl+L)");
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Choose a drive or folder to analyze");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (m_focusFilter) { ImGui::SetKeyboardFocusHere(); m_focusFilter = false; }
        if (ImGui::InputTextWithHint("##filter", g_haveIconFont ? ICON_SEARCH "  Filter this folder (Ctrl+F)" : "Filter this folder (Ctrl+F)", m_filter, sizeof(m_filter)))
            m_requestTab = Tab_Explorer;
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) m_filter[0] = 0;
    }

    // ===================================================================== drive strip
    static ImU32 UsageColor(float used)
    {
        // green -> yellow -> red
        const ImVec4 g = Hex(0x3DDC97), y = Hex(0xFFC857), r = Hex(0xFF5C5C);
        ImVec4 c;
        if (used < 0.7f) { float t = used / 0.7f; c = ImVec4(g.x + (y.x - g.x) * t, g.y + (y.y - g.y) * t, g.z + (y.z - g.z) * t, 1); }
        else { float t = std::min(1.0f, (used - 0.7f) / 0.25f); c = ImVec4(y.x + (r.x - y.x) * t, y.y + (r.y - y.y) * t, y.z + (r.z - y.z) * t, 1); }
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    static bool DriveCard(const shell::DriveInfo& d, ImVec2 size, bool active, float scale)
    {
        ImGui::PushID(ToUtf8(d.root).c_str());
        const bool clicked = ImGui::InvisibleButton("##card", size);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 bg = active ? IM_COL32(46, 58, 110, 255) : hovered ? IM_COL32(44, 50, 76, 255) : IM_COL32(32, 36, 54, 255);
        dl->AddRectFilled(a, b, bg, 6.0f * scale);
        if (active) dl->AddRect(a, b, IM_COL32(176, 107, 255, 255), 6.0f * scale, 0, 1.5f);
        const float fs = ImGui::GetFontSize();
        const float pad = 6.0f * scale;
        float x = a.x + pad;
        if (ImTextureID ic = icons::ForDrive(d.root))
        {
            dl->AddImage(ic, ImVec2(x, a.y + pad), ImVec2(x + fs, a.y + pad + fs));
            x += fs + 4 * scale;
        }
        const ImVec4 clip(a.x, a.y, b.x - pad, b.y);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(x, a.y + pad), IM_COL32(230, 233, 242, 255), d.label.c_str(), nullptr, 0, &clip);
        if (d.ready && d.total > 0)
        {
            const float used = 1.0f - (float)((double)d.free / (double)d.total);
            const float barY = b.y - pad - 6.0f * scale;
            dl->AddRectFilled(ImVec2(a.x + pad, barY), ImVec2(b.x - pad, barY + 6 * scale), IM_COL32(20, 22, 32, 255), 3 * scale);
            dl->AddRectFilled(ImVec2(a.x + pad, barY), ImVec2(a.x + pad + (b.x - a.x - 2 * pad) * used, barY + 6 * scale), UsageColor(used), 3 * scale);
            if (size.y > fs * 2.8f)
            {
                const std::string info = FormatSize(d.free) + " free of " + FormatSize(d.total);
                dl->AddText(ImGui::GetFont(), fs * 0.92f, ImVec2(a.x + pad, a.y + pad + fs + 3 * scale), IM_COL32(150, 158, 180, 255), info.c_str(), nullptr, 0, &clip);
            }
            if (hovered) ImGui::SetTooltip("%s\nUsed %s (%.1f%%)\nFree %s\nClick to analyze", d.label.c_str(),
                                           FormatSize(d.total - d.free).c_str(), used * 100.0f, FormatSize(d.free).c_str());
        }
        ImGui::PopID();
        return clicked && d.ready;
    }

    void App::DrawDriveStrip()
    {
        const float fs = ImGui::GetFontSize();
        const ImVec2 card(190.0f * m_uiScale, fs * 3.4f);
        ImGui::BeginChild("##drives", ImVec2(0, card.y + ImGui::GetStyle().ScrollbarSize + 4 * m_uiScale), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
        const std::wstring curRoot = m_tree ? m_tree->rootPath : std::wstring();
        for (size_t i = 0; i < m_drives.size(); ++i)
        {
            if (i) ImGui::SameLine();
            if (DriveCard(m_drives[i], card, PathEquals(curRoot, m_drives[i].root), m_uiScale)) StartScan(m_drives[i].root);
        }
        ImGui::SameLine();
        if (ImGui::Button(IL(ICON_FOLDER_OPEN, "Folder...").c_str(), ImVec2(0, card.y))) m_deferred = Deferred::PickFolder;
        ImGui::SetItemTooltip("Analyze any folder (also network shares)");
        ImGui::SameLine();
        DrawRecycleBinCard(ImVec2(170.0f * m_uiScale, card.y));
        // drop a folder onto the drive bar to analyze it
        AddDropZone(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight()), nullptr, Drop_Analyze);
        ImGui::EndChild();
    }

    void App::DrawRecycleBinCard(ImVec2 size)
    {
        const bool full = m_rbKnown && m_rb.count > 0;
        const bool clicked = ImGui::InvisibleButton("##recyclebin", size);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float s = m_uiScale, fs = ImGui::GetFontSize(), pad = 6.0f * s;
        dl->AddRectFilled(a, b, hovered ? IM_COL32(70, 44, 52, 255) : IM_COL32(32, 36, 54, 255), 6.0f * s);
        if (full) dl->AddRect(a, b, hovered ? IM_COL32(255, 120, 120, 255) : IM_COL32(150, 80, 90, 255), 6.0f * s);
        float x = a.x + pad;
        if (ImTextureID ic = icons::ForRecycleBin(full))
        {
            dl->AddImage(ic, ImVec2(x, a.y + pad), ImVec2(x + fs, a.y + pad + fs));
            x += fs + 4 * s;
        }
        const ImVec4 clip(a.x, a.y, b.x - pad, b.y);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(x, a.y + pad), IM_COL32(230, 233, 242, 255), "Recycle Bin", nullptr, 0, &clip);
        std::string info = !m_rbKnown ? "checking..." : full ? FormatSize(m_rb.size) + " in " + FormatCount(m_rb.count) + (m_rb.count == 1 ? " item" : " items") : "Empty";
        dl->AddText(ImGui::GetFont(), fs * 0.92f, ImVec2(a.x + pad, a.y + pad + fs + 3 * s), full ? IM_COL32(255, 160, 140, 255) : IM_COL32(150, 158, 180, 255), info.c_str(), nullptr, 0, &clip);
        if (full)
        {
            const char* hint = "click to empty";
            dl->AddText(ImGui::GetFont(), fs * 0.85f, ImVec2(a.x + pad, b.y - pad - fs * 0.85f), IM_COL32(125, 132, 153, 255), hint, nullptr, 0, &clip);
        }
        if (hovered)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Recycle Bin");
            if (full)
            {
                ImGui::Separator();
                for (size_t i = 0; i < m_rb.driveSize.size(); ++i)
                    ImGui::Text("%s  %s  (%s items)", ToUtf8(m_rb.driveSize[i].first).c_str(), FormatSize(m_rb.driveSize[i].second).c_str(),
                                FormatCount(m_rb.driveCount[i].second).c_str());
                ImGui::Separator();
                ImGui::TextDisabled("Click: empty   Right click: more options");
            }
            else ImGui::TextDisabled(m_rbKnown ? "Empty. Right click: open it" : "Checking...");
            ImGui::EndTooltip();
        }
        if (clicked && full) RequestEmptyRecycleBin(L"");
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) ImGui::OpenPopup("##rbmenu");
        if (ImGui::BeginPopup("##rbmenu"))
        {
            if (ImGui::MenuItem("Open Recycle Bin")) shell::OpenRecycleBin();
            if (ImGui::MenuItem("Refresh")) m_rbRefreshSoon = true;
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, Hex(0xFF8A8A));
            if (ImGui::MenuItem("Empty Recycle Bin (all drives)", nullptr, false, full)) RequestEmptyRecycleBin(L"");
            for (size_t i = 0; i < m_rb.driveSize.size(); ++i)
            {
                const std::string l = "Empty only " + ToUtf8(m_rb.driveSize[i].first) + "  (" + FormatSize(m_rb.driveSize[i].second) + ")";
                if (ImGui::MenuItem(l.c_str())) RequestEmptyRecycleBin(m_rb.driveSize[i].first);
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
    }

    void App::DrawScanProgress()
    {
        const Scanner::Progress p = m_scanner.GetProgress();
        float fraction = -1.0f * (float)ImGui::GetTime();   // indeterminate animation
        for (auto& d : m_drives)
            if (PathEquals(d.root, m_scanner.Root()) && d.ready && d.total > d.free)
                fraction = std::min(0.99f, (float)((double)p.bytes / (double)(d.total - d.free)));
        if (p.fraction >= 0.0f) fraction = p.fraction;
        char overlay[256];
        if (p.mft)
            snprintf(overlay, sizeof(overlay), "Fast NTFS scan: reading the master file table... %.0f%%  %.1fs", p.fraction * 100.0f, p.seconds);
        else
            snprintf(overlay, sizeof(overlay), "%s  %s files  %s folders  %s  %.1fs  (%d threads)",
                p.finalizing ? "Sorting..." : "Scanning", FormatCount(p.files).c_str(), FormatCount(p.dirs).c_str(),
                FormatSize(p.bytes).c_str(), p.seconds, p.threads);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Hex(0xB06BFF));
        ImGui::ProgressBar(fraction, ImVec2(ImGui::GetContentRegionAvail().x - 90 * m_uiScale, 0), overlay);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(IL(ICON_STOP, "Stop").c_str(), ImVec2(-FLT_MIN, 0))) m_scanner.Cancel();
        ImGui::SetItemTooltip("Stop scanning and show what was found so far");
        ImGui::TextDisabled("%s", ToUtf8(p.current).c_str());
        if (!m_elevated && m_set.fastNtfs && !p.mft)
        {
            ImGui::SameLine();
            ImGui::TextColored(Hex(0xFFC857), "  Tip: run as administrator for the fast NTFS scan (File > Restart as administrator).");
        }
    }

    // ===================================================================== welcome
    void App::DrawWelcome()
    {
        const float s = m_uiScale;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(0, avail.y * 0.08f));
        auto centered = [&](const char* text, ImVec4 col)
        {
            const float w = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPosX((avail.x - w) * 0.5f);
            ImGui::TextColored(col, "%s", text);
        };
        ImGui::SetWindowFontScale(2.2f);
        centered("GEX", Hex(0xB06BFF));
        ImGui::SetWindowFontScale(1.15f);
        centered("Graphical Explorer", Hex(0xE6E9F2));
        ImGui::SetWindowFontScale(1.0f);
        centered("See what takes the space on your disks - every file, hidden ones included - and clean it up.", Hex(0x7D8499));
        ImGui::Dummy(ImVec2(0, 20 * s));

        const ImVec2 card(260 * s, ImGui::GetFontSize() * 4.2f);
        const int perRow = std::max(1, (int)((avail.x - 40 * s) / (card.x + 12 * s)));
        const int n = (int)m_drives.size();
        for (int i = 0; i < n; ++i)
        {
            if (i % perRow == 0)
            {
                const int inRow = std::min(perRow, n - i);
                const float rowW = inRow * card.x + (inRow - 1) * 12 * s;
                ImGui::SetCursorPosX(std::max(0.0f, (avail.x - rowW) * 0.5f));
            }
            else ImGui::SameLine(0, 12 * s);
            if (DriveCard(m_drives[(size_t)i], card, false, s)) StartScan(m_drives[(size_t)i].root);
        }
        ImGui::Dummy(ImVec2(0, 16 * s));
        const char* btn = "Analyze a folder...";
        ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize(btn).x - 40 * s) * 0.5f);
        if (ImGui::Button(IL(ICON_FOLDER_OPEN, btn).c_str())) m_deferred = Deferred::PickFolder;
        ImGui::Dummy(ImVec2(0, 10 * s));
        centered("You can also drop a folder anywhere here to analyze it.", Hex(0x7D8499));
        if (!m_elevated)
        {
            centered("Tip: run as administrator for the fast NTFS scan and to measure protected system folders (File > Restart as administrator).", Hex(0x7D8499));
        }
        AddDropZone(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight()), nullptr, Drop_Analyze);
    }

    // ===================================================================== status bar
    void App::DrawStatusBar()
    {
        ImGui::Separator();
        if (!m_tree) { ImGui::TextDisabled("Ready"); return; }
        if (m_cur)
            ImGui::Text("%s items", FormatCount(m_cur->children.size()).c_str());
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        if (!m_sel.empty())
        {
            ImGui::TextColored(Hex(0x9FD8FF), "%s selected (%s)", FormatCount(m_sel.size()).c_str(), FormatSize(SelectionSize()).c_str());
            ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        }
        ImGui::Text("Total %s in %s files, %s folders", FormatSize(m_tree->root->size).c_str(),
            FormatCount(m_tree->root->fileCount).c_str(), FormatCount(m_tree->root->dirCount).c_str());
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::TextDisabled("%s in %.1fs", m_tree->usedMft ? "MFT scan" : "scanned", m_tree->scanSeconds);
        if (m_tree->usedMft) ImGui::SetItemTooltip("Built from the NTFS master file table. Hard-linked files are counted once.");
        if (m_tree->scanErrors)
        {
            ImGui::SameLine();
            ImGui::TextColored(Hex(0xFFC857), "%s %s folders not readable", Ico(ICON_WARNING), FormatCount(m_tree->scanErrors).c_str());
            ImGui::SetItemTooltip("Access denied. Restart GEX as administrator to include them.");
        }
        if (m_tree->partial) { ImGui::SameLine(); ImGui::TextColored(Hex(0xFF8A5C), "(partial scan)"); }
        if (m_subScanner.IsRunning() || !m_subQueue.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(Hex(0xB06BFF), "Rescanning %s ...", ToUtf8(FileNameOf(m_subPath)).c_str());
        }
        if (m_watcher.IsRunning())
        {
            ImGui::SameLine();
            ImGui::TextColored(m_watchPaused ? Hex(0xFFC857) : Hex(0x3DDC97), "%s", m_watchPaused ? "  Monitor paused" : "  Monitor live");
        }
        if (m_tmHover && !m_tmHover->IsRemoved())
        {
            const std::string t = m_tree->FullPathUtf8(m_tmHover) + "  " + FormatSize(m_tmHover->size);
            const float w = ImGui::CalcTextSize(t.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - w - 16 * m_uiScale));
            ImGui::TextDisabled("%s", t.c_str());
        }
    }

    // ===================================================================== tooltips
    void App::DrawNodeTooltip(Node* n, uint64_t ref)
    {
        if (!n || !m_tree) return;
        ImGui::BeginTooltip();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(CategoryColorU32(n->ColorCategory())));
        ImGui::TextUnformatted(n->name.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", m_tree->FullPathUtf8(n->parent ? n->parent : n).c_str());
        ImGui::Separator();
        ImGui::Text("Size:      %s  (%s bytes)", FormatSize(n->size).c_str(), FormatCount(n->size).c_str());
        if (ref > 0) ImGui::Text("Share:     %.2f%%", 100.0 * (double)n->size / (double)ref);
        if (n->IsDir()) ImGui::Text("Contains:  %s files, %s folders", FormatCount(n->fileCount).c_str(), FormatCount(n->dirCount).c_str());
        ImGui::Text("Type:      %s", n->IsDir() ? ((n->flags & NF_Reparse) ? "Link / junction (not followed)" : "Folder") : CategoryName(n->category));
        if (n->mtime) ImGui::Text("Modified:  %s", FormatFileTime(n->mtime).c_str());
        std::string attrs;
        if (n->attrs & 0x2) attrs += "Hidden ";
        if (n->attrs & 0x4) attrs += "System ";
        if (n->attrs & 0x1) attrs += "Read-only ";
        if (n->attrs & 0x800) attrs += "Compressed ";
        if (n->attrs & 0x400000) attrs += "Cloud (not on disk) ";
        if (!attrs.empty()) ImGui::Text("Attributes: %s", attrs.c_str());
        if (n->flags & NF_Error) ImGui::TextColored(Hex(0xFF5C5C), "Access denied - contents not counted");
        if (n->flags & NF_Partial) ImGui::TextColored(Hex(0xFF8A5C), "Not scanned (scan was stopped)");
        ImGui::EndTooltip();
    }

    // ===================================================================== context menus
    void App::DrawItemContextMenu()
    {
        if (m_openItemCtx) { ImGui::OpenPopup("##itemctx"); m_openItemCtx = false; }
        if (!ImGui::BeginPopup("##itemctx")) return;
        const auto& P = m_ctxPaths;
        if (P.empty()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        const bool single = P.size() == 1;
        Node* node = (single && m_tree) ? m_tree->Find(P[0]) : nullptr;
        shell::Stat st;
        const bool isDir = node ? node->IsDir() : (single && shell::GetStat(P[0], st) && st.isDir);

        if (single) ImGui::TextColored(Hex(0x9FD8FF), "%s", ToUtf8(FileNameOf(P[0])).c_str());
        else ImGui::TextColored(Hex(0x9FD8FF), "%d items", (int)P.size());
        ImGui::Separator();
        if (ImGui::MenuItem(IL(ICON_OPEN_EXTERNAL, "Open").c_str(), "Enter")) DoOpen(P);
        if (single && !isDir && ImGui::MenuItem(IL(ICON_OPEN_WITH, "Open with...").c_str())) shell::OpenWith(P[0]);
        if (ImGui::MenuItem(IL(ICON_FOLDER_OPEN, "Show in Windows Explorer").c_str())) shell::ShowInExplorer(P);
        if (node && node->IsDir() && ImGui::MenuItem(IL(ICON_TILES, "Open in Treemap").c_str())) { m_tmRoot = node; m_requestTab = Tab_Treemap; }
        if (node && node->parent != m_cur && ImGui::MenuItem(IL(ICON_CHEVRON, "Go to location in GEX").c_str())) Reveal(node);
        if (single && isDir && ImGui::MenuItem(IL(ICON_TERMINAL, "Open terminal here").c_str())) shell::OpenTerminal(P[0]);
        ImGui::Separator();
        if (ImGui::MenuItem(IL(ICON_CUT, "Cut").c_str(), "Ctrl+X")) DoCopy(P, true);
        if (ImGui::MenuItem(IL(ICON_COPY, "Copy").c_str(), "Ctrl+C")) DoCopy(P, false);
        if (single && node && node->IsDir() && ImGui::MenuItem(IL(ICON_PASTE, "Paste into folder").c_str(), nullptr, false, shell::ClipboardHasFiles())) DoPaste(node);
        if (ImGui::MenuItem("Copy path", "Ctrl+Shift+C"))
        {
            std::string all;
            for (auto& p : P) { if (!all.empty()) all += "\r\n"; all += ToUtf8(p); }
            shell::SetClipboardText(all);
        }
        if (single && ImGui::MenuItem("Copy name")) shell::SetClipboardText(ToUtf8(FileNameOf(P[0])));
        ImGui::Separator();
        if (single && ImGui::MenuItem(IL(ICON_RENAME, "Rename").c_str(), "F2")) RequestRename(P[0]);
        ImGui::PushStyleColor(ImGuiCol_Text, Hex(0xFF8A8A));
        if (ImGui::MenuItem(IL(ICON_DELETE, "Delete (Recycle Bin)").c_str(), "Del")) RequestDelete(P, false);
        if (ImGui::MenuItem("Delete permanently", "Shift+Del")) RequestDelete(P, true);
        ImGui::PopStyleColor();
        ImGui::Separator();
        if (node && node->IsDir() && ImGui::MenuItem(IL(ICON_REFRESH, "Rescan folder").c_str())) QueueSubtreeScan(P[0]);
        if (ImGui::MenuItem(IL(ICON_INFO, "Properties").c_str(), "Alt+Enter")) { m_deferred = Deferred::Properties; m_deferredPaths = P; }
        ImGui::Separator();
        if (ImGui::MenuItem(IL(ICON_MORE, "Show more options").c_str(), "Shift+F10"))
        {
            m_deferred = Deferred::NativeMenu;
            m_deferredPaths = P;
            m_deferredExtended = shell::IsShiftDown();
        }
        ImGui::EndPopup();
    }

    void App::DrawBackgroundContextMenu()
    {
        if (m_openBgCtx) { ImGui::OpenPopup("##bgctx"); m_openBgCtx = false; }
        if (!ImGui::BeginPopup("##bgctx")) return;
        Node* dir = m_ctxBgDir;
        if (!dir || dir->IsRemoved() || !m_tree) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        const std::wstring path = m_tree->FullPath(dir);
        ImGui::TextColored(Hex(0x9FD8FF), "%s", dir->name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(IL(ICON_NEW_FOLDER, "New folder").c_str(), "Ctrl+Shift+N")) DoNewFolder(dir);
        if (ImGui::MenuItem(IL(ICON_PASTE, "Paste").c_str(), "Ctrl+V", false, shell::ClipboardHasFiles())) DoPaste(dir);
        if (ImGui::MenuItem(IL(ICON_REFRESH, "Rescan this folder").c_str(), "F5")) QueueSubtreeScan(path);
        ImGui::Separator();
        if (ImGui::MenuItem(IL(ICON_FOLDER_OPEN, "Open in Windows Explorer").c_str())) shell::ExploreFolder(path);
        if (ImGui::MenuItem(IL(ICON_TERMINAL, "Open terminal here").c_str())) shell::OpenTerminal(path);
        if (ImGui::MenuItem("Copy folder path")) shell::SetClipboardText(ToUtf8(path));
        if (ImGui::MenuItem(IL(ICON_INFO, "Properties").c_str())) { m_deferred = Deferred::Properties; m_deferredPaths = { path }; }
        ImGui::Separator();
        if (ImGui::MenuItem(IL(ICON_MORE, "Show more options").c_str(), "Shift+F10"))
        {
            m_deferred = Deferred::NativeBgMenu;
            m_deferredDir = path;
            m_deferredExtended = shell::IsShiftDown();
        }
        ImGui::EndPopup();
    }

    // ===================================================================== modal dialogs
    void App::DrawPopups()
    {
        DrawItemContextMenu();
        DrawBackgroundContextMenu();
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

        // ---- delete confirmation
        if (m_openDelete) { ImGui::OpenPopup("Delete###del"); m_openDelete = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete###del", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const int n = (int)m_delPaths.size();
            ImGui::TextColored(m_delPermanent ? Hex(0xFF5C5C) : Hex(0xFFC857), "%s %s (%s)?",
                m_delPermanent ? "Permanently delete" : "Move to Recycle Bin:",
                n == 1 ? "1 item" : (std::to_string(n) + " items").c_str(), FormatSize(m_delSize).c_str());
            ImGui::Separator();
            for (int i = 0; i < n && i < 12; ++i) ImGui::BulletText("%s", ToUtf8(m_delPaths[(size_t)i]).c_str());
            if (n > 12) ImGui::TextDisabled("... and %d more", n - 12);
            ImGui::Separator();
            if (m_delPermanent) ImGui::TextColored(Hex(0xFF8A8A), "This cannot be undone.");
            else ImGui::TextDisabled("Very large items may not fit in the Recycle Bin - Windows will ask before deleting them permanently.");
            ImGui::Spacing();
            bool doIt = false, recycle = !m_delPermanent;
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::PushStyleColor(ImGuiCol_Button, m_delPermanent ? Hex(0x8A2B2B) : Hex(0x2E5A3A));
            if (ImGui::Button(m_delPermanent ? "Delete permanently" : "Move to Recycle Bin", ImVec2(200 * m_uiScale, 0))) doIt = true;
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (!m_delPermanent && ImGui::Button("Delete permanently instead")) { doIt = true; recycle = false; }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100 * m_uiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) doIt = true;
            if (doIt)
            {
                shell::DeleteItems(m_delPaths, recycle);
                ValidatePaths(m_delPaths);
                if (recycle) AfterRecycleBinChanged();
                for (auto& p : m_delPaths)
                {
                    auto it = m_recentIndex.find(p);
                    if (it != m_recentIndex.end() && !shell::Exists(p)) m_recent[it->second].exists = false;
                }
                m_delPaths.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- rename
        if (m_openRename) { ImGui::OpenPopup("Rename###ren"); m_openRename = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Rename###ren", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("%s", ToUtf8(ParentPath(m_renamePath)).c_str());
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(460 * m_uiScale);
            bool ok = ImGui::InputText("##newname", m_renameBuf, sizeof(m_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::Button("Rename", ImVec2(120 * m_uiScale, 0))) ok = true;
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120 * m_uiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            if (ok)
            {
                const std::wstring newName = ToWide(m_renameBuf);
                if (!newName.empty() && newName != FileNameOf(m_renamePath))
                {
                    if (shell::RenameItem(m_renamePath, newName))
                    {
                        if (m_tree)
                        {
                            const std::wstring newPath = JoinPath(ParentPath(m_renamePath), newName);
                            Node* n = m_tree->Find(m_renamePath);
                            shell::Stat st;
                            if (n && shell::GetStat(newPath, st))
                            {
                                m_tree->Rename(n, ToUtf8(newName));
                                SelectOnly(n);
                                m_reveal = n;
                            }
                            else if (n && n->parent)
                                ShallowRefresh(n->parent);  // the shell picked another name (collision)
                        }
                        auto it = m_recentIndex.find(m_renamePath);
                        if (it != m_recentIndex.end()) m_recent[it->second].exists = false;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- empty Recycle Bin
        if (m_openEmptyRb) { ImGui::OpenPopup("Empty Recycle Bin###emptyrb"); m_openEmptyRb = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Empty Recycle Bin###emptyrb", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            uint64_t size = m_rb.size, count = m_rb.count;
            if (!m_rbEmptyRoot.empty())
            {
                size = count = 0;
                for (size_t i = 0; i < m_rb.driveSize.size(); ++i)
                    if (PathEquals(m_rb.driveSize[i].first, m_rbEmptyRoot)) { size = m_rb.driveSize[i].second; count = m_rb.driveCount[i].second; }
            }
            ImGui::TextColored(Hex(0xFF5C5C), "Permanently delete %s %s (%s) from the Recycle Bin%s%s?",
                FormatCount(count).c_str(), count == 1 ? "item" : "items", FormatSize(size).c_str(),
                m_rbEmptyRoot.empty() ? "" : " on ", m_rbEmptyRoot.empty() ? "" : ToUtf8(m_rbEmptyRoot).c_str());
            if (m_rbEmptyRoot.empty() && m_rb.driveSize.size() > 1)
                for (size_t i = 0; i < m_rb.driveSize.size(); ++i)
                    ImGui::BulletText("%s  %s", ToUtf8(m_rb.driveSize[i].first).c_str(), FormatSize(m_rb.driveSize[i].second).c_str());
            if (m_rbFuture.valid()) ImGui::TextDisabled("(updating numbers...)");
            ImGui::TextDisabled("This frees the space on disk. It cannot be undone.");
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, Hex(0x8A2B2B));
            const bool go = ImGui::Button("Empty Recycle Bin", ImVec2(200 * m_uiScale, 0));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Open it first", ImVec2(140 * m_uiScale, 0))) { shell::OpenRecycleBin(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100 * m_uiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            if (go)
            {
                m_deferred = Deferred::EmptyRecycleBin;
                m_deferredDir = m_rbEmptyRoot;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- error
        if (m_openError) { ImGui::OpenPopup("GEX###err"); m_openError = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("GEX###err", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(Hex(0xFFC857), "%s", m_errorText.c_str());
            if (ImGui::Button("OK", ImVec2(120 * m_uiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- about
        if (m_openAbout) { ImGui::OpenPopup("About GEX###about"); m_openAbout = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("About GEX###about", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(Hex(0xB06BFF), "GEX - Graphical Explorer");
            ImGui::Text("Disk space analyzer and file manager for Windows.");
            ImGui::TextDisabled("Built with Dear ImGui %s (github.com/ocornut/imgui)", IMGUI_VERSION);
            ImGui::Separator();
            ImGui::BulletText("Multithreaded scan including hidden and system files");
            ImGui::BulletText("Explorer-style browsing, Windows context menu, clipboard, drag & drop");
            ImGui::BulletText("Treemap, largest files/folders, file type groups");
            ImGui::BulletText("Live monitor of recently changed files");
            if (ImGui::Button("Close", ImVec2(120 * m_uiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- shortcuts
        if (m_openShortcuts) { ImGui::OpenPopup("Keyboard shortcuts###keys"); m_openShortcuts = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Keyboard shortcuts###keys", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static const char* keys[][2] = {
                { "Enter / double click", "Open file or folder" }, { "Backspace, Alt+Up", "Parent folder" },
                { "Alt+Left / Alt+Right", "Back / forward (mouse buttons 4/5 too)" }, { "Del", "Move to Recycle Bin" },
                { "Shift+Del", "Delete permanently" }, { "F2", "Rename" }, { "Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste (works with Explorer)" },
                { "Ctrl+Shift+C", "Copy full path" }, { "Ctrl+A", "Select all" }, { "Ctrl+Shift+N", "New folder" },
                { "Alt+Enter", "Properties" }, { "Shift+F10 / Menu key", "Windows context menu" },
                { "Shift + right click", "Windows context menu (extended)" }, { "F5", "Rescan current folder" },
                { "Ctrl+F5", "Rescan everything" }, { "Ctrl+F", "Filter current folder" }, { "Ctrl+L, F4, Alt+D", "Type a path" },
                { "Ctrl+1 ... Ctrl+5", "Switch tabs" }, { "Drag a name", "Drop the files into Explorer / other apps" },
                { "Drag elsewhere in a list", "Box select (hold Ctrl to add to the selection)" },
            };
            if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
            {
                for (auto& k : keys)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextColored(Hex(0x9FD8FF), "%s", k[0]);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(k[1]);
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Close", ImVec2(120 * m_uiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}
