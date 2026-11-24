// GEX - Graphical Explorer
#include "Treemap.h"
#include "Util.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_set>

namespace gex
{
    void Treemap::Build(Node* root, ImVec2 size)
    {
        m_rects.clear();
        m_root = root;
        m_size = size;
        m_headerH = ImGui::GetFontSize() + 2.0f;
        if (!root || root->size == 0 || size.x < 2 || size.y < 2) return;
        if (!root->IsDir()) { m_rects.push_back({ 0, 0, size.x, size.y, root, -1, 0, true }); return; }
        LayoutChildren(root, 0, 0, size.x, size.y, 0, -1);
    }

    void Treemap::LayoutChildren(Node* dir, float x0, float y0, float x1, float y1, int depth, int parentIdx)
    {
        const auto& ch = dir->children;
        size_t n = 0;
        double total = 0.0;
        while (n < ch.size() && ch[n]->size > 0) { total += (double)ch[n]->size; ++n; }
        if (n == 0 || total <= 0.0) return;
        const double area = (double)(x1 - x0) * (double)(y1 - y0);
        if (area <= 1.0) return;
        const double scale = area / total;

        size_t i = 0;
        while (i < n)
        {
            const double rw = (double)(x1 - x0), rh = (double)(y1 - y0);
            const double w = std::min(rw, rh);
            if (w <= 0.25 || rw * rh < 1.0) break;
            // Grow the row while the worst aspect ratio improves (squarified algorithm).
            size_t j = i;
            double rowSum = 0.0, worstPrev = DBL_MAX;
            const double rmax = (double)ch[i]->size * scale;
            while (j < n)
            {
                const double s = (double)ch[j]->size * scale;
                const double sum = rowSum + s;
                const double worst = std::max((w * w * rmax) / (sum * sum), (sum * sum) / (w * w * s));
                if (j > i && worst > worstPrev) break;
                worstPrev = worst;
                rowSum = sum;
                ++j;
            }
            const double thickness = rowSum / w;
            if (rw >= rh)
            {
                // column on the left
                double cy = y0;
                for (size_t k = i; k < j; ++k)
                {
                    const double hk = (double)ch[k]->size * scale / thickness;
                    AddNode(ch[k], x0, (float)cy, (float)(x0 + thickness), (float)(cy + hk), depth, parentIdx);
                    cy += hk;
                }
                x0 = (float)(x0 + thickness);
            }
            else
            {
                // row on the top
                double cx = x0;
                for (size_t k = i; k < j; ++k)
                {
                    const double wk = (double)ch[k]->size * scale / thickness;
                    AddNode(ch[k], (float)cx, y0, (float)(cx + wk), (float)(y0 + thickness), depth, parentIdx);
                    cx += wk;
                }
                y0 = (float)(y0 + thickness);
            }
            i = j;
            if (m_rects.size() >= kMaxRects) break;
        }
    }

    void Treemap::AddNode(Node* n, float x0, float y0, float x1, float y1, int depth, int parentIdx)
    {
        if (m_rects.size() >= kMaxRects) return;
        const float w = x1 - x0, h = y1 - y0;
        if (w < 0.6f || h < 0.6f) return;
        if (n->IsDir() && !n->children.empty() && depth < maxDepth && w >= 5.0f && h >= 5.0f)
        {
            const int self = (int)m_rects.size();
            m_rects.push_back({ x0, y0, x1, y1, n, parentIdx, (uint16_t)depth, false });
            const float pad = w > 12 && h > 12 ? 2.0f : 1.0f;
            const float header = (showLabels && w > 50.0f && h > m_headerH * 2.5f) ? m_headerH : 0.0f;
            LayoutChildren(n, x0 + pad, y0 + pad + header, x1 - pad, y1 - pad, depth + 1, self);
        }
        else
            m_rects.push_back({ x0, y0, x1, y1, n, parentIdx, (uint16_t)depth, true });
    }

    static ImU32 Shade(ImU32 c, float f)
    {
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        v.x = std::min(1.0f, v.x * f); v.y = std::min(1.0f, v.y * f); v.z = std::min(1.0f, v.z * f);
        return ImGui::ColorConvertFloat4ToU32(v);
    }

    Treemap::Result Treemap::Draw(const char* id, const FileTree* tree, Node* root, ImVec2 size, const std::vector<Node*>& selected)
    {
        Result res;
        size.x = std::max(size.x, 10.0f);
        size.y = std::max(size.y, 10.0f);
        ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 o = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, ImVec2(o.x + size.x, o.y + size.y), IM_COL32(18, 20, 28, 255));
        if (!tree || !root) return res;

