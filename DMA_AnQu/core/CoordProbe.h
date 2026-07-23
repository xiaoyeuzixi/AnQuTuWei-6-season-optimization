#pragma once

#include "../Mem.h"
#include "../GameMatrix.h"
#include "DiagLog.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

// Runtime coordinate probe. Disabled unless DMA_ANQU_COORD_PROBE=1.
// The probe is intentionally read-only and throttled so it can run alongside
// the normal actor refresh loop without changing game state.
inline void ProbeCoordMemory(const char* kind, DWORD64 pawn, DWORD64 root,
                             const FVector& expected, DWORD64 mesh = 0) {
    static std::unordered_map<DWORD64, uint64_t> lastProbeByPawn;
    static bool enabled = false;
    static bool initialized = false;
    if (!initialized) {
        const char* value = std::getenv("DMA_ANQU_COORD_PROBE");
        enabled = value && value[0] == '1';
        initialized = true;
    }
    const uint64_t now = GetTickCount64();
    if (!enabled || (!root && !pawn && !mesh)) return;
    auto lastIt = lastProbeByPawn.find(pawn);
    if (lastIt != lastProbeByPawn.end() && now - lastIt->second < 2000) return;
    if (lastProbeByPawn.size() > 1024) lastProbeByPawn.clear();
    lastProbeByPawn[pawn] = now;

    constexpr DWORD kWindow = 0x400;
    struct Hit { const char* region; DWORD offset; FVector value; float error; };
    std::vector<Hit> hits;
    const bool expectedFinite = std::isfinite(expected.X) && std::isfinite(expected.Y) &&
                                std::isfinite(expected.Z) &&
                                (std::abs(expected.X) > 10.f || std::abs(expected.Y) > 10.f ||
                                 std::abs(expected.Z) > 10.f);

    auto scanRegion = [&](const char* region, DWORD64 base) {
        if (!base) return;
        std::vector<uint8_t> bytes(kWindow);
        if (!mem.Read(base, bytes.data(), kWindow)) {
            AiDebugLog("[COORD_PROBE] kind=%s %s=%llx read_failed", kind, region,
                       (unsigned long long)base);
            return;
        }
        for (DWORD off = 0; off + sizeof(float) * 3 <= kWindow; off += 4) {
            FVector value{};
            std::memcpy(&value, bytes.data() + off, sizeof(value));
            const bool finite = std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
            const bool bounded = std::abs(value.X) < 500000.f && std::abs(value.Y) < 500000.f &&
                                 std::abs(value.Z) < 10000.f;
            const bool worldScale = std::abs(value.X) > 10.f || std::abs(value.Y) > 10.f ||
                                    std::abs(value.Z) > 10.f;
            if (!finite || !bounded || !worldScale) continue;
            float error = 0.f;
            if (expectedFinite) {
                const float dx = value.X - expected.X;
                const float dy = value.Y - expected.Y;
                const float dz = value.Z - expected.Z;
                error = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (error > 250.f) continue;
            }
            hits.push_back({region, off, value, error});
        }
    };

    scanRegion("root", root);
    scanRegion("pawn", pawn);
    scanRegion("mesh", mesh);
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.error < b.error;
    });

    AiDebugLog("[COORD_PROBE] kind=%s pawn=%llx root=%llx mesh=%llx expected=(%.1f,%.1f,%.1f) expected_valid=%d hits=%zu",
               kind, (unsigned long long)pawn, (unsigned long long)root, (unsigned long long)mesh,
               expected.X, expected.Y, expected.Z, expectedFinite ? 1 : 0, hits.size());
    if (root) {
        FVector v150{}, v170{}, v230{};
        int flags = 0, ctwFlags = 0;
        mem.Read(root + 0x150, &v150, sizeof(v150));
        mem.Read(root + 0x170, &v170, sizeof(v170));
        mem.Read(root + 0x230, &v230, sizeof(v230));
        mem.Read(root + 0x17C, &flags, sizeof(flags));
        mem.Read(root + 0x250, &ctwFlags, sizeof(ctwFlags));
        AiDebugLog("[COORD_PROBE] root_offsets +150=(%.1f,%.1f,%.1f) +170=(%.1f,%.1f,%.1f) +230=(%.1f,%.1f,%.1f) flags17c=0x%08X flags250=0x%08X",
                   v150.X, v150.Y, v150.Z, v170.X, v170.Y, v170.Z,
                   v230.X, v230.Y, v230.Z, (unsigned)flags, (unsigned)ctwFlags);
        const FVector decodedRl = ace_decrypt_relative_location_with_ctl(root, (uint32_t)flags);
        const FVector decodedRootCtw = ace_decrypt_c2w_translation(root);
        const FVector decodedMeshCtw = mesh ? ace_decrypt_c2w_translation(mesh) : FVector{};
        AiDebugLog("[COORD_PROBE] decoded rl=(%.1f,%.1f,%.1f) rootCtw=(%.1f,%.1f,%.1f) meshCtw=(%.1f,%.1f,%.1f)",
                   decodedRl.X, decodedRl.Y, decodedRl.Z,
                   decodedRootCtw.X, decodedRootCtw.Y, decodedRootCtw.Z,
                   decodedMeshCtw.X, decodedMeshCtw.Y, decodedMeshCtw.Z);
        auto logAceMetadata = [&](const char* source, uint32_t ctl) {
            const uint32_t algo = ctl >> 29;
            const uint32_t key = ctl & 0x1FFFFFF;
            const uint32_t bucket = key ? (0x9E3779B1u * key) % 0x10001u : 0;
            ACECacheEntry entry{};
            if (algo != 0 && key) entry = ace_cache_lookup(key);
            AiDebugLog("[COORD_PROBE] ace source=%s algo=%u key=0x%07X bucket=%u node=%llx data=%llx size=%u seed=0x%08X",
                       source, algo, key, bucket,
                       (unsigned long long)entry.node,
                       (unsigned long long)entry.data_ptr,
                       entry.data_size, entry.seed);
        };
        logAceMetadata("rl", (uint32_t)flags);
        logAceMetadata("rootCtw", (uint32_t)ctwFlags);
        if (mesh) {
            const uint32_t meshCtwFlags = mem.Read<uint32_t>(mesh + Offset_RootComponentToWorldFlags);
            logAceMetadata("meshCtw", meshCtwFlags);
        }
    }
    const size_t limit = (std::min)(hits.size(), size_t(16));
    for (size_t i = 0; i < limit; ++i) {
        const auto& hit = hits[i];
        AiDebugLog("[COORD_PROBE]   %s+0x%03X value=(%.1f,%.1f,%.1f) error=%.1f",
                   hit.region, hit.offset, hit.value.X, hit.value.Y, hit.value.Z, hit.error);
    }
}
