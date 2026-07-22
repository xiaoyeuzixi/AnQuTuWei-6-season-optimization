#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/DiagLog.h"
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

// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?//  绾跨▼: 楠ㄩ DMA 璇诲彇 (鐙珛绾跨▼ #10)
//  涓撻棬璐熻矗 GetBonesBatch DMA 璇诲彇
//  鍐欏叆 gs.boneCache, 渚?ThreadBones (CPU) 娑堣垂
//  5ms 楂橀杞, DMA 鍜?CPU 瀹屽叏鍒嗙
//  鈽呬紭鍖? DMA 璇诲彇绉诲嚭 gs.boneMutex, 浠呯紦瀛樺啓鍏ュ姞閿?// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
inline void ThreadBoneDMA() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;
    // 鈽卼hread_local 涓存椂缂撳啿鍖? 閬垮厤姣忚疆 heap alloc
    static thread_local std::vector<BoneCache> tmpBones;
    static thread_local std::vector<DWORD64>   tmpPawns;
    bool clearedWhenDisabled = false;
    while (Runtime::IsRunning()) {
        DiagScope diag(kDiagBoneDMA);
        // BoneDMA 不再“一刀切”按骨骼开关关闭。
        // 方框/名字/距离需要 root+head 作为精确锚点，否则只用 ActorRoot±90 会明显偏框。
        // 但未开启骨骼线时只读 2 根骨骼(root/head)，开启骨骼线才读完整 16 根，避免旧版全量骨骼抢 DMA。
        const bool needAnyBoneAnchor =
            g_ShowBox || g_ShowRays || g_ShowDistance || g_ShowName ||
            g_ShowWeapon || g_ShowTeamId || g_ShowArmor ||
            g_ShowSkeleton || g_ShowAISkeleton || g_DrawAI;
        if (!needAnyBoneAnchor) {
            if (!clearedWhenDisabled) {
                std::lock_guard<std::shared_mutex> lk(gs.boneMutex);
                gs.boneCache.clear();
                clearedWhenDisabled = true;
            }
            throttler.sleepUntilNext(std::chrono::milliseconds(200));
            continue;
        }
        clearedWhenDisabled = false;

        std::shared_ptr<std::vector<PlayerEntry>> data;
        { std::shared_lock<std::shared_mutex> lk(gs.dataMutex); data = gs.players; }
        if (!data || data->empty()) { throttler.sleepUntilNext(std::chrono::milliseconds(80)); continue; }

        // 鈹€鈹€ Phase 1: DMA 璇诲彇 (鏃犻攣, 涓嶉樆濉?ThreadBones) 鈹€鈹€
        tmpBones.clear();
        tmpPawns.clear();
        tmpBones.reserve(data->size());
        tmpPawns.reserve(data->size());

        for (auto& e : *data) {
            if (!e.mesh) continue;
            const bool isAI = (e.state == 0);
            const bool needFullSkeleton = isAI ? g_ShowAISkeleton : g_ShowSkeleton;
            const bool needBoxAnchor = isAI ? g_DrawAI : true;
            if (!needFullSkeleton && !needBoxAnchor)
                continue;

            BoneCache bc;
            bc.mesh = e.mesh;
            bc.age  = 0;
            // 鈽呴楠肩储寮?
            // 0=Root, 1=pelvis, 2=thigh_l, 4=calf_l, 5=foot_l,
            // 7=thigh_r, 9=calf_r, 10=foot_r, 15=neck_01, 16=head,
            // 20=clavicle_r, 21=upperarm_r, 22=lowerarm_r, 24=hand_r,
            // 50=clavicle_l, 51=upperarm_l, 52=lowerarm_l, 54=hand_l
            int allIds[16] = {0,16,15,1,2,7,4,9,5,10,51,21,52,22,54,24};
            FVector allBones[16]{};
            // 鈽呬紶鍏?actorPos: 褰?CTW 鍔犲瘑鏃剁敤 actor 浣嶇疆鏋勯€犲悎鎴?CTW
            const int boneCount = needFullSkeleton ? 16 : 2;
            GetBonesBatch(e.mesh, allBones, allIds, boneCount, hScatter, &e.pos);
            auto isCoord = [](float v) -> bool {
                return !std::isnan(v) && !std::isinf(v) &&
                       std::abs(v) < 500000.f;
            };
            const bool validRoot = isCoord(allBones[0].X) && isCoord(allBones[0].Y) && isCoord(allBones[0].Z) &&
                (std::abs(allBones[0].X) > 0.1f || std::abs(allBones[0].Y) > 0.1f);
            const bool validHead = isCoord(allBones[1].X) && isCoord(allBones[1].Y) && isCoord(allBones[1].Z) &&
                (std::abs(allBones[1].X) > 0.1f || std::abs(allBones[1].Y) > 0.1f);
            if (validRoot && validHead) {
                bc.rootPos = allBones[0];
                bc.headPos = allBones[1];
                bc.age = 0;
                bc.lastUpdateMs = GetTickCount64();
                bc.hasSkeleton = needFullSkeleton;
                if (needFullSkeleton) {
                    for (int i = 0; i < 14; i++) bc.worldBones[i] = allBones[i+2];
                }
                tmpBones.push_back(bc);
                tmpPawns.push_back(e.pawn);
            }
        }

        // 鈹€鈹€ Phase 2: 缂撳瓨鍐欏叆 (鍔犻攣, 浠呭啓鍏ユ搷浣? 鈹€鈹€
        {
            std::lock_guard<std::shared_mutex> lk(gs.boneMutex);
            for (size_t i = 0; i < tmpBones.size(); i++) {
                auto& bc = tmpBones[i];
                auto pawn = tmpPawns[i];
                // ★修复: 移除 5000cm 距离验证, 始终更新骨骼缓存
                //   原验证在玩家重生/传送时阻止骨骼缓存更新, 导致偏框永久不修复
                //   ThreadBones 的 samePlace 检查已经处理了位置跳变的情况
                gs.boneCache[pawn] = bc;
            }
            // Remove boneCache entries for pawns no longer in players list (prevents ghost bones)
            std::unordered_set<DWORD64> activePawns;
            for (auto& e : *data) activePawns.insert(e.pawn);
            for (auto it = gs.boneCache.begin(); it != gs.boneCache.end(); ) {
                if (activePawns.find(it->first) == activePawns.end())
                    it = gs.boneCache.erase(it);
                else ++it;
            }
            static int cleanupCounter = 0;
            // Age-based 骨骼缓存清理：每 4 轮执行一次，减少锁持有时间。
            if (++cleanupCounter >= 2) {
                cleanupCounter = 0;
                for (auto it = gs.boneCache.begin(); it != gs.boneCache.end(); ) {
                    it->second.age++;
                    if (it->second.age > 10) it = gs.boneCache.erase(it);
                    else ++it;
                }
            }
        }

        DiagSetCounts(kDiagBoneDMA, (int64_t)tmpBones.size(), (int64_t)data->size());
        throttler.sleepUntilNext(std::chrono::milliseconds(5));
    }
    mem.CloseScatter(hScatter);
}