        const double now = ImGui::GetTime();
        const bool sizeChanged = std::abs(size.x - m_size.x) > 0.5f || std::abs(size.y - m_size.y) > 0.5f;
        if (root != m_root || sizeChanged || (tree->version != m_version && now - m_lastBuild > 0.75))
        {
            Build(root, size);
            m_version = tree->version;
            m_lastBuild = now;
        }

        dl->PushClipRect(o, ImVec2(o.x + size.x, o.y + size.y), true);
        ImFont* font = ImGui::GetFont();
        const float fs = ImGui::GetFontSize();
        for (const Rect& r : m_rects)
        {
            if (r.node->IsRemoved()) continue;
            const ImVec2 a(o.x + r.x0, o.y + r.y0), b(o.x + r.x1, o.y + r.y1);
            const float w = r.x1 - r.x0, h = r.y1 - r.y0;
            if (r.leaf)
            {
                const ImU32 base = CategoryColorU32(r.node->ColorCategory());
                if (cushion && w > 2.0f && h > 2.0f)
                    dl->AddRectFilledMultiColor(a, b, Shade(base, 1.30f), Shade(base, 0.95f), Shade(base, 0.55f), Shade(base, 0.95f));
                else
                    dl->AddRectFilled(a, b, base);
                if (w > 3.0f && h > 3.0f) dl->AddRect(a, b, IM_COL32(0, 0, 0, 110));
                if (showLabels && w > 46.0f && h > fs + 4.0f)
                {
                    const ImVec4 clip(a.x + 2, a.y + 1, b.x - 2, b.y - 1);
                    dl->AddText(font, fs, ImVec2(a.x + 4, a.y + 2), IM_COL32(10, 10, 14, 230), r.node->name.c_str(), nullptr, 0.0f, &clip);
                    if (h > fs * 2.0f + 6.0f)
                    {
                        const std::string sz = FormatSize(r.node->size);
                        dl->AddText(font, fs, ImVec2(a.x + 4, a.y + 2 + fs), IM_COL32(10, 10, 14, 180), sz.c_str(), nullptr, 0.0f, &clip);
                    }
                }
            }
            else
            {
                const int shade = std::max(20, 52 - (int)r.depth * 4);
                dl->AddRectFilled(a, b, IM_COL32(shade, shade + 3, shade + 12, 255));
                dl->AddRect(a, b, IM_COL32(0, 0, 0, 200));
                if (showLabels && w > 50.0f && h > m_headerH * 2.5f)
                {
                    const ImVec4 clip(a.x + 2, a.y, b.x - 2, a.y + m_headerH + 2);
                    std::string label = r.node->name + "  " + FormatSize(r.node->size);
                    dl->AddText(font, fs, ImVec2(a.x + 4, a.y + 2), FolderColorU32(), label.c_str(), nullptr, 0.0f, &clip);
                }
            }
        }

        // Selection outlines
        if (!selected.empty())
        {
            std::unordered_set<const Node*> sel(selected.begin(), selected.end());
            for (const Rect& r : m_rects)
                if (sel.count(r.node))
                    dl->AddRect(ImVec2(o.x + r.x0, o.y + r.y0), ImVec2(o.x + r.x1, o.y + r.y1), IM_COL32(0, 230, 255, 255), 0.0f, 0, 2.0f);
        }

        // Hover (deepest rect first: children are appended after their parent)
        if (hovered)
        {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const float mx = m.x - o.x, my = m.y - o.y;
            for (size_t i = m_rects.size(); i-- > 0;)
            {
                const Rect& r = m_rects[i];
                if (mx >= r.x0 && mx < r.x1 && my >= r.y0 && my < r.y1 && !r.node->IsRemoved())
                {
                    res.hovered = r.node;
                    dl->AddRect(ImVec2(o.x + r.x0, o.y + r.y0), ImVec2(o.x + r.x1, o.y + r.y1), IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
                    // outline the containing folders too
                    for (int k = r.parent; k >= 0; k = m_rects[(size_t)k].parent)
                    {
                        const Rect& pr = m_rects[(size_t)k];
                        dl->AddRect(ImVec2(o.x + pr.x0, o.y + pr.y0), ImVec2(o.x + pr.x1, o.y + pr.y1), IM_COL32(255, 255, 255, 90), 0.0f, 0, 1.0f);
                    }
                    break;
                }
            }
            if (res.hovered)
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) res.doubleClicked = res.hovered;
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) res.clicked = res.hovered;
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) res.rightClicked = res.hovered;
            }
        }
        dl->PopClipRect();
        return res;
    }
}
