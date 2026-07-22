#pragma once
/*
 * Overlay.h — 透明穿透叠加层 (移植自 KAKA 项目)
 *
 * 特性：
 *   - WS_EX_LAYERED + LWA_COLORKEY 黑色透明
 *   - 窗口标题伪装 "NVIDIA GeForce Overlay"
 *   - 类名伪装 "CEF-OSC-WIDGET"
 *   - 鼠标穿透动态切换 (游戏内穿透 / 菜单可交互)
 *   - D3D11 + ImGui 渲染
 */

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <functional>
#include <chrono>
#include <thread>
#include <mmsystem.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winmm.lib")

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "HoloUI.h"
#include "core/Runtime.h"

// ImGui 内部符号声明
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <cstdio>
#include <cstdlib>
extern int g_lang;  // 语言选择 (0=中文, 1=英文), 实际定义在 main.h

// ═══════════════════════════════════════
//  统一错误/正常退出: 显示控制台 + 等待回车 + 退出
//  确保任何错误路径下用户都能看到信息并按回车后才退出
// ═══════════════════════════════════════
inline void WaitEnterAndExit(int code) {
    Runtime::Stop();
    HWND hCon = GetConsoleWindow();
    if (hCon) ShowWindow(hCon, SW_SHOW);  // 控制台可能已被隐藏, 重新显示
    if (g_lang) {
        printf("\nPress ENTER to exit...\n");
    } else {
        printf("\n按回车键退出...\n");
    }
    fflush(stdin);
    while (getchar() != '\n') {}
    exit(code);
}

// ── 全局 D3D 设备 ──
inline ID3D11Device*            g_pd3dDevice        = nullptr;
inline ID3D11DeviceContext*     g_pd3dDeviceContext  = nullptr;
inline IDXGISwapChain*          g_pSwapChain         = nullptr;
inline ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
inline ID3D11ShaderResourceView* g_BgTexture = nullptr;
inline int                      g_BgTexW = 0, g_BgTexH = 0;
inline HWND                     g_OverlayHwnd = nullptr;   // overlay 窗口句柄
inline int                      g_DisplayMode = 0;         // 0=扩展 1=复制
inline int                      g_CurMonitor  = 1;         // 当前显示器
inline bool                     g_OverlayVisible = true;

// 帧率限制 (由 main.h 定义, Overlay 读取)
extern bool g_FpsLimitEnabled;
extern int  g_FpsLimit;
extern bool g_LowSpecMode;

using DrawFunc = std::function<void()>;

// 获取指定显示器矩形
inline RECT GetMonitorRect(int index) {
    RECT r{};
    DISPLAY_DEVICEW dd{sizeof(dd)};
    DEVMODEW dm{};
    if (EnumDisplayDevicesW(nullptr, index, &dd, 0) && EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
        r.left = dm.dmPosition.x; r.top = dm.dmPosition.y;
        r.right = r.left + (LONG)dm.dmPelsWidth; r.bottom = r.top + (LONG)dm.dmPelsHeight;
    } else {
        r.right = GetSystemMetrics(SM_CXSCREEN); r.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return r;
}

// 缓存版 GetMonitorRect: 避免每帧调用 EnumDisplayDevicesW + EnumDisplaySettingsW
// 显示器分辨率极少变化, 2秒刷新一次即可
inline RECT GetCachedMonitorRect(int index) {
    static RECT cachedRect[4] = {};
    static int cachedIndex[4] = {-1, -1, -1, -1};
    static std::chrono::steady_clock::time_point lastRefresh[4];
    auto now = std::chrono::steady_clock::now();
    int slot = index & 3;
    // 首次或索引变化或超过2秒: 重新查询
    if (cachedIndex[slot] != index ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefresh[slot]).count() > 2000) {
        cachedRect[slot] = GetMonitorRect(index);
        cachedIndex[slot] = index;
        lastRefresh[slot] = now;
    }
    return cachedRect[slot];
}

