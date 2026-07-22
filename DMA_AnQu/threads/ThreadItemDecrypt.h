#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/DiagLog.h"
#include "../core/MemUtils.h"
#include "../ESPUtils.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 独立物资坐标/解密线程：
// - ThreadNearbyItems 只发布 actor/root/rarity/className
// - 本线程独占 LocationFlags / Location / ACE 解密 / fallback 坐标读取
// - 明文和加密都在这里处理，避免重复读取 flags/location
// - 对加密物资记录“上次成功路径”，下一轮直接走成功路径，不再每次完整 fallback

inline bool ItemDecValidPtr(DWORD64 p) {
    return p >= 0x10000 && p <= 0x7FFFFFFFFFFF;
}

inline bool ItemDecValidCoord(float v) {
    return !std::isnan(v) && !std::isinf(v) &&
           std::abs(v) > 0.01f && std::abs(v) < 500000.f;
}

inline bool ItemDecValidPos(const FVector& p) {
    if (p.X == 0.f && p.Y == 0.f && p.Z == 0.f) return false;
    if (p.X == 1.f && p.Y == 1.f && p.Z == 1.f) return false;
    return ItemDecValidCoord(p.X) && ItemDecValidCoord(p.Y) &&
           ItemDecValidCoord(p.Z) && std::abs(p.Z) < 10000.f;
}

inline bool ItemDecTryRelativeKnownFlags(DWORD64 root, uint32_t flags, FVector& out) {
    if (!ItemDecValidPtr(root)) return false;
    uint32_t algo = flags >> 29;
    uint32_t key = flags & 0x1FFFFFF;

    if (algo == 0) {
        uint32_t raw = mem.Read<uint32_t>(root + Offset_ActorLocation);
        if (raw == ACE_DeadActorSentinel) return false;
        out = mem.Read<FVector>(root + Offset_ActorLocation);
        return ItemDecValidPos(out);
    }
    if (key == 0) return false;

    ACECacheEntry ce = ace_cache_lookup(key);
    if (!ce.data_ptr || ce.data_size < 12) return false;

    uint32_t d0 = mem.Read<uint32_t>(ce.data_ptr + 0);
    uint32_t d1 = mem.Read<uint32_t>(ce.data_ptr + 4);
    uint32_t d2 = mem.Read<uint32_t>(ce.data_ptr + 8);

    uint32_t v12 = 0x9E3779B1u * key;
    uint32_t v16 = 0x3C6EF372u;
    uint32_t v17 = 0xDAA66D2Bu;
    uint32_t STEP = 0x78DDE6E4u;

    d0 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d1 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d2 ^= ace_mask(v12, ce.seed, v16, v17);

    std::memcpy(&out.X, &d0, 4);
    std::memcpy(&out.Y, &d1, 4);
    std::memcpy(&out.Z, &d2, 4);
    return ItemDecValidPos(out);
}

inline bool ItemDecTryC2W(DWORD64 root, FVector& out) {
    if (!ItemDecValidPtr(root)) return false;

    uint32_t ctl = mem.Read<uint32_t>(root + Offset_RootComponentToWorldFlags);
    uint32_t algo = ctl >> 29;
    uint32_t key = ctl & 0x1FFFFFF;

    if (algo == 0) {
        out = mem.Read<FVector>(root + Offset_RootComponentToWorld + 16);
        return ItemDecValidPos(out);
    }
    if (key == 0) return false;

    ACECacheEntry ce = ace_cache_lookup(key);
    if (!ce.data_ptr || ce.data_size < 48) return false;

    uint32_t d4 = mem.Read<uint32_t>(ce.data_ptr + 16);
    uint32_t d5 = mem.Read<uint32_t>(ce.data_ptr + 20);
    uint32_t d6 = mem.Read<uint32_t>(ce.data_ptr + 24);

    uint32_t v12 = 0x9E3779B1u * key;
    uint32_t STEP = 0x78DDE6E4u;
    uint32_t v16 = 0x3C6EF372u + 4 * STEP;
    uint32_t v17 = 0xDAA66D2Bu + 4 * STEP;

    d4 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d5 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d6 ^= ace_mask(v12, ce.seed, v16, v17);

    std::memcpy(&out.X, &d4, 4);
    std::memcpy(&out.Y, &d5, 4);
    std::memcpy(&out.Z, &d6, 4);
    return ItemDecValidPos(out);
}

inline bool ItemDecTryMeshC2W(DWORD64 actor, FVector& out) {
    if (!ItemDecValidPtr(actor)) return false;
    DWORD64 mesh = mem.Read<DWORD64>(actor + Offset_ActorMesh);
    if (!ItemDecValidPtr(mesh)) return false;
    int meshCtwFlags = mem.Read<int>(mesh + Offset_RootComponentToWorldFlags);
    if (((unsigned int)meshCtwFlags >> 29) != 0) return false;
    out = mem.Read<FVector>(mesh + Offset_RootComponentToWorld + 16);
    return ItemDecValidPos(out);
}

