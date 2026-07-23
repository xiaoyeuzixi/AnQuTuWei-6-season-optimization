#pragma once
/*
 * core/ESPDraw.h — ESP 绘制模块
 *
 * 从 main.h 抽离的所有绘制相关函数:
 *   - DrawESP: 玩家/AI ESP 主绘制 (方框、骨骼、名字、武器、距离等)
 *   - DrawRadar: 2D 雷达绘制 (玩家点、轨迹、撤离点)
 *   - DrawNearbyItems: 附近物品 ESP 绘制
 *
 * 采用 inline header-only 模式, 依赖:
 *   - ImGui: 绘制 API
 *   - Math.h: CameraData, AnQuWorldToScreen
 *   - GameState.h: gs, WorldEntry, NearbyEntry, RadarTrailEntry, ExtractionPoint
 *   - Config.h: 所有 g_ 配置变量
 *   - ESPUtils.h: StrokeText, CalcTextWidth
 *   - ItemMap.h: kItemNameMap
 */

#include "../ImGui/imgui.h"
#include "Math.h"
#include "GameState.h"
#include "Config.h"
#include "PerfMonitor.h"
#include "DiagLog.h"
#include "../ESPUtils.h"
#include "../ItemMap.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <windows.h>

extern PerfMonitor g_Perf;

// ═══════════════════════════════════════
//  ESP 绘制 — 渲染线程实时 W2S (参考 PUBG_DMA 架构)
//  零投影延迟: 读最新相机 → W2S → 绘制, 全部在同一帧内完成
//  优化:
//    1. 先用世界坐标计算距离，提前过滤远处目标
//    2. 减少冗余 W2S 调用（避免对不可见目标浪费投影计算）
// ═══════════════════════════════════════

inline void DrawRadar(ImDrawList* dl, const CameraData& cam, int sw, int sh);

inline bool DrawSkeletonLines(ImDrawList* dl, const FVector* worldBones,
                              const CameraData& cam, int sw, int sh,
                              ImU32 color, float thickness) {
    if (!dl || !worldBones) return false;
    // ★优化: 14 个骨骼共享同一相机矩阵, 内联 W2S 避免重复函数调用和 tanf
    const FMatrix& m = GetCachedRotationMatrix(cam.camRot);
    FVector AX(m.M[0][0], m.M[0][1], m.M[0][2]);
    FVector AY(m.M[1][0], m.M[1][1], m.M[1][2]);
    FVector AZ(m.M[2][0], m.M[2][1], m.M[2][2]);

    // 缓存 scale (整个函数只算 1 次 tanf)
    static float s_fov = -1.0f, s_scale = 0.0f;
    static int s_sw = 0;
    if (s_fov != cam.camFov || s_sw != sw) {
        s_fov = cam.camFov; s_sw = sw;
        s_scale = (sw / 2.f) / tanf(cam.camFov * 3.1415926535897932f / 360.f);
    }
    const float cx = sw / 2.f, cy = sh / 2.f;

    FVector2D b[14];
    for (int i = 0; i < 14; i++) {
        FVector DA = worldBones[i] - cam.camLoc;
        float z = DA.Dot(AX);
        if (z < 1.f) { b[i] = {0, 0}; continue; }
        float x = DA.Dot(AY);
        float y = DA.Dot(AZ);
        b[i].X = cx + x * s_scale / z;
        b[i].Y = cy - y * s_scale / z;
    }
    if (b[0].X <= 0 || b[0].Y <= 0) return false;

    static constexpr int kBonePairs[][2] = {
        {0, 1}, {8, 9}, {1, 3}, {1, 2},
        {3, 5}, {2, 4}, {5, 7}, {4, 6},
        {9, 11}, {8, 10}, {11, 13}, {10, 12},
    };
    for (const auto& pair : kBonePairs) {
        dl->AddLine(ImVec2(b[pair[0]].X, b[pair[0]].Y),
                    ImVec2(b[pair[1]].X, b[pair[1]].Y),
                    color, thickness);
    }
    return true;
}

inline void FastItemText(ImDrawList* dl, const char* text, ImVec2 pos,
                         ImU32 shadowColor, ImU32 fillColor) {
    // 物资数量可能非常多，不能对每个物资使用 5 次 AddText 的完整描边。
    // 使用 1 层阴影 + 1 层正文，显著降低顶点生成和 CPU 占用，避免物资绘制卡顿。
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadowColor, text);
    dl->AddText(pos, fillColor, text);
}

