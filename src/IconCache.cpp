// GEX - Graphical Explorer
#include "IconCache.h"
#include "Util.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <unordered_map>
#include <vector>

namespace gex::icons
{
    static ID3D11Device* g_device = nullptr;
    static std::unordered_map<std::string, ID3D11ShaderResourceView*> g_byExt;   // "" key for unknown
    static std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_byPath; // exe/ico/lnk... unique icons
    static ID3D11ShaderResourceView* g_folder = nullptr;
    static bool g_folderTried = false;
    static int g_budget = 0;

    void Init(void* dev) { g_device = (ID3D11Device*)dev; }

    void Shutdown()
    {
        for (auto& kv : g_byExt) if (kv.second) kv.second->Release();
        for (auto& kv : g_byPath) if (kv.second) kv.second->Release();
        g_byExt.clear(); g_byPath.clear();
        if (g_folder) { g_folder->Release(); g_folder = nullptr; }
    }

    void NewFrame() { g_budget = 12; }

    static ID3D11ShaderResourceView* TextureFromIcon(HICON icon)
    {
        if (!icon || !g_device) return nullptr;
        ICONINFO ii{};
        if (!::GetIconInfo(icon, &ii)) return nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        if (ii.hbmColor)
        {
            BITMAP bm{};
            ::GetObjectW(ii.hbmColor, sizeof(bm), &bm);
            const int w = bm.bmWidth, h = bm.bmHeight;
            if (w > 0 && h > 0 && w <= 256 && h <= 256)
            {
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = w;
                bi.bmiHeader.biHeight = -h; // top-down
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                std::vector<uint32_t> px((size_t)w * h), mask((size_t)w * h);
                HDC dc = ::GetDC(nullptr);
                ::GetDIBits(dc, ii.hbmColor, 0, (UINT)h, px.data(), &bi, DIB_RGB_COLORS);
                bool hasAlpha = false;
                for (uint32_t p : px) if (p >> 24) { hasAlpha = true; break; }
                if (!hasAlpha && ii.hbmMask)
                {
                    ::GetDIBits(dc, ii.hbmMask, 0, (UINT)h, mask.data(), &bi, DIB_RGB_COLORS);
                    for (size_t i = 0; i < px.size(); ++i)
                        px[i] = (px[i] & 0x00FFFFFF) | ((mask[i] & 0x00FFFFFF) ? 0u : 0xFF000000u);
                }
                ::ReleaseDC(nullptr, dc);

                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = (UINT)w; desc.Height = (UINT)h;
                desc.MipLevels = 1; desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // GDI pixel order
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_IMMUTABLE;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{};
                init.pSysMem = px.data();
                init.SysMemPitch = (UINT)w * 4;
                ID3D11Texture2D* tex = nullptr;
                if (SUCCEEDED(g_device->CreateTexture2D(&desc, &init, &tex)) && tex)
                {
                    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                    sd.Format = desc.Format;
                    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    sd.Texture2D.MipLevels = 1;
                    g_device->CreateShaderResourceView(tex, &sd, &srv);
                    tex->Release();
                }
            }
            ::DeleteObject(ii.hbmColor);
        }
        if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
        return srv;
    }

    static ID3D11ShaderResourceView* Extract(const wchar_t* path, DWORD attrs, bool useAttributes)
    {
        SHFILEINFOW sfi{};
        UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
        if (useAttributes) flags |= SHGFI_USEFILEATTRIBUTES;
        if (!::SHGetFileInfoW(path, attrs, &sfi, sizeof(sfi), flags) || !sfi.hIcon) return nullptr;
        ID3D11ShaderResourceView* srv = TextureFromIcon(sfi.hIcon);
        ::DestroyIcon(sfi.hIcon);
        return srv;
    }

    static ImTextureID ToId(ID3D11ShaderResourceView* srv) { return (ImTextureID)(intptr_t)srv; }

    static bool HasUniqueIcon(const std::string& ext)
    {
        return ext == "exe" || ext == "ico" || ext == "lnk" || ext == "url" || ext == "cur" || ext == "ani" || ext == "msc" || ext == "appref-ms";
    }

    bool NeedsFullPath(const std::string& nameUtf8) { return HasUniqueIcon(ExtensionOf(nameUtf8)); }

    ImTextureID ForFile(const std::wstring& fullPath, const std::string& nameUtf8)
    {
        const std::string ext = ExtensionOf(nameUtf8);
        if (HasUniqueIcon(ext) && !fullPath.empty())
        {
            auto it = g_byPath.find(fullPath);
            if (it != g_byPath.end()) return ToId(it->second);
            if (g_budget > 0 && g_byPath.size() < 4000)
            {
                --g_budget;
                ID3D11ShaderResourceView* srv = Extract(fullPath.c_str(), 0, false);
                g_byPath[fullPath] = srv;
                if (srv) return ToId(srv);
            }
            // fall through: generic icon for the extension while waiting
        }
        auto it = g_byExt.find(ext);
        if (it != g_byExt.end()) return ToId(it->second);
        const std::wstring fake = L"file." + ToWide(ext);
        ID3D11ShaderResourceView* srv = Extract(ext.empty() ? L"file" : fake.c_str(), FILE_ATTRIBUTE_NORMAL, true);
        g_byExt[ext] = srv;
        return ToId(srv);
    }

    ImTextureID ForFolder()
    {
        if (!g_folderTried)
        {
            g_folderTried = true;
            g_folder = Extract(L"folder", FILE_ATTRIBUTE_DIRECTORY, true);
        }
        return ToId(g_folder);
    }

    ImTextureID ForDrive(const std::wstring& root)
    {
        auto it = g_byPath.find(root);
        if (it != g_byPath.end()) return ToId(it->second);
        ID3D11ShaderResourceView* srv = Extract(root.c_str(), 0, false);
        g_byPath[root] = srv;
        return ToId(srv);
    }
}
