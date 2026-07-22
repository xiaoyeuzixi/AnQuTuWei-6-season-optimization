#pragma once
/*
 * core/Config.h — 全局配置变量集中声明
 *
 * 从 main.h 抽离的所有可配置项:
 *   - 服务器版本 & 语言
 *   - ESP 开关 / 距离 / 颜色
 *   - 人机 / 物资 / 雷达 配置
 *   - 快捷键 & 帧率限制
 *   - 战斗辅助系统变量
 *
 * 采用 C++17 inline 变量, header-only
 * SaveGlobalConfig/LoadGlobalConfig 实现在 core/Config.cpp
 *
 * 依赖: Windows.h (VK_*), <string>, <cstdint>
 */

#include <Windows.h>
#include <string>
#include <cstdint>

// ═══════════════════════════════════════
//  服务器版本 & 语言
// ═══════════════════════════════════════
inline bool g_IsGL = false;        // false=国服(CN), true=国际服(GL)
inline int  g_lang  = 0;           // 0=中文 1=English
#define L(cn, en) (g_lang ? (en) : (cn))

// ═══════════════════════════════════════
//  基础开关 & 快捷键
// ═══════════════════════════════════════
inline bool  g_ShowRays       = true;
inline bool  g_HideUI         = false;
inline int   g_HotkeyVK       = VK_F5;     // 隐藏菜单快捷键 (可自定义)
inline bool  g_ForceShowUI    = true;      // 启动时强制显示菜单
inline int   g_ItemsHotkeyVK  = VK_F9;     // 物品显示快捷键
inline int   g_BindingHotkey  = 0;         // 0=无 1=绑定菜单键 2=绑定物品键
inline int   g_OverlayHotkeyVK = VK_F6;    // 黑窗快捷键

// ═══════════════════════════════════════
//  物资遍历
// ═══════════════════════════════════════
inline bool  g_ScanItems          = true;
inline bool  g_ShowContainerItems = true;  // 显示容器里面的物质
inline bool  g_ShowNearbyItems    = true;  // 显示周围的物质
inline int   g_NearbyItemsDist    = 150;   // 周围物质距离 (米)

// ═══════════════════════════════════════
//  ESP 配置
// ═══════════════════════════════════════
inline int   g_MaxDist        = 300;
inline bool  g_DrawAll        = false;
inline bool  g_ShowBox        = true;
inline bool  g_DrawSelf       = false;
inline bool  g_DrawTeammate   = false;
inline bool  g_ShowSkeleton   = true;
inline bool  g_ShowWeapon     = true;
inline bool  g_ShowTeamId     = true;
inline bool  g_CheckTeam      = false;
inline bool  g_ShowName       = false;
inline bool  g_ShowDistance   = true;
inline bool  g_ShowHealth     = true;      // 血量显示
inline bool  g_ShowArmor      = true;      // 真人护甲耐久
inline bool  g_ShowAmmo       = true;      // 武器弹药数

// ── 人机 ──
inline bool  g_DrawAI         = true;
inline bool  g_ShowAISkeleton = false;
inline int   g_AIMaxDist      = 200;

// ── 稀有度 (索引 1-6 对应稀有度 1-6; 索引 0 未使用; 稀有度 7 并入 6) ──
inline bool  g_ShowRarity[7]  = { false, true, true, true, true, true, true };

// ═══════════════════════════════════════
//  雷达系统
// ═══════════════════════════════════════
inline bool  g_ShowRadar      = false;
inline int   g_RadarRange     = 200;
inline int   g_RadarPos       = 1;     // 0=左上 1=右上 2=左下 3=右下
inline bool  g_RadarTrail     = true;
inline float g_RadarAlpha     = 0.8f;
inline bool  g_MarkExtraction = false;