inline bool ItemDecTryRootBounds(DWORD64 root, DWORD64 offset, FVector& out) {
    if (!ItemDecValidPtr(root)) return false;
    out = mem.Read<FVector>(root + offset);
    return ItemDecValidPos(out);
}

inline bool ItemDecTryActorPlainComponent(DWORD64 actor, DWORD64 offset, FVector& out) {
    if (!ItemDecValidPtr(actor)) return false;
    DWORD64 ptr = mem.Read<DWORD64>(actor + offset);
    if (!ItemDecValidPtr(ptr)) return false;
    int pFlags = mem.Read<int>(ptr + Offset_ActorLocationFlags);
    if (((unsigned int)pFlags >> 29) != 0) return false;
    out = mem.Read<FVector>(ptr + Offset_ActorLocation);
    return ItemDecValidPos(out);
}

inline bool ItemDecTryRootPlainComponent(DWORD64 root, DWORD64 offset, FVector& out) {
    if (!ItemDecValidPtr(root)) return false;
    DWORD64 ptr = mem.Read<DWORD64>(root + offset);
    if (!ItemDecValidPtr(ptr)) return false;
    int pFlags = mem.Read<int>(ptr + Offset_ActorLocationFlags);
    if (((unsigned int)pFlags >> 29) != 0) return false;
    out = mem.Read<FVector>(ptr + Offset_ActorLocation);
    return ItemDecValidPos(out);
}

struct ItemDecActorCache {
    FVector lastPos{};
    DWORD64 root = 0;
    uint32_t lastSeen = 0;
    bool hasPos = false;
};

