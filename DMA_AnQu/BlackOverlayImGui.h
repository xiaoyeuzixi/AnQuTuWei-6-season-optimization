/**
 * @file      BlackOverlayImGui.h
 * @brief     黑屏融合器叠加层 — 纯黑背景 + ImGui ESP 绘制
 * @details   适用于 DMA + 视频融合器方案。
 *           游戏画面由采集卡输入融合器，此窗口输出黑底 ESP 图形，
 *           融合器将两者合成后输出到显示器。
 *
 * 用法:
 * @code
 *   BlackOverlay overlay;
 *   overlay.Init(hInstance, 1);   // 1=副屏
 *   overlay.Run(MyDrawCallback);   // 主循环
 *   overlay.Shutdown();
 * @endcode
 */

#pragma once
#include <functional>
#include <vector>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>           // WIC — 用于加载 PNG/JPG
#pragma comment(lib, "windowscodecs.lib")
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using BlackDrawCallback = std::function<void()>;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class BlackOverlay
{
public:
    BlackOverlay() = default;
    ~BlackOverlay() { Shutdown(); }

    // ────────────────────────────────────────
    //  Init — 初始化黑屏叠加窗口 (配合融合器 luma key)
    //
    //  原理: 全屏纯黑背景 + ImGui 画 ESP
    //        融合器识别黑像素(luma=0) → 抠掉 → 露出副机游戏画面
    //        非黑像素(ESP 方框文字) → 保留 → 合成到游戏上
    //
    //  ★ 扩展屏用法 (双头显卡):
    //        HDMI1 → 主显示器, HDMI2 → 融合器
    //        Windows 设"扩展", Init(hInst, 1) → 黑屏放扩展屏
    //        按 F6 → 窗口在主屏/扩展屏之间切换 (调 UI)
    //
    //  monitorIndex: 0=主屏, 1=扩展屏, ...
    //  clickThrough: true=鼠标穿透, false=可点 UI (扩展屏建议 true)
    //  fontSize:     中文字体大小
    // ────────────────────────────────────────
    bool Init(HINSTANCE hInstance, int monitorIndex = 1, bool clickThrough = true, float fontSize = 18.0f)
    {
        RECT monitorRect = GetMonitorRect(monitorIndex);
        m_w = monitorRect.right  - monitorRect.left;
        m_h = monitorRect.bottom - monitorRect.top;
        m_x = monitorRect.left;
        m_y = monitorRect.top;

        ZeroMemory(&m_wc, sizeof(m_wc));
        m_wc.cbSize        = sizeof(WNDCLASSEXW);
        m_wc.style         = CS_CLASSDC;
        m_wc.lpfnWndProc   = WndProc;
        m_wc.hInstance     = hInstance;
        m_wc.lpszClassName = L"BlackOverlayFusion";
        RegisterClassExW(&m_wc);

        // 全屏黑窗: 置顶 + 不抢焦点 + 无边框
        // 扩展屏加 WS_EX_TRANSPARENT (鼠标透过, 不影响窗口管理)
        DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE;
        m_clickThrough = clickThrough;
        if (clickThrough) exStyle |= WS_EX_TRANSPARENT;

        m_hwnd = CreateWindowExW(
            exStyle, L"BlackOverlayFusion", L"FusionOverlay",
            WS_POPUP,
            m_x, m_y, m_w, m_h,
            NULL, NULL, hInstance, this);
        if (!m_hwnd) return false;

        if (!CreateDeviceD3D()) return false;

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.MouseDrawCursor = !clickThrough;

        LoadFont(fontSize);
        SetupStyle();

        ImGui_ImplWin32_Init(m_hwnd);
        ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

        ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);
        UpdateWindow(m_hwnd);
        return true;
    }

    // ────────────────────────────────────────
    //  Run — 主渲染循环 (黑底 + ESP)
    // ────────────────────────────────────────
    void Run(BlackDrawCallback drawFunc)
    {
        MSG msg = {};
        while (true)
        {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT) return;
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            drawFunc();

            ImGui::Render();

            // 纯黑背景 — 融合器 luma key 把黑像素变透明
            const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, NULL);
            m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, black);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            m_pSwapChain->Present(0, 0); // 不限帧率
        }
    }

    void Shutdown()
    {
        if (m_pd3dDeviceContext) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        if (m_bgTextureView) { m_bgTextureView->Release(); m_bgTextureView = nullptr; }
        CleanupDeviceD3D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
        UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
    }

    HWND  GetHWND()   const { return m_hwnd; }
    int   GetWidth()  const { return m_w; }
    int   GetHeight() const { return m_h; }
    ID3D11ShaderResourceView* GetBgTexture() const { return m_bgTextureView; }
    int  GetBgTexW() const { return m_bgW; }
    int  GetBgTexH() const { return m_bgH; }

    // ────────────────────────────────────────
    //  SetClickThrough — 动态切换鼠标穿透
    //   true = 鼠标穿透到下层 (游戏/融合器), 看不见 UI 也点不到
    //   false = UI 可点击, 但鼠标事件会被本窗口拦截 (游戏收不到)
    //
    //  ★ 调试方案 (融合器场景):
    //      1) 程序启动时 SetClickThrough(false)  ← 能点击 UI
    //      2) 按 INSERT 或 F6 等快捷键 → 切换为 true ← 游戏中不挡鼠标
    //      3) 切换回 UI 编辑 → 再按一次
    // ────────────────────────────────────────
    void SetClickThrough(bool enable)
    {
        m_clickThrough = enable;
        if (!m_hwnd) return;
        LONG ex = GetWindowLongW(m_hwnd, GWL_EXSTYLE);
        if (enable)  ex |=  WS_EX_TRANSPARENT;
        else         ex &= ~WS_EX_TRANSPARENT;
        SetWindowLongW(m_hwnd, GWL_EXSTYLE, ex);
    }
    bool IsClickThrough() const { return m_clickThrough; }

    // ────────────────────────────────────────
    //  MoveToMonitor — 把窗口瞬间移动到指定显示器
    //   index: 0=主屏, 1=扩展屏, ...
    //   ★ 移到主屏: 自动关闭穿透 + 显示鼠标 → 能点击 UI
    //   ★ 移到扩展屏: 自动开启穿透 + 隐藏鼠标 → 不影响融合器
    //   两个屏分辨率不同时也会自动调整 D3D 渲染目标
    // ────────────────────────────────────────
    bool MoveToMonitor(int index)
    {
        RECT rc = GetMonitorRect(index);
        int nw = rc.right  - rc.left;
        int nh = rc.bottom - rc.top;
        int nx = rc.left;
        int ny = rc.top;

        if (!m_hwnd || !m_pSwapChain) return false;

        // 分辨率变了 → 重建 swap chain
        if (nw != m_w || nh != m_h) {
            if (m_mainRenderTargetView) {
                m_mainRenderTargetView->Release();
                m_mainRenderTargetView = nullptr;
            }
            if (FAILED(m_pSwapChain->ResizeBuffers(0, (UINT)nw, (UINT)nh,
                DXGI_FORMAT_UNKNOWN, 0))) return false;
            ID3D11Texture2D* bb = nullptr;
            m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
            if (bb) {
                m_pd3dDevice->CreateRenderTargetView(bb, NULL, &m_mainRenderTargetView);
                bb->Release();
            }
        }

        SetWindowPos(m_hwnd, HWND_TOPMOST, nx, ny, nw, nh,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_x = nx; m_y = ny; m_w = nw; m_h = nh;

        // 主屏(0) → 能点击, 扩展屏(1) → 穿透
        bool isExt = (index != 0);
        SetClickThrough(isExt);
        if (ImGui::GetCurrentContext()) {
            ImGui::GetIO().MouseDrawCursor = !isExt;
        }

        return true;
    }
    int  CurrentMonitor() const {
        // 推断当前在哪个显示器 (按窗口左上角)
        for (int i = 0; i < 4; i++) {
            RECT rc = GetMonitorRect(i);
            if (m_x >= rc.left && m_x < rc.right &&
                m_y >= rc.top  && m_y < rc.bottom) return i;
        }
        return 0;
    }

    // ────────────────────────────────────────
    //  LoadBackgroundImage — 从文件加载 PNG/JPG
    //   path: 绝对路径 或 相对当前目录的路径 (如 L"res\\bg.png")
    //   返回 true=成功, false=文件不存在或解码失败
    // ────────────────────────────────────────
    bool LoadBackgroundImage(const wchar_t* path)
    {
        if (!m_pd3dDevice) return false;
        if (m_bgTextureView) { m_bgTextureView->Release(); m_bgTextureView = nullptr; }
        m_bgW = m_bgH = 0;

        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        bool needUninit = SUCCEEDED(hr);

        IWICImagingFactory* wicFactory = nullptr;
        hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr) || !wicFactory) { if (needUninit) CoUninitialize(); return false; }

        IWICBitmapDecoder* decoder = nullptr;
        hr = wicFactory->CreateDecoderFromFilename(path, NULL, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr) || !decoder) { wicFactory->Release(); if (needUninit) CoUninitialize(); return false; }

        IWICBitmapFrameDecode* frame = nullptr;
        decoder->GetFrame(0, &frame);

        IWICFormatConverter* converter = nullptr;
        wicFactory->CreateFormatConverter(&converter);
        converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeMedianCut);

        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);
        if (w == 0 || h == 0) {
            converter->Release(); frame->Release(); decoder->Release(); wicFactory->Release();
            if (needUninit) CoUninitialize(); return false;
        }

        std::vector<BYTE> pixels(w * h * 4);
        converter->CopyPixels(NULL, w * 4, (UINT)pixels.size(), pixels.data());

        // 创建 D3D11 纹理
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = pixels.data();
        sd.SysMemPitch = w * 4;

        ID3D11Texture2D* tex = nullptr;
        hr = m_pd3dDevice->CreateTexture2D(&td, &sd, &tex);
        if (FAILED(hr) || !tex) {
            converter->Release(); frame->Release(); decoder->Release(); wicFactory->Release();
            if (needUninit) CoUninitialize(); return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        m_pd3dDevice->CreateShaderResourceView(tex, &srvd, &m_bgTextureView);

        tex->Release();
        converter->Release(); frame->Release(); decoder->Release(); wicFactory->Release();
        if (needUninit) CoUninitialize();

        m_bgW = (int)w; m_bgH = (int)h;
        return m_bgTextureView != nullptr;
    }

