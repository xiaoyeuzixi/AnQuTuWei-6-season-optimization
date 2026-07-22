#pragma once
/*
 * 游戏逻辑模块 — 可替换
 * 包含: 名字解析、类型判定、相机线程、角色线程、缓存刷新
 * 对外暴露: GameInit() / GameStart() / DrawUI() / DrawESP()
 */

#include "Mem.h"
#include "Offset.h"
#include "GameMatrix.h"
#include "core/Math.h"
#include "Throttler.h"
#include "core/Runtime.h"
#include "core/Config.h"
#include "core/GameState.h"
#include "core/MemUtils.h"
#include "core/NameResolve.h"
#include "core/Decryption.h"
#include "core/PerfMonitor.h"
#include "core/DiagLog.h"
#include "core/ESPDraw.h"

#include "threads/ThreadCamera.h"
#include "threads/ThreadActors.h"
#include "threads/ThreadItems.h"
#include "threads/ThreadBoneDMA.h"
#include "threads/ThreadBones.h"
#include "threads/ThreadInfo.h"
#include "threads/ThreadCombat.h"
#include "threads/ThreadThreat.h"
#include "threads/ThreadRadar.h"
#include "threads/ThreadEncPlayers.h"
#include "threads/ThreadEncItems.h"

#include "ESPUtils.h"
#include "ItemMap.h"
#include "DMAKey.h"
#include "Security.h"

#include "ImGui/imgui.h"
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

inline PerfMonitor g_Perf;

inline void ReserveContainers() {
    gs.cache.reserve(512);
    gs.boneCache.reserve(128);
    gs.playerInfo.reserve(128);
    gs.combatData.reserve(128);
    gs.radarData.reserve(128);
    gs.threatData.reserve(128);
    gs.itemPosCache.reserve(512);
}

inline void JoinIfJoinable(std::thread& t) {
    if (t.joinable()) t.join();
}

inline void GameStart(std::thread& t0, std::thread& t1, std::thread& t2,
                      std::thread& t3, std::thread& t4, std::thread& t5,
                      std::thread& t6, std::thread& t7, std::thread& t8, std::thread& t9,
                      std::thread& t10) {
    t0 = std::thread(ThreadCamera);
    // 相机是所有 W2S 的时间基准，不能低于物资/解密线程，否则会出现跟屏幕不同步的卡顿感。
    SetThreadPriority(t0.native_handle(), THREAD_PRIORITY_NORMAL);
    t1 = std::thread(ThreadActors);
    SetThreadPriority(t1.native_handle(), THREAD_PRIORITY_NORMAL);
    t2 = std::thread(ThreadBones);
    SetThreadPriority(t2.native_handle(), THREAD_PRIORITY_NORMAL);
    t3 = std::thread(ThreadInfo);
    SetThreadPriority(t3.native_handle(), THREAD_PRIORITY_NORMAL);
    t4 = std::thread(ThreadItems);
    SetThreadPriority(t4.native_handle(), THREAD_PRIORITY_NORMAL);
    t5 = std::thread(ThreadCombat);
    SetThreadPriority(t5.native_handle(), THREAD_PRIORITY_NORMAL);
    t6 = std::thread(ThreadRadar);
    SetThreadPriority(t6.native_handle(), THREAD_PRIORITY_NORMAL);
    t7 = std::thread(ThreadThreat);
    SetThreadPriority(t7.native_handle(), THREAD_PRIORITY_NORMAL);
    t8 = std::thread(ThreadEncPlayers);
    SetThreadPriority(t8.native_handle(), THREAD_PRIORITY_NORMAL);
    t9 = std::thread(ThreadEncItems);
    SetThreadPriority(t9.native_handle(), THREAD_PRIORITY_NORMAL);
    t10 = std::thread(ThreadBoneDMA);
    SetThreadPriority(t10.native_handle(), THREAD_PRIORITY_NORMAL);
}