struct ItemLabelCacheEntry {
    std::string displayName;
    float       nameWidth = 0.0f;
    bool        skip = false;
};

inline const ItemLabelCacheEntry& ResolveItemLabelCached(const std::string& className) {
    static std::unordered_map<std::string, ItemLabelCacheEntry> s_cache;
    auto found = s_cache.find(className);
    if (found != s_cache.end()) return found->second;

    ItemLabelCacheEntry entry;
    auto itName = kItemNameMap.find(className);
    if (itName != kItemNameMap.end()) {
        entry.displayName = itName->second;
    } else {
        static const char* const prefixes[] = {
            "BP_Item_", "Pickup_", "Loot_", "Dropped_", "GroundItem_",
            "Supply_", "Ammo_", "Weapon_", "Armor_", "Medical_",
            "Item_", "Equipment_", "Attachment_", "Consumable_", "Container_"
        };
        static const size_t prefixLens[] = {8,8,5,9,12,7,5,7,6,8,5,11,11,11,9};

        const char* src = className.c_str();
        size_t srcLen = className.size();
        size_t startOff = 0;
        for (int pi = 0; pi < 15; ++pi) {
            if (srcLen >= prefixLens[pi] && strncmp(src, prefixes[pi], prefixLens[pi]) == 0) {
                startOff = prefixLens[pi];
                break;
            }
        }
        size_t copyLen = srcLen - startOff;
        if (copyLen > 2 && src[startOff + copyLen - 2] == '_')
            copyLen -= 2;
        entry.displayName.assign(src + startOff, copyLen);
        for (char& ch : entry.displayName)
            if (ch == '_') ch = ' ';
    }

    entry.skip = (entry.displayName == u8"口袋" || entry.displayName == u8"空手");
    entry.nameWidth = entry.displayName.empty() ? 0.0f : ImGui::CalcTextSize(entry.displayName.c_str()).x;
    auto inserted = s_cache.emplace(className, std::move(entry));
    return inserted.first->second;
}

inline float CachedItemTextWidth(const std::string& text) {
    if (text.empty()) return 0.0f;
    static std::unordered_map<std::string, float> s_widthCache;
    auto found = s_widthCache.find(text);
    if (found != s_widthCache.end()) return found->second;
    float w = ImGui::CalcTextSize(text.c_str()).x;
    s_widthCache.emplace(text, w);
    return w;
}

