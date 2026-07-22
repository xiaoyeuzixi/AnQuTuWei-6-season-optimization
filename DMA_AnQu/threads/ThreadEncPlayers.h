#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/MemUtils.h"
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
#include <cmath>
#include <memory>
#include <chrono>

// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
//  绾跨▼: 鍔犲瘑鐜╁浣嶇疆璇诲彇 (鐙珛绾跨▼, 甯︾紦瀛?
//  涓撻棬澶勭悊 RL 鍔犲瘑鐨勭帺瀹?AI, 鐢ㄦ寚閽堣拷韪?Bounds鍥為€€璇讳綅缃?
//  璇诲埌鐨勪綅缃啓鍥?gs.players, 閬垮厤闃诲 ThreadActors
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
inline void ThreadEncPlayers() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;
    // 鐜╁浣嶇疆缂撳瓨: pawn 鈫?last valid pos
    std::unordered_map<DWORD64, FVector> playerPosCache;
    playerPosCache.reserve(128);
    // 鈽卼hread_local 澶嶇敤: 閬垮厤姣忚疆 heap alloc
    static thread_local std::vector<DWORD64> tmpRoots;
    static thread_local std::vector<int>     tmpFlags;
    static thread_local std::vector<size_t>  validIdx;

    while (Runtime::IsRunning()) {
        std::shared_ptr<std::vector<PlayerEntry>> data;
        { std::shared_lock<std::shared_mutex> lk(gs.dataMutex); data = gs.players; }
        if (!data) { throttler.sleepUntilNext(std::chrono::milliseconds(50)); continue; }
        if (data->empty()) {
            playerPosCache.clear();
            throttler.sleepUntilNext(std::chrono::milliseconds(80));
            continue;
        }

        // 鈽呭彧鏀堕泦鏈夋晥鏇存柊, 涓嶅仛鍏ㄩ噺 copy-writeback (閬垮厤瑕嗙洊 ThreadActors 鐨勬湁鏁堟暟鎹?
        static thread_local std::unordered_map<DWORD64, FVector> updates;
        updates.clear();
        size_t n = data->size();
        tmpRoots.assign(n, 0);
        tmpFlags.assign(n, 0);
        validIdx.clear();

        // 鈽呮壒閲忎紭鍖?Phase 1: 涓€娆catter璇绘墍鏈?root 鎸囬拡
        for (size_t i = 0; i < n; i++) {
            auto& e = (*data)[i];
            if (e.pawn == 0) continue;
            validIdx.push_back(i);
        }
        if (validIdx.empty()) {
            throttler.sleepUntilNext(std::chrono::milliseconds(30));
            continue;
        }
        // Scatter: 璇绘墍鏈?root 鎸囬拡 (澶嶇敤 hScatter)
        for (size_t idx : validIdx)
            mem.AddScatter(hScatter, (*data)[idx].pawn + Offset_RootComponent, &tmpRoots[idx], 8);
        mem.ExecuteScatter(hScatter);
        // Scatter: 璇绘墍鏈?flags
        for (size_t idx : validIdx) {
            if (tmpRoots[idx])
                mem.AddScatter(hScatter, tmpRoots[idx] + Offset_ActorLocationFlags, &tmpFlags[idx], 4);
        }
        mem.ExecuteScatter(hScatter);

        // 鈽呮壒閲忎紭鍖?Phase 2: 鍙鍔犲瘑鐜╁璋冪敤 ReadActorLocation
        for (size_t idx : validIdx) {
            auto& e = (*data)[idx];
            if (!tmpRoots[idx]) continue;
            int rlEnc = (unsigned int)tmpFlags[idx] >> 29;
            if (rlEnc == 0) continue; // 鏄庢枃, ThreadActors 宸插鐞?

            // 鍔犲瘑: 鐢?ReadActorLocation 鍥為€€
            FVector p = ReadActorLocation(tmpRoots[idx], e.pawn);
            auto isCoord = [](float v) -> bool {
                return !std::isnan(v) && !std::isinf(v) &&
                       std::abs(v) > 0.01f && std::abs(v) < 500000.f;
            };
            // 鈽匷 鑼冨洿妫€鏌?(涓?ThreadActors 涓€鑷?
            if (isCoord(p.X) && isCoord(p.Y) && isCoord(p.Z) && std::abs(p.Z) < 10000.f) {
                updates[e.pawn] = p;
                playerPosCache[e.pawn] = p;
            } else {
                // 璇诲彇澶辫触, 鐢ㄧ紦瀛?
                // ReadActorLocation failed: do NOT use cache, leave ThreadActors position as-is
                // Cache fallback causes ghost boxes at stale locations
                // 鈽呮棤缂撳瓨涔熶笉鏇存柊, 淇濈暀 ThreadActors 宸插啓鍏ョ殑 pos
            }
        }
        // 娓呯悊缂撳瓨
        if (playerPosCache.size() > data->size() * 2) {
            std::unordered_map<DWORD64, FVector> nc;
            nc.reserve(data->size());
            for (auto& e : *data)
                if (e.pawn) {
                    auto it = playerPosCache.find(e.pawn);
                    if (it != playerPosCache.end()) nc.emplace(e.pawn, it->second);
                }
            playerPosCache = std::move(nc);
        }
        // 鈽呭彧鏇存柊鏈夋湁鏁堜綅缃殑 entry, 涓嶈鐩栧叾浠?entry
        if (!updates.empty()) {
            // ★修复: 不再直接修改 gs.players 指向的 vector (数据竞争)
            //   ThreadBones 已通过 shared_lock 拷贝 shared_ptr 后无锁遍历, 直接修改 vector 内容会造成撕裂读
            //   改为: 创建新 vector 替换 shared_ptr (原子发布模式), 旧 vector 仍被 ThreadBones 安全持有
            std::shared_ptr<std::vector<PlayerEntry>> oldData;
            { std::shared_lock<std::shared_mutex> rlk(gs.dataMutex); oldData = gs.players; }
            if (oldData && !oldData->empty()) {
                auto newArr = std::make_shared<std::vector<PlayerEntry>>(*oldData);
                for (auto& e : *newArr) {
                    auto it = updates.find(e.pawn);
                    if (it != updates.end()) e.pos = it->second;
                }
                std::lock_guard<std::shared_mutex> lk(gs.dataMutex);
                gs.players = newArr;
            }
            InterlockedExchange(&gs.dirty.bonesDirty, 1);
        }
        // Do NOT reset playersDirty here — ThreadEncPlayers doesn't own this flag.
        // Unconditional reset causes ThreadBones to skip, leaving stale gs.worldData.
        throttler.sleepUntilNext(std::chrono::milliseconds(30));
    }
    mem.CloseScatter(hScatter);
}

