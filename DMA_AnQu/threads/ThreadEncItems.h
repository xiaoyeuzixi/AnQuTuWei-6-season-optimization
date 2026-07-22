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
//  绾跨▼: 鍔犲瘑鐗╄祫浣嶇疆璇诲彇 (鐙珛绾跨▼, 甯︾紦瀛?
//  涓撻棬澶勭悊 RL 鍔犲瘑鐨勭墿璧? 鐢ㄦ寚閽堣拷韪?Bounds鍥為€€璇讳綅缃?
//  璇诲埌鐨勪綅缃啓鍥?gs.nearbyItems, 閬垮厤闃诲 ThreadItems
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
// (gs.itemPosCache / gs.itemCacheMutex 宸茬Щ鑷冲叏灞€澹版槑鍖? 瑙?L710)

inline void ThreadEncItems() {
    Throttler throttler;
    while (Runtime::IsRunning()) {
        try {
        DiagScope diag(kDiagEncItems);
        if (!gs.world || !gs.base) { DiagSetCounts(kDiagEncItems, 0, 0); throttler.sleepUntilNext(std::chrono::milliseconds(100)); continue; }
        // 加密物资位置读取只跟随 g_ScanItems，不跟随显示开关，避免显示开启后缓存重建造成卡顿。
        if (!g_ScanItems) {
            DiagSetCounts(kDiagEncItems, 0, 0);
            throttler.sleepUntilNext(std::chrono::milliseconds(250));
            continue;
        }

        // ThreadItems 已经批量读好加密物资的 actor/root/flags；
        // EncItems 只消费这份请求，不再重新遍历 ItemScanRequest 读 root/flags。
        std::shared_ptr<ItemDecryptRequest> req;
        { std::shared_lock<std::shared_mutex> lk(gs.itemDecryptMutex); req = gs.itemDecryptReq; }
        if (!req || req->items.empty()) {
            DiagSetCounts(kDiagEncItems, 0, 0);
            throttler.sleepUntilNext(std::chrono::milliseconds(30));
            continue;
        }

        auto isCoord = [](float v) -> bool {
            return !std::isnan(v) && !std::isinf(v) &&
                   std::abs(v) > 0.01f && std::abs(v) < 500000.f;
        };

        // 鍙鐞嗗姞瀵嗙殑actor
        bool anyUpdated = false;
        // 鈽卼hread_local 澶嶇敤: 閬垮厤姣忚疆 heap alloc
        static thread_local std::unordered_map<DWORD64, FVector> updates;
        static thread_local uint64_t lastSeq = 0;
        static thread_local size_t cursor = 0;
        updates.clear();

        if (lastSeq != req->sequence) {
            lastSeq = req->sequence;
            if (cursor >= req->items.size()) cursor = 0;
        }

        int processed = 0;
        const size_t n = req->items.size();
        // ★修复: 每轮处理上限 48→100, 预算 6ms→8ms
        //   加密物资更快刷新, 新丢物资不会等太久
        constexpr int kMaxPerLoop = 100;
        constexpr int kBudgetUs = 8000;
        const auto loopStart = std::chrono::steady_clock::now();

        for (size_t scanned = 0; scanned < n && processed < kMaxPerLoop; ++scanned) {
            const size_t idx = (cursor + scanned) % n;
            const NearbyEntry& src = req->items[idx];
            DWORD64 actorAddr = src.actor;
            DWORD64 root = src.root;
            if (!actorAddr || !root) continue;
            processed++;

            FVector p = ReadActorLocationKnownFlags(root, actorAddr, src.flags);
            // 鈽呰繃婊?(1,1,1) 浣嶇疆 鈥?闄勭潃鍦ㄨ鑹茶韩涓婄殑閰嶄欢
            if (p.X == 1.f && p.Y == 1.f && p.Z == 1.f) continue;
            if (isCoord(p.X) && isCoord(p.Y) && isCoord(p.Z) && std::abs(p.Z) < 10000.f) {
                updates[actorAddr] = p;
                anyUpdated = true;
            }

            auto usedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - loopStart).count();
            if (usedUs >= kBudgetUs) {
                cursor = (idx + 1) % n;
                break;
            }
            cursor = (idx + 1) % n;
        }

        if (anyUpdated) {
            // 鏇存柊缂撳瓨
            { std::lock_guard<std::shared_mutex> lk(gs.itemCacheMutex);
              for (auto& [k, v] : updates) gs.itemPosCache[k] = v; }
            // 鈽呬笉鍐嶉噸寤哄垪琛?鈥?ThreadItems 涓嬩竴甯т細浠?gs.itemPosCache 璇诲彇鏈€鏂颁綅缃?
        }
        DiagSetCounts(kDiagEncItems, (int64_t)processed, (int64_t)updates.size());
        } catch (...) {
            DiagBumpError(kDiagEncItems);
            throttler.sleepUntilNext(std::chrono::milliseconds(100));
        }
        throttler.sleepUntilNext(std::chrono::milliseconds(30));
    }
}

