// GEX - Graphical Explorer
// Entry point: Win32 window + Direct3D 11 + Dear ImGui.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <d3d11.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "App.h"
#include "IconCache.h"
#include "Icons.h"
#include "ShellOps.h"
#include "Util.h"

static ID3D11Device*            g_device = nullptr;
static ID3D11DeviceContext*     g_context = nullptr;
static IDXGISwapChain*          g_swapChain = nullptr;
static bool                     g_occluded = false;
static UINT                     g_resizeW = 0, g_resizeH = 0;
static ID3D11RenderTargetView*  g_rtv = nullptr;
static ULONGLONG                g_lastInput = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_device->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}

static void CleanupRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr != S_OK) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Owner-drawn submenus of the native Explorer context menu ("Send to", "Open with"...)
    intptr_t menuResult = 0;
    if (gex::shell::HandleMenuMessage(msg, (uintptr_t)wParam, (intptr_t)lParam, &menuResult)) return (LRESULT)menuResult;

    if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || msg == WM_SIZE || msg == WM_ACTIVATE)
        g_lastInput = ::GetTickCount64();

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_resizeW = (UINT)LOWORD(lParam);
        g_resizeH = (UINT)HIWORD(lParam);
        return 0;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 800;
        mmi->ptMinTrackSize.y = 500;
        return 0;
    }
    case WM_DPICHANGED:
    {
        const RECT* r = (const RECT*)lParam;
        ::SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // no ALT application menu
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool FileExists(const wchar_t* p) { return ::GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static void LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    wchar_t winDir[MAX_PATH] = L"C:\\Windows";
    ::GetWindowsDirectoryW(winDir, MAX_PATH);
    const std::wstring fonts = std::wstring(winDir) + L"\\Fonts\\";
    auto path = [&](const wchar_t* f) { return fonts + f; };

    ImFont* main = nullptr;
    const std::wstring segoe = path(L"segoeui.ttf");
    if (FileExists(segoe.c_str()))
        main = io.Fonts->AddFontFromFileTTF(gex::ToUtf8(segoe).c_str());
    if (!main) { io.Fonts->AddFontDefault(); return; }

    // Merge fallbacks so file names in any script render (Dear ImGui >= 1.92 loads glyphs on demand).
    ImFontConfig merge;
    merge.MergeMode = true;
    const wchar_t* fallbacks[] = { L"seguisym.ttf", L"msyh.ttc", L"YuGothM.ttc", L"msgothic.ttc", L"malgun.ttf", L"Nirmala.ttc", L"seguihis.ttf" };
    for (const wchar_t* f : fallbacks)
    {
        const std::wstring p = path(f);
        if (FileExists(p.c_str())) io.Fonts->AddFontFromFileTTF(gex::ToUtf8(p).c_str(), 0.0f, &merge);
    }

    // Icon font: Windows 11 "Segoe Fluent Icons", Windows 10 "Segoe MDL2 Assets"
    ImFontConfig icons;
    icons.MergeMode = true;
    icons.GlyphOffset = ImVec2(0.0f, 2.0f);
    static const ImWchar iconRanges[] = { ICON_RANGE_MIN, ICON_RANGE_MAX, 0 };
    const wchar_t* iconFonts[] = { L"SegoeIcons.ttf", L"segmdl2.ttf" };
    for (const wchar_t* f : iconFonts)
    {
        const std::wstring p = path(f);
        if (FileExists(p.c_str()) && io.Fonts->AddFontFromFileTTF(gex::ToUtf8(p).c_str(), 0.0f, &icons, iconRanges))
        {
            gex::g_haveIconFont = true;
            break;
        }
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    ::OleInitialize(nullptr); // COM (apartment threaded) + OLE drag & drop + clipboard

    // Optional command line: GEX.exe "D:\Some\Folder"
    std::wstring startPath;
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv && argc > 1) startPath = argv[1];
        if (argv) ::LocalFree(argv);
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = ::LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = ::LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor = ::LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"GEX_MainWindow";
    ::RegisterClassExW(&wc);
    const std::wstring title = gex::IsProcessElevated() ? L"GEX - Graphical Explorer (Administrator)" : L"GEX - Graphical Explorer";
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                (int)(1440 * scale), (int)(900 * scale), nullptr, nullptr, hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, hInstance);
        ::MessageBoxW(nullptr, L"Direct3D 11 could not be initialized.", L"GEX", MB_ICONERROR);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // GEX handles arrow keys itself (Explorer-like)

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    gex::icons::Init(g_device);

    gex::App app;
    app.Init(hwnd, startPath);               // sets colors/metrics
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 16.0f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    app.SetUiScale(scale);
    LoadFonts();

    bool done = false;
    while (!done)
    {
        // Sleep when nothing happens so GEX uses ~0% CPU while idle.
        const ULONGLONG now = ::GetTickCount64();
        const bool recentInput = now - g_lastInput < 600;
        if (!recentInput && !app.NeedsFastRefresh())
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, app.NeedsSlowRefresh() ? 120 : 500, QS_ALLINPUT);
        else if (!recentInput)
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 30, QS_ALLINPUT);

        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(15);
            continue;
        }
        g_occluded = false;

        if (g_resizeW != 0 && g_resizeH != 0)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.Frame();
        ImGui::Render();

        const float clear[4] = { 0.07f, 0.08f, 0.106f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT hr = g_swapChain->Present(1, 0);
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);

        app.PostFrame(); // native menus / drag & drop / dialogs run here (they have their own message loops)
        if (app.WantsQuit()) ::DestroyWindow(hwnd);
    }

    app.Shutdown();
    gex::icons::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    if (::IsWindow(hwnd)) ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, hInstance);
    ::OleUninitialize();
    return 0;
}
