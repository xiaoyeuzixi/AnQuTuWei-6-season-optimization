#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/MemUtils.h"
#include "../core/NameResolve.h"
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
inline void ThreadCombat() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;
    bool combatCleared = false;

    // 鈽卼hread_local 澶嶇敤: 閬垮厤姣忚疆 heap alloc
    static thread_local std::vector<size_t>   validIdx;
    static thread_local std::vector<uint8_t>  tmpFaction;

    while (Runtime::IsRunning()) {
        if (!gs.world || !gs.base) { throttler.sleepUntilNext(std::chrono::milliseconds(100)); continue; }

        // 闆疯揪鍏抽棴鏃讹紝鎴樻枟鏁版嵁涔熶笉闇€瑕佹洿鏂?
        if (!g_ShowRadar) {
            if (!combatCleared) {
                std::lock_guard<std::shared_mutex> lk(gs.combatMutex);
                gs.combatData.clear();
                combatCleared = true;
            }
            throttler.sleepUntilNext(std::chrono::milliseconds(200));
            continue;
        }
        combatCleared = false;

        std::shared_ptr<std::vector<PlayerEntry>> data;
        { std::shared_lock<std::shared_mutex> lk(gs.dataMutex); data = gs.players; }
        if (!data || data->empty()) { throttler.sleepUntilNext(std::chrono::milliseconds(100)); continue; }

        // 鈹€鈹€ Dirty Flag 妫€娴? 鐜╁鏁版嵁鏈彉鍖栨椂璺宠繃 鈹€鈹€
        if (InterlockedCompareExchange(&gs.dirty.combatDirty, 0, 0) == 0) {
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }

        size_t n = data->size();
        validIdx.clear();
        tmpFaction.assign(n, 0);

        // 鈽呮壒閲忎紭鍖? 涓€娆catter璇绘墍鏈?FactionType (1瀛楄妭)
        for (size_t i = 0; i < n; i++) {
            auto& e = (*data)[i];
            if (e.pawn == 0 || e.pawn < 0x10000 || e.pawn == g_LocalPawn) continue;
            validIdx.push_back(i);
            mem.AddScatter(hScatter, e.pawn + Offset_FactionType, &tmpFaction[i], 1);
        }
        if (validIdx.empty()) {
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }
        mem.ExecuteScatter(hScatter);

        // 鍐欏叆缁撴灉 (闆禗MA)
        std::unordered_map<DWORD64, CombatEntry> combatMap;
        combatMap.reserve(validIdx.size());
        for (size_t idx : validIdx) {
            auto& e = (*data)[idx];
            CombatEntry ce;
            ce.pawn = e.pawn;
            ce.factionType = tmpFaction[idx];
            combatMap[e.pawn] = ce;
        }

        { std::lock_guard<std::shared_mutex> lk(gs.combatMutex); gs.combatData = std::move(combatMap); }
        InterlockedExchange(&gs.dirty.combatDirty, 0);
        throttler.sleepUntilNext(std::chrono::milliseconds(50));
    }
    mem.CloseScatter(hScatter);
}

