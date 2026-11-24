// GEX - Graphical Explorer
// Main views: folder tree, explorer list, treemap, largest files/folders, file types, recent changes.
#include "App.h"
#include "IconCache.h"
#include "Icons.h"
#include "Util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace gex
{
    static std::string IL(const char* icon, const char* text)
    {
        if (!g_haveIconFont) return text;
        return std::string(icon) + "  " + text;
    }

    static ImVec4 Hex(uint32_t rgb, float a = 1.0f)
    {
        return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
    }

    static const float kPi = 3.14159265358979f;
    static uint64_t Mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
    static uint64_t HashStr(const char* s) { uint64_t h = 1469598103934665603ull; while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ull; } return h; }

    static bool IsAncestor(const Node* a, const Node* n)
    {
        for (const Node* p = n ? n->parent : nullptr; p; p = p->parent) if (p == a) return true;
        return false;
    }

    static void TextRight(const char* text)
    {
        const float w = ImGui::CalcTextSize(text).x;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
        ImGui::TextUnformatted(text);
    }

    bool App::PassesFilter(const Node* n) const
    {
        if (!m_set.showHidden && n->IsHidden()) return false;
        if (!m_set.showSystem && n->IsSystem()) return false;
        return true;
    }

    // ===================================================================== cells
    void App::DrawNameCell(Node* n, const std::wstring* fullPathHint)
    {
        const float fs = ImGui::GetFontSize();
        ImTextureID tex = 0;
        if (n->IsDir()) tex = icons::ForFolder();
        else if (icons::NeedsFullPath(n->name)) tex = icons::ForFile(fullPathHint ? *fullPathHint : m_tree->FullPath(n), n->name);
        else tex = icons::ForFile(std::wstring(), n->name);
        if (tex)
            ImGui::Image(tex, ImVec2(fs, fs));
        else
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + fs - 2, p.y + fs - 2), CategoryColorU32(n->ColorCategory()), 3.0f);
            ImGui::Dummy(ImVec2(fs, fs));
        }
        ImGui::SameLine(0, 6);
        ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        if (n->IsHidden() || n->IsSystem()) col.w = 0.55f;
        if (n->IsRemoved()) col = Hex(0x7D8499);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(n->name.c_str());
        ImGui::PopStyleColor();
        if (n->flags & NF_Error) { ImGui::SameLine(); ImGui::TextColored(Hex(0xFF5C5C), "(access denied)"); }
        else if (n->flags & NF_Reparse) { ImGui::SameLine(); ImGui::TextColored(Hex(0x5CC8FF), "(link)"); }
        else if (n->flags & NF_Partial) { ImGui::SameLine(); ImGui::TextColored(Hex(0xFF8A5C), "(not scanned)"); }
        else if (n->IsRemoved()) { ImGui::SameLine(); ImGui::TextDisabled("(deleted)"); }
    }

    void App::DrawSizeBar(uint64_t size, uint64_t total, int colorCat)
    {
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = ImGui::GetFontSize();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float frac = total ? (float)((double)size / (double)total) : 0.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x, p.y + 2), ImVec2(p.x + w, p.y + h - 1), IM_COL32(30, 34, 48, 255), 3.0f);
        if (frac > 0.0f)
        {
            const ImU32 c = CategoryColorU32(colorCat);
            const ImU32 c2 = CategoryColorU32(colorCat, 0.55f);
            dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + 2), ImVec2(p.x + std::max(2.0f, w * frac), p.y + h - 1), c, c2, c2, c);
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f%%", frac * 100.0f);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y), IM_COL32(240, 242, 250, 230), buf);
        ImGui::Dummy(ImVec2(w, h));
    }

    // ===================================================================== sorting
    static bool LessNameCI(const std::string& a, const std::string& b)
    {
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i)
        {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if (x != y) return (unsigned char)x < (unsigned char)y;
        }
        return a.size() < b.size();
    }

    void App::SortRows(ListState& st)
    {
        auto& r = st.rows;
        const bool asc = st.sortAsc;
        switch (st.sortCol)
        {
        case 0: // name, folders first like Explorer
            std::sort(r.begin(), r.end(), [asc](const Node* a, const Node* b)
            {
                if (a->IsDir() != b->IsDir()) return a->IsDir();
                return asc ? LessNameCI(a->name, b->name) : LessNameCI(b->name, a->name);
            });
            break;
        case 3: // items
            std::sort(r.begin(), r.end(), [asc](const Node* a, const Node* b)
            {
                const uint64_t x = a->IsDir() ? (uint64_t)a->fileCount + a->dirCount : 0, y = b->IsDir() ? (uint64_t)b->fileCount + b->dirCount : 0;
                if (x != y) return asc ? x < y : x > y;
                return a->size > b->size;
            });
            break;
        case 4: // modified
            std::sort(r.begin(), r.end(), [asc](const Node* a, const Node* b) { return asc ? a->mtime < b->mtime : a->mtime > b->mtime; });
            break;
        case 5: // type
            std::sort(r.begin(), r.end(), [asc](const Node* a, const Node* b)
            {
                const int x = a->ColorCategory(), y = b->ColorCategory();
                if (x != y) return asc ? x < y : x > y;
                return a->size > b->size;
            });
            break;
        case 6: // folder
            std::sort(r.begin(), r.end(), [asc](const Node* a, const Node* b)
            {
                if (a->parent != b->parent)
                {
                    const std::string& x = a->parent ? a->parent->name : a->name;
                    const std::string& y = b->parent ? b->parent->name : b->name;
                    return asc ? LessNameCI(x, y) : LessNameCI(y, x);
                }
                return a->size > b->size;
            });
            break;
        default: // size / percent
            if (asc) std::sort(r.begin(), r.end(), [](const Node* a, const Node* b) { return a->size < b->size; });
            else std::sort(r.begin(), r.end(), NodeBiggerFirst);
            break;
        }
    }

    // ===================================================================== generic node table
    void App::DrawNodeList(const char* id, ListState& st, ListMode mode, uint64_t key,
                           const std::function<void(std::vector<Node*>&)>& build, ImVec2 size,
                           uint64_t percentOf, int defaultSortCol)
    {
        ImGuiIO& io = ImGui::GetIO();
        const double now = ImGui::GetTime();
        const bool paramsChanged = key != st.key;
        // Live monitor changes bump the tree version constantly: rebuild big flat lists less often.
        const double minInterval = mode == List_Explorer ? 0.5 : 2.0;
        const bool versionChanged = st.version != m_tree->version && now - st.builtAt > minInterval;
        if (paramsChanged || versionChanged)
        {
            Node* focusNode = (st.focus >= 0 && st.focus < (int)st.rows.size()) ? st.rows[(size_t)st.focus] : nullptr;
            st.rows.clear();
            build(st.rows);
            st.key = key;
            st.version = m_tree->version;
            st.builtAt = now;
            st.sortDirty = true;
            st.totalSize = 0;
            for (Node* n : st.rows) st.totalSize += n->size;
            st.focus = -1;
            if (focusNode)
                for (size_t i = 0; i < st.rows.size(); ++i) if (st.rows[i] == focusNode) { st.focus = (int)i; break; }
            if (paramsChanged) st.anchor = -1;
        }

        ImGui::PushID(id);
        ImGui::BeginChild("##wrap", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImGuiTableFlags tflags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingFixedFit;
        const bool flat = mode != List_Explorer;
        const int ncols = 6;
        if (ImGui::BeginTable("##t", ncols, tflags, ImVec2(0, 0)))
        {
            const float s = m_uiScale;
            auto def = [&](int col) { return (ImGuiTableColumnFlags)(col == defaultSortCol ? (ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending) : 0); };
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide | def(0), 0.0f, 0);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending | def(1), 90 * s, 1);
            ImGui::TableSetupColumn(flat ? "% of total" : "% of folder", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending | def(2), 120 * s, 2);
            if (!flat) ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending | def(3), 80 * s, 3);
            ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending | def(4), 130 * s, 4);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed | def(5), 110 * s, 5);
            if (flat) ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch | def(6), 0.0f, 6);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs())
            {
                if ((ss->SpecsDirty || st.sortDirty) && ss->SpecsCount > 0)
                {
                    st.sortCol = (int)ss->Specs[0].ColumnUserID;
                    st.sortAsc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                    // rows come pre-sorted by size (desc); skip the work in that case
                    if (!(st.sortCol <= 2 && !st.sortAsc && st.sortDirty && !ss->SpecsDirty)) SortRows(st);
                    ss->SpecsDirty = false;
                    st.sortDirty = false;
                }
            }

            if (m_reveal && mode == List_Explorer)
            {
                for (size_t i = 0; i < st.rows.size(); ++i)
                    if (st.rows[i] == m_reveal) { st.focus = st.anchor = (int)i; st.scrollToFocus = true; break; }
                m_reveal = nullptr;
            }

            const bool listFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            st.measuredRow = -1;   // pitch (row height) is kept from previous frames
            ImGuiListClipper clipper;
            clipper.Begin((int)st.rows.size());
            if (st.scrollToFocus && st.focus >= 0) clipper.IncludeItemByIndex(st.focus);
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    Node* n = st.rows[(size_t)i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(n);
                    const bool sel = IsSelected(n);
                    const ImVec2 rowStart = ImGui::GetCursorPos();
                    const ImVec2 rowStartScreen = ImGui::GetCursorScreenPos();
                    {
                        // content-space y of this row -> lets the selection box map y to row indices
                        const float contentY = rowStartScreen.y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
                        if (st.measuredRow >= 0 && i == st.measuredRow + 1 && contentY > st.measuredY) st.pitch = contentY - st.measuredY;
                        st.measuredRow = i;
                        st.measuredY = contentY;
                        if (st.pitch > 0.0f) st.rowY0 = contentY - (float)i * st.pitch;
                    }
                    ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick);
                    const bool hovered = ImGui::IsItemHovered();
                    if (!n->IsRemoved())
                    {
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        {
                            if (io.KeyCtrl) { ToggleSelect(n); st.anchor = i; }
                            else if (io.KeyShift && st.anchor >= 0 && st.anchor < (int)st.rows.size())
                            {
                                std::vector<Node*> range;
                                for (int k = std::min(st.anchor, i); k <= std::max(st.anchor, i); ++k) range.push_back(st.rows[(size_t)k]);
                                SetSelection(range);
                            }
                            else if (!sel) { SelectOnly(n); st.anchor = i; }
                            st.focus = i;
                            // Like Explorer: dragging the name drags the files out, dragging elsewhere draws a selection box
                            const float nameEnd = rowStartScreen.x + ImGui::GetFontSize() + 6.0f + ImGui::CalcTextSize(n->name.c_str()).x + 6.0f;
                            if (io.MousePos.x <= nameEnd) m_dragArmed = true;
                            else BeginBoxSelect(st, io.KeyCtrl);
                        }
                        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && sel && m_sel.size() > 1 && !io.KeyCtrl && !io.KeyShift &&
                            io.MouseDragMaxDistanceSqr[0] < 16.0f)
                        { SelectOnly(n); st.anchor = i; }
                        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift) Activate(n);
                        if (ImGui::IsItemActive() && m_dragArmed && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10.0f * m_uiScale))
                        {
                            m_dragArmed = false;
                            if (!IsSelected(n)) SelectOnly(n);
                            m_deferred = Deferred::DragOut;
                            m_deferredPaths = SelectionPaths();
                        }
                        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                        {
                            if (!IsSelected(n)) { SelectOnly(n); st.anchor = i; }
                            st.focus = i;
                            OpenItemMenu(SelectionPaths(), io.KeyShift);
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            DrawNodeTooltip(n, flat ? percentOf : (n->parent ? n->parent->size : 0));
                    }
                    if (st.scrollToFocus && i == st.focus)
                    {
                        if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
                        st.scrollToFocus = false;
                    }
                    if (i == st.focus && listFocused)
                        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(176, 107, 255, 160), 3.0f);

                    ImGui::SetCursorPos(rowStart);
                    std::wstring fullPath;
                    const std::wstring* hint = nullptr;
                    if (!n->IsDir() && icons::NeedsFullPath(n->name)) { fullPath = m_tree->FullPath(n); hint = &fullPath; }
                    DrawNameCell(n, hint);

                    ImGui::TableNextColumn();
                    TextRight(FormatSize(n->size).c_str());
                    ImGui::TableNextColumn();
                    DrawSizeBar(n->size, flat ? percentOf : (n->parent ? n->parent->size : percentOf), n->ColorCategory());
                    if (!flat)
                    {
                        ImGui::TableNextColumn();
                        if (n->IsDir()) { TextRight(FormatCount((uint64_t)n->fileCount + n->dirCount).c_str()); }
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", FormatFileTime(n->mtime).c_str());
                    ImGui::TableNextColumn();
                    {
                        const std::string ext = n->IsDir() ? std::string() : ExtensionOf(n->name);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(CategoryColorU32(n->ColorCategory(), 0.95f)));
                        if (n->IsDir()) ImGui::TextUnformatted((n->flags & NF_Reparse) ? "Link" : "Folder");
                        else if (!ext.empty()) { std::string up = ext; for (char& c : up) if (c >= 'a' && c <= 'z') c = (char)(c - 32); ImGui::Text("%s", up.c_str()); ImGui::SameLine(0, 4); ImGui::TextDisabled("%s", CategoryName(n->category)); }
                        else ImGui::TextUnformatted(CategoryName(n->category));
                        ImGui::PopStyleColor();
                    }
                    if (flat)
                    {
                        ImGui::TableNextColumn();
                        if (n->parent) ImGui::TextDisabled("%s", m_tree->FullPathUtf8(n->parent).c_str());
                    }
                    ImGui::PopID();
                }
            }
            // empty space below the last row: click clears the selection, drag draws a selection box
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                io.MousePos.x < ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize)
            {
                if (!io.KeyCtrl) ClearSelection();
                BeginBoxSelect(st, io.KeyCtrl);
            }
            UpdateBoxSelect(st);
            ImGui::EndTable();
        }
        // right click on empty space -> folder background menu
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right) && mode == List_Explorer)
        { ClearSelection(); OpenBackgroundMenu(m_cur, io.KeyShift); }
        HandleListShortcuts(st, mode);
        ImGui::EndChild();
        ImGui::PopID();
    }

    // ===================================================================== rubber-band (box) selection
    // Called from inside the table's scrolling window. Coordinates are stored in content space
    // (window-relative + scroll) so the box keeps its anchor while the list auto-scrolls.
    void App::BeginBoxSelect(ListState& st, bool additive)
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const ImVec2 wp = ImGui::GetWindowPos();
        st.boxPending = true;
        st.boxActive = false;
        st.boxStart = ImVec2(m.x - wp.x + ImGui::GetScrollX(), m.y - wp.y + ImGui::GetScrollY());
        st.boxBase = additive ? m_sel : std::vector<Node*>();
    }

    void App::UpdateBoxSelect(ListState& st)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { st.boxPending = st.boxActive = false; return; }
        if (st.boxPending && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f * m_uiScale))
        {
            st.boxPending = false;
            st.boxActive = true;
            m_dragArmed = false;
        }
        if (!st.boxActive) return;
        m_boxSelecting = true;

        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        const float pitch = st.pitch > 0.0f ? st.pitch : ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
        const float headerH = pitch;                       // frozen header row
        const float top = wp.y + headerH, bottom = wp.y + ws.y - ImGui::GetStyle().ScrollbarSize * 0.5f;

        // auto-scroll when the mouse leaves the list vertically
        const ImVec2 m = io.MousePos;
        float scrollY = ImGui::GetScrollY();
        if (m.y < top) scrollY = std::max(0.0f, scrollY - std::min(60.0f, (top - m.y) * 0.35f + 2.0f));
        else if (m.y > bottom) scrollY = std::min(ImGui::GetScrollMaxY(), scrollY + std::min(60.0f, (m.y - bottom) * 0.35f + 2.0f));
        if (scrollY != ImGui::GetScrollY()) ImGui::SetScrollY(scrollY);

        // rows covered by the box (content space)
        const float curY = std::clamp(m.y, top, bottom) - wp.y + scrollY;
        const float y0 = std::min(st.boxStart.y, curY), y1 = std::max(st.boxStart.y, curY);
        std::vector<Node*> sel = st.boxBase;
        std::unordered_set<const Node*> seen(sel.begin(), sel.end());
        int first = -1, last = -1;
        if (!st.rows.empty() && st.rowY0 > -1e20f)
        {
            const float rowTop0 = st.rowY0 - ImGui::GetStyle().CellPadding.y;   // rowY0 is the first cell's content start
            first = (int)std::floor((y0 - rowTop0) / pitch);
            last = (int)std::floor((y1 - rowTop0) / pitch);
            first = std::max(first, 0);
            last = std::min(last, (int)st.rows.size() - 1);
            for (int i = first; i <= last; ++i)
            {
                Node* n = st.rows[(size_t)i];
                if (!n->IsRemoved() && seen.insert(n).second) sel.push_back(n);
            }
        }
        SetSelection(sel);
        if (last >= first && first >= 0)
        {
            const bool down = curY >= st.boxStart.y;
            st.focus = down ? last : first;
            st.anchor = down ? first : last;
        }

        // draw the box (screen space), clipped to the list area
        const float scrollX = ImGui::GetScrollX();
        const ImVec2 a(wp.x + st.boxStart.x - scrollX, wp.y + st.boxStart.y - scrollY);
        const ImVec2 b(std::clamp(m.x, wp.x, wp.x + ws.x), std::clamp(m.y, top, bottom));
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->PushClipRect(ImVec2(wp.x, top), ImVec2(wp.x + ws.x, wp.y + ws.y), false);
        const ImVec2 mn(std::min(a.x, b.x), std::min(a.y, b.y)), mx(std::max(a.x, b.x), std::max(a.y, b.y));
        dl->AddRectFilled(mn, mx, IM_COL32(91, 124, 255, 45));
        dl->AddRect(mn, mx, IM_COL32(120, 150, 255, 220), 0.0f, 0, 1.0f);
        dl->PopClipRect();
    }

    // ===================================================================== folder tree (left)
    // Row of the folder tree. 'more[d]' tells whether the ancestor at depth d still has siblings below it
    // (needed to draw the classic connector lines).
    void App::DrawFolderTreeNode(Node* n, int depth, std::vector<uint8_t>& more)
    {
        // visible sub folders
        std::vector<Node*> kids;
        for (Node* c : n->children) if (c->IsDir() && PassesFilter(c)) kids.push_back(c);
        const bool hasKids = !kids.empty();
        if (m_syncTreeToCur && hasKids && (IsAncestor(n, m_cur))) m_treeOpen.insert(n);
        bool open = hasKids && m_treeOpen.count(n) != 0;

        const ImGuiStyle& style = ImGui::GetStyle();
        const float fs = ImGui::GetFontSize();
        ImGui::TableNextRow();
        const bool sizesFirst = m_set.treeSizesFirst;
        auto sizeCells = [&]()
        {
            ImGui::TableNextColumn();
            TextRight(FormatSize(n->size).c_str());
            ImGui::TableNextColumn();
            DrawSizeBar(n->size, n->parent ? n->parent->size : n->size, Cat_Folder);
        };
        ImGui::PushID(n);
        if (sizesFirst) sizeCells();
        ImGui::TableNextColumn();
        // The folder column scrolls horizontally on its own (sizes stay pinned): shift its content left.
        if (depth == 0)
        {
            m_treeColX = ImGui::GetCursorScreenPos().x;
            m_treeColW = ImGui::GetContentRegionAvail().x;
        }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - m_treeScrollX);

        float rowStartX = 0.0f;
        bool clicked = false, rightClicked = false, hovered = false;
        if (m_set.treeStyle == 1)
        {
            // ---- Dear ImGui arrows
            // NoTreePushOnOpen: children are indented by us (works the same whichever column holds the tree)
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAllColumns |
                                       ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (n == m_cur) flags |= ImGuiTreeNodeFlags_Selected;
            if (!hasKids) flags |= ImGuiTreeNodeFlags_Leaf;
            ImGui::SetNextItemOpen(open, ImGuiCond_Always);
            rowStartX = ImGui::GetCursorScreenPos().x;
            const bool nowOpen = ImGui::TreeNodeEx((void*)n, flags, "%s", "");
            if (ImGui::IsItemToggledOpen()) { if (nowOpen) m_treeOpen.insert(n); else m_treeOpen.erase(n); }
            clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
            hovered = ImGui::IsItemHovered();
            open = hasKids && nowOpen;
            ImGui::SameLine(0, 2);
        }
        else
        {
            // ---- classic Windows tree: connector lines and [+] / [-] boxes
            const float indent = fs * 1.15f;
            const ImVec2 cellPos = ImGui::GetCursorScreenPos();
            const ImVec2 cellLocal = ImGui::GetCursorPos();
            const float rowTop = cellPos.y - style.CellPadding.y;
            const float rowBot = cellPos.y + fs + style.CellPadding.y;
            const float midY = std::floor(cellPos.y + fs * 0.5f) + 0.5f;
            auto cx = [&](int d) { return std::floor(cellPos.x + d * indent + indent * 0.5f) + 0.5f; };
            const float half = std::floor(fs * 0.27f);
            const ImVec2 boxMin(cx(depth) - half, midY - half), boxMax(cx(depth) + half + 1, midY + half + 1);

            ImGui::Selectable("##tn", n == m_cur, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, fs));
            hovered = ImGui::IsItemHovered();
            const ImVec2 m = ImGui::GetIO().MousePos;
            const bool onBox = hasKids && m.x >= boxMin.x - 3 && m.x <= boxMax.x + 3 && m.y >= rowTop && m.y <= rowBot;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                if (onBox || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (hasKids) { if (open) m_treeOpen.erase(n); else m_treeOpen.insert(n); open = !open; }
                }
                else clicked = true;
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 lineCol = IM_COL32(105, 114, 140, 210);
            // ancestors' vertical lines
            for (int a = 1; a < depth; ++a)
                if (more[(size_t)a]) dl->AddLine(ImVec2(cx(a - 1), rowTop), ImVec2(cx(a - 1), rowBot), lineCol);
            if (depth > 0)
            {
                // elbow from the parent's column
                dl->AddLine(ImVec2(cx(depth - 1), rowTop), ImVec2(cx(depth - 1), more[(size_t)depth] ? rowBot : midY), lineCol);
                dl->AddLine(ImVec2(cx(depth - 1), midY), ImVec2(hasKids ? boxMin.x : cx(depth) + half, midY), lineCol);
            }
            if (hasKids)
            {
                if (open) dl->AddLine(ImVec2(cx(depth), boxMax.y), ImVec2(cx(depth), rowBot), lineCol);
                const bool boxHot = hovered && onBox;
                dl->AddRectFilled(boxMin, boxMax, boxHot ? IM_COL32(58, 69, 112, 255) : IM_COL32(30, 34, 48, 255));
                dl->AddRect(boxMin, boxMax, boxHot ? IM_COL32(176, 107, 255, 255) : IM_COL32(140, 148, 172, 255));
                const ImU32 sign = IM_COL32(225, 230, 242, 255);
                const float pad = std::max(2.0f, half * 0.45f);
                dl->AddLine(ImVec2(boxMin.x + pad, midY), ImVec2(boxMax.x - pad, midY), sign);
                if (!open) dl->AddLine(ImVec2(cx(depth), boxMin.y + pad), ImVec2(cx(depth), boxMax.y - pad), sign);
            }
            rowStartX = cellPos.x + depth * indent;
            ImGui::SetCursorPos(ImVec2(cellLocal.x + (depth + 1) * indent + 2.0f, cellLocal.y));
        }

        if (clicked) Navigate(n);
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) rightClicked = true;
        if (rightClicked) OpenItemMenu({ m_tree->FullPath(n) }, shell::IsShiftDown());
        if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
            DrawNodeTooltip(n, n->parent ? n->parent->size : n->size);
        if (n == m_cur && m_syncTreeToCur && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.35f);

        if (ImTextureID tex = icons::ForFolder()) { ImGui::Image(tex, ImVec2(fs, fs)); ImGui::SameLine(0, 5); }
        if (n->flags & NF_Error) ImGui::TextColored(Hex(0xFF8A8A), "%s", n->name.c_str());
        else if (n->IsHidden()) ImGui::TextColored(ImVec4(1, 1, 1, 0.55f), "%s", n->name.c_str());
        else if (n == m_cur) ImGui::TextColored(Hex(0x9FD8FF), "%s", n->name.c_str());
        else ImGui::TextUnformatted(n->name.c_str());
        {
            // Measure the folder column's content width (unshifted) so the scrollbar knows its range.
            const float right = ImGui::GetItemRectMax().x - m_treeColX + m_treeScrollX;
            m_treeNameWidthNext = std::max(m_treeNameWidthNext, right);
            if (n == m_cur && m_syncTreeToCur)
            {
                // keep the current folder's name visible horizontally
                const float left = rowStartX - m_treeColX + m_treeScrollX;
                if (right - m_treeScrollX > m_treeColW || left < m_treeScrollX)
                    m_treeScrollX = std::max(0.0f, left - fs * 1.5f);
            }
        }
        if (!sizesFirst) sizeCells();
        ImGui::PopID();

        if (open)
        {
            if (m_set.treeStyle == 1) ImGui::Indent(style.IndentSpacing);
            if (more.size() < (size_t)depth + 2) more.resize((size_t)depth + 2);
            for (size_t i = 0; i < kids.size(); ++i)
            {
                more[(size_t)depth + 1] = (uint8_t)(i + 1 < kids.size());
                DrawFolderTreeNode(kids[i], depth + 1, more);
            }
            if (m_set.treeStyle == 1) ImGui::Unindent(style.IndentSpacing);
        }
    }

    void App::DrawFolderTree()
    {
        if (!m_tree) return;
        ImGui::TextColored(Hex(0xFFC857), "%s", IL(ICON_FOLDER, "Folders by size").c_str());
        ImGui::SetItemTooltip("Long names: use the scrollbar under the folder names or Shift + mouse wheel. You can also drag the right edge of this pane to widen it.");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize("View").x - ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::SmallButton("View")) ImGui::OpenPopup("##treeview");
        if (ImGui::BeginPopup("##treeview"))
        {
            bool dirty = false;
            ImGui::TextDisabled("Tree style");
            if (ImGui::RadioButton("Classic lines  [+] / [-]", m_set.treeStyle == 0)) { m_set.treeStyle = 0; dirty = true; }
            if (ImGui::RadioButton("Arrows", m_set.treeStyle == 1)) { m_set.treeStyle = 1; dirty = true; }
            ImGui::Separator();
            ImGui::TextDisabled("Columns");
            if (ImGui::RadioButton("Names first, sizes pinned on the right", !m_set.treeSizesFirst)) { m_set.treeSizesFirst = false; dirty = true; }
            if (ImGui::RadioButton("Sizes on the left", m_set.treeSizesFirst)) { m_set.treeSizesFirst = true; dirty = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Collapse all")) { m_treeOpen.clear(); m_treeOpen.insert(m_tree->root); }
            if (dirty) SaveSettings();
            ImGui::EndPopup();
        }

        // Only the folder-name column scrolls horizontally (own scrollbar below it); size columns stay pinned.
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGuiIO& io = ImGui::GetIO();
        const float sizeW = 70 * m_uiScale, pctW = 56 * m_uiScale;
        const bool sizesFirst = m_set.treeSizesFirst;
        const float contentW = m_treeNameWidth + ImGui::GetFontSize();
        const float maxScroll = std::max(0.0f, contentW - m_treeColW);
        m_treeScrollX = std::clamp(m_treeScrollX, 0.0f, maxScroll);
        const bool needBar = maxScroll > 1.0f;
        const float barH = std::floor(st.ScrollbarSize * 0.85f);
        m_treeNameWidthNext = 0.0f;
        if (m_treeOpen.empty()) m_treeOpen.insert(m_tree->root);
        if (ImGui::BeginTable(sizesFirst ? "##tree" : "##tree2", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, needBar ? -(barH + st.ItemSpacing.y) : 0.0f)))
        {
            if (sizesFirst)
            {
                // indentation must be enabled on the tree column explicitly when it is not the first one
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, sizeW);
                ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, pctW);
                ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
            }
            else
            {
                ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, sizeW);
                ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, pctW);
            }
            std::vector<uint8_t> more(64, 0);
            DrawFolderTreeNode(m_tree->root, 0, more);
            // Shift + wheel (or a horizontal wheel / touchpad swipe) scrolls the names
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            {
                const float wheel = io.MouseWheelH + (io.KeyShift ? io.MouseWheel : 0.0f);
                if (wheel != 0.0f) m_treeScrollX = std::clamp(m_treeScrollX - wheel * ImGui::GetFontSize() * 3.0f, 0.0f, maxScroll);
            }
            ImGui::EndTable();
        }

        if (needBar)
        {
            // custom horizontal scrollbar, exactly under the folder column
            const float trackX = m_treeColX, trackW = std::max(20.0f, m_treeColW);
            ImGui::SetCursorScreenPos(ImVec2(trackX, ImGui::GetCursorScreenPos().y));
            ImGui::InvisibleButton("##treehscroll", ImVec2(trackW, barH));
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            const float thumbW = std::max(24.0f * m_uiScale, trackW * (m_treeColW / contentW));
            const float travel = std::max(1.0f, trackW - thumbW);
            float thumbX = travel * (m_treeScrollX / maxScroll);
            const float mx = io.MousePos.x - trackX;
            if (ImGui::IsItemActivated())
            {
                // grab the thumb where clicked, or jump so the thumb centers on the mouse
                m_treeGrabOffset = (mx >= thumbX && mx <= thumbX + thumbW) ? mx - thumbX : thumbW * 0.5f;
            }
            if (ImGui::IsItemActive())
            {
                thumbX = std::clamp(mx - m_treeGrabOffset, 0.0f, travel);
                m_treeScrollX = maxScroll * (thumbX / travel);
            }
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = barH * 0.5f;
            dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_ScrollbarBg), r);
            dl->AddRectFilled(ImVec2(a.x + thumbX + 2, a.y + 2), ImVec2(a.x + thumbX + thumbW - 2, b.y - 2),
                              ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ScrollbarGrabActive : hot ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab), r);
            ImGui::SetItemTooltip("Scroll folder names (Shift + mouse wheel works too)");
        }
        else
            m_treeScrollX = 0.0f;
        m_treeNameWidth = m_treeNameWidthNext;
        m_syncTreeToCur = false;
    }

    // ===================================================================== tabs
    void App::DrawTabs()
    {
        if (!ImGui::BeginTabBar("##tabs")) return;
        auto tab = [&](int idx, const char* icon, const char* label, const char* id) -> bool
        {
            ImGuiTabItemFlags f = (m_requestTab == idx) ? ImGuiTabItemFlags_SetSelected : 0;
            const std::string l = IL(icon, label) + id;
            const bool open = ImGui::BeginTabItem(l.c_str(), nullptr, f);
            if (open) m_activeTab = idx;
            return open;
        };
        if (tab(Tab_Explorer, ICON_FOLDER, "Explorer", "###explorer")) { DrawExplorerTab(); ImGui::EndTabItem(); }
        if (tab(Tab_Treemap, ICON_TILES, "Treemap", "###treemap")) { DrawTreemapTab(); ImGui::EndTabItem(); }
        if (tab(Tab_Top, ICON_LIST, "Largest", "###top")) { DrawTopTab(); ImGui::EndTabItem(); }
        if (tab(Tab_Types, ICON_PIE, "File types", "###types")) { DrawTypesTab(); ImGui::EndTabItem(); }
        if (tab(Tab_Recent, ICON_HISTORY, "Recent changes", "###recent")) { DrawRecentTab(); ImGui::EndTabItem(); }
        m_requestTab = -1;
        ImGui::EndTabBar();
    }

    // ===================================================================== explorer tab
    void App::DrawExplorerTab()
    {
        if (!m_cur) return;
        ImGui::TextColored(Hex(0xFFC857), "%s", m_cur->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  %s files in %s folders", FormatSize(m_cur->size).c_str(), FormatCount(m_cur->fileCount).c_str(), FormatCount(m_cur->dirCount).c_str());
        if (m_filter[0])
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Search all subfolders"))
            {
                snprintf(m_topSearch, sizeof(m_topSearch), "%s", m_filter);
                m_topScopeCur = true; m_topMode = 0; m_requestTab = Tab_Top;
            }
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 120 * m_uiScale);
        if (ImGui::Checkbox("Treemap", &m_set.treemapInExplorer)) SaveSettings();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float splitterH = 6.0f * m_uiScale;
        const float listH = m_set.treemapInExplorer ? std::max(80.0f, (avail.y - splitterH) * (1.0f - m_set.treemapSplit)) : 0.0f;

        const std::string filterLower = ToLowerAscii(m_filter);
        uint64_t key = Mix(Mix(Mix((uint64_t)(uintptr_t)m_cur, HashStr(filterLower.c_str())), m_set.showHidden), m_set.showSystem);
        Node* cur = m_cur;
        DrawNodeList("##explorer", m_explorerList, List_Explorer, key, [&](std::vector<Node*>& rows)
        {
            rows.reserve(cur->children.size());
            for (Node* c : cur->children)
                if (PassesFilter(c) && ContainsNoCase(c->name, filterLower)) rows.push_back(c);
        }, ImVec2(0, listH), cur->size, 1);

        if (m_set.treemapInExplorer)
        {
            ImGui::InvisibleButton("##split", ImVec2(avail.x, splitterH));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActive())
                m_set.treemapSplit = std::clamp(m_set.treemapSplit - ImGui::GetIO().MouseDelta.y / std::max(1.0f, avail.y), 0.1f, 0.9f);
            if (ImGui::IsItemDeactivated()) SaveSettings();
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, (a.y + b.y) * 0.5f), ImVec2(b.x, (a.y + b.y) * 0.5f),
                ImGui::IsItemHovered() ? IM_COL32(176, 107, 255, 255) : IM_COL32(60, 66, 90, 255), 2.0f);
            DrawTreemapView(m_tmExplorer, "##tmexp", m_cur, ImGui::GetContentRegionAvail(), true);
        }
    }

    // ===================================================================== treemap
    void App::DrawTreemapView(Treemap& tm, const char* id, Node* root, ImVec2 size, bool navigateOnDouble)
    {
        ImGuiIO& io = ImGui::GetIO();
        const Treemap::Result r = tm.Draw(id, m_tree.get(), root, size, m_sel);
        if (r.hovered) m_tmHover = r.hovered;
        if (r.hovered && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            DrawNodeTooltip(r.hovered, root ? root->size : 0);
        if (r.clicked)
        {
            if (io.KeyCtrl) ToggleSelect(r.clicked); else SelectOnly(r.clicked);
            m_dragArmed = true;
        }
        if (r.doubleClicked)
        {
            Node* top = r.doubleClicked;
            while (top->parent && top->parent != root) top = top->parent;
            if (top->IsDir())
            {
                if (navigateOnDouble) Navigate(top); else m_tmRoot = top;
            }
            else Reveal(r.doubleClicked);
        }
        if (ImGui::IsItemActive() && m_dragArmed && !m_sel.empty() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10.0f * m_uiScale))
        {
            m_dragArmed = false;
            m_deferred = Deferred::DragOut;
            m_deferredPaths = SelectionPaths();
        }
        if (r.rightClicked)
        {
            if (!IsSelected(r.rightClicked)) SelectOnly(r.rightClicked);
            OpenItemMenu(SelectionPaths(), io.KeyShift);
        }
        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_sel.empty() && !io.WantTextInput)
            RequestDelete(SelectionPaths(), io.KeyShift);
    }

    void App::DrawTreemapTab()
    {
        if (!m_tmRoot || m_tmRoot->IsRemoved()) m_tmRoot = m_cur;
        if (!m_tmRoot) return;
        ImGui::BeginDisabled(!m_tmRoot->parent);
        if (ImGui::Button(IL(ICON_ZOOM_OUT, "Zoom out").c_str()) && m_tmRoot->parent) m_tmRoot = m_tmRoot->parent;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(IL(ICON_HOME, "Explorer folder").c_str())) m_tmRoot = m_cur;
        ImGui::SetItemTooltip("Show the folder currently open in the Explorer tab");
        ImGui::SameLine();
        if (ImGui::Button(IL(ICON_FOLDER_OPEN, "Open this folder in Explorer tab").c_str())) { Navigate(m_tmRoot); m_requestTab = Tab_Explorer; }
        ImGui::SameLine();
        ImGui::TextColored(Hex(0xFFC857), "%s", m_tree->FullPathUtf8(m_tmRoot).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", FormatSize(m_tmRoot->size).c_str());

        // legend
        for (int c = 0; c < Cat_COUNT; ++c)
        {
            if (c) ImGui::SameLine(0, 14 * m_uiScale);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float fs = ImGui::GetFontSize();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 3), ImVec2(p.x + fs - 4, p.y + fs - 1), CategoryColorU32(c), 3.0f);
            ImGui::Dummy(ImVec2(fs - 2, fs));
            ImGui::SameLine(0, 4);
            ImGui::TextUnformatted(CategoryName(c));
        }
        ImGui::TextDisabled("Click: select   Double click: zoom into folder   Right click: menu   Zoom out: parent folder");
        DrawTreemapView(m_tmMain, "##tmmain", m_tmRoot, ImGui::GetContentRegionAvail(), false);
    }

    // ===================================================================== largest files / folders
    void App::DrawTopTab()
    {
        static const char* kMinSizes[] = { "Any size", "> 1 MB", "> 10 MB", "> 100 MB", "> 1 GB", "> 10 GB" };
        static const uint64_t kMinBytes[] = { 0, 1ull << 20, 10ull << 20, 100ull << 20, 1ull << 30, 10ull << 30 };
        ImGui::RadioButton("Files", &m_topMode, 0); ImGui::SameLine();
        ImGui::RadioButton("Folders", &m_topMode, 1); ImGui::SameLine();
        ImGui::SetNextItemWidth(220 * m_uiScale);
        ImGui::InputTextWithHint("##topsearch", "Search name...", m_topSearch, sizeof(m_topSearch));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150 * m_uiScale);
        if (ImGui::BeginCombo("##cat", m_topCat < 0 ? "All types" : CategoryName(m_topCat)))
        {
            if (ImGui::Selectable("All types", m_topCat < 0)) m_topCat = -1;
            for (int c = 0; c < Cat_COUNT; ++c)
            {
                ImGui::PushID(c);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const std::string label = std::string("      ") + CategoryName(c);
                if (ImGui::Selectable(label.c_str(), m_topCat == c)) m_topCat = c;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + 2, p.y + 3), ImVec2(p.x + ImGui::GetFontSize() - 2, p.y + ImGui::GetFontSize() - 1), CategoryColorU32(c), 3.0f);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110 * m_uiScale);
        ImGui::Combo("##min", &m_topMinSize, kMinSizes, IM_ARRAYSIZE(kMinSizes));
        ImGui::SameLine();
        ImGui::Checkbox("Only inside current folder", &m_topScopeCur);
        ImGui::SameLine();
        if (ImGui::Button(IL(ICON_EXPORT, "Export CSV").c_str())) ExportCsv(m_topList.rows);
        ImGui::SetItemTooltip("Saves the list to %%APPDATA%%\\GEX\\gex_export.csv and shows it in Explorer");

        const std::string search = ToLowerAscii(m_topSearch);
        const int mode = m_topMode, cat = m_topCat;
        const uint64_t minBytes = kMinBytes[m_topMinSize];
        Node* scope = m_topScopeCur ? m_cur : nullptr;
        uint64_t key = Mix(Mix(Mix(Mix(Mix(Mix(HashStr(search.c_str()), (uint64_t)mode), (uint64_t)(cat + 1)), minBytes),
                           (uint64_t)(uintptr_t)scope), m_set.showHidden), m_set.showSystem);
        ImGui::TextDisabled("%s items, %s", FormatCount(m_topList.rows.size()).c_str(), FormatSize(m_topList.totalSize).c_str());
        DrawNodeList("##top", m_topList, List_Flat, key, [&](std::vector<Node*>& rows)
        {
            const auto& src = mode == 0 ? m_tree->AllFiles() : m_tree->AllDirs();
            for (Node* n : src)
            {
                if (n->size < minBytes) break; // sorted by size
                if (!PassesFilter(n)) continue;
                if (mode == 0 && cat >= 0 && n->category != cat) continue;
                if (!search.empty() && !ContainsNoCase(n->name, search)) continue;
                if (scope && !IsAncestor(scope, n)) continue;
                rows.push_back(n);
            }
        }, ImVec2(0, 0), m_tree->root->size, 1);
    }

    // ===================================================================== file types
    void App::ComputeTypeStats()
    {
        Node* scope = m_typeScopeCur ? m_cur : m_tree->root;
        const uint64_t key = Mix(Mix((uint64_t)(uintptr_t)scope, (uint64_t)m_typeGroupMode), m_set.showHidden * 2 + m_set.showSystem);
        const double now = ImGui::GetTime();
        if (key == m_typeStatsKey && (m_tree->version == m_typeStatsVersion || now - m_typeStatsAt < 1.5)) return;
        m_typeStatsKey = key;
        m_typeStatsVersion = m_tree->version;
        m_typeStatsAt = now;
        m_typeStats.clear();
        m_typeTotal = 0;

        std::vector<TypeStat> cats(Cat_COUNT);
        std::unordered_map<std::string, TypeStat> exts;
        auto add = [&](Node* f)
        {
            if (!PassesFilter(f)) return;
            m_typeTotal += f->size;
            if (m_typeGroupMode == 0)
            {
                TypeStat& t = cats[f->category < Cat_COUNT ? f->category : Cat_Other];
                t.size += f->size; t.count++;
            }
            else
            {
                const std::string e = ExtensionOf(f->name);
                TypeStat& t = exts[e];
                t.size += f->size; t.count++;
            }
        };
        if (scope == m_tree->root)
            for (Node* f : m_tree->AllFiles()) add(f);
        else
        {
            std::vector<Node*> stack{ scope };
            while (!stack.empty())
            {
                Node* d = stack.back(); stack.pop_back();
                for (Node* c : d->children) { if (c->IsDir()) stack.push_back(c); else add(c); }
            }
        }
        if (m_typeGroupMode == 0)
        {
            for (int c = 0; c < Cat_COUNT; ++c)
                if (cats[(size_t)c].count) { cats[(size_t)c].key = CategoryName(c); cats[(size_t)c].cat = c; cats[(size_t)c].catIndex = c; m_typeStats.push_back(cats[(size_t)c]); }
        }
        else
        {
            for (auto& kv : exts)
            {
                TypeStat t = kv.second;
                t.key = kv.first.empty() ? "(no extension)" : "." + kv.first;
                t.cat = ClassifyExtension(kv.first);
                m_typeStats.push_back(t);
            }
        }
        std::sort(m_typeStats.begin(), m_typeStats.end(), [](const TypeStat& a, const TypeStat& b) { return a.size > b.size; });
        if (m_typeStats.size() > 3000) m_typeStats.resize(3000);
        if (m_typeSelKey.empty() && !m_typeStats.empty()) m_typeSelKey = m_typeStats[0].key;
    }

    void App::DrawTypesTab()
    {
        bool changed = false;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Group by:"); ImGui::SameLine();
        changed |= ImGui::RadioButton("Media groups", &m_typeGroupMode, 0); ImGui::SameLine();
        changed |= ImGui::RadioButton("Extensions", &m_typeGroupMode, 1); ImGui::SameLine();
        changed |= ImGui::Checkbox("Only current folder", &m_typeScopeCur);
        if (changed) m_typeSelKey.clear();
        ComputeTypeStats();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##typesleft", ImVec2(avail.x * 0.42f, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
        {
            // ---- donut chart
            const float s = m_uiScale;
            const float chartH = std::min(260.0f * s, ImGui::GetContentRegionAvail().x * 0.8f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##donut", ImVec2(w, chartH));
            const bool hov = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 c(p.x + w * 0.5f, p.y + chartH * 0.5f);
            const float R = chartH * 0.46f, r = R * 0.58f;
            const ImVec2 m = ImGui::GetIO().MousePos;
            const float mdx = m.x - c.x, mdy = m.y - c.y;
            const float mdist = std::sqrt(mdx * mdx + mdy * mdy);
            float mang = std::atan2(mdy, mdx) + kPi * 0.5f;
            if (mang < 0) mang += 2 * kPi;
            int hovered = -1;
            const float kTwoPi = 2.0f * kPi;
            float a0 = 0.0f;
            const int maxSeg = 16;
            uint64_t restSize = 0;
            for (size_t i = 0; i < m_typeStats.size(); ++i) if ((int)i >= maxSeg) restSize += m_typeStats[i].size;
            const size_t segs = std::min(m_typeStats.size(), (size_t)maxSeg) + (restSize ? 1 : 0);
            for (size_t i = 0; i < segs && m_typeTotal; ++i)
            {
                const bool isRest = (int)i >= maxSeg;
                const uint64_t sz = isRest ? restSize : m_typeStats[i].size;
                const float span = kTwoPi * (float)((double)sz / (double)m_typeTotal);
                const float a1 = a0 + span;
                const bool isHover = hov && mdist >= r && mdist <= R * 1.08f && mang >= a0 && mang < a1;
                if (isHover) hovered = (int)i;
                const bool isSel = !isRest && m_typeStats[i].key == m_typeSelKey;
                ImU32 col = isRest ? IM_COL32(90, 96, 115, 255) : CategoryColorU32(m_typeStats[i].cat);
                if (m_typeGroupMode == 1 && !isRest)
                {
                    // vary brightness between extensions of the same group
                    ImVec4 v = ImGui::ColorConvertU32ToFloat4(col);
                    const float f = 0.75f + 0.25f * (float)((i * 7) % 5) / 4.0f;
                    col = ImGui::ColorConvertFloat4ToU32(ImVec4(v.x * f, v.y * f, v.z * f, 1));
                }
                const float ro = (isHover || isSel) ? R * 1.06f : R;
                const int steps = std::max(2, (int)(span * 40.0f));
                for (int k = 0; k < steps; ++k)
                {
                    const float t0 = a0 + span * k / steps - kPi * 0.5f, t1 = a0 + span * (k + 1) / steps - kPi * 0.5f;
                    const ImVec2 o0(c.x + std::cos(t0) * ro, c.y + std::sin(t0) * ro), o1(c.x + std::cos(t1) * ro, c.y + std::sin(t1) * ro);
                    const ImVec2 i0(c.x + std::cos(t0) * r, c.y + std::sin(t0) * r), i1(c.x + std::cos(t1) * r, c.y + std::sin(t1) * r);
                    dl->AddQuadFilled(o0, o1, i1, i0, col);
                }
                a0 = a1;
            }
            const std::string total = FormatSize(m_typeTotal);
            const ImVec2 ts = ImGui::CalcTextSize(total.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y), IM_COL32(230, 233, 242, 255), total.c_str());
            const char* sub = m_typeScopeCur ? "in this folder" : "in total";
            const ImVec2 ss = ImGui::CalcTextSize(sub);
            dl->AddText(ImVec2(c.x - ss.x * 0.5f, c.y + 2), IM_COL32(125, 132, 153, 255), sub);
            if (hovered >= 0)
            {
                const bool isRest = hovered >= maxSeg;
                const uint64_t sz = isRest ? restSize : m_typeStats[(size_t)hovered].size;
                ImGui::SetTooltip("%s\n%s (%.1f%%)", isRest ? "Other types" : m_typeStats[(size_t)hovered].key.c_str(), FormatSize(sz).c_str(), 100.0 * (double)sz / (double)m_typeTotal);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isRest) m_typeSelKey = m_typeStats[(size_t)hovered].key;
            }

            // ---- table of groups
            if (ImGui::BeginTable("##types", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("##color", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                ImGui::TableSetupColumn(m_typeGroupMode == 0 ? "Group" : "Extension", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80 * s);
                ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 90 * s);
                ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthFixed, 70 * s);
                ImGui::TableHeadersRow();
                ImGuiListClipper clip;
                clip.Begin((int)m_typeStats.size());
                while (clip.Step())
                    for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    {
                        TypeStat& t = m_typeStats[(size_t)i];
                        ImGui::TableNextRow();
                        ImGui::PushID(i);
                        ImGui::TableNextColumn();
                        if (m_typeGroupMode == 0)
                        {
                            if (ImGui::ColorEdit3("##col", GetCategoryInfo(t.cat).color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                            { m_tmMain.Invalidate(); m_tmExplorer.Invalidate(); }
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
                            ImGui::SetItemTooltip("Click to change the color of this group");
                        }
                        else
                        {
                            const ImVec2 q = ImGui::GetCursorScreenPos();
                            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(q.x + 3, q.y + 3), ImVec2(q.x + ImGui::GetFontSize(), q.y + ImGui::GetFontSize()), CategoryColorU32(t.cat), 3.0f);
                            ImGui::Dummy(ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
                        }
                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(t.key.c_str(), t.key == m_typeSelKey, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                            m_typeSelKey = t.key;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("%s", m_typeGroupMode == 1 ? CategoryName(t.cat) : GetCategoryInfo(t.cat).description);
                        ImGui::TableNextColumn(); TextRight(FormatSize(t.size).c_str());
                        ImGui::TableNextColumn(); DrawSizeBar(t.size, m_typeTotal, t.cat);
                        ImGui::TableNextColumn(); TextRight(FormatCount(t.count).c_str());
                        ImGui::PopID();
                    }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##typesright", ImVec2(0, 0));
        {
            int selCat = -1; std::string selExt; bool byExt = m_typeGroupMode == 1;
            for (auto& t : m_typeStats)
                if (t.key == m_typeSelKey)
                {
                    selCat = t.cat;
                    if (byExt) selExt = t.key == "(no extension)" ? std::string() : t.key.substr(1);
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(CategoryColorU32(t.cat)), "%s", t.key.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s in %s files", FormatSize(t.size).c_str(), FormatCount(t.count).c_str());
                    break;
                }
            if (selCat < 0) ImGui::TextDisabled("Select a group on the left");
            Node* scope = m_typeScopeCur ? m_cur : nullptr;
            const uint64_t key = Mix(Mix(Mix(HashStr(m_typeSelKey.c_str()), (uint64_t)byExt), (uint64_t)(uintptr_t)scope), m_set.showHidden * 2 + m_set.showSystem);
            DrawNodeList("##typefiles", m_typeFilesList, List_Flat, key, [&](std::vector<Node*>& rows)
            {
                if (selCat < 0) return;
                for (Node* f : m_tree->AllFiles())
                {
                    if (!PassesFilter(f)) continue;
                    if (byExt) { if (ExtensionOf(f->name) != selExt) continue; }
                    else if (f->category != selCat) continue;
                    if (scope && !IsAncestor(scope, f)) continue;
                    rows.push_back(f);
                }
            }, ImVec2(0, 0), m_typeTotal, 1);
        }
        ImGui::EndChild();
    }

    // ===================================================================== recent changes
    void App::DrawRecentTab()
    {
        if (ImGui::BeginTabBar("##recenttabs"))
        {
            if (ImGui::BeginTabItem(IL(ICON_PULSE, "Live monitor").c_str())) { DrawLiveMonitor(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(IL(ICON_HISTORY, "Recently modified (from scan)").c_str())) { DrawRecentFromScan(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }

    void App::DrawRecentFromScan()
    {
        static const char* kWindows[] = { "Last 15 minutes", "Last hour", "Last 24 hours", "Last 7 days", "Last 30 days", "Last 365 days" };
        static const uint64_t kSecs[] = { 15 * 60, 3600, 86400, 7 * 86400, 30 * 86400, 365 * 86400 };
        ImGui::SetNextItemWidth(170 * m_uiScale);
        if (ImGui::Combo("##window", &m_set.recentWindow, kWindows, IM_ARRAYSIZE(kWindows))) SaveSettings();
        m_set.recentWindow = std::clamp(m_set.recentWindow, 0, (int)IM_ARRAYSIZE(kWindows) - 1);
        ImGui::SameLine();
        ImGui::TextDisabled("%s files | based on the last scan%s", FormatCount(m_recentScanList.rows.size()).c_str(),
            (m_watcher.IsRunning() && m_set.liveUpdateTree) ? " + live monitor updates" : " (start the Live monitor to keep it current)");
        const uint64_t now = NowFileTime();
        const uint64_t cutoff = now - kSecs[m_set.recentWindow] * 10000000ull;
        const uint64_t key = Mix(Mix((uint64_t)m_set.recentWindow, now / (600000000ull)), m_set.showHidden * 2 + m_set.showSystem);
        DrawNodeList("##recentscan", m_recentScanList, List_FlatRecent, key, [&](std::vector<Node*>& rows)
        {
            for (Node* f : m_tree->AllFiles())
                if (f->mtime >= cutoff && f->mtime <= now + 36000000000ull && PassesFilter(f)) rows.push_back(f);
        }, ImVec2(0, 0), m_tree->root->size, 4);
    }

    static ImVec4 ActionColor(uint32_t a)
    {
        switch (a)
        {
        case Fs_Added: return Hex(0x3DDC97);
        case Fs_Removed: return Hex(0xFF5C5C);
        case Fs_Modified: return Hex(0x5CB8FF);
        default: return Hex(0xFFB35C);
        }
    }

    void App::DrawLiveMonitor()
    {
        const float s = m_uiScale;
        const bool running = m_watcher.IsRunning();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Folder");
        ImGui::SameLine();
        ImGui::BeginDisabled(running);
        ImGui::SetNextItemWidth(320 * s);
        ImGui::InputText("##watchpath", m_watchPathBuf, sizeof(m_watchPathBuf));
        ImGui::SameLine();
        if (ImGui::Button("...##pickwatch")) m_deferred = Deferred::PickWatchFolder;
        ImGui::SameLine();
        if (ImGui::Button("Use scanned folder") && m_tree) snprintf(m_watchPathBuf, sizeof(m_watchPathBuf), "%s", ToUtf8(m_tree->rootPath).c_str());
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (!running)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Hex(0x2E7A4F));
            if (ImGui::Button(IL(ICON_PLAY, "Start").c_str(), ImVec2(90 * s, 0)) && m_watchPathBuf[0]) StartWatcher(ToWide(m_watchPathBuf));
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, m_watchPaused ? Hex(0x2E7A4F) : Hex(0x8A6A1F));
            if (ImGui::Button(m_watchPaused ? IL(ICON_PLAY, "Resume").c_str() : IL(ICON_PAUSE, "Pause").c_str(), ImVec2(100 * s, 0))) m_watchPaused = !m_watchPaused;
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Pause freezes the list; changes keep being recorded and appear when you resume.");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, Hex(0x8A2B2B));
            if (ImGui::Button(IL(ICON_STOP, "Stop").c_str(), ImVec2(90 * s, 0))) { m_watcher.Stop(); m_watchPaused = false; }
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::Button(IL(ICON_CLEAR, "Clear").c_str())) { m_recent.clear(); m_recentIndex.clear(); m_recentOrder.clear(); m_recentAnchor = -1; m_recentDirty = true; }

        ImGui::Checkbox("Files only", &m_set.liveFilesOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Update scan results live", &m_set.liveUpdateTree);
        ImGui::SetItemTooltip("Apply changes to sizes in the Explorer, Treemap and other tabs as they happen");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200 * s);
        ImGui::InputTextWithHint("##livefilter", "Filter...", m_liveFilter, sizeof(m_liveFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##exclude", "Exclude paths containing (separate with ;)  e.g.  \\AppData\\Local\\Temp; .log", m_liveExcludeBuf, sizeof(m_liveExcludeBuf));

        // status line
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float fs = ImGui::GetFontSize();
            const ImU32 dot = !running ? IM_COL32(110, 116, 135, 255) : m_watchPaused ? IM_COL32(255, 200, 87, 255) : IM_COL32(61, 220, 151, 255);
            const float pulse = running && !m_watchPaused ? 0.6f + 0.4f * std::sin((float)ImGui::GetTime() * 5.0f) : 1.0f;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + fs * 0.5f, p.y + fs * 0.55f), fs * 0.32f * pulse, dot);
            ImGui::Dummy(ImVec2(fs, fs));
            ImGui::SameLine();
            if (running)
                ImGui::Text("%s %s  |  %s events  |  %s files listed%s%s", m_watchPaused ? "Paused:" : "Watching", ToUtf8(m_watcher.Root()).c_str(),
                    FormatCount(m_watcher.TotalEvents()).c_str(), FormatCount(m_recent.size()).c_str(),
                    m_watchPaused ? ("  |  " + FormatCount(m_watcher.Queued()) + " waiting").c_str() : "",
                    m_watcher.Overflows() ? ("  |  " + FormatCount(m_watcher.Overflows()) + " bursts too fast (some events lost)").c_str() : "");
            else
            {
                const std::string err = m_watcher.LastError();
                if (!err.empty()) ImGui::TextColored(Hex(0xFF5C5C), "%s", err.c_str());
                else ImGui::TextDisabled("Stopped. Press Start to watch every create / modify / delete / rename below the folder in real time.");
            }
        }

        // ---- build visible order
        const std::string filter = ToLowerAscii(m_liveFilter);
        const uint64_t fkey = HashStr(filter.c_str());
        const double now = ImGui::GetTime();
        const bool needSort = m_recentDirty && (now - m_recentSortedAt > 0.25 || fkey != m_recentFilterKey);
        if (needSort || fkey != m_recentFilterKey)
        {
            m_recentOrder.clear();
            for (size_t i = 0; i < m_recent.size(); ++i)
            {
                const RecentEntry& e = m_recent[i];
                if (!filter.empty() && !ContainsNoCase(e.nameUtf8, filter) && !ContainsNoCase(e.dirUtf8, filter)) continue;
                m_recentOrder.push_back((int)i);
            }
            const int col = m_recentSortCol; const bool asc = m_recentSortAsc;
            const auto& R = m_recent;
            std::sort(m_recentOrder.begin(), m_recentOrder.end(), [&](int a, int b)
            {
                const RecentEntry& x = R[(size_t)a]; const RecentEntry& y = R[(size_t)b];
                switch (col)
                {
                case 1: if (x.lastAction != y.lastAction) return asc ? x.lastAction < y.lastAction : x.lastAction > y.lastAction; break;
                case 2: { const bool l = LessNameCI(x.nameUtf8, y.nameUtf8); const bool g = LessNameCI(y.nameUtf8, x.nameUtf8); if (l || g) return asc ? l : g; break; }
                case 3: if (x.size != y.size) return asc ? x.size < y.size : x.size > y.size; break;
                case 4: { const int64_t dx = (int64_t)x.size - (int64_t)x.prevSize, dy = (int64_t)y.size - (int64_t)y.prevSize; if (dx != dy) return asc ? dx < dy : dx > dy; break; }
                case 5: if (x.count != y.count) return asc ? x.count < y.count : x.count > y.count; break;
                case 6: { const bool l = LessNameCI(x.dirUtf8, y.dirUtf8); const bool g = LessNameCI(y.dirUtf8, x.dirUtf8); if (l || g) return asc ? l : g; break; }
                default: break;
                }
                return asc ? x.lastTime < y.lastTime : x.lastTime > y.lastTime;
            });
            m_recentDirty = false;
            m_recentSortedAt = now;
            m_recentFilterKey = fkey;
        }

        // ---- table
        const uint64_t nowFt = NowFileTime();
        ImGuiIO& io = ImGui::GetIO();
        ImGui::BeginChild("##livewrap", ImVec2(0, 0));
        const ImGuiTableFlags tflags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##live", 7, tflags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 80 * s, 0);
            ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthFixed, 90 * s, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0, 2);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 85 * s, 3);
            ImGui::TableSetupColumn("Growth", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 85 * s, 4);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 55 * s, 5);
            ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 0, 6);
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs())
                if (ss->SpecsDirty && ss->SpecsCount > 0)
                {
                    m_recentSortCol = (int)ss->Specs[0].ColumnUserID;
                    m_recentSortAsc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                    ss->SpecsDirty = false;
                    m_recentDirty = true;
                    m_recentSortedAt = -1.0;
                }

            ImGuiListClipper clip;
            clip.Begin((int)m_recentOrder.size());
            while (clip.Step())
                for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
                {
                    const int idx = m_recentOrder[(size_t)row];
                    if (idx < 0 || idx >= (int)m_recent.size()) continue;
                    RecentEntry& e = m_recent[(size_t)idx];
                    ImGui::TableNextRow();
                    // fade highlight for fresh changes
                    const double age = (double)(nowFt > e.lastTime ? nowFt - e.lastTime : 0) / 1e7;
                    if (age < 4.0)
                    {
                        ImVec4 c = ActionColor(e.lastAction);
                        c.w = (float)(0.28 * (1.0 - age / 4.0));
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(c));
                    }
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(idx);
                    const ImVec2 rowStart = ImGui::GetCursorPos();
                    if (ImGui::Selectable("##r", e.selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        if (io.KeyCtrl) e.selected = !e.selected;
                        else if (io.KeyShift && m_recentAnchor >= 0)
                        {
                            for (auto& x : m_recent) x.selected = false;
                            const int a = std::min(m_recentAnchor, row), b = std::max(m_recentAnchor, row);
                            for (int k = a; k <= b && k < (int)m_recentOrder.size(); ++k) m_recent[(size_t)m_recentOrder[(size_t)k]].selected = true;
                        }
                        else { for (auto& x : m_recent) x.selected = false; e.selected = true; m_recentAnchor = row; }
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && e.exists)
                        {
                            Node* n = m_tree ? m_tree->Find(e.path) : nullptr;
                            if (n && n->IsDir()) Activate(n);
                            else shell::OpenItem(e.path);
                        }
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                    {
                        if (!e.selected) { for (auto& x : m_recent) x.selected = false; e.selected = true; m_recentAnchor = row; }
                        std::vector<std::wstring> paths;
                        for (auto& x : m_recent) if (x.selected && x.exists) paths.push_back(x.path);
                        if (!paths.empty()) OpenItemMenu(paths, io.KeyShift);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                        ImGui::SetTooltip("%s\nFirst seen %s\nLast change %s\n%u change(s)", ToUtf8(e.path).c_str(),
                            FormatFileTime(e.firstTime).c_str(), FormatFileTime(e.lastTime).c_str(), e.count);
                    ImGui::SetCursorPos(rowStart);
                    ImGui::TextDisabled("%s", FormatAgo(e.lastTime, nowFt).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ActionColor(e.lastAction), "%s", FsActionName(e.lastAction));
                    ImGui::TableNextColumn();
                    {
                        const float fs = ImGui::GetFontSize();
                        ImTextureID tex = e.isDir ? icons::ForFolder() : icons::ForFile(std::wstring(), e.nameUtf8);
                        if (tex) { ImGui::Image(tex, ImVec2(fs, fs)); ImGui::SameLine(0, 6); }
                        if (e.exists) ImGui::TextUnformatted(e.nameUtf8.c_str());
                        else ImGui::TextDisabled("%s", e.nameUtf8.c_str());
                    }
                    ImGui::TableNextColumn();
                    if (!e.isDir && e.exists) TextRight(FormatSize(e.size).c_str());
                    ImGui::TableNextColumn();
                    if (!e.isDir && e.exists && e.size != e.prevSize)
                    {
                        const bool grow = e.size > e.prevSize;
                        const std::string d = (grow ? "+" : "-") + FormatSize(grow ? e.size - e.prevSize : e.prevSize - e.size);
                        ImGui::PushStyleColor(ImGuiCol_Text, grow ? Hex(0xFFB35C) : Hex(0x3DDC97));
                        TextRight(d.c_str());
                        ImGui::PopStyleColor();
                    }
                    ImGui::TableNextColumn();
                    TextRight(std::to_string(e.count).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", e.dirUtf8.c_str());
                    ImGui::PopID();
                }
            ImGui::EndTable();
        }
        // keyboard
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        {
            std::vector<std::wstring> paths;
            for (auto& x : m_recent) if (x.selected && x.exists) paths.push_back(x.path);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !paths.empty()) RequestDelete(paths, io.KeyShift);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !paths.empty()) DoCopy(paths, false);
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !paths.empty()) DoOpen(paths);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) for (int i : m_recentOrder) m_recent[(size_t)i].selected = true;
        }
        ImGui::EndChild();
    }
}
