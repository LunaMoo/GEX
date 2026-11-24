// GEX - Graphical Explorer
// Real Windows shell icons (SHGetFileInfo) uploaded as D3D11 textures for ImGui.
#pragma once
#include "imgui.h"
#include <string>

namespace gex::icons
{
    void Init(void* d3d11Device);
    void Shutdown();
    void NewFrame();                                   // resets per-frame extraction budget

    // Returns 0 when no icon is available (caller draws a colored fallback).
    ImTextureID ForFile(const std::wstring& fullPath, const std::string& nameUtf8);
    bool        NeedsFullPath(const std::string& nameUtf8);   // exe/lnk/ico have per-file icons
    ImTextureID ForFolder();
    ImTextureID ForDrive(const std::wstring& root);
    ImTextureID ForRecycleBin(bool full);
}
