// GEX - Graphical Explorer
// Squarified, nested, cushion-shaded treemap of a folder (graphical "what takes space" view).
#pragma once
#include "FileTree.h"
#include "imgui.h"
#include <vector>

namespace gex
{
    class Treemap
    {
    public:
        struct Result
        {
            Node* hovered = nullptr;
            Node* clicked = nullptr;
            Node* doubleClicked = nullptr;
            Node* rightClicked = nullptr;
        };

        // Draws the treemap of 'root' filling 'size'. 'selected' items are outlined.
        Result Draw(const char* id, const FileTree* tree, Node* root, ImVec2 size, const std::vector<Node*>& selected);
        void Invalidate() { m_root = nullptr; }

        int   maxDepth = 64;      // nesting limit
        bool  showLabels = true;
        bool  cushion = true;     // gradient shading

    private:
        struct Rect { float x0, y0, x1, y1; Node* node; int parent; uint16_t depth; bool leaf; };
        void Build(Node* root, ImVec2 size);
        void LayoutChildren(Node* dir, float x0, float y0, float x1, float y1, int depth, int parentIdx);
        void AddNode(Node* n, float x0, float y0, float x1, float y1, int depth, int parentIdx);

        std::vector<Rect>  m_rects;
        std::vector<Node*> m_scratch;
        Node*    m_root = nullptr;
        uint64_t m_version = 0;
        ImVec2   m_size = ImVec2(0, 0);
        double   m_lastBuild = 0.0;
        float    m_headerH = 16.0f;
        static constexpr size_t kMaxRects = 150000;
    };
}
