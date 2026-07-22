#include <cstdio>          // printf / getchar / fflush / stdin
#include <cstdlib>         // exit
#include <exception>
#include <csignal>
#include <windows.h>     // dbghelp.h 依赖 Windows 类型 (HANDLE/DWORD/ULONG 等)
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

// 前向声明 main.h 中的全局变量 (崩溃处理器需要用到)
extern int g_lang;

// ═══════════════════════════════════════
//  全局崩溃捕获 — 任何异常都显示错误信息并等待回车退出
// ═══════════════════════════════════════
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    // 重新显示控制台
    ShowWindow(GetConsoleWindow(), SW_SHOW);
    if (g_lang) {
        printf("\n\n========== CRASH ==========\n");
        printf("Exception Code: 0x%08X\n", ep ? ep->ExceptionRecord->ExceptionCode : 0);
        printf("Exception Addr: 0x%016llX\n", ep ? (unsigned long long)ep->ExceptionRecord->ExceptionAddress : 0);
        if (ep && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
            printf("Access Violation (read/write invalid memory)\n");
            if (ep->ExceptionRecord->NumberParameters >= 2)
                printf("  Type: %s  Addr: 0x%016llX\n",
                    ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" :
                    ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE" : "EXEC",
                    (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        }
        printf("===========================\n");
        printf("Press ENTER to exit...\n");
    } else {
        printf("\n\n========== 崩溃信息 ==========\n");
        printf("异常代码: 0x%08X\n", ep ? ep->ExceptionRecord->ExceptionCode : 0);
        printf("异常地址: 0x%016llX\n", ep ? (unsigned long long)ep->ExceptionRecord->ExceptionAddress : 0);
        if (ep && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
            printf("内存访问违规 (读/写非法地址)\n");
            if (ep->ExceptionRecord->NumberParameters >= 2)
                printf("  类型: %s  地址: 0x%016llX\n",
                    ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "读取" :
                    ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "写入" : "执行",
                    (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        }
        printf("==============================\n");
        printf("按回车键退出...\n");
    }
    // 清空输入缓冲区, 等待用户按回车
    fflush(stdin);
    while (getchar() != '\n') {}
    exit(1);
    return EXCEPTION_EXECUTE_HANDLER;
}

void TerminateHandler() {
    ShowWindow(GetConsoleWindow(), SW_SHOW);
    if (g_lang) {
        printf("\n\n========== UNHANDLED EXCEPTION ==========\n");
        printf("std::terminate was called (uncaught exception)\n");
        printf("=========================================\n");
        printf("Press ENTER to exit...\n");
    } else {
        printf("\n\n========== 未捕获异常 ==========\n");
        printf("程序抛出了未捕获的异常 (std::terminate)\n");
        printf("================================\n");
        printf("按回车键退出...\n");
    }
    fflush(stdin);
    while (getchar() != '\n') {}
    exit(1);
}

// 信号捕获: SIGABRT (assert/abort), SIGINT (Ctrl+C), SIGILL, SIGFPE
void SignalHandler(int sig) {
    ShowWindow(GetConsoleWindow(), SW_SHOW);
    const char* name;
    switch (sig) {
        case SIGABRT: name = g_lang ? "SIGABRT (abort/assert)" : "SIGABRT (异常终止/断言失败)"; break;
        case SIGINT:  name = g_lang ? "SIGINT (Ctrl+C)"        : "SIGINT (Ctrl+C 中断)";        break;
        case SIGILL:  name = g_lang ? "SIGILL (illegal instr)" : "SIGILL (非法指令)";           break;
        case SIGFPE:  name = g_lang ? "SIGFPE (float err)"     : "SIGFPE (浮点异常)";           break;
        default:      name = g_lang ? "Unknown signal"         : "未知信号";                    break;
    }
    if (g_lang) {
        printf("\n\n========== SIGNAL ==========\n");
        printf("Signal: %s (%d)\n", name, sig);
        printf("============================\n");
        printf("Press ENTER to exit...\n");
    } else {
        printf("\n\n========== 信号中断 ==========\n");
        printf("信号: %s (%d)\n", name, sig);
        printf("==============================\n");
        printf("按回车键退出...\n");
    }
    fflush(stdin);
    while (getchar() != '\n') {}
    exit(1);
}

#include "Security.h"
#include "Mem.h"
#include "Overlay.h"
#include "main.h"
#include "HoloSplash.h"

// Offset.h extern 实际定义
uint64_t NameKey   = 0;
uint64_t BaseName  = 0;
uint64_t BaseWorld = 0;
// g_IsGL 已在 main.h 中定义为 inline bool, 此处不再重复定义

// 服务器配置 (Security 模块)
static const char* SERVER_HOST = "192.144.142.27";
static const int   SERVER_PORT = 80;
// 通过环境变量注入，避免将密钥写入源码和版本历史。
static const char* API_KEY     = std::getenv("DMA_ANQU_API_KEY");

// ═══════════════════════════════════════
//  UI 菜单（全息科幻风格 — 双标签页）
// ═══════════════════════════════════════

// 单个分类的 Checkbox 网格渲染 (使用 HoloCheckbox)
static void RenderCategoryCheckboxes(const ItemCategory& cat, int cols = 8) {
    auto& filter = ItemFilterTable::Get();
    int col = 0;
    for (size_t i = 0; i < cat.items.size(); i++) {
        if (col > 0) ImGui::SameLine();
        const char* displayName = GetItemDisplayName(cat.items[i]);
        bool enabled = filter.IsEnabled(cat.items[i]);
        int idHash = (int)(std::hash<std::string>{}(cat.items[i]) & 0x7FFF);
        ImGui::PushID(idHash);
        if (HoloCheckbox(displayName, &enabled)) {
            filter.SetEnabled(cat.items[i], enabled);
        }
        ImGui::PopID();
        if (++col >= cols) col = 0;
    }
}

static void DrawUI() {
    if (g_HideUI) return;

    static bool first = true;
    static float uiW = 1480.0f, uiH = 1000.0f;
    // ── 拖拽状态 ──
    static bool  s_Dragging = false;
    static ImVec2 s_DragOffset(0, 0);
    if (first) {
        // 根据屏幕尺寸自适应，确保不超过屏幕 92%
        RECT r = GetMonitorRect(g_CurMonitor >= 0 ? g_CurMonitor : 0);
        float maxW = (float)(r.right - r.left) * 0.92f;
        float maxH = (float)(r.bottom - r.top) * 0.92f;
        uiW = 1480.0f; uiH = 1000.0f;
        if (uiW > maxW) uiW = maxW;
        if (uiH > maxH) uiH = maxH;
        // 窗口居中
        float cx = (r.right - r.left) * 0.5f - uiW * 0.5f;
        float cy = (r.bottom - r.top) * 0.5f - uiH * 0.5f;
        if (cx < 0) cx = 20; if (cy < 0) cy = 20;
        ImGui::SetNextWindowPos(ImVec2(cx, cy));
        ImGui::SetNextWindowSize(ImVec2(uiW, uiH));
        first = false;
    }

    // ── 窗口样式: 无标题栏, 透明背景, 自定义绘制 ──
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    ImGui::Begin(L(u8"VEX-暗区-DMA", "DaDaDa DMA"), nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse);

    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    auto* dl = ImGui::GetWindowDrawList();

    // ══════════════════════════════════════
    //  全息背景层 (替代 bg.png)
    // ══════════════════════════════════════
    HoloWindowBackground(dl, winPos, winSize, 40.0f);

    // 外框发光边框 + 角标
    ImU32 frameCol = Holo::ToU32(Holo::CYAN, 0.6f);
    HoloGlowRect(dl, winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                 frameCol, 4.0f, 8.0f);
    HoloCornerBrackets(dl, winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                       18.0f, Holo::ToU32(Holo::CYAN, 0.9f), 3.0f);

    // ══════════════════════════════════════
    //  顶部全息光条导航栏 (可拖拽移动窗口)
    // ══════════════════════════════════════
    float titleBarH = 40.0f;
    HoloTitleBar(L(u8"VEX-DMA HOLOGRAPHIC INTERFACE", "VEX-DMA HOLOGRAPHIC INTERFACE"),
                 winPos, ImVec2(winSize.x, titleBarH));

    // ── 标题栏拖拽: 鼠标在标题栏区域按下时拖动整个窗口 ──
    {
        ImVec2 titleP1(winPos.x, winPos.y);
        ImVec2 titleP2(winPos.x + winSize.x, winPos.y + titleBarH);
        bool hoverTitle = ImGui::IsMouseHoveringRect(titleP1, titleP2, false);

        // 鼠标在标题栏且未悬停任何控件时，按下左键开始拖拽
        if (hoverTitle && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
            s_Dragging = true;
            ImVec2 mp = ImGui::GetIO().MousePos;
            s_DragOffset = ImVec2(mp.x - winPos.x, mp.y - winPos.y);
        }
        // 拖拽中：跟随鼠标移动窗口
        if (s_Dragging) {
            if (ImGui::IsMouseDown(0)) {
                ImVec2 mp = ImGui::GetIO().MousePos;
                ImVec2 newPos(mp.x - s_DragOffset.x, mp.y - s_DragOffset.y);
                // 边界限制：至少保留 120px 在屏幕内
                RECT mr = GetCachedMonitorRect(g_CurMonitor >= 0 ? g_CurMonitor : 0);
                if (newPos.x < (float)mr.left - winSize.x + 120.0f)
                    newPos.x = (float)mr.left - winSize.x + 120.0f;
                if (newPos.x > (float)mr.right - 120.0f)
                    newPos.x = (float)mr.right - 120.0f;
                if (newPos.y < (float)mr.top)
                    newPos.y = (float)mr.top;
                if (newPos.y > (float)mr.bottom - titleBarH)
                    newPos.y = (float)mr.bottom - titleBarH;
                ImGui::SetWindowPos(newPos);
            } else {
                s_Dragging = false;
            }
        }
        // 悬停时显示手型光标提示可拖拽
        if (hoverTitle && !s_Dragging)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // ══════════════════════════════════════
    //  仪表盘数据区 (4 个状态卡片)
    // ══════════════════════════════════════
    size_t playerCount = 0;
    { std::shared_lock<std::shared_mutex> lk(gs.dataMutex); if (gs.players) playerCount = gs.players->size(); }
    int itemEnabled = ItemFilterTable::Get().TotalEnabled();
    int itemTotal = ItemFilterTable::Get().TotalItems();
    float fps = ImGui::GetIO().Framerate;

    float statCardW = (winSize.x - 40.0f) / 4.0f;
    float statCardH = 50.0f;
    float statY = titleBarH + 8;

    // 辅助 lambda: 绘制状态卡片
    auto DrawStatCard = [&](float x, float y, float w, float h, const char* label,
                            const char* value, const float accent[4]) {
        ImVec2 p1(winPos.x + x, winPos.y + y);
        ImVec2 p2(winPos.x + x + w, winPos.y + y + h);
        ImU32 ac = Holo::ToU32(accent, 0.8f);
        dl->AddRectFilled(p1, p2, Holo::ToU32(Holo::BG_PANEL, 0.75f), 5.0f);
        HoloGlowRect(dl, p1, p2, ac, 2.0f, 5.0f);
        dl->AddRect(p1, p2, Holo::ToU32(Holo::CYAN_DIM, 0.5f), 5.0f, 0, 1.0f);
        dl->AddRectFilled(ImVec2(p1.x, p1.y), ImVec2(p2.x, p1.y + 2), ac, 2.0f);
        dl->AddText(ImVec2(p1.x + 12, p1.y + 6), Holo::ToU32(Holo::TEXT_DIM), label);
        ImU32 valGlow = (ac & 0x00FFFFFF) | (60 << IM_COL32_A_SHIFT);
        dl->AddText(ImVec2(p1.x + 13, p1.y + 23), valGlow, value);  // shadow (右下偏移)
        dl->AddText(ImVec2(p1.x + 12, p1.y + 22), Holo::ToU32(Holo::TEXT_BRIGHT), value);  // main
        // 高帧率时跳过 pulse 动画计算 (节省 CPU)
        float pulseAlpha = (fps > 120.0f) ? 0.7f : HoloAnim::Pulse(2.0f, 0.3f, 1.0f);
        dl->AddCircleFilled(ImVec2(p2.x - 12, p1.y + 10), 3.0f,
                            (ac & 0x00FFFFFF) | ((ImU32)(pulseAlpha * 200) << IM_COL32_A_SHIFT));
    };

    char fpsBuf[32], plyBuf[32], itemBuf[32];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", fps);
    snprintf(plyBuf, sizeof(plyBuf), "%zu", playerCount);
    snprintf(itemBuf, sizeof(itemBuf), "%d/%d", itemEnabled, itemTotal);

    DrawStatCard(10, statY, statCardW, statCardH, L(u8"帧率", "FPS"), fpsBuf, Holo::CYAN);
    DrawStatCard(15 + statCardW, statY, statCardW, statCardH, L(u8"角色", "Players"), plyBuf, Holo::ELECTRIC_BLUE);
    DrawStatCard(20 + statCardW * 2, statY, statCardW, statCardH, L(u8"物品过滤", "Items"), itemBuf, Holo::PURPLE);

    // 第 4 卡: 系统状态 + 雷达扫描
    {
        float x = 25 + statCardW * 3;
        ImVec2 p1(winPos.x + x, winPos.y + statY);
        ImVec2 p2(winPos.x + x + statCardW, winPos.y + statY + statCardH);
        ImU32 ac = Holo::ToU32(Holo::GREEN_NEON, 0.7f);
        dl->AddRectFilled(p1, p2, Holo::ToU32(Holo::BG_PANEL, 0.75f), 5.0f);
        HoloGlowRect(dl, p1, p2, ac, 2.0f, 5.0f);
        dl->AddRect(p1, p2, Holo::ToU32(Holo::CYAN_DIM, 0.5f), 5.0f, 0, 1.0f);
        dl->AddRectFilled(ImVec2(p1.x, p1.y), ImVec2(p2.x, p1.y + 2), ac, 2.0f);
        dl->AddText(ImVec2(p1.x + 12, p1.y + 6), Holo::ToU32(Holo::TEXT_DIM), L(u8"系统状态", "Status"));
        dl->AddText(ImVec2(p1.x + 12, p1.y + 22), ac, L(u8"● ONLINE", "● ONLINE"));
        ImVec2 radarCenter(p2.x - 18, p1.y + statCardH * 0.5f + 4);
        // 高帧率时雷达扫描降频 (每 3 帧 1 次，节省 CPU)
        if (fps <= 120.0f || ((int)(ImGui::GetTime() * 60) % 3 == 0))
            HoloRadarSweep(dl, radarCenter, 12.0f, 1.5f, ac);
    }

    // ══════════════════════════════════════
    //  主内容区面板
    // ══════════════════════════════════════
    float contentY = statY + statCardH + 10;
    float contentH = winSize.y - contentY - 10;
    {
        ImVec2 panelP1(winPos.x + 10, winPos.y + contentY);
        ImVec2 panelP2(winPos.x + winSize.x - 10, winPos.y + contentY + contentH);
        HoloPanel(dl, panelP1, panelP2, nullptr, 6.0f, Holo::ToU32(Holo::CYAN, 0.4f));
    }

    ImGui::SetCursorPos(ImVec2(20, contentY + 10));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::BeginChild("MainContent", ImVec2(winSize.x - 40, contentH - 20), false);

    // ── 标签栏 (全息风格) ──────────────────────────────
    HoloPushTabStyle();
    if (ImGui::BeginTabBar("MainTabs")) {

        // ══════════════════════════════════════
        //  标签 1: 玩家 ESP
        // ══════════════════════════════════════
        if (ImGui::BeginTabItem(L(u8"◆ 玩家ESP", "◆ Player ESP"))) {
            ImGui::Spacing();

            HoloHeader(L(u8"视觉选项", "Visual Options"));
            HoloCheckbox(L(u8"绘制全部", "Draw All"),      &g_DrawAll);
            ImGui::SameLine();
            HoloCheckbox(L(u8"显示方框", "Show Box"),      &g_ShowBox);
            ImGui::SameLine();
            HoloCheckbox(L(u8"开启射线", "Show Rays"),     &g_ShowRays);

            HoloCheckbox(L(u8"显示骨骼", "Skeleton"),      &g_ShowSkeleton);
            ImGui::SameLine();
            HoloCheckbox(L(u8"距离透视", "Distance"),      &g_ShowDistance);
            ImGui::SameLine();
            HoloCheckbox(L(u8"绘制自己", "Draw Self"),     &g_DrawSelf);

            HoloCheckbox(L(u8"绘制人机", "Draw AI"),        &g_DrawAI);
            ImGui::SameLine();
            HoloCheckbox(L(u8"人机骨骼", "AI Skeleton"),    &g_ShowAISkeleton);
            ImGui::SameLine();
            HoloSliderInt(L(u8"人机距离", "AI Dist"),       &g_AIMaxDist, 0, 1000);
            ImGui::SameLine();
            HoloCheckbox(L(u8"绘制队友", "Draw Teammate"), &g_DrawTeammate);
            ImGui::SameLine();
            HoloCheckbox(L(u8"队伍检查", "Team Check"),    &g_CheckTeam);
            ImGui::SameLine();
            HoloCheckbox(L(u8"显示队伍ID", "Team ID"),     &g_ShowTeamId);

            HoloCheckbox(L(u8"血量透视", "Health"),        &g_ShowHealth);
            ImGui::SameLine();
            HoloCheckbox(L(u8"显示武器", "Weapon"),         &g_ShowWeapon);
            ImGui::SameLine();
            HoloCheckbox(L(u8"显示名字", "Name"),           &g_ShowName);

            HoloCheckbox(L(u8"显示护甲", "Armor"),          &g_ShowArmor);
            ImGui::SameLine();
            HoloSliderInt(L(u8"绘制距离", "Max Dist"),     &g_MaxDist, 0, 3000);

            HoloSeparator();

            HoloHeader(L(u8"物质扫描", "Item Scanning"));
            HoloCheckbox(L(u8"是否遍历物质", "Scan Items"), &g_ScanItems);
            ImGui::SameLine();
            ImGui::TextColored(Holo::ToVec4(Holo::RED_ALERT, 0.9f),
                L(u8"⚠ 开启物质遍历会大幅消耗CPU性能",
                  "⚠ Enabling item scan heavily consumes CPU"));

            if (g_ScanItems) {
                ImGui::Indent(20);
                HoloCheckbox(L(u8"显示容器里面的物质", "Container Items"),   &g_ShowContainerItems);
                ImGui::SameLine();
                HoloCheckbox(L(u8"显示周围的物质", "Nearby Items"),          &g_ShowNearbyItems);
                if (g_ShowNearbyItems)
                    HoloSliderInt(L(u8"周围物质距离", "Nearby Dist"),        &g_NearbyItemsDist, 5, 500);
                HoloCheckbox(L(u8"显示物品价值", "Item Price"),              &g_ShowItemPrice);

                if (g_ShowItemPrice) {
                    ImGui::Indent(20);
                    static const ImVec4 kRarityColors[7] = {
                        ImVec4(1,1,1,1),
                        ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                        ImVec4(150/255.f, 220/255.f, 1.0f, 1.0f),
                        ImVec4(80/255.f, 130/255.f, 1.0f, 1.0f),
                        ImVec4(180/255.f, 80/255.f, 1.0f, 1.0f),
                        ImVec4(1.0f, 215/255.f, 0.0f, 1.0f),
                        ImVec4(1.0f, 60/255.f, 60/255.f, 1.0f),
                    };
                    static const char* const kRarityLabels[7] = {
                        "", u8"1 白色", u8"2 浅蓝", u8"3 蓝色",
                        u8"4 紫色", u8"5 金色", u8"6 红色",
                    };
                    ImGui::TextColored(Holo::ToVec4(Holo::TEXT_DIM), L(u8"稀有度显示:","Rarity shown:"));
                    for (int r = 1; r <= 6; r++) {
                        if (r > 1) ImGui::SameLine();
                        ImU32 rareCol = IM_COL32(
                            (int)(kRarityColors[r].x*255),
                            (int)(kRarityColors[r].y*255),
                            (int)(kRarityColors[r].z*255), 200);
                        ImGui::PushStyleColor(ImGuiCol_Text, kRarityColors[r]);
                        HoloCheckbox(kRarityLabels[r], &g_ShowRarity[r], rareCol);
                        ImGui::PopStyleColor();
                    }
                    ImGui::Unindent(20);
                }
                ImGui::Unindent(20);
            }

            HoloSeparator();

            HoloHeader(L(u8"性能与热键", "Performance & Hotkeys"));

            // ★新增: 服务器/显示模式 UI 切换
            HoloHeader(L(u8"服务器与显示", "Server & Display"));
            {
                static int s_serverChoice = g_IsGL ? 1 : 0;
                static int s_oldServer = s_serverChoice;
                ImGui::RadioButton(L(u8"国服", "CN"), &s_serverChoice, 0); ImGui::SameLine();
                ImGui::RadioButton(L(u8"国际服", "GL"), &s_serverChoice, 1);
                if (s_serverChoice != s_oldServer) {
                    s_oldServer = s_serverChoice;
                    if (s_serverChoice == 1) {
                        BaseWorld = BaseWorld_GL; BaseName = BaseName_GL; NameKey = NameKey_GL;
                        g_IsGL = true; ACE_CacheTable_RVA = ACE_CacheTable_RVA_GL;
                    } else {
                        BaseWorld = BaseWorld_CN; BaseName = BaseName_CN; NameKey = NameKey_CN;
                        g_IsGL = false; ACE_CacheTable_RVA = ACE_CacheTable_RVA_CN;
                    }
                    printf(L("[UI] 服务器已热切换: %s\n", "[UI] Server hot-switched: %s\n"),
                           s_serverChoice == 1 ? L("国际服", "GL") : L("国服", "CN"));
                }
                ImGui::TextColored(Holo::ToVec4(Holo::GREEN_NEON, 0.8f),
                    L(u8"当前: %s  (已热切换)", "Current: %s  (hot-switched)"),
                    g_IsGL ? L("国际服", "GL") : L("国服", "CN"));
            }
            {
                static int s_displayMode = 1;  // 默认复制模式
                static int s_oldMode = s_displayMode;
                ImGui::RadioButton(L(u8"扩展屏幕", "Extended"), &s_displayMode, 0); ImGui::SameLine();
                ImGui::RadioButton(L(u8"复制屏幕", "Clone"), &s_displayMode, 1);
                if (s_displayMode != s_oldMode) {
                    s_oldMode = s_displayMode;
                    g_DisplayMode = s_displayMode;
                    if (s_displayMode == 0) {
                        // 扩展模式: 移到副屏
                        MoveToMonitor(1);
                    } else {
                        // 复制模式: 移到主屏
                        MoveToMonitor(0);
                    }
                }
            }

            HoloSeparator();
            HoloHeader(L(u8"帧率优化", "FPS Optimization"));
            if (HoloCheckbox(L(u8"低配专属优化方案", "Low Spec Optimization"), &g_LowSpecMode)) {
                if (g_LowSpecMode) {
                    ApplyLowSpecProfile();
                } else {
                    // 关闭低配后恢复普通稳定帧同步，避免 Overlay 3000FPS 空转抢占数据线程。
                    g_FpsLimitEnabled = true;
                    g_FpsLimit = 144;
                }
            }
            if (g_LowSpecMode) {
                ApplyLowSpecProfile();
                ImGui::SameLine();
                ImGui::TextColored(Holo::ToVec4(Holo::CYAN, 0.9f),
                    L(u8"已启用：固定120FPS + 平滑帧同步", "Enabled: fixed 120FPS + smooth frame pacing"));
                ImGui::TextColored(Holo::ToVec4(Holo::TEXT_DIM),
                    L(u8"不限制真人/AI/物资绘制，不限制物资读取，避免目标闪烁和跳帧",
                      "No player/AI/item draw cap and no item read cap to avoid flicker/stutter"));
            } else {
                HoloCheckbox(L(u8"帧率限制", "FPS Limit"), &g_FpsLimitEnabled);
            }
            if (!g_LowSpecMode && g_FpsLimitEnabled)
                HoloSliderInt(L(u8"帧率上限", "Max FPS"), &g_FpsLimit, 30, 200);

            HoloSeparator();

            HoloHeader(L(u8"颜色配置", "Color Configuration"));
            HoloColorEdit3(L(u8"方框颜色", "Box Color"),   g_colBox);
            ImGui::SameLine();
            HoloColorEdit3(L(u8"人机颜色", "AI Color"),    g_colAI);
            ImGui::SameLine();
            HoloColorEdit3(L(u8"骨骼颜色", "Bone Color"),  g_colSkeleton);

            HoloColorEdit3(L(u8"射线颜色", "Ray Color"),   g_colRay);
            ImGui::SameLine();
            HoloColorEdit3(L(u8"文字颜色", "Text Color"),  g_colText);

            HoloSeparator();

            HoloHeader(L(u8"快捷键与操作", "Hotkeys & Actions"));
            {
                if (g_DMAKey.IsReady()) {
                    ImGui::TextColored(Holo::ToVec4(Holo::GREEN_NEON, 0.9f),
                        L(u8"● 副机键盘: 已连接 (DMA+本机双检测)", "● DMA Key: Connected (dual detect)"));
                } else {
                    ImGui::TextColored(Holo::ToVec4(Holo::RED_ALERT, 0.9f),
                        L(u8"⚠ 副机键盘: 未连接 (仅本机检测)", "⚠ DMA Key: Disconnected (local only)"));
                }
            }
            {
                char label[64];
                snprintf(label, sizeof(label), "%s: [%s]%s",
                    L(u8"隐藏快捷键","Hide Key"), VKName(g_HotkeyVK),
                    g_BindingHotkey==1 ? L(u8" <- 请按键..."," <- press key...") : "");
                if (HoloButton(label, ImVec2(280, 30))) g_BindingHotkey = 1;
            }
            {
                char label[64];
                snprintf(label, sizeof(label), "%s: [%s]%s",
                    L(u8"物品快捷键","Item Key"), VKName(g_ItemsHotkeyVK),
                    g_BindingHotkey==2 ? L(u8" <- 请按键..."," <- press key...") : "");
                if (HoloButton(label, ImVec2(280, 30))) g_BindingHotkey = 2;
            }
            {
                char label[64];
                snprintf(label, sizeof(label), "%s: [%s]%s",
                    L(u8"黑窗开关","Overlay Key"), VKName(g_OverlayHotkeyVK),
                    g_BindingHotkey==3 ? L(u8" <- 请按键..."," <- press key...") : "");
                if (HoloButton(label, ImVec2(280, 30))) g_BindingHotkey = 3;
            }

            ImGui::Spacing();
            if (HoloButton(L(u8"安全退出", "Exit"), ImVec2(110, 32),
                           Holo::ToU32(Holo::RED_ALERT, 0.8f)))
                WaitEnterAndExit(0);
            ImGui::SameLine();
            if (HoloButton(L(u8"保存配置", "Save Cfg"), ImVec2(110, 32),
                           Holo::ToU32(Holo::GREEN_NEON, 0.8f)))
                SaveGlobalConfig();
            ImGui::SameLine();
            if (HoloButton(L(u8"加载配置", "Load Cfg"), ImVec2(110, 32),
                           Holo::ToU32(Holo::ELECTRIC_BLUE, 0.8f)))
                LoadGlobalConfig();

            ImGui::EndTabItem();
        }

        // ══════════════════════════════════════
        //  标签 2: 战斗辅助
        // ══════════════════════════════════════
        if (ImGui::BeginTabItem(L(u8"◆ 战斗辅助", "◆ Combat Assist"))) {
            ImGui::Spacing();

            // ── 瞄准预警 ──
            HoloHeader(L(u8"瞄准预警系统", "Aim Warning System"));
            HoloCheckbox(L(u8"瞄准预警", "Aim Warning"),       &g_AimWarning);
            if (g_AimWarning) {
                ImGui::Indent(20);
                HoloCheckbox(L(u8"距离环", "Dist Ring"),         &g_ShowDistRing);
                ImGui::SameLine();
                { int sA = (int)g_AimWarnAngle;
                  if (HoloSliderInt(L(u8"预警角度", "Warn Angle"), &sA, 5, 60)) g_AimWarnAngle = (float)sA; }
                HoloSliderInt(L(u8"预警距离", "Warn Dist(m)"),   &g_AimWarnDist, 50, 500);
                ImGui::Unindent(20);
            }

            HoloSeparator();

            // ── 雷达系统 ──
            HoloHeader(L(u8"雷达系统", "Radar System"));
            HoloCheckbox(L(u8"显示雷达", "Show Radar"),          &g_ShowRadar);
            ImGui::SameLine();
            HoloCheckbox(L(u8"撤离点标记", "Extraction Mark"),   &g_MarkExtraction);
            if (g_ShowRadar) {
                ImGui::Indent(20);
                HoloCheckbox(L(u8"移动轨迹", "Trail"),            &g_RadarTrail);
                ImGui::SameLine();
                HoloSliderInt(L(u8"雷达范围", "Radar Range(m)"), &g_RadarRange, 50, 500);
                ImGui::SameLine();
                HoloSliderInt(L(u8"雷达位置", "Radar Pos"),      &g_RadarPos, 0, 3);
                ImGui::Unindent(20);
            }

            HoloSeparator();

            // ── 视野与威胁 ──
            HoloHeader(L(u8"视野与威胁", "View & Threat"));
            HoloCheckbox(L(u8"视野锥", "View Cone"),             &g_ShowViewCone);
            ImGui::SameLine();
            HoloCheckbox(L(u8"威胁面板", "Threat Panel"),         &g_ShowThreatPanel);
            ImGui::SameLine();
            HoloCheckbox(L(u8"移动箭头", "Move Arrow"),           &g_ShowMoveArrow);
            if (g_ShowViewCone) {
                ImGui::Indent(20);
                { int sC = (int)g_ViewConeAngle;
                  if (HoloSliderInt(L(u8"锥角度", "Cone Angle"), &sC, 10, 90)) g_ViewConeAngle = (float)sC; }
                HoloSliderInt(L(u8"锥范围", "Cone Range(m)"),    &g_ViewConeRange, 5, 50);
                ImGui::Unindent(20);
            }

            HoloSeparator();

            // ── 扩展功能 ──
            HoloHeader(L(u8"扩展战斗功能", "Extended Combat"));
            HoloCheckbox(L(u8"投掷物", "Throwable"),              &g_ShowThrowable);
            ImGui::SameLine();
            HoloCheckbox(L(u8"开镜FOV", "Aiming FOV"),           &g_ShowAimingFOV);

            HoloCheckbox(L(u8"受伤方向", "Damage Dir"),           &g_ShowDamageDir);
            ImGui::SameLine();
            HoloCheckbox(L(u8"治疗检测", "Healing Detect"),       &g_ShowHealing);

            HoloSeparator();

            // ── 提示信息 ──
            ImGui::TextColored(Holo::ToVec4(Holo::CYAN_DIM, 0.8f),
                L(u8"⚠ 战斗辅助功能默认关闭，按需开启以节省CPU性能",
                  "⚠ Combat features are off by default to save CPU"));
            ImGui::TextColored(Holo::ToVec4(Holo::TEXT_DIM),
                L(u8"已启用专用物资解密线程: 元数据/坐标解密分离，渲染不等待解密",
                  "Dedicated item decrypt thread: metadata/position decrypt separated, render never waits"));

            ImGui::EndTabItem();
        }

        // ══════════════════════════════════════
        //  标签 3: 物品过滤
        // ══════════════════════════════════════
        if (ImGui::BeginTabItem(L(u8"◆ 物品过滤", "◆ Item Filter"))) {
            auto& filter = ItemFilterTable::Get();
            ImGui::Spacing();

            HoloHeader(L(u8"过滤器状态", "Filter Status"));
            ImGui::TextColored(Holo::ToVec4(Holo::GREEN_NEON, 0.9f),
                L(u8"物品过滤 (%d/%d 启用)", "Item Filter (%d/%d enabled)"),
                filter.TotalEnabled(), filter.TotalItems());

            ImGui::SameLine();
            if (HoloSmallButton(L(u8"全勾选", "All On"))) filter.SetAllEnabled(true);
            ImGui::SameLine();
            if (HoloSmallButton(L(u8"全取消", "All Off"))) filter.SetAllUnchecked();
            ImGui::SameLine();
            if (HoloSmallButton(L(u8"保存", "Save"))) filter.SaveConfig();
            ImGui::SameLine();
            if (HoloSmallButton(L(u8"加载", "Load"))) filter.LoadConfig();

            HoloSeparator();

            // 透明 Checkbox 样式 (物品过滤用)
            ImGui::PushStyleColor(ImGuiCol_FrameBg,       ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f,0.15f,0.15f,0.5f));
            ImGui::PushStyleColor(ImGuiCol_CheckMark,      Holo::ToVec4(Holo::GREEN_NEON));

            HoloPushTabStyle();
            if (ImGui::BeginTabBar("ItemCats")) {
                for (size_t ci = 0; ci < kItemCategories.size(); ci++) {
                    auto& cat = kItemCategories[ci];
                    int enabledCnt = filter.CategoryCount(cat);
                    int totalCnt = (int)cat.items.size();
                    char tabLabel[160];
                    snprintf(tabLabel, sizeof(tabLabel), "%s(%d/%d)###cat%zu",
                        cat.categoryName, enabledCnt, totalCnt, ci);

                    if (ImGui::BeginTabItem(tabLabel)) {
                        ImGui::PushID((int)(uintptr_t)cat.categoryName);
                        ImU32 catColor = IM_COL32(
                            (int)(g_CategoryColors[ci][0]*255),
                            (int)(g_CategoryColors[ci][1]*255),
                            (int)(g_CategoryColors[ci][2]*255), 255);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, Holo::ToVec4(Holo::BG_DEEP, 0.70f));
                        ImGui::ColorEdit3("##catcolor", g_CategoryColors[ci], ImGuiColorEditFlags_NoInputs);
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) {
                            HoloGlowRect(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                         catColor, 1.5f, 3.0f);
                        }
                        ImGui::SameLine();
                        if (HoloSmallButton(L(u8"全勾选", "All")))
                            filter.SetCategoryEnabled(cat, true);
                        ImGui::SameLine();
                        if (HoloSmallButton(L(u8"全取消", "None")))
                            filter.SetCategoryEnabled(cat, false);
                        ImGui::PopID();
                        ImGui::Spacing();
                        RenderCategoryCheckboxes(cat, 8);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            HoloPopTabStyle();

            ImGui::PopStyleColor(3);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    HoloPopTabStyle();

    ImGui::EndChild();
    ImGui::PopStyleVar();   // WindowPadding
    ImGui::PopStyleColor(); // ChildBg
    ImGui::End();
    ImGui::PopStyleColor();  // WindowBg
    ImGui::PopStyleVar(3);    // WindowPadding, Rounding, BorderSize
}

// ═══════════════════════════════════════
//  渲染回调
// ═══════════════════════════════════════
static void ThreadDiagLogger() {
    DiagInitFile();
    while (Runtime::IsRunning()) {
        if (g_Perf.ShouldSample()) g_Perf.Sample();

        int playerCount = 0, worldCount = 0, nearbyCount = 0, boneCacheCount = 0, itemReqCount = 0;
        {
            std::shared_lock<std::shared_mutex> lk(gs.dataMutex);
            if (gs.players) playerCount = (int)gs.players->size();
        }
        {
            std::shared_lock<std::shared_mutex> lk(gs.screenMutex);
            if (gs.worldData) worldCount = (int)gs.worldData->size();
        }
        {
            std::shared_lock<std::shared_mutex> lk(gs.nearbyMutex);
            if (gs.nearbyItems) nearbyCount = (int)gs.nearbyItems->size();
        }
        {
            std::shared_lock<std::shared_mutex> lk(gs.boneMutex);
            boneCacheCount = (int)gs.boneCache.size();
        }
        {
            std::shared_lock<std::shared_mutex> lk(gs.itemReqMutex);
            if (gs.itemReq) itemReqCount = (int)gs.itemReq->nearbyActors.size();
        }

        DiagWriteSnapshot(g_Perf.GetFps(), g_Perf.GetCpuPercent(), g_Perf.GetMemoryMB(),
                          playerCount, worldCount, nearbyCount, boneCacheCount,
                          itemReqCount, g_OverlayVisible, g_ScanItems);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    DiagShutdownFile();
}

static void Render() {
    DiagScope diag(kDiagOverlay);
    g_Perf.TickFrame();
    // Security: 被踢下线时自动退出
    if (Security::g_kvKicked) {
        printf("\n[Security] 卡密已被踢下线，程序即将退出...\n");
        Runtime::Stop();
        PostQuitMessage(1);
        return;
    }

    // 屏幕尺寸取当前 overlay 所在显示器 (仅窗口可见时更新)
    // 优化: 使用缓存版 GetMonitorRect, 避免每帧调用 EnumDisplayDevicesW
    if (g_OverlayVisible) {
        RECT r = GetCachedMonitorRect(g_CurMonitor);
        gs.screenW = r.right - r.left;
        gs.screenH = r.bottom - r.top;
    }

    // 每帧更新 DMA 键盘状态
    g_DMAKey.Update();

    // ★修改: 双键盘检测 — DMA副机 + 主机本机键盘
    auto KeyDown = [](int vk) -> bool {
        return g_DMAKey.Down(vk) || (GetAsyncKeyState(vk) & 0x8000);
    };

    // 黑窗显示/隐藏 (DMA+本机) — 始终切换显示/隐藏 (绑定期间暂停)
    if (g_BindingHotkey != 3) {
        static bool lastF6 = false;
        bool curF6 = KeyDown(g_OverlayHotkeyVK);
        if (curF6 && !lastF6) {
            g_OverlayVisible = !g_OverlayVisible;
            // 使用 SW_SHOWNOACTIVATE 而非 SW_MAXIMIZE, 避免 ImGui 输入失效
            ShowWindow(g_OverlayHwnd, g_OverlayVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
        lastF6 = curF6;
    }

    // 热键绑定: DMA+本机双检测
    if (g_BindingHotkey != 0) {
        for (int vk = 1; vk < 256; vk++) {
            if (vk == g_OverlayHotkeyVK) continue;
            if (KeyDown(vk)) {
                if (g_BindingHotkey == 1) g_HotkeyVK = vk;
                else if (g_BindingHotkey == 2) g_ItemsHotkeyVK = vk;
                else if (g_BindingHotkey == 3) g_OverlayHotkeyVK = vk;
                g_BindingHotkey = 0;
                break;
            }
        }
    }

    // 隐藏/显示 UI (DMA+本机)
    if (g_BindingHotkey != 1) {
        static bool last = false;
        bool cur = KeyDown(g_HotkeyVK);
        if (cur && !last) { g_HideUI = !g_HideUI; ClickThrough(g_HideUI); }
        last = cur;
    }

    // 切换周围物质 (DMA+本机)
    if (g_BindingHotkey != 2) {
        static bool last = false;
        bool cur = KeyDown(g_ItemsHotkeyVK);
        if (cur && !last) g_ShowNearbyItems = !g_ShowNearbyItems;
        last = cur;
    }

    // Overlay 隐藏时仅保留热键轮询，不再执行 UI/ESP/物资绘制。
    if (!g_OverlayVisible) {
        return;
    }

    // ── 开场动画 ──
    // 首帧初始化 splash
    static bool splashInit = false;
    if (!splashInit) {
        RECT mr = GetCachedMonitorRect(g_CurMonitor);
        HoloSplash::Init((float)(mr.right - mr.left), (float)(mr.bottom - mr.top));
        splashInit = true;
    }

    HoloSplash::Update();

    if (HoloSplash::IsActive()) {
        // 开场动画期间只渲染 splash，跳过正常 UI
        RECT mr = GetCachedMonitorRect(g_CurMonitor);
        HoloSplash::Render((float)(mr.right - mr.left), (float)(mr.bottom - mr.top));
        DiagSetCounts(kDiagOverlay, 0, 1);
    } else {
        // 正常 UI 渲染
        DrawUI();
        DrawESP();
        DrawNearbyItems();
        int worldCount = 0, nearbyCount = 0;
        {
            std::shared_lock<std::shared_mutex> lk(gs.screenMutex);
            if (gs.worldData) worldCount = (int)gs.worldData->size();
        }
        {
            std::shared_lock<std::shared_mutex> lk(gs.nearbyMutex);
            if (gs.nearbyItems) nearbyCount = (int)gs.nearbyItems->size();
        }
        DiagSetCounts(kDiagOverlay, worldCount, nearbyCount);
    }
}

// ═══════════════════════════════════════
//  主入口
// ═══════════════════════════════════════
int main() {
    using namespace Security;

    // 设置控制台代码页为 UTF-8, 配合 /utf-8 编译选项,
    // 使源文件中的 UTF-8 中文字符串在控制台正确显示 (不依赖系统默认 GBK 代码页)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 注册崩溃捕获: SEH 异常 + C++ 未捕获异常 + 信号
    SetUnhandledExceptionFilter(CrashHandler);
    std::set_terminate(TerminateHandler);
    signal(SIGABRT, SignalHandler);
    signal(SIGINT,  SignalHandler);
    signal(SIGILL,  SignalHandler);
    signal(SIGFPE,  SignalHandler);

    // 1. 初始化安全模块 (反作弊检测 + 异常注册)
 /*   if (!Init(SERVER_HOST, SERVER_PORT, API_KEY)) {
        printf("\n安全模块初始化失败，程序即将退出...\n");
        WaitEnterAndExit(1);
    }*/

    // 2. 卡密验证
    //printf(L("\n请输入卡密: ", "\nEnter card key: "));
    //char cardKey[256] = { 0 };
    //fgets(cardKey, sizeof(cardKey), stdin);
    //// 去除末尾换行
    //size_t klen = strlen(cardKey);
    //while (klen > 0 && (cardKey[klen-1] == '\n' || cardKey[klen-1] == '\r')) cardKey[--klen] = 0;
    //if (!VerifyCard(cardKey)) {
    //    printf("\n卡密验证失败，程序即将退出...\n");
    //    Cleanup();
    //    WaitEnterAndExit(1);
    //}

    // ★修改: 去掉控制台交互, 使用默认值 (中文/国际服/复制模式)
    g_lang = 0;  // 默认中文
    printf("语言: 中文\n");

    // 默认国际服
    BaseWorld = BaseWorld_GL; BaseName = BaseName_GL; NameKey = NameKey_GL;
    g_IsGL = true; ACE_CacheTable_RVA = ACE_CacheTable_RVA_GL;
    printf("服务器: 国际服 (ACE RVA: 0x%llX)\n", (unsigned long long)ACE_CacheTable_RVA);

    // 默认复制模式
    int mode = 1;
    printf("显示模式: 复制屏幕\n\n");

    // 4. DMA 初始化
    if (!mem.Init("UAGame.exe")) { printf(L("DMA 初始化失败!\n", "DMA init failed!\n")); Cleanup(); WaitEnterAndExit(1); }
    printf(L("已找到进程 PID: %d  模块基址: 0x%llX\n", "Found PID: %d  Base: 0x%llX\n"), mem.pid, mem.base);
    if (g_DMAKey.Init()) {
        printf("[DMAKey] 内核键盘初始化成功 (副机键盘检测已启用)\n");
    } else {
        printf("[DMAKey] ⚠ 内核键盘初始化失败! 仅检测本机键盘\n");
        printf("[DMAKey] 可能原因: win32kbase.sys 未找到 gafAsyncKeyState 导出, 或 DMA 无内核内存读取权限\n");
    }

    // 4.5 初始化性能监控 (仅一次, 避免 DrawESP 热路径每帧重置基线)
    g_Perf.Initialize(GetCurrentProcess());

    // 5. 启动心跳保活
    StartHeartbeat();

    // 6. 容器预分配，避免运行时 rehash
    ReserveContainers();

    // 7. 启动游戏线程：对齐调试版 ThreadItems + ThreadEncItems + ThreadBoneDMA 顺序
    std::thread t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10;
    std::thread tDiag;
    Runtime::Start();
    GameStart(t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10);
    tDiag = std::thread(ThreadDiagLogger);

    // 8. 进入 Overlay 渲染
    OverlayRun(Render, mode);

    // 9. 安全退出
    Runtime::Stop();
    StopHeartbeat();

    JoinIfJoinable(t0); JoinIfJoinable(t1); JoinIfJoinable(t2); JoinIfJoinable(t3); JoinIfJoinable(t4);
    JoinIfJoinable(t5); JoinIfJoinable(t6); JoinIfJoinable(t7); JoinIfJoinable(t8); JoinIfJoinable(t9); JoinIfJoinable(t10);
    JoinIfJoinable(tDiag);

    Cleanup();

    // 正常退出也等待回车 (方便看最后的日志)
    WaitEnterAndExit(0);
}