private:
    HWND                     m_hwnd = nullptr;
    ID3D11Device*            m_pd3dDevice = nullptr;
    ID3D11DeviceContext*     m_pd3dDeviceContext = nullptr;
    IDXGISwapChain*          m_pSwapChain = nullptr;
    ID3D11RenderTargetView*  m_mainRenderTargetView = nullptr;
    WNDCLASSEXW              m_wc = {};
    int m_x = 0, m_y = 0, m_w = 1920, m_h = 1080;
    ID3D11ShaderResourceView* m_bgTextureView = nullptr;
    int  m_bgW = 0, m_bgH = 0;
    bool m_clickThrough = true;

    // ── 获取指定显示器的矩形 ──
    static RECT GetMonitorRect(int index)
    {
        RECT fallback = { 0, 0, 1920, 1080 };
        struct Ctx { int target, cur; RECT rc; } ctx = { index, 0, fallback };
        EnumDisplayMonitors(NULL, NULL,
            [](HMONITOR, HDC, LPRECT prc, LPARAM lp) -> BOOL {
                auto* c = (Ctx*)lp;
                if (c->cur == c->target) { c->rc = *prc; return FALSE; }
                c->cur++;
                return TRUE;
            }, (LPARAM)&ctx);
        return ctx.rc;
    }

    // ── ImGui 样式 ──
    static void SetupStyle()
    {
        ImGui::StyleColorsDark();
        ImVec4* c = ImGui::GetStyle().Colors;
        // 高亮翠绿/亮蓝配色 — 在黑底上清晰可见
        c[ImGuiCol_Text]             = ImVec4(0.90f, 1.00f, 0.95f, 1.00f);
        c[ImGuiCol_WindowBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
        c[ImGuiCol_ChildBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg]          = ImVec4(0.10f, 0.10f, 0.12f, 0.80f);
        c[ImGuiCol_FrameBgHovered]   = ImVec4(0.15f, 0.15f, 0.18f, 0.90f);
        c[ImGuiCol_Button]           = ImVec4(0.05f, 0.40f, 0.20f, 1.00f);
        c[ImGuiCol_ButtonHovered]    = ImVec4(0.08f, 0.50f, 0.25f, 1.00f);
        c[ImGuiCol_ButtonActive]     = ImVec4(0.03f, 0.30f, 0.15f, 1.00f);
        c[ImGuiCol_CheckMark]        = ImVec4(0.20f, 0.90f, 0.40f, 1.00f);
        c[ImGuiCol_SliderGrab]       = ImVec4(0.20f, 0.70f, 0.90f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.10f, 0.60f, 0.85f, 1.00f);
        c[ImGuiCol_Header]           = ImVec4(0.10f, 0.35f, 0.55f, 0.50f);
        c[ImGuiCol_Border]           = ImVec4(0.15f, 0.15f, 0.20f, 0.50f);
        c[ImGuiCol_TitleBg]          = ImVec4(0.05f, 0.05f, 0.08f, 0.90f);
        c[ImGuiCol_TitleBgActive]    = ImVec4(0.08f, 0.08f, 0.12f, 0.90f);
        c[ImGuiCol_PopupBg]          = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
        ImGui::GetStyle().WindowRounding = 4.0f;
    }

    // ── D3D11 设备 ──
    bool CreateDeviceD3D()
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount  = 2;
        sd.BufferDesc.Width  = m_w;
        sd.BufferDesc.Height = m_h;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator   = 0;
        sd.BufferDesc.RefreshRate.Denominator = 0;
        sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = m_hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed     = TRUE;
        sd.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags        = 0;

        D3D_FEATURE_LEVEL feaLevel;
        if (FAILED(D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
            NULL, 0, D3D11_SDK_VERSION, &sd,
            &m_pSwapChain, &m_pd3dDevice, &feaLevel, &m_pd3dDeviceContext)))
            return false;

        ID3D11Texture2D* bb = nullptr;
        m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
        if (bb) {
            m_pd3dDevice->CreateRenderTargetView(bb, NULL, &m_mainRenderTargetView);
            bb->Release();
        }
        return true;
    }

    void CleanupDeviceD3D()
    {
        if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
        if (m_pSwapChain)           { m_pSwapChain->Release();        m_pSwapChain        = nullptr; }
        if (m_pd3dDeviceContext)    { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext  = nullptr; }
        if (m_pd3dDevice)           { m_pd3dDevice->Release();        m_pd3dDevice         = nullptr; }
    }

    // ── 字体 ──
    static bool LoadFont(float fontSize)
    {
        ImGuiIO& io = ImGui::GetIO();
        static const ImWchar ranges[] = {
            0x0020, 0x00FF,   // Latin
            0x4E00, 0x9FFF,   // CJK Unified
            0x3000, 0x30FF,   // CJK Symbols
            0 };
        ImFontConfig cfg;
        cfg.MergeMode = false;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", fontSize, &cfg, ranges);
        return true;
    }

    // ── 窗口过程 (不转发鼠标/键盘 — 融合器不需要) ──
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)((LPCREATESTRUCT)lParam)->lpCreateParams);

        BlackOverlay* self = (BlackOverlay*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

        // 所有鼠标键盘消息只给 ImGui，不转发
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg)
        {
        case WM_SIZE:
            if (self && self->m_pd3dDevice && wParam != SIZE_MINIMIZED) {
                UINT nw = LOWORD(lParam), nh = HIWORD(lParam);
                if (self->m_mainRenderTargetView) {
                    self->m_mainRenderTargetView->Release();
                    self->m_mainRenderTargetView = nullptr;
                }
                self->m_pSwapChain->ResizeBuffers(0, nw, nh, DXGI_FORMAT_UNKNOWN, 0);
                ID3D11Texture2D* bb = nullptr;
                self->m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
                if (bb) {
                    self->m_pd3dDevice->CreateRenderTargetView(bb, NULL, &self->m_mainRenderTargetView);
                    bb->Release();
                }
                self->m_w = nw; self->m_h = nh;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};
