#pragma once
/*
 * Throttler.h — 轻量级精确节流器 (i3 4代优化)
 *
 * 参考 PUBG_DMA 项目，用 sleep_until 替代 Sleep(N)
 * Windows 默认计时器粒度 15.6ms，Sleep(8) 实际睡眠 15-16ms
 * 配合 timeBeginPeriod(1) 可精确到 1-2ms
 *
 * ★修复: 改回直接 sleep_until, 不走 Runtime::SleepUntil (condition_variable)
 *   原因: 所有线程共用同一个 g_SleepMutex, 1ms 频率调用 = 每秒千次锁竞争
 */
#include <chrono>
#include <functional>
#include <optional>
#include <thread>

class Throttler {
public:
    // 精确睡眠式节流：不足 interval 时 sleep_until 补足
    void sleepUntilNext(std::chrono::microseconds interval) {
        using clock = std::chrono::high_resolution_clock;
        if (lastTime_.has_value()) {
            auto target = lastTime_.value() + interval;
            auto now = clock::now();
            if (now < target)
                std::this_thread::sleep_until(target);  // 直接系统调用, 零锁开销
        }
        lastTime_ = clock::now();
    }

    void reset() { lastTime_.reset(); }

private:
    std::optional<std::chrono::high_resolution_clock::time_point> lastTime_;
};
