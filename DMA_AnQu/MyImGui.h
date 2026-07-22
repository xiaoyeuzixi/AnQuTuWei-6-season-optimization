#pragma once
#include <functional>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <iostream>
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx11.h"
#include <tchar.h>
#include "Font.h"



// 定义绘制函数的回调类型
using DrawFunction = std::function<void()>;

class MyImGui
{
public:
    // 初始化 ImGui 和 Direct3D
    bool Init(HWND hWnd = nullptr, const wchar_t* windowTitle = L"ImGui Window", int width = 1280, int height = 800, int flag = SW_SHOWDEFAULT, float FontSize = 17.0f);

    // 运行主循环，传入用户绘制函数
    void Run(DrawFunction drawFunc);

    // 清理资源
    void Shutdown();

    // 获取窗口句柄
    HWND GetHWND() const { return m_hwnd; }

private:
    // 内部成员变量
    HWND m_hwnd = nullptr;
    ID3D11Device* m_pd3dDevice = nullptr;
    ID3D11DeviceContext* m_pd3dDeviceContext = nullptr;
    IDXGISwapChain* m_pSwapChain = nullptr;
    ID3D11RenderTargetView* m_mainRenderTargetView = nullptr;
    bool m_SwapChainOccluded = false;
    UINT m_ResizeWidth = 0, m_ResizeHeight = 0;
    WNDCLASSEXW m_wc = {};

    // 内部辅助函数
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