// ======================== 背景图加载 ========================
inline bool LoadBgTexture(const wchar_t* path) {
    if (g_BgTexture) { g_BgTexture->Release(); g_BgTexture = nullptr; }
    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return false;

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr)) { pFactory->Release(); return false; }

    IWICBitmapFrameDecode* pFrame = nullptr;
    pDecoder->GetFrame(0, &pFrame);
    pFrame->GetSize((UINT*)&g_BgTexW, (UINT*)&g_BgTexH);

    IWICFormatConverter* pConverter = nullptr;
    pFactory->CreateFormatConverter(&pConverter);
    pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = g_BgTexW; desc.Height = g_BgTexH;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = new BYTE[g_BgTexW * g_BgTexH * 4];
    init.SysMemPitch = g_BgTexW * 4;
    pConverter->CopyPixels(nullptr, g_BgTexW * 4, g_BgTexW * g_BgTexH * 4, (BYTE*)init.pSysMem);

    ID3D11Texture2D* pTex = nullptr;
    g_pd3dDevice->CreateTexture2D(&desc, &init, &pTex);
    g_pd3dDevice->CreateShaderResourceView(pTex, nullptr, &g_BgTexture);
    pTex->Release();
    delete[] (BYTE*)init.pSysMem;

    pConverter->Release(); pFrame->Release(); pDecoder->Release(); pFactory->Release();
    return true;
}

// ======================== Overlay 工具函数 ========================

inline bool CreateDeviceD3D(HWND hwnd, int w, int h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width  = w;
    sd.BufferDesc.Height = h;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, nullptr, &g_pd3dDeviceContext);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return true;
}

inline void CleanupDeviceD3D() {
    if (g_BgTexture)           { g_BgTexture->Release(); g_BgTexture = nullptr; }
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain)          { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext)   { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)          { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// 鼠标穿透切换
inline void ClickThrough(bool through) {
    if (!g_OverlayHwnd) return;
    LONG ex = GetWindowLong(g_OverlayHwnd, GWL_EXSTYLE);
    if (through)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    SetWindowLong(g_OverlayHwnd, GWL_EXSTYLE, ex);
}

// 移动窗口到指定显示器
inline void MoveToMonitor(int idx) {
    auto r = GetMonitorRect(idx);
    SetWindowPos(g_OverlayHwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
    g_CurMonitor = idx;
}

inline int CurrentMonitor() { return g_CurMonitor; }

inline void BeginDraw() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

// ======================== Dark Purple Tech UI Theme ========================
inline void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // ── 暗紫科技色板: 纯紫单色调 + 统一层次感 ──

    // ── 文字梯度 (per spec) ──
    colors[ImGuiCol_Text]                   = ImVec4(0.878f, 0.878f, 0.878f, 1.00f);   // #E0E0E0 body text
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.627f, 0.627f, 0.722f, 1.00f);   // #A0A0B8 auxiliary

    // ── 背景基底 ──
    colors[ImGuiCol_WindowBg]               = ImVec4(0.051f, 0.051f, 0.102f, 0.97f);   // #0D0D1A deep base
    colors[ImGuiCol_ChildBg]                = ImVec4(0.071f, 0.063f, 0.149f, 0.60f);   // #121026 panel
    colors[ImGuiCol_PopupBg]                = ImVec4(0.059f, 0.039f, 0.102f, 0.96f);   // #0F0A1A popup

    // ── 边框: 紫色调发光 ──
    colors[ImGuiCol_Border]                 = ImVec4(0.486f, 0.227f, 0.929f, 0.55f);   // #7C3AED violet
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.10f, 0.04f, 0.20f, 0.30f);

    // ── 输入框/帧背景: 暗紫色阶 ──
    colors[ImGuiCol_FrameBg]                = ImVec4(0.176f, 0.106f, 0.306f, 0.65f);   // #2D1B4E
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.290f, 0.176f, 0.478f, 0.45f);   // #4A2D7A
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.420f, 0.247f, 0.627f, 0.70f);   // #6B3FA0

    // ── 标题栏: 暗紫→亮紫渐变 ──
    colors[ImGuiCol_TitleBg]                = ImVec4(0.071f, 0.063f, 0.149f, 1.00f);   // #121026
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.176f, 0.106f, 0.306f, 1.00f);   // #2D1B4E
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.05f,  0.04f,  0.10f, 0.60f);

    // ── 菜单栏 ──
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.059f, 0.050f, 0.130f, 1.00f);

    // ── 滚动条: 紫色调 ──
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.039f, 0.039f, 0.080f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.486f, 0.227f, 0.929f, 0.90f);   // #7C3AED
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);   // #A78BFA
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);   // #C4B5FD

    // ── 复选框/滑块: 紫色系 ──
    colors[ImGuiCol_CheckMark]              = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);   // #A78BFA
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.486f, 0.227f, 0.929f, 1.00f);   // #7C3AED
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);   // #C4B5FD

    // ── 按钮: 主按钮亮紫填充 / 次按钮暗紫描边 (per spec) ──
    colors[ImGuiCol_Button]                 = ImVec4(0.290f, 0.176f, 0.478f, 0.50f);   // #4A2D7A dark-purple base
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.486f, 0.227f, 0.929f, 0.90f);   // #7C3AED bright violet hover
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);   // #A78BFA pressed

    // ── 标题行/选择项: 紫色调 ──
    colors[ImGuiCol_Header]                 = ImVec4(0.102f, 0.063f, 0.180f, 0.40f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.486f, 0.227f, 0.929f, 0.70f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);

    // ── 分隔线: 紫色调 ──
    colors[ImGuiCol_Separator]              = ImVec4(0.290f, 0.176f, 0.478f, 0.45f);   // #4A2D7A default border
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.655f, 0.545f, 0.980f, 0.80f);   // highlight
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);

    // ── 调整大小手柄 ──
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.486f, 0.227f, 0.929f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.655f, 0.545f, 0.980f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.769f, 0.710f, 0.992f, 1.00f);

    // ── 标签页: 紫色调 (选中态亮紫) ──
    colors[ImGuiCol_Tab]                    = ImVec4(0.176f, 0.106f, 0.306f, 0.85f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.486f, 0.227f, 0.929f, 0.80f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.06f, 0.05f, 0.12f, 0.90f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.137f, 0.086f, 0.247f, 1.00f);

    // ── 圆角 (增强科技感) ──
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 5.0f;

    // ── 边框尺寸 ──
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;

    // ── 间距 (更宽松, 悬浮卡片感) ──
    style.WindowPadding      = ImVec2(12.0f, 12.0f);
    style.FramePadding       = ImVec2(8.0f, 5.0f);
    style.ItemSpacing        = ImVec2(10.0f, 7.0f);
    style.ItemInnerSpacing   = ImVec2(7.0f, 6.0f);
    style.ScrollbarSize      = 14.0f;
    style.GrabMinSize        = 10.0f;

    // ── 窗口最小尺寸 ──
    style.WindowMinSize      = ImVec2(200.0f, 200.0f);

    // ── 抗锯齿 ──
    style.AntiAliasedLines       = true;
    style.AntiAliasedFill        = true;
    style.AntiAliasedLinesUseTex = true;

    // ── 曲线 (更平滑的动画过渡) ──
    style.CurveTessellationTol   = 1.25f;
    style.CircleTessellationMaxError = 0.30f;
}

