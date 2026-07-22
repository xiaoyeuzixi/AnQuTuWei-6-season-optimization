#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/DiagLog.h"
#include "../ItemMap.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 周围物资元数据线程：
// - 只负责 actor → root/cdc/rarity/className 的缓存和请求发布
// - 不再读取 Location / Flags / 解密坐标，避免和解密线程重复读取
// - 位置刷新全部交给 ThreadItemDecrypt，渲染线程永远读取完整快照

inline bool NearbyMetaValidPtr(DWORD64 p) {
    return p >= 0x10000 && p <= 0x7FFFFFFFFFFF;
}

inline void NearbyBuildDisplayName(const std::string& className,
                                   std::string& displayName,
                                   bool& labelSkip) {
    auto itName = kItemNameMap.find(className);
    if (itName != kItemNameMap.end()) {
        displayName = itName->second;
    } else {
        static const char* const prefixes[] = {
            "BP_Item_", "Pickup_", "Loot_", "Dropped_", "GroundItem_",
            "Supply_", "Ammo_", "Weapon_", "Armor_", "Medical_",
            "Item_", "Equipment_", "Attachment_", "Consumable_", "Container_"
        };
        static const size_t prefixLens[] = {8,8,5,9,12,7,5,7,6,8,5,11,11,11,9};

        const char* src = className.c_str();
        size_t srcLen = className.size();
        size_t startOff = 0;
        for (int pi = 0; pi < 15; ++pi) {
            if (srcLen >= prefixLens[pi] && std::strncmp(src, prefixes[pi], prefixLens[pi]) == 0) {
                startOff = prefixLens[pi];
                break;
            }
        }
        size_t copyLen = srcLen - startOff;
        if (copyLen > 2 && src[startOff + copyLen - 2] == '_')
            copyLen -= 2;
        displayName.assign(src + startOff, copyLen);
        for (char& ch : displayName)
            if (ch == '_') ch = ' ';
    }

    labelSkip = displayName.empty() ||
                displayName == u8"口袋" ||
                displayName == u8"空手";
}

