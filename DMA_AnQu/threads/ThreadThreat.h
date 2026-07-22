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
inline void ThreadThreat() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;
    bool threatCleared = false;

    // 鈽卼hread_local 澶嶇敤: 閬垮厤姣忚疆 heap alloc
    static thread_local std::vector<size_t>   validIdx;
    static thread_local std::vector<DWORD64>  tmpRoots;
    static thread_local std::vector<DWORD64>  tmpAnimInst;
    static thread_local std::vector<float>    tmpYaws;
    static thread_local std::vector<uint8_t>  tmpAiming;

    while (Runtime::IsRunning()) {
        if (!gs.world || !gs.base) { throttler.sleepUntilNext(std::chrono::milliseconds(100)); continue; }

        // 闆疯揪闇€瑕?yaw + isAiming
        if (!g_ShowRadar) {
            if (!threatCleared) {
                std::lock_guard<std::shared_mutex> lk(gs.threatMutex);
                gs.threatData.clear();
                threatCleared = true;
            }
            throttler.sleepUntilNext(std::chrono::milliseconds(200));
            continue;
        }
        threatCleared = false;

        std::shared_ptr<std::vector<PlayerEntry>> data;
        { std::shared_lock<std::shared_mutex> lk(gs.dataMutex); data = gs.players; }
        if (!data || data->empty()) { throttler.sleepUntilNext(std::chrono::milliseconds(100)); continue; }

        // 鈹€鈹€ Dirty Flag 妫€娴? 鐜╁鏁版嵁鏈彉鍖栨椂璺宠繃 鈹€鈹€
        if (InterlockedCompareExchange(&gs.dirty.bonesDirty, 0, 0) == 0) {
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }

        size_t n = data->size();
        validIdx.clear();
        tmpRoots.assign(n, 0);
        tmpAnimInst.assign(n, 0);
        tmpYaws.assign(n, 0.f);
        tmpAiming.assign(n, 0);

        // 鈽呮壒閲忎紭鍖?Phase 1: 涓€娆catter璇绘墍鏈?root + animInst 鎸囬拡
        for (size_t i = 0; i < n; i++) {
            auto& e = (*data)[i];
            if (e.pawn == 0 || e.pawn < 0x10000 || e.pawn == g_LocalPawn) continue;
            if (e.teamId == g_LocalTeamId && g_LocalTeamId > 0 && !g_DrawTeammate) continue;
            validIdx.push_back(i);
            mem.AddScatter(hScatter, e.pawn + Offset_RootComponent, &tmpRoots[i], 8);
            if (e.mesh)
                mem.AddScatter(hScatter, e.mesh + Offset_AnimInstance, &tmpAnimInst[i], 8);
        }
        if (validIdx.empty()) {
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }
        mem.ExecuteScatter(hScatter);

        // 鈽呮壒閲忎紭鍖?Phase 2: 涓€娆catter璇绘墍鏈?yaw + bIsAiming
        for (size_t idx : validIdx) {
            if (tmpRoots[idx])
                mem.AddScatter(hScatter, tmpRoots[idx] + Offset_ActorYaw, &tmpYaws[idx], 4);
            if (tmpAnimInst[idx] && tmpAnimInst[idx] > 0x10000)
                mem.AddScatter(hScatter, tmpAnimInst[idx] + Offset_bIsAiming, &tmpAiming[idx], 1);
        }
        mem.ExecuteScatter(hScatter);

        // Phase 3: 鍐欏叆缁撴灉 (闆禗MA)
        std::unordered_map<DWORD64, ThreatEntry> threatMap;
        threatMap.reserve(validIdx.size());
        for (size_t idx : validIdx) {
            auto& e = (*data)[idx];
            ThreatEntry te;
            te.pawn = e.pawn;
            te.yaw = tmpYaws[idx];
            te.isAiming = (tmpAiming[idx] != 0);
            threatMap[e.pawn] = te;
        }

        { std::lock_guard<std::shared_mutex> lk(gs.threatMutex); gs.threatData = std::move(threatMap); }
        throttler.sleepUntilNext(std::chrono::milliseconds(50));
    }
    mem.CloseScatter(hScatter);
}

