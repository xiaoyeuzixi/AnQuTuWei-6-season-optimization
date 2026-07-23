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
#include "../core/CoordProbe.h"
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

inline void ThreadActors() {
    VMMDLL_SCATTER_HANDLE hScatter = mem.CreateScatter();
    Throttler throttler;

    struct CharacterCandidate {
        DWORD64 pawn = 0;
        std::string className;
        DWORD64 mesh = 0;
        DWORD64 state = 0;
        int teamId = 0;
    };

    static std::unordered_map<DWORD64, std::string> classCache;
    static std::unordered_map<DWORD64, DWORD64> rootCache;
    static std::unordered_map<DWORD64, FVector> actorPosCache;
    static thread_local std::unordered_set<DWORD64> aliveAddrs;
    static thread_local std::vector<CharacterCandidate> trackedCharacters;
    static thread_local std::vector<PlayerEntry> trackedDrawAllObjects;
    classCache.reserve(1024);
    rootCache.reserve(256);
    actorPosCache.reserve(256);
    trackedCharacters.reserve(128);
    trackedDrawAllObjects.reserve(128);

    auto isValidPos = [](const FVector& v) -> bool {
        auto isCoord = [](float c) -> bool {
            return !std::isnan(c) && !std::isinf(c) &&
                   std::abs(c) > 0.01f && std::abs(c) < 500000.f;
        };
        if (!(isCoord(v.X) && isCoord(v.Y) && isCoord(v.Z) && std::abs(v.Z) < 10000.f))
            return false;
        return std::abs(v.X) > 10.f || std::abs(v.Y) > 10.f;
    };

    static auto lastFullScanTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    static auto lastItemScanTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    static int cacheGC = 0;
    static size_t gcCursor = 0;

    while (Runtime::IsRunning()) {
        try {
            DiagScope diag(kDiagActors);
            if (!gs.world || !gs.base) {
                throttler.sleepUntilNext(std::chrono::milliseconds(100));
                continue;
            }

            static int vfy = 0;
            if (++vfy >= 30) {
                vfy = 0;
                DWORD64 w = mem.Read<DWORD64>(gs.base + BaseWorld);
                if (w && w != gs.world) gs.world = w;
            }

            auto now = std::chrono::steady_clock::now();
            // 玩家坐标仍然每轮高频刷新；但全量 Actor 名称/物资地址枚举不能每 2ms 跑。
            // ★修复: 全量扫描间隔 100ms→50ms, 新丢物资更快被发现
            //   相机已改为6ms节流, DMA 总线有更多余量
            const bool doFullScan = trackedCharacters.empty() ||
                (now - lastFullScanTime >= std::chrono::milliseconds(50));
            const bool doItemScan = g_ScanItems && doFullScan;

            bool rosterRefreshed = false;
            if (doFullScan) {
                DWORD64 pl = mem.Read<DWORD64>(gs.world + Offset_PersistentLevel);
                if (!pl) {
                    throttler.sleepUntilNext(std::chrono::milliseconds(100));
                    continue;
                }

                struct { DWORD64 pls; DWORD cnt; } ch{};
                mem.Read(pl + Offset_LevelActors, &ch, sizeof(ch));
                if (!ch.pls || !ch.cnt || ch.cnt > 5000) {
                    throttler.sleepUntilNext(std::chrono::milliseconds(100));
                    continue;
                }

                DWORD cnt = ch.cnt;
                DWORD64 pls = ch.pls;
                static thread_local std::vector<DWORD64> actors;
                actors.assign(cnt, 0);
                if (!mem.Read(pls, actors.data(), (DWORD)(cnt * 8))) {
                    throttler.sleepUntilNext(std::chrono::milliseconds(100));
                    continue;
                }

                aliveAddrs.clear();
                aliveAddrs.reserve(cnt);

                std::vector<CharacterCandidate> scannedCharacters;
                std::vector<std::pair<DWORD64, std::string>> tmpNearby;
                std::vector<PlayerEntry> tmpDrawAll;
                scannedCharacters.reserve(128);
                if (doItemScan) tmpNearby.reserve(256);
                if (g_DrawAll) tmpDrawAll.reserve(128);

                for (DWORD i = 0; i < cnt; i++) {
                    DWORD64 aa = actors[i];
                    if (!aa || aa < 0x10000) continue;
                    aliveAddrs.insert(aa);

                    std::string className;
                    auto it = classCache.find(aa);
                    if (it != classCache.end()) {
                        className = it->second;
                    } else {
                        int actorId = mem.Read<int>(aa + Offset_UObjectFNameIndex);
                        className = GetNameAnQu(actorId);
                        if (!className.empty()) classCache[aa] = className;
                    }
                    if (className.empty()) continue;

                    if (className.find("Character") != std::string::npos) {
                        if (className.find("Template") == std::string::npos &&
                            className.find("Preview") == std::string::npos) {
                            scannedCharacters.push_back({aa, className});
                        }
                        continue;
                    }

                    if (!doItemScan && !g_DrawAll) continue;
                    if (className.find("Inventory_") != std::string::npos) continue;

                    static thread_local std::string lower;
                    lower.clear();
                    lower.reserve(className.size());
                    for (char c : className)
                        lower += (char)tolower((unsigned char)c);

                    bool isEngineObj =
                        lower.find("light") != std::string::npos ||
                        lower.find("effect") != std::string::npos ||
                        lower.find("particle") != std::string::npos ||
                        lower.find("sound") != std::string::npos ||
                        lower.find("camera") != std::string::npos ||
                        lower.find("trigger") != std::string::npos ||
                        lower.find("volume") != std::string::npos ||
                        lower.find("marker") != std::string::npos ||
                        lower.find("nav") != std::string::npos ||
                        lower.find("path") != std::string::npos ||
                        lower.find("decal") != std::string::npos;
                    if (isEngineObj) continue;

                    if (doItemScan)
                        tmpNearby.push_back({aa, className});
                    if (g_DrawAll)
                        tmpDrawAll.push_back({aa, 0, 0, 0, {}, className});
                }

                if (!scannedCharacters.empty()) {
                    const size_t SCATTER_BATCH = 100;
                    for (size_t base = 0; base < scannedCharacters.size(); base += SCATTER_BATCH) {
                        size_t end = (std::min)(base + SCATTER_BATCH, scannedCharacters.size());
                        for (size_t i = base; i < end; ++i) {
                            mem.AddScatter(hScatter, scannedCharacters[i].pawn + Offset_ActorMesh, &scannedCharacters[i].mesh, 8);
                            mem.AddScatter(hScatter, scannedCharacters[i].pawn + Offset_PlayerState, &scannedCharacters[i].state, 8);
                        }
                        mem.ExecuteScatter(hScatter);
                    }
                    for (size_t base = 0; base < scannedCharacters.size(); base += SCATTER_BATCH) {
                        size_t end = (std::min)(base + SCATTER_BATCH, scannedCharacters.size());
                        for (size_t i = base; i < end; ++i) {
                            if (scannedCharacters[i].state)
                                mem.AddScatter(hScatter, scannedCharacters[i].state + Offset_TeamId, &scannedCharacters[i].teamId, 4);
                        }
                        mem.ExecuteScatter(hScatter);
                    }
                }

                // === AI 调试日志: 必须在 std::move 之前记录, 否则 className 被 move 走后 cls='' ===
                {
                    static uint64_t lastAiLog = 0;
                    uint64_t nowMs2 = GetTickCount64();
                    if (nowMs2 - lastAiLog > 3000) {
                        lastAiLog = nowMs2;
                        // 先计算 tracked 数量 (与下面的过滤逻辑一致)
                        int trackedCount = 0;
                        for (auto& c : scannedCharacters) {
                            if (!(!c.state && c.className.find("AICharacter") == std::string::npos))
                                trackedCount++;
                        }
                        AiDebugLog("=== ThreadActors scan: %d scanned, %d tracked ===",
                                   (int)scannedCharacters.size(), trackedCount);
                        for (size_t i = 0; i < scannedCharacters.size(); i++) {
                            auto& c = scannedCharacters[i];
                            bool isAI = (c.className.find("AICharacter") != std::string::npos);
                            // tracked 判定与下面 trackedCharacters 构建逻辑一致
                            bool tracked = !(!c.state && c.className.find("AICharacter") == std::string::npos);
                            AiDebugLog("  [%d] pawn=%llx cls='%s' isAI=%d state=%llx teamId=%d mesh=%llx tracked=%d",
                                       (int)i, c.pawn, c.className.c_str(), isAI, c.state, c.teamId, c.mesh, tracked);
                        }
                    }
                }

                trackedCharacters.clear();
                trackedCharacters.reserve(scannedCharacters.size());
                for (auto& c : scannedCharacters) {
                    if (!c.state && c.className.find("AICharacter") == std::string::npos)
                        continue;
                    trackedCharacters.push_back(std::move(c));
                }
                trackedDrawAllObjects = std::move(tmpDrawAll);
                lastFullScanTime = now;
                if (doItemScan) lastItemScanTime = now;
                rosterRefreshed = true;

                if (++cacheGC >= 5 && !classCache.empty()) {
                    cacheGC = 0;
                    int cleanBudget = 50;
                    auto it = classCache.begin();
                    std::advance(it, gcCursor % classCache.size());
                    while (it != classCache.end() && cleanBudget > 0) {
                        if (aliveAddrs.count(it->first)) ++it;
                        else { it = classCache.erase(it); cleanBudget--; }
                        gcCursor++;
                    }
                }

                int cleanBudget = 80;
                for (auto it = rootCache.begin(); it != rootCache.end() && cleanBudget > 0; ) {
                    if (aliveAddrs.count(it->first)) ++it;
                    else { it = rootCache.erase(it); cleanBudget--; }
                }
                for (auto it = actorPosCache.begin(); it != actorPosCache.end(); ) {
                    if (aliveAddrs.count(it->first)) ++it;
                    else it = actorPosCache.erase(it);
                }

                if (doItemScan) {
                    auto req = std::make_shared<ItemScanRequest>();
                    req->nearbyActors = std::move(tmpNearby);
                    std::lock_guard<std::shared_mutex> lk(gs.itemReqMutex);
                    gs.itemReq = std::move(req);
                }
            }

            static bool lastScanItems = false;
            if (lastScanItems && !g_ScanItems) {
                {
                    std::lock_guard<std::shared_mutex> lk(gs.nearbyMutex);
                    gs.nearbyItems = std::make_shared<std::vector<NearbyEntry>>();
                }
                {
                    std::lock_guard<std::shared_mutex> lk(gs.itemReqMutex);
                    gs.itemReq = std::make_shared<ItemScanRequest>();
                }
            }
            lastScanItems = g_ScanItems;

            if (trackedCharacters.empty() && trackedDrawAllObjects.empty()) {
                {
                    std::lock_guard<std::shared_mutex> lk(gs.dataMutex);
                    gs.players = std::make_shared<std::vector<PlayerEntry>>();
                }
                {
                    std::lock_guard<std::shared_mutex> lk(gs.drawAllMutex);
                    gs.drawAllObjects = std::make_shared<std::vector<PlayerEntry>>();
                }
                InterlockedExchange(&gs.dirty.playersDirty, 1);
                InterlockedExchange(&gs.dirty.bonesDirty, 1);
                InterlockedExchange(&gs.dirty.infoDirty, 1);
                InterlockedExchange(&gs.dirty.combatDirty, 1);
                throttler.sleepUntilNext(std::chrono::milliseconds(5));
                continue;
            }

            std::vector<PlayerEntry> tmpPlayers;
            tmpPlayers.reserve(trackedCharacters.size());
            for (const auto& c : trackedCharacters)
                tmpPlayers.push_back({c.pawn, c.mesh, c.state, c.teamId, {}, c.className});

            static thread_local std::vector<DWORD64> tmpRC;
            static thread_local std::vector<FVector>  tmpPos;
            static thread_local std::vector<int>      tmpFlags;
            static thread_local std::vector<size_t>   needRC;
            size_t n = tmpPlayers.size();
            tmpRC.assign(n, 0);
            tmpPos.assign(n, FVector{});
            tmpFlags.assign(n, 0);
            needRC.clear();
            needRC.reserve(n);

            for (size_t i = 0; i < n; i++) {
                auto it = rootCache.find(tmpPlayers[i].pawn);
                if (it != rootCache.end()) tmpRC[i] = it->second;
                else needRC.push_back(i);
            }

            if (!needRC.empty()) {
                const size_t SCATTER_BATCH = 200;
                for (size_t base = 0; base < needRC.size(); base += SCATTER_BATCH) {
                    size_t end = (std::min)(base + SCATTER_BATCH, needRC.size());
                    for (size_t bi = base; bi < end; bi++)
                        mem.AddScatter(hScatter, tmpPlayers[needRC[bi]].pawn + Offset_RootComponent, &tmpRC[needRC[bi]], 8);
                    mem.ExecuteScatter(hScatter);
                }
                for (size_t idx : needRC)
                    rootCache[tmpPlayers[idx].pawn] = tmpRC[idx];
            }

            const size_t SCATTER_BATCH = 100;
            for (size_t base = 0; base < n; base += SCATTER_BATCH) {
                size_t end = (std::min)(base + SCATTER_BATCH, n);
                for (size_t i = base; i < end; i++) {
                    if (tmpRC[i]) {
                        mem.AddScatter(hScatter, tmpRC[i] + Offset_ActorLocation, &tmpPos[i], sizeof(FVector));
                        mem.AddScatter(hScatter, tmpRC[i] + Offset_ActorLocationFlags, &tmpFlags[i], sizeof(int));
                    }
                }
                mem.ExecuteScatter(hScatter);
            }

            for (size_t i = 0; i < n; i++) {
                if (tmpRC[i]) {
                    FVector p = ReadActorLocation(tmpRC[i], tmpPlayers[i].pawn);
                    if (isValidPos(p)) {
                        tmpPos[i] = p;
                        actorPosCache[tmpPlayers[i].pawn] = p;
                    } else {
                        // ReadActorLocation failed: zero out position, ThreadBones skips {0,0,0}
                        // Do NOT use actorPosCache fallback — it causes ghost boxes at stale locations
                        tmpPos[i] = {0.f, 0.f, 0.f};
                    }
                }
                tmpPlayers[i].pos = tmpPos[i];
            }

            // Probe every tracked pawn when explicitly enabled. ProbeCoordMemory
            // applies a per-pawn interval, so duplicate coordinates can be
            // compared without flooding the normal refresh loop.
            for (const auto& p : tmpPlayers) {
                const bool isAI = p.clazz.find("AICharacter") != std::string::npos;
                DWORD64 root = 0;
                auto rootIt = rootCache.find(p.pawn);
                if (rootIt != rootCache.end()) root = rootIt->second;
                // AI positions can be invalid when ACE decoding is wrong; still
                // probe its component memory so the plaintext source can be found.
                if (!root || (!isAI && !isValidPos(p.pos))) continue;
                ProbeCoordMemory(isAI ? "AI" : "PLAYER", p.pawn, root, p.pos, p.mesh);
            }

            {
                std::lock_guard<std::shared_mutex> lk(gs.dataMutex);
                gs.players = std::make_shared<std::vector<PlayerEntry>>(std::move(tmpPlayers));
            }

            if (rosterRefreshed) {
                // DrawAll 对象数量可能接近上千，不能在 1ms 位置刷新循环里反复深拷贝 string/vector。
                // 只在全量 Actor 枚举刷新时发布一次，避免 heap 抖动造成画面卡顿。
                for (auto& e : trackedDrawAllObjects) {
                    auto it = actorPosCache.find(e.pawn);
                    if (it != actorPosCache.end()) e.pos = it->second;
                }
                auto drawAllArr = std::make_shared<std::vector<PlayerEntry>>(trackedDrawAllObjects);
                std::lock_guard<std::shared_mutex> lk(gs.drawAllMutex);
                gs.drawAllObjects = std::move(drawAllArr);
            }

            DiagSetCounts(kDiagActors, (int64_t)trackedCharacters.size(),
                          (int64_t)trackedDrawAllObjects.size());
            InterlockedExchange(&gs.dirty.playersDirty, 1);
            InterlockedExchange(&gs.dirty.bonesDirty, 1);
            if (rosterRefreshed) {
                // 只有角色集合/队伍/PlayerState 刷新时才唤醒这些重线程，
                // 位置 1ms 刷新不应每轮都触发 Info / Combat 全量重扫。
                InterlockedExchange(&gs.dirty.infoDirty, 1);
                InterlockedExchange(&gs.dirty.combatDirty, 1);
            }
        } catch (...) {
            DiagBumpError(kDiagActors);
            throttler.sleepUntilNext(std::chrono::milliseconds(100));
        }

        // ★修复: 1ms→2ms 节流, 减少 DMA 总线竞争 (与正常项目一致)
        //   1ms 过于激进, 与相机线程竞争 DMA 导致 BoneDMA/Items 被饿死
        //   全量 Actor 枚举保持 100ms 不变, 位置刷新 2ms 足够保证框体平滑
        throttler.sleepUntilNext(std::chrono::milliseconds(2));
    }
    mem.CloseScatter(hScatter);
}