// ═══════════════════════════════════════
//  ESP 颜色
// ═══════════════════════════════════════
inline float g_colBox[4]      = { 1.0f, 1.0f, 1.0f, 1.0f };
inline float g_colAI[4]       = { 1.0f, 0.8f, 0.2f, 1.0f };
inline float g_colSkeleton[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
inline float g_colRay[4]      = { 1.0f, 1.0f, 1.0f, 1.0f };
inline float g_colText[4]     = { 1.0f, 1.0f, 1.0f, 1.0f };

// ═══════════════════════════════════════
//  本地玩家 (运行时填充)
// ═══════════════════════════════════════
inline int     g_LocalTeamId = 0;
inline DWORD64 g_LocalPawn   = 0;

// ═══════════════════════════════════════
//  帧率限制
//  ★优化: 默认开启 144FPS 限帧, 避免渲染线程 100% 空转抢占 DMA 工作线程
// ═══════════════════════════════════════
inline bool  g_FpsLimitEnabled = true;
inline int   g_FpsLimit        = 144;

// ═══════════════════════════════════════
//  低配专属优化方案
//  目标: i3 4代级别机器保持绘制流畅，开启后固定 120FPS
// ═══════════════════════════════════════
inline bool  g_LowSpecMode         = false;
inline int   g_LowSpecFPS          = 120;  // 低配模式固定帧率

inline int LowSpecFixedFPS() {
    return 120;
}

inline void ApplyLowSpecProfile() {
    if (!g_LowSpecMode) return;
    g_LowSpecFPS = LowSpecFixedFPS();
    g_FpsLimitEnabled = true;
    g_FpsLimit = LowSpecFixedFPS();
}

// ═══════════════════════════════════════
//  战斗辅助系统
// ═══════════════════════════════════════
inline bool  g_AimWarning      = false;
inline float g_AimWarnAngle    = 15.f;
inline int   g_AimWarnDist     = 200;
inline bool  g_ShowViewCone    = false;
inline float g_ViewConeAngle   = 30.f;
inline int   g_ViewConeRange   = 15;
inline bool  g_ShowDistRing    = false;
inline bool  g_ShowThreatPanel = false;
inline bool  g_ShowMoveArrow   = false;
inline bool  g_ShowThrowable   = false;
inline bool  g_ShowAimingFOV   = false;
inline bool  g_ShowDamageDir   = false;
inline bool  g_ShowHealing     = false;
inline bool  g_ShowItemPrice   = true;

// ═══════════════════════════════════════
//  辅助函数
// ═══════════════════════════════════════

// 相机线程: 永不 sleep, 最大频率更新
inline int SleepCamera() { return 0; }

// 虚拟键名查询 (中英双语)
inline const char* VKName(int vk) {
    if (vk >= VK_F1 && vk <= VK_F12) {
        static char b[4]; snprintf(b, 4, "F%d", vk - VK_F1 + 1); return b;
    }
    switch (vk) {
        case VK_LBUTTON: return u8"鼠标左键"; case VK_RBUTTON: return u8"鼠标右键";
        case VK_MBUTTON: return u8"鼠标中键"; case VK_XBUTTON1: return u8"侧键1";
        case VK_XBUTTON2: return u8"侧键2"; case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt"; case VK_SHIFT: return "Shift";
        case VK_TAB: return "Tab"; case VK_SPACE: return u8"空格";
        case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete";
        case VK_HOME: return "Home"; case VK_END: return "End";
        case VK_PRIOR: return "PgUp"; case VK_NEXT: return "PgDn";
        default: {
            static char k[2];
            k[0] = (char)MapVirtualKeyA(vk, MAPVK_VK_TO_CHAR);
            k[1] = 0;
            return (k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9') ? k : "...";
        }
    }
}

// ═══════════════════════════════════════
//  配置保存/加载 (实现在 Config.cpp)
// ═══════════════════════════════════════
void SaveGlobalConfig(const char* fn = "global.cfg");
void LoadGlobalConfig(const char* fn = "global.cfg");
