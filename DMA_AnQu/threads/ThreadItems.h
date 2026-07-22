#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/MemUtils.h"
#include "../core/DiagLog.h"
#include "../ESPUtils.h"
#include "../ItemMap.h"
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

//  绾跨▼: 鐗╄祫璇诲彇 (鐙珛绾跨▼, 娑堣垂 ThreadActors 鎺ㄩ€佺殑鍦板潃闃熷垪)
//  鍘熸湰鍦?ThreadActors 涓悓姝ユ墽琛? 鐜版媶鍑洪伩鍏嶉樆濉?actor 鎵弿
//  鍙傝€?PUBG_DMA 14+1 绾跨▼鏋舵瀯鐨勭嫭绔?ItemWorker 鎬濊矾
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
inline void ThreadItems() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;
    while (Runtime::IsRunning()) {
        try {
        DiagScope diag(kDiagItems);
        if (!gs.world || !gs.base) { DiagSetCounts(kDiagItems, 0, 0); throttler.sleepUntilNext(std::chrono::milliseconds(100)); continue; }
        static bool s_wasActive = false;
        // 只在完全关闭物资扫描时暂停读取；显示开关不再影响读取节奏。
        // 否则开启显示时需要重新建缓存，会出现一帧一帧跳动/闪烁。
        if (!g_ScanItems) {
            if (s_wasActive) {
                { std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
                  gs.nearbyItems = std::make_shared<std::vector<NearbyEntry>>(); }
                { std::lock_guard<std::shared_mutex> lk(gs.itemCacheMutex);
                  gs.itemPosCache.clear(); }
                s_wasActive = false;
            }
            DiagSetCounts(kDiagItems, 0, 0);
            throttler.sleepUntilNext(std::chrono::milliseconds(250));
            continue;
        }
        s_wasActive = true;

        // 鍙栧嚭鏈€鏂拌姹?(鑻ユ湭鏇存柊鍒欑┖杞? 绛夊緟涓嬩竴杞?
        std::shared_ptr<ItemScanRequest> req;
        {
            std::shared_lock<std::shared_mutex> lk(gs.itemReqMutex);
            req = gs.itemReq;
        }
        if (!req || req->nearbyActors.empty()) {
            DiagSetCounts(kDiagItems, 0, 0);
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }

        // 鈹€鈹€ 鍦伴潰鐗╁搧 鈹€鈹€
        if (!req->nearbyActors.empty()) {
            const auto& tmpNearby = req->nearbyActors;
            // 鈽卼hread_local 澶嶇敤: 閬垮厤姣忚疆 heap alloc
            static thread_local std::vector<DWORD64> nearbyRC;
            static thread_local std::vector<FVector> nearbyPos;
            static thread_local std::vector<int>     nearbyFlags;
            size_t nn = tmpNearby.size();
            nearbyRC.assign(nn, 0);
            nearbyPos.assign(nn, FVector{});
            nearbyFlags.assign(nn, 0);
            // 鈽呭垎鎵筍catter: VMMDLL Scatter 鍗曟鏈€澶х害256鏉＄洰, 瓒呰繃浼氶潤榛樹涪寮?
            //   姣忔壒鏈€澶?00鏉? 閬垮厤鎺ヨ繎涓婇檺
            const size_t SCATTER_BATCH = 200;
            // Phase 1: 鍒嗘壒璇绘墍鏈?RootComponent 鎸囬拡
            for (size_t base = 0; base < nn; base += SCATTER_BATCH) {
                size_t end = (std::min)(base + SCATTER_BATCH, nn);
                for (size_t i = base; i < end; i++)
                    mem.AddScatter(hScatter, tmpNearby[i].first + Offset_RootComponent, &nearbyRC[i], 8);
                mem.ExecuteScatter(hScatter);
            }
            // Phase 2: 鍒嗘壒璇?Location + Flags
            for (size_t base = 0; base < nn; base += SCATTER_BATCH/2) {
                size_t end = (std::min)(base + SCATTER_BATCH/2, nn);
                for (size_t i = base; i < end; i++) {
                    if (nearbyRC[i]) {
                        mem.AddScatter(hScatter, nearbyRC[i] + Offset_ActorLocation, &nearbyPos[i], sizeof(FVector));
                        mem.AddScatter(hScatter, nearbyRC[i] + Offset_ActorLocationFlags, &nearbyFlags[i], sizeof(int));
                    }
                }
                mem.ExecuteScatter(hScatter);
            }

            auto nearbyArr = std::make_shared<std::vector<NearbyEntry>>();
            nearbyArr->reserve(tmpNearby.size());
            auto decryptReq = std::make_shared<ItemDecryptRequest>();
            decryptReq->items.reserve(tmpNearby.size());
            static uint64_t decryptSeq = 0;
            // 鈽呬綅缃紦瀛? ACE鍔ㄦ€佸姞瀵? 涓婁竴甯ц鍒扮殑浣嶇疆涓嬩竴甯у彲鑳借鍔犲瘑
            //   缂撳瓨鏈€杩戞湁鏁堜綅缃? 璇诲彇澶辫触鏃剁敤缂撳瓨閬垮厤闂儊
            static std::unordered_map<DWORD64, FVector> posCache;
            static std::unordered_map<DWORD64, int> rarityCache;
            static int rarityCacheGC = 0;
            static thread_local std::vector<std::pair<DWORD64, FVector>> plainUpdates;
            plainUpdates.clear();
            plainUpdates.reserve(tmpNearby.size());
            for (size_t i = 0; i < tmpNearby.size(); i++) {
                if (!nearbyRC[i]) continue;
                NearbyEntry e;
                int rlEnc = (unsigned int)nearbyFlags[i] >> 29;
                const char* className = tmpNearby[i].second.c_str();
                DWORD64 actorAddr = tmpNearby[i].first;
                e.actor = actorAddr;
                e.root = nearbyRC[i];
                e.flags = (uint32_t)nearbyFlags[i];
                e.className = tmpNearby[i].second;

                auto rit = rarityCache.find(actorAddr);
                if (rit != rarityCache.end()) {
                    e.rarity = rit->second;
                } else {
                    ItemInfo info = GetItemInfo(tmpNearby[i].first);
                    e.rarity = info.rarity;
                    // ★修复: 只缓存有效稀有度 (>0), 不缓存 0
                    if (e.rarity > 0) rarityCache[actorAddr] = e.rarity;
                }
                // ★修复: rarity=0 时赋默认值1 (白色), 不再跳过
                //   手动丢到地上的物资可能 GetItemInfo 读不到稀有度
                //   但位置有效就应该显示, 用默认稀有度1保证可见
                if (e.rarity <= 0) e.rarity = 1;

                if (rlEnc == 0) {
                    e.pos = nearbyPos[i];
                    if (e.pos.X == 1.f && e.pos.Y == 1.f && e.pos.Z == 1.f) {
                        continue;
                    }
                    posCache[actorAddr] = e.pos;
                    plainUpdates.emplace_back(actorAddr, e.pos);
                } else {
                    // 加密物资不在 ThreadItems 内重复 ReadActorLocation。
                    // ThreadItems 已经批量读到 root/flags，只提交给 ThreadEncItems；
                    // 这里仅使用缓存，避免 Items 与 EncItems 对同一批 800+ 物资重复解密。
                    decryptReq->items.push_back(e);
                    auto it = posCache.find(actorAddr);
                    if (it != posCache.end()) {
                        e.pos = it->second;
                    } else {
                        std::shared_lock<std::shared_mutex> lk(gs.itemCacheMutex);
                        auto git = gs.itemPosCache.find(actorAddr);
                        if (git != gs.itemPosCache.end()) {
                            e.pos = git->second;
                            posCache[actorAddr] = e.pos;
                        } else {
                            e.pos = {};
                        }
                    }
                }
                if ((e.pos.X == 0.f && e.pos.Y == 0.f && e.pos.Z == 0.f) ||
                    (e.pos.X == 1.f && e.pos.Y == 1.f && e.pos.Z == 1.f))
                    continue;
                nearbyArr->push_back(std::move(e));
            }
            if (!plainUpdates.empty()) {
                std::lock_guard<std::shared_mutex> lk(gs.itemCacheMutex);
                for (const auto& kv : plainUpdates)
                    gs.itemPosCache[kv.first] = kv.second;
            }
            // 娓呯悊缂撳瓨涓笉瀛樺湪鐨刟ctor (閬垮厤鍐呭瓨澧為暱)
            if (posCache.size() > tmpNearby.size() * 2) {
                std::unordered_map<DWORD64, FVector> newCache;
                for (size_t i = 0; i < tmpNearby.size(); i++) {
                    auto it = posCache.find(tmpNearby[i].first);
                    if (it != posCache.end()) newCache[it->first] = it->second;
                }
                posCache = std::move(newCache);
            }
            // 鈽卹arityCache GC: 瀹氭湡娓呯悊涓嶅瓨鍦ㄧ殑 actor (姣?0甯ф竻鐞嗕竴娆?
            if (++rarityCacheGC >= 30 && rarityCache.size() > tmpNearby.size() * 2) {
                rarityCacheGC = 0;
                std::unordered_map<DWORD64, int> newRarity;
                for (size_t i = 0; i < tmpNearby.size(); i++) {
                    auto it = rarityCache.find(tmpNearby[i].first);
                    if (it != rarityCache.end()) newRarity[it->first] = it->second;
                }
                rarityCache = std::move(newRarity);
            }
            decryptReq->sequence = ++decryptSeq;
            {
                std::lock_guard<std::shared_mutex> lk(gs.itemDecryptMutex);
                gs.itemDecryptReq = decryptReq;
            }
            std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
            gs.nearbyItems = nearbyArr;
            DiagSetCounts(kDiagItems, (int64_t)tmpNearby.size(), (int64_t)nearbyArr->size());
        }

        } catch (...) { DiagBumpError(kDiagItems); throttler.sleepUntilNext(std::chrono::milliseconds(100)); }
        // 对齐调试版稳定节奏: 不限制读取范围/数量，但避免高频 DMA 抢占渲染线程。
        throttler.sleepUntilNext(std::chrono::milliseconds(50));
    }
    mem.CloseScatter(hScatter);
}