inline void ThreadItemDecrypt() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;

    std::unordered_map<DWORD64, ItemDecActorCache> cache;
    std::unordered_set<DWORD64> alive;
    cache.reserve(1024);
    alive.reserve(1024);

    uint32_t frameId = 1;
    int gcTick = 0;
    bool publishedEmpty = false;
    uint64_t lastProcessedSequence = 0;

    while (Runtime::IsRunning()) {
        try {
            DiagScope diag(kDiagItemDecrypt);
            if (!gs.world || !gs.base || !g_ScanItems) {
                if (!publishedEmpty) {
                    std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
                    gs.nearbyItems = std::make_shared<std::vector<NearbyEntry>>();
                    publishedEmpty = true;
                }
                lastProcessedSequence = 0;
                throttler.sleepUntilNext(std::chrono::milliseconds(100));
                continue;
            }
            publishedEmpty = false;

            std::shared_ptr<ItemDecryptRequest> req;
            {
                std::shared_lock<std::shared_mutex> lk(gs.itemDecryptMutex);
                req = gs.itemDecryptReq;
            }
            if (!req || req->items.empty()) {
                if (!publishedEmpty) {
                    std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
                    gs.nearbyItems = std::make_shared<std::vector<NearbyEntry>>();
                    publishedEmpty = true;
                }
                lastProcessedSequence = 0;
                throttler.sleepUntilNext(std::chrono::milliseconds(30));
                continue;
            }

            // 同一个物资快照只完整解密一次。
            // ThreadNearbyItems 会按固定节奏发布新 sequence；在新 sequence 到来前重复读同一批
            // Location/Flags 只会挤占 DMA 通道，造成相机/角色线程抖动。
            if (req->sequence != 0 && req->sequence == lastProcessedSequence) {
                throttler.sleepUntilNext(std::chrono::milliseconds(2));
                continue;
            }
            lastProcessedSequence = req->sequence;

            if (++frameId == 0) {
                frameId = 1;
                for (auto& kv : cache) kv.second.lastSeen = 0;
            }

            const auto& items = req->items;
            cache.reserve(items.size() * 2 + 64);
            alive.clear();
            alive.reserve(items.size() * 2 + 64);

            auto nearbyArr = std::make_shared<std::vector<NearbyEntry>>();
            nearbyArr->reserve(items.size());

            static thread_local std::vector<size_t> pendingIdx;
            pendingIdx.clear();
            pendingIdx.reserve(items.size());

            for (size_t i = 0; i < items.size(); ++i) {
                const NearbyEntry& src = items[i];
                if (!ItemDecValidPtr(src.actor) || !ItemDecValidPtr(src.root) || src.rarity <= 0) continue;
                alive.insert(src.actor);

                ItemDecActorCache& c = cache[src.actor];
                c.lastSeen = frameId;

                // 已经解出并且 root 没变的物资直接复用上次坐标，
                // 不重复跑 flags/location/decrypt，避免同一批物资反复把 DMA 打爆。
                if (c.hasPos && c.root == src.root) {
                    NearbyEntry e = src;
                    e.pos = c.lastPos;
                    nearbyArr->push_back(std::move(e));
                    continue;
                }

                c.root = src.root;
                pendingIdx.push_back(i);
            }

            if (pendingIdx.empty()) {
                {
                    std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
                    gs.nearbyItems = nearbyArr;
                }
                DiagSetCounts(kDiagItemDecrypt, (int64_t)items.size(), (int64_t)nearbyArr->size());
                InterlockedExchange(&gs.dirty.itemsDirty, 1);
                throttler.sleepUntilNext(std::chrono::milliseconds(6));
                continue;
            }

            static thread_local std::vector<FVector> plainPos;
            static thread_local std::vector<uint32_t> flags;
            plainPos.assign(items.size(), FVector{});
            flags.assign(items.size(), 0);

            const size_t POS_BATCH = 100; // 每物资 2 条读取，100=200条，稳定低延迟
            for (size_t base = 0; base < pendingIdx.size(); base += POS_BATCH) {
                size_t end = (std::min)(base + POS_BATCH, pendingIdx.size());
                for (size_t k = base; k < end; ++k) {
                    const size_t i = pendingIdx[k];
                    if (!ItemDecValidPtr(items[i].root)) continue;
                    mem.AddScatter(hScatter, items[i].root + Offset_ActorLocation, &plainPos[i], sizeof(FVector));
                    mem.AddScatter(hScatter, items[i].root + Offset_ActorLocationFlags, &flags[i], sizeof(uint32_t));
                }
                mem.ExecuteScatter(hScatter);
            }

            static thread_local std::vector<std::pair<DWORD64, FVector>> updates;
            updates.clear();
            updates.reserve(pendingIdx.size());

            for (size_t k = 0; k < pendingIdx.size(); ++k) {
                const size_t i = pendingIdx[k];
                const NearbyEntry& src = items[i];
                ItemDecActorCache& c = cache[src.actor];

                FVector pos{};
                bool ok = false;
                const uint32_t enc = (flags[i] >> 29);

                if (enc == 0) {
                    pos = plainPos[i];
                    ok = ItemDecValidPos(pos);
                } else {
                    // 这里改回旧版的轻量路径：
                    // 明文走 scatter，密文只走一次 ReadActorLocation，
                    // 不再为每个物资做多层 fallback 探测，否则 300+ 物资会把 DMA 通道打爆。
                    pos = ReadActorLocation(src.root, src.actor);
                    ok = ItemDecValidPos(pos);
                }

                if (ok) {
                    c.lastPos = pos;
                    c.hasPos = true;
                    updates.push_back({src.actor, pos});
                } else if (c.hasPos) {
                    pos = c.lastPos; // 解密偶发失败时保留上一帧坐标，避免闪烁/跳贴
                    ok = true;
                }

                if (!ok || !ItemDecValidPos(pos)) continue;

                NearbyEntry e = src;
                e.pos = pos;
                e.flags = flags[i];
                nearbyArr->push_back(std::move(e));
            }

            {
                std::lock_guard<std::shared_mutex> lk(gs.itemCacheMutex);
                gs.itemPosCache.reserve(updates.size() * 2 + 64);
                for (const auto& kv : updates) {
                    gs.itemPosCache[kv.first] = kv.second;
                }

                if (++gcTick >= 120 || gs.itemPosCache.size() > items.size() * 3 + 256) {
                    gcTick = 0;
                    for (auto it = gs.itemPosCache.begin(); it != gs.itemPosCache.end(); ) {
                        if (alive.count(it->first)) ++it;
                        else it = gs.itemPosCache.erase(it);
                    }
                }
            }

            {
                std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
                gs.nearbyItems = nearbyArr;
            }
            DiagSetCounts(kDiagItemDecrypt, (int64_t)items.size(), (int64_t)nearbyArr->size());
            InterlockedExchange(&gs.dirty.itemsDirty, 1);

            if (gcTick == 0 || cache.size() > items.size() * 3 + 256) {
                for (auto it = cache.begin(); it != cache.end(); ) {
                    if (it->second.lastSeen == frameId) ++it;
                    else it = cache.erase(it);
                }
            }
        } catch (...) {
            DiagBumpError(kDiagItemDecrypt);
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
        }

        // 专用解密线程保持高频，但不能 1ms 空转重复读同一批物资：
        // DMA 通道被物资解密打满后，相机/骨骼读不到最新数据，反而会更卡。
        // 6ms 约 166Hz，高于 120FPS，不限制物资数量/距离，只做总线节奏控制。
        throttler.sleepUntilNext(std::chrono::milliseconds(6));
    }

    mem.CloseScatter(hScatter);
}