inline void EndDraw() {
    static const float clear[4] = {0,0,0,0};
    ImGui::Render();
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(0, 0);  // vsync=0
}

inline LRESULT WINAPI OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* pBackBuffer;
            g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
            pBackBuffer->Release();
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ======================== Overlay 初始化 & 主循环 ========================
// mode: 0=扩展(副屏黑底, F6切换显示器)  1=复制(主屏黑底, F6显示/隐藏)
inline int OverlayRun(DrawFunc draw, int mode) {
    g_DisplayMode = mode;

    RECT mr;
    if (mode == 0) {
        mr = GetMonitorRect(1);
        if (mr.right - mr.left <= 0) mr = GetMonitorRect(0); // 单屏兜底
        g_CurMonitor = 1;
    } else {
        mr = GetMonitorRect(0);
        g_CurMonitor = 0;
    }
    int sw = mr.right - mr.left, sh = mr.bottom - mr.top;
    if (sw <= 0 || sh <= 0) { sw = GetSystemMetrics(SM_CXSCREEN); sh = GetSystemMetrics(SM_CYSCREEN); }

    // 注册窗口类
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hbrBackground = CreateSolidBrush(RGB(0,0,0));
    wc.lpszClassName = L"CEF-OSC-WIDGET";
    RegisterClassExW(&wc);

    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;

    g_OverlayHwnd = CreateWindowExW(exStyle,
        L"CEF-OSC-WIDGET", L"NVIDIA GeForce Overlay", WS_POPUP,
        mr.left, mr.top, sw, sh, nullptr, nullptr, nullptr, nullptr);
    if (!g_OverlayHwnd) { printf(g_lang ? "Overlay: CreateWindowEx failed!\n" : "Overlay: 创建窗口失败!\n"); WaitEnterAndExit(1); }

    ShowWindow(g_OverlayHwnd, SW_SHOWNOACTIVATE);

    if (!CreateDeviceD3D(g_OverlayHwnd, sw, sh)) {
        printf(g_lang ? "Overlay: CreateDeviceD3D failed!\n" : "Overlay: D3D设备创建失败!\n");
        CleanupDeviceD3D();
        DestroyWindow(g_OverlayHwnd);
        WaitEnterAndExit(1);
    }

    // 加载背景图
    CoInitialize(nullptr);
    wchar_t bgPath[MAX_PATH];
    GetModuleFileNameW(nullptr, bgPath, MAX_PATH);
    wchar_t* p = wcsrchr(bgPath, L'\\');
    if (p) *(p + 1) = L'\0';
    wcscat_s(bgPath, L"res\\bg.png");
    if (!LoadBgTexture(bgPath))
        printf("[WARN] res/bg.png not found\n");

    // ImGui 初始化
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // 加载中文字体 (微软雅黑)
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 17.0f, nullptr,
        io.Fonts->GetGlyphRangesChineseFull());
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(g_OverlayHwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // 应用赛博朋克 UI 主题
    SetupImGuiStyle();

    g_OverlayVisible = true;

    // 提高系统定时器精度到 1ms (默认 15.6ms, 会导致 sleep_for 精度极差)
    timeBeginPeriod(1);

    // 保持默认线程优先级。
    // 7.3 备份里渲染线程和数据线程都是 NORMAL；之前把渲染线程提到 ABOVE_NORMAL，
    // 同时又把 Actor/Bones 等数据线程降到 BELOW_NORMAL，在 i3 4代上会导致数据线程被渲染线程饿死：
    // 即使只画一个人机框，WorldEntry/骨骼/坐标也会间歇更新，表现为绘制一卡一卡。

    // ── 主循环 ──
    bool running = true;
    while (running && Runtime::IsRunning()) {
        auto frameStart = std::chrono::steady_clock::now();

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        BeginDraw();
        draw();
        EndDraw();

        // ★修复: 帧率限制改回调试版的简单实现 (sleep_for + yield 自旋)
        //   原发布版用 Runtime::SleepUntil (condition_variable + mutex), 每帧加锁
        //   调试版用 std::this_thread::sleep_for, 直接系统调用, 零锁开销
        int targetFps = 0;
        if (!g_OverlayVisible) {
            targetFps = 20;                 // 隐藏时低频保活热键
        } else if (g_LowSpecMode) {
            targetFps = 120;                // 低配专属模式固定120
        } else if (g_FpsLimitEnabled) {
            targetFps = g_FpsLimit;         // 普通限帧由UI控制
        } else {
            // 不允许 Overlay 3000FPS 空转抢 CPU。这里不限制读取/绘制对象，
            // 只做帧同步，避免 FPS 数字很高但画面卡爆。
            targetFps = 144;
        }

        if (targetFps > 0) {
            if (targetFps < 20) targetFps = 20;
            if (targetFps > 240) targetFps = 240;

            // 对齐调试版的帧同步：粗睡 + 2ms yield 精等，减少 Windows 睡眠过头导致的帧时间抖动。
            auto targetTime = std::chrono::microseconds(1000000 / targetFps);
            auto elapsed = std::chrono::steady_clock::now() - frameStart;
            if (elapsed < targetTime) {
                auto remaining = targetTime - elapsed;
                auto coarseSleep = remaining - std::chrono::microseconds(2000);
                if (coarseSleep > std::chrono::microseconds(0))
                    std::this_thread::sleep_for(coarseSleep);
                while (std::chrono::steady_clock::now() - frameStart < targetTime)
                    std::this_thread::yield();
            }
        }
    }

    timeEndPeriod(1);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_OverlayHwnd);
    UnregisterClassW(L"CEF-OSC-WIDGET", nullptr);
    return 0;
}
