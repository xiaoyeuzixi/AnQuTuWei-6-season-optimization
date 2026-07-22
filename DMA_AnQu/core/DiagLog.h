#pragma once

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <mutex>

enum DiagThreadId : int {
    kDiagOverlay = 0,
    kDiagCamera,
    kDiagActors,
    kDiagBones,
    kDiagBoneDMA,
    kDiagInfo,
    kDiagItems,
    kDiagEncItems,
    kDiagCount
};

struct DiagThreadStats {
    std::atomic<uint64_t> loops{0};
    std::atomic<uint64_t> totalUs{0};
    std::atomic<uint64_t> maxUs{0};
    std::atomic<uint64_t> lastUs{0};
    std::atomic<int64_t>  countA{0};
    std::atomic<int64_t>  countB{0};
    std::atomic<uint64_t> errors{0};
};

inline DiagThreadStats g_DiagStats[kDiagCount];
inline std::mutex g_DiagFileMutex;
inline FILE* g_DiagFile = nullptr;
inline char g_DiagPath[MAX_PATH] = {};

inline const char* DiagThreadName(DiagThreadId id) {
    switch (id) {
    case kDiagOverlay:    return "Overlay";
    case kDiagCamera:     return "Camera";
    case kDiagActors:     return "Actors";
    case kDiagBones:      return "Bones";
    case kDiagBoneDMA:    return "BoneDMA";
    case kDiagInfo:       return "Info";
    case kDiagItems:      return "Items";
    case kDiagEncItems:   return "EncItems";
    default:              return "Unknown";
    }
}

class DiagScope {
public:
    explicit DiagScope(DiagThreadId id)
        : id_(id), start_(std::chrono::steady_clock::now()) {}

    ~DiagScope() {
        const uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_).count();
        auto& s = g_DiagStats[(int)id_];
        s.loops.fetch_add(1, std::memory_order_relaxed);
        s.totalUs.fetch_add(us, std::memory_order_relaxed);
        s.lastUs.store(us, std::memory_order_relaxed);

        uint64_t oldMax = s.maxUs.load(std::memory_order_relaxed);
        while (us > oldMax &&
               !s.maxUs.compare_exchange_weak(oldMax, us, std::memory_order_relaxed)) {
        }
    }

private:
    DiagThreadId id_;
    std::chrono::steady_clock::time_point start_;
};

inline void DiagSetCounts(DiagThreadId id, int64_t a, int64_t b = 0) {
    auto& s = g_DiagStats[(int)id];
    s.countA.store(a, std::memory_order_relaxed);
    s.countB.store(b, std::memory_order_relaxed);
}

inline void DiagBumpError(DiagThreadId id) {
    g_DiagStats[(int)id].errors.fetch_add(1, std::memory_order_relaxed);
}

inline void DiagInitFile() {
    std::lock_guard<std::mutex> lk(g_DiagFileMutex);
    if (g_DiagFile) return;

    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    char* slash = strrchr(modulePath, '\\');
    if (slash) *(slash + 1) = '\0';
    snprintf(g_DiagPath, MAX_PATH, "%sperf_thread.log", modulePath);

    fopen_s(&g_DiagFile, g_DiagPath, "w");
    if (!g_DiagFile) return;

    setvbuf(g_DiagFile, nullptr, _IONBF, 0);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(g_DiagFile,
        "=== Perf Thread Log Start %04d-%02d-%02d %02d:%02d:%02d ===\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

inline void DiagShutdownFile() {
    std::lock_guard<std::mutex> lk(g_DiagFileMutex);
    if (!g_DiagFile) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(g_DiagFile,
        "=== Perf Thread Log End %04d-%02d-%02d %02d:%02d:%02d ===\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    fclose(g_DiagFile);
    g_DiagFile = nullptr;
}

inline void DiagWriteSnapshot(float fps, float cpu, float memMb,
                              int playerCount, int worldCount, int nearbyCount, int boneCacheCount,
                              int itemReqCount, bool overlayVisible, bool scanItems) {
    std::lock_guard<std::mutex> lk(g_DiagFileMutex);
    if (!g_DiagFile) return;

    static uint64_t prevLoops[kDiagCount] = {};
    static uint64_t prevTotalUs[kDiagCount] = {};
    static uint64_t prevErrors[kDiagCount] = {};

    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(g_DiagFile,
        "[%02d:%02d:%02d] fps=%.1f cpu=%.1f mem=%.1fMB players=%d world=%d nearby=%d boneCache=%d itemReq=%d overlay=%d scanItems=%d\n",
        st.wHour, st.wMinute, st.wSecond, fps, cpu, memMb,
        playerCount, worldCount, nearbyCount, boneCacheCount, itemReqCount,
        overlayVisible ? 1 : 0, scanItems ? 1 : 0);

    for (int i = 0; i < kDiagCount; ++i) {
        auto& s = g_DiagStats[i];
        uint64_t loops = s.loops.load(std::memory_order_relaxed);
        uint64_t totalUs = s.totalUs.load(std::memory_order_relaxed);
        uint64_t lastUs = s.lastUs.load(std::memory_order_relaxed);
        uint64_t maxUs = s.maxUs.load(std::memory_order_relaxed);
        uint64_t errors = s.errors.load(std::memory_order_relaxed);
        int64_t countA = s.countA.load(std::memory_order_relaxed);
        int64_t countB = s.countB.load(std::memory_order_relaxed);

        uint64_t dLoops = loops - prevLoops[i];
        uint64_t dTotalUs = totalUs - prevTotalUs[i];
        uint64_t dErrors = errors - prevErrors[i];
        double avgMs = dLoops ? (double)dTotalUs / (double)dLoops / 1000.0 : 0.0;

        prevLoops[i] = loops;
        prevTotalUs[i] = totalUs;
        prevErrors[i] = errors;

        fprintf(g_DiagFile,
            "  %-11s loops=%5llu avg=%7.3fms last=%7.3fms max=%7.3fms a=%lld b=%lld err+%llu\n",
            DiagThreadName((DiagThreadId)i),
            (unsigned long long)dLoops,
            avgMs,
            (double)lastUs / 1000.0,
            (double)maxUs / 1000.0,
            (long long)countA,
            (long long)countB,
            (unsigned long long)dErrors);
    }
    fprintf(g_DiagFile, "\n");
}
