#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/MemUtils.h"
#include "../core/NameResolve.h"
#include "../core/DiagLog.h"
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
#include <chrono>

// ═══════════════════════════════════════
//  线程: 摄像头头 + 缓存刷新 (合并线程)
//  ★修复: 正常路径添加 6ms 节流, 避免 DMA 总线被相机线程独占
//         导致 ThreadBoneDMA / ThreadItems 被饿死, 骨骼缓存过期闪烁
//         参考正常项目 ARS_AnQu_WB 的 6ms 节流策略
// ═══════════════════════════════════════
inline void ThreadCamera() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;
    while (Runtime::IsRunning()) {
        DiagScope diag(kDiagCamera);
        // 相机线程是 W2S 的实时基准，不能在热路径里做 TLB/Mem 刷新。
        // 日志里的 200ms+ 周期尖峰就是刷新阻塞 DMA 总线造成的。
        // 读取已恢复为调试版 NOCACHE 实时模式，这里不再周期刷新。

        if (!mem.base) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        gs.base = mem.base;

        static DWORD64 lastWorld = 0;
        static DWORD64 gi=0, in=0, gp=0, pc=0;

        DWORD64 world = mem.Read<DWORD64>(gs.base + BaseWorld);
        if (!world) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        if (world != lastWorld) {
            lastWorld = world;
            gs.world   = world;
            gi = in = gp = pc = 0;
        }

        if (!gi) { gi = mem.Read<DWORD64>(world + Offset_GameInstance);    if (!gi) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; } }
        if (!in) { in = mem.Read<DWORD64>(gi + Offset_GamePlayer);            if (!in) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; } }
        if (!gp) { gp = mem.Read<DWORD64>(in);                         if (!gp) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; } }
        if (!pc) { pc = mem.Read<DWORD64>(gp + Offset_PlayerController);      if (!pc) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; } }

        DWORD64 camMgr = 0, pawn = 0;
        mem.AddScatter(hScatter, pc + Offset_CameraManager, &camMgr, 8);
        mem.AddScatter(hScatter, pc + Offset_APawn, &pawn, 8);
        mem.ExecuteScatter(hScatter);
        if (!camMgr) { gi=in=gp=pc=0; std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

        struct { FVector loc; FVector rot; float fov; } camCache{};
        mem.Read(camMgr + Offset_CameraCache + 0x10, &camCache, sizeof(camCache));

        FVector localPos{};
        DWORD64 root = 0, st = 0;
        int teamId = 0;
        if (pawn) {
            mem.AddScatter(hScatter, pawn + Offset_RootComponent, &root, 8);
            mem.AddScatter(hScatter, pawn + Offset_PlayerState, &st, 8);
            mem.ExecuteScatter(hScatter);
            if (root || st) {
                if (st)   mem.AddScatter(hScatter, st + Offset_TeamId, &teamId, 4);
                mem.ExecuteScatter(hScatter);
                if (root) {
                    // RelativeLocation is ACE-protected on the CN client; the
                    // raw +0x170 bytes are not a usable local world position.
                    localPos = ReadCharacterLocation(root, pawn);
                    const bool finite = std::isfinite(localPos.X) &&
                                        std::isfinite(localPos.Y) &&
                                        std::isfinite(localPos.Z);
                    const bool planar = std::abs(localPos.X) > 10.f ||
                                        std::abs(localPos.Y) > 10.f;
                    if (!finite || !planar)
                        localPos = camCache.loc;
                }
                if (st) g_LocalTeamId = teamId;
            }
        }
        g_LocalPawn = pawn;

        { std::lock_guard<std::shared_mutex> lk(gs.camMutex);
            gs.camera.cameraMgr   = camMgr;
            gs.camera.camLoc      = camCache.loc;
            gs.camera.camRot      = camCache.rot;
            gs.camera.camFov      = camCache.fov;
            gs.camera.localPos    = localPos;
            gs.camera.localTeamId = g_LocalTeamId;
        }
        DiagSetCounts(kDiagCamera, pawn ? 1 : 0, camMgr ? 1 : 0);
        // ★修复: 6ms 节流 (~166fps), 让出 DMA 总线给 ThreadBoneDMA / ThreadItems
        //   无节流会导致 DMA 总线被相机独占, 骨骼缓存 >300ms 过期 → 闪烁
        throttler.sleepUntilNext(std::chrono::milliseconds(6));
    }
    mem.CloseScatter(hScatter);
}
