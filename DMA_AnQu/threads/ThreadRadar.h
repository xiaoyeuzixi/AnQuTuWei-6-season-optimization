#pragma once
#include "../Mem.h"
#include "../Offset.h"
#include "../core/Math.h"
#include "../Throttler.h"
#include "../core/Config.h"
#include "../core/GameState.h"
#include "../core/MemUtils.h"
#include "../core/NameResolve.h"
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

// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
inline void ThreadRadar() {
    Throttler throttler;
    bool extractionScanned = false;
    bool radarCleared = false;

    while (Runtime::IsRunning()) {
        if (!gs.world || !gs.base) { throttler.sleepUntilNext(std::chrono::milliseconds(100)); continue; }

        if (!g_ShowRadar && !g_MarkExtraction) {
            extractionScanned = false;
            if (!radarCleared) {
                std::lock_guard<std::shared_mutex> lk(gs.radarMutex);
                gs.radarData.clear();
                radarCleared = true;
            }
            throttler.sleepUntilNext(std::chrono::milliseconds(250));
            continue;
        }
        radarCleared = false;

        if (g_MarkExtraction && !extractionScanned) {
            DWORD64 pl = mem.Read<DWORD64>(gs.world + Offset_PersistentLevel);
            if (pl) {
                struct { DWORD64 pls; DWORD cnt; } ch;
                mem.Read(pl + Offset_LevelActors, &ch, sizeof(ch));
                if (ch.pls && ch.cnt > 0 && ch.cnt < 5000) {
                    static thread_local std::vector<DWORD64> actors;
                    actors.assign(ch.cnt, 0);
                    if (mem.Read(ch.pls, actors.data(), (DWORD)(ch.cnt * 8))) {
                        std::vector<ExtractionPoint> extractions;
                        for (DWORD i = 0; i < ch.cnt; i++) {
                            DWORD64 aa = actors[i];
                            if (!aa || aa < 0x10000) continue;
                            int actorId = mem.Read<int>(aa + Offset_UObjectFNameIndex);
                            std::string cn = GetNameAnQu(actorId);
                            if (ContainsI(cn, "extraction") || ContainsI(cn, "extract")) {
                                DWORD64 root = mem.Read<DWORD64>(aa + Offset_RootComponent);
                                if (root) {
                                    FVector pos = ReadActorLocation(root, aa);
                                    extractions.push_back({pos, cn});
                                }
                            }
                        }
                        { std::lock_guard<std::shared_mutex> lk(gs.extractionMutex); gs.extractions = std::move(extractions); }
                        extractionScanned = true;
                    }
                }
            }
        }
        if (!g_MarkExtraction) extractionScanned = false;

        if (g_ShowRadar) {
            std::shared_ptr<std::vector<WorldEntry>> data;
            { std::shared_lock<std::shared_mutex> lk(gs.screenMutex); data = gs.worldData; }
            std::unordered_map<DWORD64, ThreatEntry> threatData;
            { std::shared_lock<std::shared_mutex> lk(gs.threatMutex); threatData = gs.threatData; }

            if (data && !data->empty()) {
                std::lock_guard<std::shared_mutex> lkW(gs.radarMutex);
                auto& radarMap = gs.radarData;
                static thread_local std::unordered_set<DWORD64> alivePawns;
                alivePawns.clear();
                alivePawns.reserve(data->size());
                for (auto& we : *data) {
                    if (we.pawn == 0 || we.pawn == g_LocalPawn) continue;
                    alivePawns.insert(we.pawn);
                    auto& rt = radarMap[we.pawn];
                    if (rt.pawn != we.pawn) { rt.pawn = we.pawn; rt.trailCount = 0; rt.trailHead = 0; }
                    rt.currentPos = we.worldBot;
                    auto tit = threatData.find(we.pawn);
                    if (tit != threatData.end()) { rt.yaw = tit->second.yaw; rt.isAiming = tit->second.isAiming; }
                    { std::shared_lock<std::shared_mutex> lkC(gs.combatMutex);
                        auto cit = gs.combatData.find(we.pawn);
                        if (cit != gs.combatData.end()) rt.factionType = cit->second.factionType;
                    }
                    if (g_RadarTrail) {
                        rt.trail[rt.trailHead] = we.worldBot;
                        rt.trailHead = (rt.trailHead + 1) % RadarTrailEntry::TRAIL_MAX;
                        if (rt.trailCount < RadarTrailEntry::TRAIL_MAX) rt.trailCount++;
                    }
                }
                for (auto it = radarMap.begin(); it != radarMap.end(); ) {
                    if (alivePawns.count(it->first) == 0) it = radarMap.erase(it);
                    else ++it;
                }
            }
        }

        throttler.sleepUntilNext(std::chrono::milliseconds(50));
    }
}