inline void ThreadNearbyItems() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;

    struct ItemMetaCache {
        DWORD64 root = 0;
        DWORD64 cdc = 0;
        std::string className;
        std::string displayName;
        int rarity = 0;
        uint32_t lastSeen = 0;
        uint8_t cdcMisses = 0;
        bool rarityResolved = false;
        bool labelSkip = false;
    };

    std::unordered_map<DWORD64, ItemMetaCache> cache;
    std::unordered_set<DWORD64> alive;
    cache.reserve(1024);
    alive.reserve(1024);

    uint32_t frameId = 1;
    uint64_t sequence = 1;
    int gcTick = 0;
    bool wasActive = false;

    auto publishEmpty = [&]() {
        {
            std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
            gs.nearbyItems = std::make_shared<std::vector<NearbyEntry>>();
        }
        {
            std::lock_guard<std::shared_mutex> lk(gs.itemCacheMutex);
            gs.itemPosCache.clear();
        }
        {
            std::lock_guard<std::shared_mutex> lk(gs.itemDecryptMutex);
            gs.itemDecryptReq = std::make_shared<ItemDecryptRequest>();
        }
    };

    while (Runtime::IsRunning()) {
        try {
            DiagScope diag(kDiagNearbyMeta);
            if (!gs.world || !gs.base) {
                throttler.sleepUntilNext(std::chrono::milliseconds(100));
                continue;
            }

            if (!g_ScanItems) {
                if (wasActive) {
                    publishEmpty();
                    cache.clear();
                    alive.clear();
                    wasActive = false;
                }
                throttler.sleepUntilNext(std::chrono::milliseconds(250));
                continue;
            }
            wasActive = true;

            std::shared_ptr<ItemScanRequest> req;
            {
                std::shared_lock<std::shared_mutex> lk(gs.itemReqMutex);
                req = gs.itemReq;
            }
            if (!req || req->nearbyActors.empty()) {
                publishEmpty();
                throttler.sleepUntilNext(std::chrono::milliseconds(50));
                continue;
            }

            if (++frameId == 0) {
                frameId = 1;
                for (auto& kv : cache) kv.second.lastSeen = 0;
            }

            const auto& src = req->nearbyActors;
            cache.reserve(src.size() * 2 + 64);
            alive.clear();
            alive.reserve(src.size() * 2 + 64);

            static thread_local std::vector<DWORD64> actors;
            actors.clear();
            actors.reserve(src.size());

            for (const auto& it : src) {
                DWORD64 actor = it.first;
                if (!NearbyMetaValidPtr(actor)) continue;
                if (!alive.insert(actor).second) continue; // 去重，同一 actor 只处理一次

                ItemMetaCache& c = cache[actor];
                if (c.className != it.second) {
                    c.className = it.second;
                    c.displayName.clear();
                    c.labelSkip = false;
                    c.root = 0;
                    c.cdc = 0;
                    c.rarity = 0;
                    c.cdcMisses = 0;
                    c.rarityResolved = false;
                }
                c.lastSeen = frameId;
                actors.push_back(actor);
            }

            if (actors.empty()) {
                publishEmpty();
                throttler.sleepUntilNext(std::chrono::milliseconds(50));
                continue;
            }

            // Phase 1: 只给新 actor 读取 RootComponent
            static thread_local std::vector<DWORD64> needRoot;
            needRoot.clear();
            needRoot.reserve(actors.size());
            for (DWORD64 actor : actors) {
                ItemMetaCache& c = cache[actor];
                if (!NearbyMetaValidPtr(c.root)) {
                    c.root = 0;
                    needRoot.push_back(actor);
                }
            }

            const size_t ROOT_BATCH = 200;
            for (size_t base = 0; base < needRoot.size(); base += ROOT_BATCH) {
                size_t end = (std::min)(base + ROOT_BATCH, needRoot.size());
                for (size_t i = base; i < end; ++i) {
                    DWORD64 actor = needRoot[i];
                    mem.AddScatter(hScatter, actor + Offset_RootComponent, &cache[actor].root, 8);
                }
                mem.ExecuteScatter(hScatter);
            }
            for (DWORD64 actor : needRoot) {
                ItemMetaCache& c = cache[actor];
                if (!NearbyMetaValidPtr(c.root)) c.root = 0;
            }

            // Phase 2: rarity 只解析一次，批量 CDC + Rarity
            static thread_local std::vector<DWORD64> needCdc;
            static thread_local std::vector<DWORD64> needRarity;
            needCdc.clear();
            needRarity.clear();
            needCdc.reserve(actors.size());
            needRarity.reserve(actors.size());

            for (DWORD64 actor : actors) {
                ItemMetaCache& c = cache[actor];
                if (!c.rarityResolved) {
                    if (!NearbyMetaValidPtr(c.cdc)) {
                        c.cdc = 0;
                        needCdc.push_back(actor);
                    } else {
                        needRarity.push_back(actor);
                    }
                }
            }

            const size_t CDC_BATCH = 200;
            for (size_t base = 0; base < needCdc.size(); base += CDC_BATCH) {
                size_t end = (std::min)(base + CDC_BATCH, needCdc.size());
                for (size_t i = base; i < end; ++i) {
                    DWORD64 actor = needCdc[i];
                    mem.AddScatter(hScatter, actor + Offset_CommonDataComponent, &cache[actor].cdc, 8);
                }
                mem.ExecuteScatter(hScatter);
            }

            for (DWORD64 actor : needCdc) {
                ItemMetaCache& c = cache[actor];
                if (NearbyMetaValidPtr(c.cdc)) {
                    needRarity.push_back(actor);
                } else {
                    c.cdc = 0;
                    if (++c.cdcMisses >= 3) {
                        c.rarity = 0;
                        c.rarityResolved = true;
                    }
                }
            }

            const size_t RARITY_BATCH = 200;
            for (size_t base = 0; base < needRarity.size(); base += RARITY_BATCH) {
                size_t end = (std::min)(base + RARITY_BATCH, needRarity.size());
                for (size_t i = base; i < end; ++i) {
                    DWORD64 actor = needRarity[i];
                    ItemMetaCache& c = cache[actor];
                    if (NearbyMetaValidPtr(c.cdc))
                        mem.AddScatter(hScatter, c.cdc + Offset_Rarity, &c.rarity, sizeof(int));
                }
                mem.ExecuteScatter(hScatter);
            }

            for (DWORD64 actor : needRarity) {
                ItemMetaCache& c = cache[actor];
                if (c.rarity < 0 || c.rarity > 7) c.rarity = 0;
                c.rarityResolved = true;
            }

            auto decReq = std::make_shared<ItemDecryptRequest>();
            decReq->items.reserve(actors.size());
            decReq->sequence = sequence++;
            for (DWORD64 actor : actors) {
                ItemMetaCache& c = cache[actor];
                if (c.rarity <= 0 || !NearbyMetaValidPtr(c.root)) continue;
                if (c.displayName.empty()) {
                    NearbyBuildDisplayName(c.className, c.displayName, c.labelSkip);
                }
                if (c.labelSkip) continue;

                NearbyEntry e;
                e.actor = actor;
                e.root = c.root;
                e.className = c.className;
                e.displayName = c.displayName;
                e.rarity = c.rarity;
                e.labelSkip = c.labelSkip;
                decReq->items.push_back(std::move(e));
            }

            {
                std::lock_guard<std::shared_mutex> lk(gs.itemDecryptMutex);
                gs.itemDecryptReq = decReq;
            }
            DiagSetCounts(kDiagNearbyMeta, (int64_t)actors.size(), (int64_t)decReq->items.size());

            if (++gcTick >= 30 || cache.size() > actors.size() * 3 + 256) {
                gcTick = 0;
                for (auto it = cache.begin(); it != cache.end(); ) {
                    if (it->second.lastSeen == frameId) ++it;
                    else it = cache.erase(it);
                }
            }
        } catch (...) {
            DiagBumpError(kDiagNearbyMeta);
            throttler.sleepUntilNext(std::chrono::milliseconds(100));
        }

        // 元数据不需要高频，位置/解密由 ThreadItemDecrypt 高频处理。
        throttler.sleepUntilNext(std::chrono::milliseconds(33));
    }

    mem.CloseScatter(hScatter);
}
