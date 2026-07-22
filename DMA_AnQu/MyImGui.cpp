#include "MyImGui.h"

// 显式声明ImGui的Win32消息处理函数
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool MyImGui::Init(HWND hWnd, const wchar_t* windowTitle, int width, int height, int flag, float FontSize)
{



    // 处理窗口创建（如果外部未提供HWND）
    if (hWnd == nullptr)
    {
        ImGui_ImplWin32_EnableDpiAwareness();
        float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
            ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY)
        );

        // 注册窗口类
        m_wc = { sizeof(m_wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr),
                nullptr, nullptr, nullptr, nullptr, L"ImGui Class", nullptr };
        ::RegisterClassExW(&m_wc);

        // 窗口样式：无边框 (WS_POPUP) + 分层透明 + 鼠标穿透 + 置顶
        DWORD windowStyle = WS_POPUP;
        DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST;

        // 窗口位置和尺寸
        int x = 0;
        int y = 0;
        int w = (int)(width * main_scale);
        int h = (int)(height * main_scale);

        // 创建窗口（使用扩展样式）
        m_hwnd = ::CreateWindowExW(
            exStyle, m_wc.lpszClassName, windowTitle, windowStyle,
            x, y, w, h,
            nullptr, nullptr, m_wc.hInstance, this
        );
        ::SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::ShowWindow(m_hwnd, flag);
        ::UpdateWindow(m_hwnd);


    }
    else
    {
        m_hwnd = hWnd;
    }

    // 初始化Direct3D
    if (!CreateDeviceD3D())
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
        return false;
    }

    // 将纯黑色 (RGB 0,0,0) 设为透明色键：视觉透明 + 鼠标穿透
    ::SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 启用键盘导航
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // 启用游戏手柄导航

    // 设置ImGui样式
    ImGui::StyleColorsDark();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY)
    );
    ImGui::GetStyle().ScaleAllSizes(main_scale);
    ImGui::GetStyle().FontScaleDpi = main_scale;

    // 初始化ImGui后端
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);


    io.IniFilename = nullptr;
    ImFontConfig Font_cfg;
    Font_cfg.FontDataOwnedByAtlas = false;
    ImFont* Font = io.Fonts->AddFontFromMemoryTTF((void*)Font_data, Font_size, FontSize, &Font_cfg, io.Fonts->GetGlyphRangesChineseFull());

    return true;
}

void MyImGui::Run(DrawFunction drawFunc)
{
    bool done = false;
    while (!done)
    {
        // 消息循环
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        // 窗口 occlusion 处理
        if (m_SwapChainOccluded && m_pSwapChain->Present(0, 0) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        m_SwapChainOccluded = false;

        // 处理窗口大小调整
        if (m_ResizeWidth != 0 && m_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            m_pSwapChain->ResizeBuffers(0, m_ResizeWidth, m_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            m_ResizeWidth = m_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // 开始ImGui帧
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 调用外部传入的绘制函数
        drawFunc();

        // 渲染
        ImGui::Render();
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };  // 纯黑背景，由 LWA_COLORKEY 变为透明
        m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
        m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // 显示渲染结果
        HRESULT hr = m_pSwapChain->Present(1, 0);  // 带垂直同步
        m_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }
}

void MyImGui::Shutdown()
{
    // 清理ImGui
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // 清理Direct3D
    CleanupDeviceD3D();

    // 清理窗口
    if (m_hwnd)
        ::DestroyWindow(m_hwnd);
    ::UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
}

bool MyImGui::CreateDeviceD3D()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;  // 调试模式下启用调试
#endif

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    // 创建D3D设备和交换链
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain,
        &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext
    );

    // 如果硬件加速不可用，尝试使用WARP软件渲染
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain,
            &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext
        );

    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void MyImGui::CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
    if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
    if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

void MyImGui::CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
    pBackBuffer->Release();
}

void MyImGui::CleanupRenderTarget()
{
    if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
}

LRESULT WINAPI MyImGui::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 通过窗口数据获取MyImGui实例
    if (msg == WM_NCCREATE)
    {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    MyImGui* imgui = (MyImGui*)::GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    // 让ImGui处理消息
    if (imgui && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    // 处理窗口消息
    switch (msg)
    {
    case WM_NCHITTEST:
    {
        // 始终返回 HTCLIENT，让窗口先接收所有鼠标事件
        // 穿透处理移至 WM_LBUTTONDOWN 等实际点击消息中
        LRESULT hit = ::DefWindowProcW(hWnd, msg, wParam, lParam);
        return (hit == HTCLIENT) ? HTCLIENT : hit;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    {
        // ImGui 已在上方处理过，若不需要鼠标则转发给下层窗口
        if (imgui && !ImGui::GetIO().WantCaptureMouse) {
            POINT pt = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
            ClientToScreen(hWnd, &pt);
            HWND hUnder = WindowFromPoint(pt);
            if (hUnder && hUnder != hWnd) {
                ScreenToClient(hUnder, &pt);
                PostMessage(hUnder, msg, wParam, MAKELPARAM(pt.x, pt.y));
                return 0;
            }
        }
        break;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        if (imgui) {
            imgui->m_ResizeWidth = (UINT)LOWORD(lParam);
            imgui->m_ResizeHeight = (UINT)HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // 禁用ALT菜单
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