inline void DrawESP() {
    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    std::shared_ptr<std::vector<WorldEntry>> data;
    {
    std::shared_lock<std::shared_mutex> lk(gs.screenMutex); data = gs.worldData; }
    CameraData cam;
    { std::shared_lock<std::shared_mutex> lk(gs.camMutex); cam = gs.camera; }
    if (!cam.cameraMgr) return;
    if (!data || data->empty()) return;

    // ★修复: 拷贝 playerInfo 快照后立即释放锁, 不再全程持有 infoMutex
    //   原代码在整个绘制循环中持有 shared_lock, 阻塞 ThreadInfo 写入
    std::unordered_map<DWORD64, PlayerInfo> infoMapCopy;
    {
        std::shared_lock<std::shared_mutex> lk(gs.infoMutex);
        infoMapCopy = gs.playerInfo;
    }
    const auto& infoMap = infoMapCopy;

    const ImU32 kOutline = IM_COL32(0, 0, 0, 255);
    const ImU32 cBox  = IM_COL32((int)(g_colBox[0]*255),(int)(g_colBox[1]*255),(int)(g_colBox[2]*255),255);
    const ImU32 cAI   = IM_COL32((int)(g_colAI[0]*255),(int)(g_colAI[1]*255),(int)(g_colAI[2]*255),255);
    const ImU32 cBone = IM_COL32((int)(g_colSkeleton[0]*255),(int)(g_colSkeleton[1]*255),(int)(g_colSkeleton[2]*255),255);
    const ImU32 cRay  = IM_COL32((int)(g_colRay[0]*255),(int)(g_colRay[1]*255),(int)(g_colRay[2]*255),255);
    const ImU32 cText = IM_COL32((int)(g_colText[0]*255),(int)(g_colText[1]*255),(int)(g_colText[2]*255),255);
    char buf[256];

    const bool checkTeam = g_CheckTeam;
    const int localTeamId = cam.localTeamId;
    const DWORD64 localPawn = g_LocalPawn;
    const int maxDist = g_MaxDist;
    const int sw = gs.screenW, sh = gs.screenH;

    // AI调试日志节流: 每~1s输出一次 (@60fps)
    static int aiLogFrame = 0;
    bool logAiThisFrame = (++aiLogFrame >= 60);
    if (logAiThisFrame) aiLogFrame = 0;

    // 相机诊断日志 (每~1s)
    if (logAiThisFrame) {
        AiDebugLog("[CAM] camLoc=(%.0f,%.0f,%.0f) camRot=(%.1f,%.1f,%.1f) fov=%.1f localPos=(%.0f,%.0f,%.0f) team=%d sw=%d sh=%d",
                   cam.camLoc.X, cam.camLoc.Y, cam.camLoc.Z,
                   cam.camRot.X, cam.camRot.Y, cam.camRot.Z, cam.camFov,
                   cam.localPos.X, cam.localPos.Y, cam.localPos.Z, cam.localTeamId, sw, sh);
    }

    for (auto& we : *data) {
        if (logAiThisFrame) {
            if (we.isAI) {
                AiDebugLog("[DRAW] AI in data: pawn=%llx team=%d mesh=%llx pos=(%.0f,%.0f,%.0f)",
                           (unsigned long long)we.pawn, we.teamId, (unsigned long long)we.mesh,
                           we.worldBot.X, we.worldBot.Y, we.worldBot.Z);
            } else if (we.mesh) {
                // 非 AI 玩家也记录, 帮助排查 "none 都绘制出来了" 的误判
                AiDebugLog("[DRAW] Player in data: pawn=%llx team=%d mesh=%llx pos=(%.0f,%.0f,%.0f) clazz=%s",
                           (unsigned long long)we.pawn, we.teamId, (unsigned long long)we.mesh,
                           we.worldBot.X, we.worldBot.Y, we.worldBot.Z, we.clazz.c_str());
            }
        }
        if (std::isnan(we.worldBot.X) || std::isnan(we.worldBot.Y) || std::isnan(we.worldBot.Z)) {
            if (we.isAI) AiDebugLog("[DRAW] SKIP NaN: pawn=%llx", (unsigned long long)we.pawn);
            continue;
        }
        if (we.worldBot.X == 0.f && we.worldBot.Y == 0.f && we.worldBot.Z == 0.f) {
            if (we.isAI) AiDebugLog("[DRAW] SKIP zero pos: pawn=%llx", (unsigned long long)we.pawn);
            continue;
        }
        // ★修复: AI不受队伍/队友过滤影响
        if (!g_DrawAll && checkTeam && localTeamId > 0 && we.teamId == localTeamId && !we.isAI) continue;
        if (!g_DrawSelf && localPawn && we.pawn == localPawn) continue;
        if (!g_DrawTeammate && localTeamId > 0 && we.teamId == localTeamId && !we.isAI) continue;

        float dx = we.worldBot.X - cam.localPos.X;
        float dy = we.worldBot.Y - cam.localPos.Y;
        float dz = we.worldBot.Z - cam.localPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;
        float maxCm = maxDist * 100.f;
        const bool isLikelyAI = we.isAI;
        const bool isDrawAllEntry = (g_DrawAll || !we.mesh);
        // 真人不再套用 g_MaxDist 距离裁剪，保证真人绘制距离不被限制；
        // DrawAll/非角色对象仍保留总距离限制，AI 使用下面的 g_AIMaxDist。
        if (isDrawAllEntry && distSq > maxCm * maxCm) {
            if (we.isAI && logAiThisFrame)
                AiDebugLog("[DRAW] SKIP DrawAllDist: pawn=%llx dist=%dm", (unsigned long long)we.pawn, (int)(sqrtf(distSq)/100.f));
            continue;
        }
        int dist = (int)(sqrtf(distSq) / 100.f);

        FVector2D screenBot = AnQuWorldToScreen(we.worldBot, cam, sw, sh);
        if (screenBot.X <= 0 || screenBot.Y <= 0) {
            if (logAiThisFrame)
                AiDebugLog("[DRAW] SKIP W2S-bot: pawn=%llx isAI=%d screen=(%.1f,%.1f) aiPos=(%.0f,%.0f,%.0f) camLoc=(%.0f,%.0f,%.0f) camRot=(%.1f,%.1f,%.1f)",
                           (unsigned long long)we.pawn, (int)we.isAI, screenBot.X, screenBot.Y,
                           we.worldBot.X, we.worldBot.Y, we.worldBot.Z,
                           cam.camLoc.X, cam.camLoc.Y, cam.camLoc.Z,
                           cam.camRot.X, cam.camRot.Y, cam.camRot.Z);
            continue;
        }
        // ★优化: 跳过完全在屏幕右侧/下侧的实体 (box 在 screenTop~screenBot 之间)
        //   screenBot.X >= sw 表示实体脚部已经超出屏幕右边界, 整个框都在屏幕外
        if (screenBot.X > sw + 200 || screenBot.Y > sh + 200) {
            if (logAiThisFrame && we.isAI)
                AiDebugLog("[DRAW] SKIP offscreen: pawn=%llx screen=(%.1f,%.1f) sw=%d sh=%d",
                           (unsigned long long)we.pawn, screenBot.X, screenBot.Y, sw, sh);
            continue;
        }

        if (isDrawAllEntry) {
            if (we.isAI && logAiThisFrame)
                AiDebugLog("[DRAW] AI no-mesh simple: pawn=%llx dist=%dm clazz=%s", (unsigned long long)we.pawn, dist, we.clazz.c_str());
            snprintf(buf, sizeof(buf), u8"距离:%dm %s", dist, we.clazz.c_str());
            StrokeText(dl, buf, ImVec2(screenBot.X - CalcTextWidth(buf) * 0.5f, screenBot.Y), kOutline, cText);
            continue;
        }

        FVector2D screenTop = AnQuWorldToScreen(we.worldTop, cam, sw, sh);
        if (screenTop.X <= 0 || screenTop.Y <= 0) {
            if (we.isAI && logAiThisFrame)
                AiDebugLog("[DRAW] SKIP W2S-top: pawn=%llx screen=(%.1f,%.1f)", (unsigned long long)we.pawn, screenTop.X, screenTop.Y);
            continue;
        }

        float boxH = fabsf(screenTop.Y - screenBot.Y);
        float boxW = boxH * 0.65f;
        const float boxCenterX = (screenTop.X + screenBot.X) * 0.5f;
        float boxLeft  = boxCenterX - boxW / 2.f;
        float boxRight = boxCenterX + boxW / 2.f;

        auto infoIt = infoMap.find(we.pawn);
        const char* nameStr = "";
        const char* weaponStr = "";
        if (infoIt != infoMap.end()) {
            nameStr = infoIt->second.nameStr.c_str();
            weaponStr = infoIt->second.weaponName.c_str();
        }

        if (isLikelyAI) {
            if (logAiThisFrame)
                AiDebugLog("[DRAW] AI path: pawn=%llx dist=%dm g_DrawAI=%d g_AIMaxDist=%d hasBones=%d",
                           (unsigned long long)we.pawn, dist, (int)g_DrawAI, g_AIMaxDist, (int)we.hasBones);
            if (!g_DrawAI) {
                if (logAiThisFrame) AiDebugLog("[DRAW] SKIP g_DrawAI=off: pawn=%llx", (unsigned long long)we.pawn);
                continue;
            }
            if (dist > g_AIMaxDist) {
                if (logAiThisFrame) AiDebugLog("[DRAW] SKIP AI dist: pawn=%llx dist=%d > %d", (unsigned long long)we.pawn, dist, g_AIMaxDist);
                continue;
            }
            if (dist <= 2) {
                if (logAiThisFrame) AiDebugLog("[DRAW] SKIP AI too close: pawn=%llx dist=%d", (unsigned long long)we.pawn, dist);
                continue;
            }
            StrokeText(dl, u8"人机", ImVec2(screenTop.X - CalcTextWidth(u8"人机") * 0.5f, screenTop.Y - 18), kOutline, cAI);
            if (g_ShowBox)
                dl->AddRect(ImVec2(boxLeft, screenTop.Y), ImVec2(boxRight, screenTop.Y + boxH), cAI, 0, 0, 1.f);
            if (g_ShowDistance) {
                snprintf(buf, sizeof(buf), u8"%dm", dist);
                StrokeText(dl, buf, ImVec2(boxRight + 5.f, screenTop.Y), kOutline, cAI);
            }
            if (g_ShowAISkeleton && we.hasBones)
                DrawSkeletonLines(dl, we.worldBones, cam, sw, sh, cBone, 2.f);
            if (logAiThisFrame)
                AiDebugLog("[DRAW] AI OK: pawn=%llx dist=%dm screen=(%.1f,%.1f)", (unsigned long long)we.pawn, dist, screenTop.X, screenTop.Y);
            continue;
        }

        if (dist <= 2) continue;
        if (g_ShowBox)
            dl->AddRect(ImVec2(boxLeft, screenTop.Y), ImVec2(boxRight, screenTop.Y + boxH), cBox, 0, 0, 1.f);
        if (g_ShowRays)
            dl->AddLine(ImVec2(sw / 2.f, 0.f), ImVec2(screenTop.X, screenTop.Y), cRay, 2.f);

        float panelX = boxRight + 5.f;
        float panelY = screenTop.Y;
        if (g_ShowDistance) {
            snprintf(buf, sizeof(buf), u8"%dm", dist);
            StrokeText(dl, buf, ImVec2(panelX, panelY), kOutline, cText);
            panelY += 15;
        }
        if (g_ShowTeamId) {
            snprintf(buf, sizeof(buf), u8"队:%d", we.teamId);
            StrokeText(dl, buf, ImVec2(panelX, panelY), kOutline, cText);
            panelY += 15;
        }
        if (g_ShowArmor && infoIt != infoMap.end()) {
            auto& ai = infoIt->second;
            if (ai.helmetLevel > 0 || ai.armorLevel > 0) {
                snprintf(buf, sizeof(buf), u8"盔%d甲%d", ai.helmetLevel, ai.armorLevel);
                StrokeText(dl, buf, ImVec2(panelX, panelY), kOutline, IM_COL32(100, 180, 255, 255));
            }
        }

        float headY = screenTop.Y - 20;
        if (g_ShowName && nameStr[0]) {
            StrokeText(dl, nameStr, ImVec2(screenTop.X - CalcTextWidth(nameStr) * 0.5f, headY), kOutline, cText);
            headY -= 16;
        }
        if (g_ShowWeapon && weaponStr[0])
            StrokeText(dl, weaponStr, ImVec2(screenTop.X - CalcTextWidth(weaponStr) * 0.5f, headY), kOutline, cText);

        if (g_ShowSkeleton && we.hasBones)
            DrawSkeletonLines(dl, we.worldBones, cam, sw, sh, cBone, 2.f);

        // ★新增: 非AI玩家绘制完成日志
        if (logAiThisFrame)
            AiDebugLog("[DRAW] Player OK: pawn=%llx team=%d dist=%dm name='%s' screen=(%.1f,%.1f)",
                       (unsigned long long)we.pawn, we.teamId, dist, nameStr, screenTop.X, screenTop.Y);
    }

    DrawRadar(dl, cam, sw, sh);
}

