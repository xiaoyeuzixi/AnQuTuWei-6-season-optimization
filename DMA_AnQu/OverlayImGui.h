#pragma once
#include <functional>
#include <vector>
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

using DrawCallback = std::function<void()>;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class OverlayImGui
{
public:
    bool Init(HINSTANCE hInstance, const wchar_t* bgImagePath = nullptr, float fontSize = 18.0f)
    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        ZeroMemory(&m_wc, sizeof(m_wc));
        m_wc.cbSize        = sizeof(WNDCLASSEXW);
        m_wc.style         = CS_CLASSDC;
        m_wc.lpfnWndProc   = WndProc;
        m_wc.hInstance     = hInstance;
        m_wc.lpszClassName = L"WBOverlayClass";
        RegisterClassExW(&m_wc);

        m_hwnd = CreateWindowExW(0x08080088L, L"WBOverlayClass", L"", WS_POPUP,
            0, 0, sw, sh, NULL, NULL, hInstance, this);
        if (!m_hwnd) return false;

        SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        if (!CreateDeviceD3D(sw, sh)) return false;

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        LoadFont(fontSize);

        ImGui::StyleColorsLight();
        ImVec4* c = ImGui::GetStyle().Colors;
        c[ImGuiCol_Text]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        c[ImGuiCol_TextDisabled]    = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        c[ImGuiCol_WindowBg]        = ImVec4(0.12f, 0.15f, 0.20f, 1.00f);
        c[ImGuiCol_ChildBg]         = ImVec4(0.12f, 0.15f, 0.20f, 1.00f);
        c[ImGuiCol_TitleBg]         = ImVec4(0.08f, 0.10f, 0.14f, 1.00f);
        c[ImGuiCol_TitleBgActive]   = ImVec4(0.15f, 0.18f, 0.25f, 1.00f);
        c[ImGuiCol_MenuBarBg]       = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
        c[ImGuiCol_Button]          = ImVec4(0.08f, 0.35f, 0.65f, 1.00f);
        c[ImGuiCol_ButtonHovered]   = ImVec4(0.12f, 0.45f, 0.80f, 1.00f);
        c[ImGuiCol_ButtonActive]    = ImVec4(0.06f, 0.28f, 0.55f, 1.00f);
        c[ImGuiCol_FrameBg]         = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
        c[ImGuiCol_FrameBgHovered]  = ImVec4(0.22f, 0.26f, 0.34f, 1.00f);
        c[ImGuiCol_FrameBgActive]   = ImVec4(0.26f, 0.30f, 0.40f, 1.00f);
        c[ImGuiCol_CheckMark]       = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrabActive]= ImVec4(0.20f, 0.50f, 0.85f, 1.00f);
        c[ImGuiCol_Header]          = ImVec4(0.20f, 0.40f, 0.65f, 0.50f);
        c[ImGuiCol_HeaderHovered]   = ImVec4(0.20f, 0.40f, 0.65f, 0.70f);
        c[ImGuiCol_HeaderActive]    = ImVec4(0.15f, 0.35f, 0.60f, 0.90f);
        c[ImGuiCol_Border]          = ImVec4(0.30f, 0.35f, 0.45f, 0.50f);
        c[ImGuiCol_ResizeGrip]      = ImVec4(0.30f, 0.65f, 1.00f, 0.50f);
        c[ImGuiCol_ResizeGripHovered]=ImVec4(0.30f, 0.65f, 1.00f, 0.75f);
        c[ImGuiCol_PopupBg]         = ImVec4(0.15f, 0.18f, 0.25f, 1.00f);
        c[ImGuiCol_Separator]       = ImVec4(0.30f, 0.35f, 0.45f, 0.50f);

        ImGuiStyle& style = ImGui::GetStyle();
        style.FramePadding     = ImVec2(8.0f, 5.0f);
        style.ItemSpacing      = ImVec2(10.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 5.0f);
        style.GrabMinSize      = 14.0f;

        if (bgImagePath && bgImagePath[0])
            LoadBackgroundTexture(bgImagePath);

        ImGui_ImplWin32_Init(m_hwnd);
        ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        return true;
    }

    void Run(DrawCallback drawFunc)
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
            float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, NULL);
            m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            m_pSwapChain->Present(0, 0);
        }
    }

    void Shutdown()
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (m_bgTexture) { m_bgTexture->Release(); m_bgTexture = nullptr; }
        CleanupDeviceD3D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
        UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
    }

    HWND GetHWND() const { return m_hwnd; }
    ID3D11ShaderResourceView* GetBgTexture() const { return m_bgTexture; }
    int GetBgTexW() const { return m_bgTexW; }
    int GetBgTexH() const { return m_bgTexH; }

