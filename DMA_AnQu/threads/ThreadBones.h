#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/DiagLog.h"
#include "../core/MemUtils.h"
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
//  绾跨▼: 楠ㄩ涓栫晫鍧愭爣 + WorldEntry 鏋勫缓 (绾疌PU绾跨▼ #2)
//  鍙 gs.boneCache 鏋勫缓 WorldEntry, 闆禗MA, 鏋佷綆寤惰繜
//  1ms 楂橀杞, 淇濊瘉娓叉煋甯х巼
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
inline void ThreadBones() {
    Throttler throttler;
    bool publishedEmpty = false;
    while (Runtime::IsRunning()) {
        DiagScope diag(kDiagBones);
        std::shared_ptr<std::vector<PlayerEntry>> players;
        std::shared_ptr<std::vector<PlayerEntry>> drawAllData;
        { std::shared_lock<std::shared_mutex> lk(gs.dataMutex); players = gs.players; }
        { std::shared_lock<std::shared_mutex> lk(gs.drawAllMutex); drawAllData = gs.drawAllObjects; }
        if (!players || !drawAllData) { throttler.sleepUntilNext(std::chrono::milliseconds(50)); continue; }
        if (players->empty() && drawAllData->empty()) {
            if (!publishedEmpty) {
                std::lock_guard<std::shared_mutex> lk(gs.screenMutex);
                gs.worldData = std::make_shared<std::vector<WorldEntry>>();
                publishedEmpty = true;
            }
            throttler.sleepUntilNext(std::chrono::milliseconds(80));
            continue;
        }
        publishedEmpty = false;

        // 鈹€鈹€ Dirty Flag 妫€娴? 楠ㄩ鏁版嵁鏈彉鍖栨椂璺宠繃涓栫晫鍧愭爣鏋勫缓 鈹€鈹€
        if (gs.dirty.bonesDirty == 0) {
            throttler.sleepUntilNext(std::chrono::milliseconds(16));  // 数据未变化时降频
            continue;
        }

        // 閲嶇疆 dirty 鏍囧織
        InterlockedExchange(&gs.dirty.bonesDirty, 0);

        // 鈹€鈹€ 鏋勫缓涓栫晫鍧愭爣鏁版嵁 (闆?W2S, 闆?info DMA, 绾笘鐣屽潗鏍? 鈹€鈹€
        const bool anySkeleton = (g_ShowSkeleton || g_ShowAISkeleton);
        const bool needBoneCache =
            !g_DrawAll && (g_ShowBox || g_ShowRays || g_ShowDistance || g_ShowName ||
            g_ShowWeapon || g_ShowTeamId || g_ShowArmor || g_ShowSkeleton ||
            g_ShowAISkeleton || g_DrawAI);
        // ★修复: 双缓冲复用, 避免每帧 make_shared 堆分配压力
        static thread_local std::shared_ptr<std::vector<WorldEntry>> worldBuf[2] = {
            std::make_shared<std::vector<WorldEntry>>(),
            std::make_shared<std::vector<WorldEntry>>()
        };
        static thread_local int bufIdx = 0;
        bufIdx = 1 - bufIdx;
        auto& worldArr = worldBuf[bufIdx];
        worldArr->clear();
        worldArr->reserve(players->size() + drawAllData->size());
        int boneReadyCount = 0;
        const uint64_t nowMs = GetTickCount64();
        {
            std::unique_ptr<std::shared_lock<std::shared_mutex>> boneLock;
            if (needBoneCache)
                boneLock = std::make_unique<std::shared_lock<std::shared_mutex>>(gs.boneMutex);
            for (auto& e : *players) {
                if (e.pawn == 0) continue;
                // 鈽呰烦杩囨棤鏁堜綅缃?(NaN/0,0,0) 鈥?涓嶇敓鎴?WorldEntry, 涓嶇粯鍒?
                if (std::isnan(e.pos.X) || std::isnan(e.pos.Y) || std::isnan(e.pos.Z)) {
                    // === AI 调试: NaN 位置跳过 ===
                    if (e.clazz.find("AICharacter") != std::string::npos) {
                        static uint64_t lastNaN = 0;
                        uint64_t nowMs = GetTickCount64();
                        if (nowMs - lastNaN > 3000) { lastNaN = nowMs;
                            AiDebugLog("  [SKIP-NaN] pawn=%llx cls='%s' pos=NaN", e.pawn, e.clazz.c_str());
                        }
                    }
                    continue;
                }
                if (e.pos.X == 0.f && e.pos.Y == 0.f && e.pos.Z == 0.f) {
                    // === AI 调试: 零位置跳过 (ACE解密失败) ===
                    if (e.clazz.find("AICharacter") != std::string::npos) {
                        static uint64_t lastZero = 0;
                        uint64_t nowMs = GetTickCount64();
                        if (nowMs - lastZero > 3000) { lastZero = nowMs;
                            AiDebugLog("  [SKIP-ZeroPos] pawn=%llx cls='%s' state=%llx teamId=%d mesh=%llx",
                                       e.pawn, e.clazz.c_str(), e.state, e.teamId, e.mesh);
                        }
                    }
                    continue;
                }

                WorldEntry we{};
                we.pawn   = e.pawn;
                we.mesh   = e.mesh;
                we.teamId = e.teamId;
                we.isAI   = (e.clazz.find("AICharacter") != std::string::npos);
                we.hasBones = false;
                we.clazz  = e.clazz;  // always set clazz for diagnostics
                // 默认兜底：ActorRoot 在角色中部，使用 ±90 构建框。
                // 如果 BoneDMA 提供了“新鲜 root/head”，下面会用骨骼锚点覆盖，解决 ActorRoot±90 带来的偏框。
                we.worldBot = FVector(e.pos.X, e.pos.Y, e.pos.Z - 90.f);
                we.worldTop = FVector(e.pos.X, e.pos.Y, e.pos.Z + 90.f);

                const bool wantsBones = anySkeleton && (we.isAI ? g_ShowAISkeleton : g_ShowSkeleton);
                if (g_DrawAll || !e.mesh || !needBoneCache) {
                    // 鈽匯ootComponent 鍦ㄨ鑹蹭腑蹇? 涓婁笅鍚?90 = 鑴氬簳鍒板ご椤?
                    // we.clazz already set above
                } else {
                    // 有骨骼缓存：root/head 用于精准方框；worldBones 仅在开启骨骼线且缓存完整时使用。
                    // ★修复: 恢复 samePlace 检查 (阈值放宽到 800cm), 移除 fresh 时间检查
                    //   samePlace 防止重生/传送时用旧骨骼位置画框
                    //   fresh 检查不再需要 (DMA 饥饿已通过相机6ms节流修复)
                    auto it = gs.boneCache.find(e.pawn);
                    if (it != gs.boneCache.end() && it->second.mesh == e.mesh) {
                        auto& bc = it->second;
                        const float dx = std::abs(bc.rootPos.X - e.pos.X);
                        const float dy = std::abs(bc.rootPos.Y - e.pos.Y);
                        const float dz = std::abs(bc.rootPos.Z - e.pos.Z);
                        const bool samePlace = (dx < 200.f && dy < 200.f && dz < 200.f);
                        const bool validHead =
                            !std::isnan(bc.headPos.X) && !std::isnan(bc.headPos.Y) && !std::isnan(bc.headPos.Z) &&
                            (std::abs(bc.headPos.X) > 0.1f || std::abs(bc.headPos.Y) > 0.1f);
                        const bool validRoot =
                            !std::isnan(bc.rootPos.X) && !std::isnan(bc.rootPos.Y) && !std::isnan(bc.rootPos.Z) &&
                            (std::abs(bc.rootPos.X) > 0.1f || std::abs(bc.rootPos.Y) > 0.1f);
                        // Restore bone cache for accurate box position (rootPos=headPos)
                        // Ghost boxes were caused by actorPosCache/playerPosCache fallbacks (now removed),
                        // NOT by bone cache. Active pawn cleanup + age=10 + samePlace=200cm ensure safety.
                        if (samePlace && validRoot && validHead) {
                            // Keep the box on the verified Actor/Root XY anchor.  The
                            // head transform can carry mesh-local rotation (or a
                            // synthetic CTW fallback) and its XY component is not a
                            // stable actor center, which produces a consistent lateral
                            // box shift even when the root location is correct.
                            we.worldBot = FVector(e.pos.X, e.pos.Y, bc.rootPos.Z);
                            we.worldTop = FVector(e.pos.X, e.pos.Y, bc.headPos.Z + 15.f);
                        }
                        if (samePlace && validHead && wantsBones && bc.hasSkeleton) {
                            for (int i = 0; i < 14; i++) we.worldBones[i] = bc.worldBones[i];
                            we.hasBones = true;  // 鈽呮仮澶嶉楠肩粯鍒?
                            boneReadyCount++;
                        }
                    }
                }
                worldArr->push_back(std::move(we));
            }

            for (auto& e : *drawAllData) {
                if (e.pawn == 0) continue;
                if (std::isnan(e.pos.X) || std::isnan(e.pos.Y) || std::isnan(e.pos.Z)) continue;
                if (e.pos.X == 0.f && e.pos.Y == 0.f && e.pos.Z == 0.f) continue;

                WorldEntry we{};
                we.pawn = e.pawn;
                we.mesh = 0;
                we.teamId = 0;
                we.isAI = false;
                we.hasBones = false;
                we.worldBot = FVector(e.pos.X, e.pos.Y, e.pos.Z - 90.f);
                we.worldTop = FVector(e.pos.X, e.pos.Y, e.pos.Z + 90.f);
                we.clazz = e.clazz;
                worldArr->push_back(std::move(we));
            }
        }
        { std::lock_guard<std::shared_mutex> lk(gs.screenMutex); gs.worldData = worldArr; }
        DiagSetCounts(kDiagBones, (int64_t)worldArr->size(), (int64_t)boneReadyCount);

        // DMA 楠ㄩ璇诲彇宸茬Щ鑷?ThreadBoneDMA (鐙珛绾跨▼)
        // ★修复: 1ms→2ms 节流 (与正常项目 ThreadBonesReal 一致)
        //   减少 CPU 空转和锁竞争, 2ms (500fps) 远超渲染需求
        throttler.sleepUntilNext(std::chrono::milliseconds(2));
    }
}

