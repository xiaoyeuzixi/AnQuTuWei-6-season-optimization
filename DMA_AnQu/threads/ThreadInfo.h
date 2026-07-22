#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/DiagLog.h"
#include "../core/MemUtils.h"
#include "../core/NameResolve.h"
#include "../ESPUtils.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <shared_mutex>
#include <thread>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
inline void ThreadInfo() {
    Throttler throttler;
    bool infoCleared = false;
    while (Runtime::IsRunning()) {
        DiagScope diag(kDiagInfo);
        if (!g_ShowName && !g_ShowWeapon && !g_ShowArmor) {
            if (!infoCleared) {
                std::lock_guard<std::shared_mutex> lk(gs.infoMutex);
                gs.playerInfo.clear();
                infoCleared = true;
            }
            throttler.sleepUntilNext(std::chrono::milliseconds(300));
            continue;
        }
        infoCleared = false;

        std::shared_ptr<std::vector<PlayerEntry>> data;
        { std::shared_lock<std::shared_mutex> lk(gs.dataMutex); data = gs.players; }
        if (!data) { throttler.sleepUntilNext(std::chrono::milliseconds(50)); continue; }

        // 鈹€鈹€ Dirty Flag 妫€娴? 鐜╁鏁版嵁鏈彉鍖栨椂璺宠繃 info 澶勭悊 鈹€鈹€
        if (InterlockedCompareExchange(&gs.dirty.infoDirty, 0, 0) == 0) {
            // 鏁版嵁鏈彉鍖栵紝璺宠繃鏈疆澶勭悊
            throttler.sleepUntilNext(std::chrono::milliseconds(16));  // ~60fps
            continue;
        }

        std::unordered_map<DWORD64, PlayerInfo> infoMap;
        infoMap.reserve(data->size());

        for (auto& e : *data) {
            if (e.pawn == 0 || e.state == 0) continue;  // 璺宠繃浜烘満 (鏃?PlayerState)

            PlayerInfo pi;

            if (g_ShowName && e.state) {
                struct { DWORD64 addr; int len; } nameData{};
                mem.Read(e.state + Offset_PlayerNamePrivate, &nameData, sizeof(nameData));
                if (nameData.addr && nameData.len > 0 && nameData.len < 1024) {
                    pi.nameStr = GetName2(nameData.addr, nameData.len);
                    if (pi.nameStr.empty()) pi.nameStr = "Player";
                } else {
                    pi.nameStr = "Player";
                }
            }
            if (g_ShowWeapon) pi.weaponName = GetWeaponName(e.pawn);
            // 琛€閲忓姛鑳藉凡绂佺敤 鈥?GetCurrentHealth 璋冪敤绉婚櫎
            //if (g_ShowHealth) {
            //    float rawHp = GetCurrentHealth(e.pawn);
            //    pi.hp = (int)rawHp;
            //    pi.maxHp = 450;
            //}
            // 璇诲彇鎶ょ敳+澶寸洈绛夌骇 (涓嶈鑰愪箙搴? 鍙绛夌骇)
            if (g_ShowArmor) {
                auto parts = GetPlayerArmorParts(e.pawn);
                if (!parts.empty()) {
                    pi.helmetLevel = parts[0].level;
                }
                if (parts.size() > 1) {
                    pi.armorLevel = parts[1].level;
                }
            }
            infoMap[e.pawn] = std::move(pi);
        }

        const int infoCount = (int)infoMap.size();
        { std::lock_guard<std::shared_mutex> lk(gs.infoMutex); gs.playerInfo = std::move(infoMap); }
        DiagSetCounts(kDiagInfo, (int64_t)infoCount, (int64_t)data->size());
        InterlockedExchange(&gs.dirty.infoDirty, 0);
        throttler.sleepUntilNext(std::chrono::milliseconds(100));
    }
}