private:
    HWND                       m_hwnd = nullptr;
    ID3D11Device*              m_pd3dDevice = nullptr;
    ID3D11DeviceContext*       m_pd3dDeviceContext = nullptr;
    IDXGISwapChain*            m_pSwapChain = nullptr;
    ID3D11RenderTargetView*    m_mainRenderTargetView = nullptr;
    ID3D11ShaderResourceView*  m_bgTexture = nullptr;
    int                        m_bgTexW = 0, m_bgTexH = 0;
    WNDCLASSEXW                m_wc = {};

    static void ForwardMouseToUnderlying(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hWnd, &pt);
        HWND hUnder = WindowFromPoint(pt);
        if (hUnder && hUnder != hWnd) {
            ScreenToClient(hUnder, &pt);
            PostMessageW(hUnder, msg, wParam, MAKELPARAM(pt.x, pt.y));
        }
    }

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE) {
            SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                (LONG_PTR)((LPCREATESTRUCT)lParam)->lpCreateParams);
        }
        OverlayImGui* self = (OverlayImGui*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            if (!ImGui::GetIO().WantCaptureMouse) {
                ForwardMouseToUnderlying(hWnd, msg, wParam, lParam);
                return 0;
            }
            return true;
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            if (!ImGui::GetIO().WantCaptureKeyboard) {
                HWND gameHwnd = FindWindowA("UnrealWindow", NULL);
                if (gameHwnd && IsWindow(gameHwnd))
                    PostMessageW(gameHwnd, msg, wParam, lParam);
                return 0;
            }
            return true;
        case WM_DESTROY:
            PostQuitMessage(0); return 0;
        case WM_SIZE:
            if (self && self->m_pd3dDevice && wParam != SIZE_MINIMIZED) {
                if (self->m_mainRenderTargetView) {
                    self->m_mainRenderTargetView->Release();
                    self->m_mainRenderTargetView = nullptr;
                }
                self->m_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                ID3D11Texture2D* bb = nullptr;
                self->m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
                if (bb) {
                    self->m_pd3dDevice->CreateRenderTargetView(bb, NULL, &self->m_mainRenderTargetView);
                    bb->Release();
                }
            }
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool CreateDeviceD3D(int width, int height)
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = width;
        sd.BufferDesc.Height = height;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = m_hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL feaLevel;
        if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
            NULL, 0, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice,
            &feaLevel, &m_pd3dDeviceContext)))
            return false;

        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D()
    {
        CleanupRenderTarget();
        if (m_pSwapChain)        { m_pSwapChain->Release();        m_pSwapChain = nullptr; }
        if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
        if (m_pd3dDevice)        { m_pd3dDevice->Release();        m_pd3dDevice = nullptr; }
    }

    void CreateRenderTarget()
    {
        ID3D11Texture2D* pBack = nullptr;
        m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBack);
        if (pBack) {
            m_pd3dDevice->CreateRenderTargetView(pBack, NULL, &m_mainRenderTargetView);
            pBack->Release();
        }
    }

    void CleanupRenderTarget()
    {
        if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
    }

    bool LoadBackgroundTexture(const wchar_t* path)
    {
        if (!path || !path[0]) return false;
        IWICImagingFactory* wic = nullptr;
        CoInitialize(nullptr);
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC, IID_PPV_ARGS(&wic))) || !wic)
            return false;
        IWICBitmapDecoder* dec = nullptr;
        if (FAILED(wic->CreateDecoderFromFilename(path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) || !dec)
            { wic->Release(); return false; }
        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(dec->GetFrame(0, &frame)) || !frame)
            { dec->Release(); wic->Release(); return false; }
        IWICFormatConverter* conv = nullptr;
        wic->CreateFormatConverter(&conv);
        conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
        UINT tw = 0, th = 0;
        conv->GetSize(&tw, &th);
        std::vector<BYTE> pixels(tw * th * 4);
        conv->CopyPixels(NULL, tw * 4, tw * th * 4, pixels.data());
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = tw; td.Height = th; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA srd = { pixels.data(), tw * 4, 0 };
        ID3D11Texture2D* tex = nullptr;
        m_pd3dDevice->CreateTexture2D(&td, &srd, &tex);
        if (tex) {
            m_pd3dDevice->CreateShaderResourceView(tex, NULL, &m_bgTexture);
            m_bgTexW = tw; m_bgTexH = th;
            tex->Release();
        }
        conv->Release(); frame->Release(); dec->Release(); wic->Release();
        return m_bgTexture != nullptr;
    }

    bool LoadFont(float fontSize)
    {
        ImGuiIO& io = ImGui::GetIO();
        static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x4E00, 0x9FFF, 0x3000, 0x30FF, 0 };
        ImFontConfig cfg;
        cfg.MergeMode = false;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", fontSize, &cfg, ranges);
        return true;
    }
};
