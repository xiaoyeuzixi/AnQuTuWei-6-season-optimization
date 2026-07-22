#pragma once
/*
 * Runtime.h — 运行生命周期与低 CPU 休眠辅助
 *
 * 目标：
 *   1) 所有工作线程共享同一个运行标志，Overlay 退出时能统一收尾。
 *   2) 用 condition_variable 包装 sleep，停止时能立即唤醒，避免 join 卡住。
 *   3) 保持 header-only，降低本轮重构对工程文件的影响。
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Runtime {

inline std::atomic_bool g_Running{false};
inline std::mutex g_SleepMutex;
inline std::condition_variable g_SleepCv;

inline void Start() {
    g_Running.store(true, std::memory_order_release);
}

inline void Stop() {
    g_Running.store(false, std::memory_order_release);
    g_SleepCv.notify_all();
}

inline bool IsRunning() {
    return g_Running.load(std::memory_order_acquire);
}

template <class Rep, class Period>
inline bool SleepFor(const std::chrono::duration<Rep, Period>& duration) {
    if (!IsRunning()) return false;
    std::unique_lock<std::mutex> lk(g_SleepMutex);
    g_SleepCv.wait_for(lk, duration, [] { return !IsRunning(); });
    return IsRunning();
}

template <class Clock, class Duration>
inline bool SleepUntil(const std::chrono::time_point<Clock, Duration>& target) {
    if (!IsRunning()) return false;
    std::unique_lock<std::mutex> lk(g_SleepMutex);
    g_SleepCv.wait_until(lk, target, [] { return !IsRunning(); });
    return IsRunning();
}

} // namespace Runtime