// ═══════════════════════════════════════
//  雷达绘制
// ═══════════════════════════════════════
inline void DrawRadar(ImDrawList* dl, const CameraData& cam, int sw, int sh) {
    if (!g_ShowRadar) return;
    const float radarRadius = 100.f;
    float cx, cy;
    switch (g_RadarPos) {
        case 0: cx = radarRadius + 20; cy = radarRadius + 20; break;
        case 1: cx = sw - radarRadius - 20; cy = radarRadius + 20; break;
        case 2: cx = radarRadius + 20; cy = sh - radarRadius - 20; break;
        default: cx = sw - radarRadius - 20; cy = sh - radarRadius - 20; break;
    }
    const ImU32 bgCol = IM_COL32(10,10,20,(int)(180*g_RadarAlpha));
    const ImU32 borderCol = IM_COL32(80,80,120,(int)(200*g_RadarAlpha));
    const ImU32 gridCol = IM_COL32(60,60,80,(int)(100*g_RadarAlpha));
    const ImU32 selfCol = IM_COL32(0,255,0,255), enemyCol = IM_COL32(255,60,60,255);
    const ImU32 aiCol = IM_COL32(255,200,50,255), teamCol = IM_COL32(80,150,255,255);

    dl->AddCircleFilled(ImVec2(cx,cy), radarRadius, bgCol, 64);
    dl->AddCircle(ImVec2(cx,cy), radarRadius, borderCol, 64, 2.f);
    dl->AddCircle(ImVec2(cx,cy), radarRadius*0.33f, gridCol, 48, 1.f);
    dl->AddCircle(ImVec2(cx,cy), radarRadius*0.66f, gridCol, 48, 1.f);
    dl->AddLine(ImVec2(cx-radarRadius,cy), ImVec2(cx+radarRadius,cy), gridCol);
    dl->AddLine(ImVec2(cx,cy-radarRadius), ImVec2(cx,cy+radarRadius), gridCol);

    float yawRad = cam.camRot.Y * 3.14159265f / 180.f;
    float cosY = cosf(yawRad), sinY = sinf(yawRad);

    float tipX = cx, tipY = cy - 9;
    float leftX = cx - 5, leftY = cy + 4;
    float rightX = cx + 5, rightY = cy + 4;
    dl->AddTriangleFilled(ImVec2(tipX,tipY), ImVec2(leftX,leftY), ImVec2(rightX,rightY), selfCol);

    auto worldToRadar = [&](float dx, float dy, float s) -> ImVec2 {
        float fwdComp = dx * cosY + dy * sinY;
        float rightComp = -dx * sinY + dy * cosY;
        return ImVec2(cx + rightComp * s, cy - fwdComp * s);
    };

    float scale = radarRadius / (float)(g_RadarRange * 100.f);
    std::shared_lock<std::shared_mutex> radarLk(gs.radarMutex);
    const auto& radarData = gs.radarData;

    for (auto& [pawn, rt] : radarData) {
        if (pawn == g_LocalPawn) continue;
        float dx = rt.currentPos.X - cam.localPos.X, dy = rt.currentPos.Y - cam.localPos.Y;
        float dist = sqrtf(dx*dx + dy*dy);
        float s = scale;
        if (dist > (float)(g_RadarRange * 100)) { s = radarRadius / dist * 0.9f; }
        ImVec2 pt = worldToRadar(dx, dy, s);

        ImU32 dotCol = enemyCol;
        if (g_LocalTeamId > 0) {
            if (rt.factionType == FT_NormalScav || (rt.factionType >= FT_RebelForce && rt.factionType <= FT_Abyss)) dotCol = aiCol;
            else dotCol = enemyCol;
        }
        if (rt.isAiming) dotCol = IM_COL32(255,0,0,255);

        if (g_RadarTrail && rt.trailCount > 1) {
            for (int i = 0; i < rt.trailCount - 1; i++) {
                int idx0 = (rt.trailHead + i) % RadarTrailEntry::TRAIL_MAX;
                int idx1 = (rt.trailHead + i + 1) % RadarTrailEntry::TRAIL_MAX;
                float tdx0 = rt.trail[idx0].X - cam.localPos.X;
                float tdy0 = rt.trail[idx0].Y - cam.localPos.Y;
                float tdx1 = rt.trail[idx1].X - cam.localPos.X;
                float tdy1 = rt.trail[idx1].Y - cam.localPos.Y;
                ImVec2 p0 = worldToRadar(tdx0, tdy0, scale);
                ImVec2 p1 = worldToRadar(tdx1, tdy1, scale);
                float alpha = (float)(i+1) / (float)rt.trailCount * 80.f;
                dl->AddLine(p0, p1, IM_COL32(255,100,100,(int)alpha), 1.f);
            }
        }
        dl->AddCircleFilled(pt, 4.f, dotCol);
        if (rt.isAiming) dl->AddCircle(pt, 7.f, IM_COL32(255,0,0,200), 16, 1.5f);
    }

    if (g_MarkExtraction) {
        std::shared_lock<std::shared_mutex> extLk(gs.extractionMutex);
        const auto& extractions = gs.extractions;
        for (auto& ep : extractions) {
            float dx = ep.pos.X - cam.localPos.X, dy = ep.pos.Y - cam.localPos.Y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist > (float)(g_RadarRange * 100)) continue;
            ImVec2 pt = worldToRadar(dx, dy, scale);
            dl->AddCircleFilled(pt, 5.f, IM_COL32(0,255,255,200), 4);
        }
    }
}

// ═══════════════════════════════════════
//  周围物质 ESP — 渲染线程实时 W2S (零延迟)
// ═══════════════════════════════════════
inline void DrawNearbyItems() {
    if (!g_ShowNearbyItems || !g_ScanItems) return;
    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    std::shared_ptr<std::vector<NearbyEntry>> items;
    { std::shared_lock<std::shared_mutex> lk(gs.nearbyMutex); items = gs.nearbyItems; }
    if (!items || items->empty()) return;
    CameraData cam;
    { std::shared_lock<std::shared_mutex> lk(gs.camMutex); cam = gs.camera; }
    if (!cam.cameraMgr) return;

    auto& filter = ItemFilterTable::Get();
    const ImU32 kItemCol = IM_COL32(255, 200, 0, 255);
    const ImU32 kOutline = IM_COL32(0, 0, 0, 255);
    const int sw = gs.screenW, sh = gs.screenH;
    char buf[256];
    const float maxCm = g_NearbyItemsDist * 100.f;
    auto RarityColor = [](int r) -> ImU32 {
        switch (r) {
            case 1: return IM_COL32(255, 255, 255, 255);
            case 2: return IM_COL32(150, 220, 255, 255);
            case 3: return IM_COL32( 80, 130, 255, 255);
            case 4: return IM_COL32(180,  80, 255, 255);
            case 5: return IM_COL32(255, 215,   0, 255);
            case 6:
            case 7: return IM_COL32(255,  60,  60, 255);
            default: return 0;
        }
    };

    static float s_digitW = 0.0f;
    static float s_suffixW = 0.0f;
    if (s_digitW <= 0.0f) {
        s_digitW = ImGui::CalcTextSize("0").x;
        s_suffixW = ImGui::CalcTextSize(" [m]").x;
    }

    for (auto& it : *items) {
        if (it.pos.X == 0.f && it.pos.Y == 0.f && it.pos.Z == 0.f) continue;
        if (it.pos.X == 1.f && it.pos.Y == 1.f && it.pos.Z == 1.f) continue;
        float dx = it.pos.X - cam.localPos.X, dy = it.pos.Y - cam.localPos.Y, dz = it.pos.Z - cam.localPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > maxCm * maxCm) continue;
        int dist = (int)(sqrtf(distSq) / 100.f);

        ImU32 rareCol = RarityColor(it.rarity);
        if (rareCol == 0) continue;
        int filterIdx = (it.rarity == 7) ? 6 : it.rarity;
        if (filterIdx < 1 || filterIdx > 6 || !g_ShowRarity[filterIdx]) continue;
        if (!filter.IsEnabled(it.className)) continue;

        const std::string* displayName = &it.displayName;
        float nameWidth = 0.0f;
        if (it.labelSkip) continue;
        if (displayName->empty()) {
            // 兼容旧 ThreadItems 路径：没有预计算显示名时仍走缓存解析。
            const auto& label = ResolveItemLabelCached(it.className);
            if (label.skip || label.displayName.empty()) continue;
            displayName = &label.displayName;
            nameWidth = label.nameWidth;
        } else {
            nameWidth = CachedItemTextWidth(*displayName);
        }
        if (displayName->empty()) continue;

        FVector2D sc = AnQuWorldToScreen(it.pos, cam, sw, sh);
        if (sc.X <= -50 || sc.Y <= -30 || sc.X >= sw + 50 || sc.Y >= sh + 30) continue;

        snprintf(buf, sizeof(buf), "%s [%dm]", displayName->c_str(), dist);
        int digits = (dist >= 1000) ? 4 : (dist >= 100) ? 3 : (dist >= 10) ? 2 : 1;
        float textW = nameWidth + s_suffixW + digits * s_digitW;
        ImVec2 textPos(sc.X - textW * 0.5f, sc.Y);

        if (g_ShowItemPrice)
            dl->AddRectFilled(ImVec2(textPos.x - 7.0f, textPos.y + 3.0f),
                              ImVec2(textPos.x - 3.0f, textPos.y + 13.0f),
                              rareCol, 1.0f);
        FastItemText(dl, buf, textPos, kOutline, kItemCol);
    }
}
