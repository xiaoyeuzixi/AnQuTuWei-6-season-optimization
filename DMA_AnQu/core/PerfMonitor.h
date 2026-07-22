#pragma once
/*
 * core/PerfMonitor.h — 性能监控器 (Header-only)
 *
 * 功能:
 *   - CPU 使用率:  GetProcessTimes() + 时间差计算
 *   - 内存占用:   GetProcessMemoryInfo() / psapi.h (WorkingSet + PrivateBytes)
 *   - 帧率统计:   外部调用 TickFrame() 累计
 *
 * 设计:
 *   - 零堆分配: 所有数据成员都是 POD 类型
 *   - 单线程采样: 只在主线程调用 Sample()，不需要锁
 *   - 环形缓冲: 64 个历史样本，用于绘制曲线
 *
 * 用法:
 *   PerfMonitor perf;
 *   perf.Initialize(GetCurrentProcess());
 *   // 每帧:
 *   perf.TickFrame();
 *   // 每 250ms 采样一次:
 *   if (perf.ShouldSample()) perf.Sample();
 *   // 读取:
 *   float cpu = perf.GetCpuPercent();
 *   float memMB = perf.GetMemoryMB();
 *   float fps = perf.GetFps();
 */

#include <Windows.h>
#include <psapi.h>
#include <cstdint>
#include <algorithm>  // std::clamp

#pragma comment(lib, "psapi.lib")

// 历史样本数 (环形缓冲大小)
constexpr size_t kPerfHistorySize = 64;

struct PerfSample {
    float cpuPercent;   // 0.0f ~ 100.0f
    float memoryMB;     // Working Set (MB)
    float fps;          // 瞬时帧率
    float privateMB;    // Private Bytes (MB)
};

class PerfMonitor {
public:
    PerfMonitor() = default;
    ~PerfMonitor() = default;

    // 不可拷贝 (避免句柄复制问题)
    PerfMonitor(const PerfMonitor&) = delete;
    PerfMonitor& operator=(const PerfMonitor&) = delete;

    // 初始化 (传入当前进程句柄)
    void Initialize(HANDLE hProcess) {
        m_hProcess = hProcess;
        m_lastSampleTime = GetTickCount64();
        m_lastKernelTime.QuadPart = 0;
        m_lastUserTime.QuadPart = 0;
        m_sysLastIdle.QuadPart = 0;
        m_sysLastKernel.QuadPart = 0;
        m_sysLastUser.QuadPart = 0;

        // 初始采样
        FILETIME ftCreate, ftExit, ftKernel, ftUser;
        if (GetProcessTimes(m_hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
            m_lastKernelTime.LowPart = ftKernel.dwLowDateTime;
            m_lastKernelTime.HighPart = ftKernel.dwHighDateTime;
            m_lastUserTime.LowPart = ftUser.dwLowDateTime;
            m_lastUserTime.HighPart = ftUser.dwHighDateTime;
        }

        // 系统时间
        FILETIME sysIdle, sysKernel, sysUser;
        if (GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) {
            m_sysLastIdle.LowPart = sysIdle.dwLowDateTime;
            m_sysLastIdle.HighPart = sysIdle.dwHighDateTime;
            m_sysLastKernel.LowPart = sysKernel.dwLowDateTime;
            m_sysLastKernel.HighPart = sysKernel.dwHighDateTime;
            m_sysLastUser.LowPart = sysUser.dwLowDateTime;
            m_sysLastUser.HighPart = sysUser.dwHighDateTime;
        }

        m_frameCount = 0;
        m_fpsLastTime = GetTickCount64();
        m_currentFps = 0.0f;
        m_currentCpu = 0.0f;
        m_currentMemoryMB = 0.0f;
        m_currentPrivateMB = 0.0f;
        m_historyIndex = 0;
        m_historyCount = 0;
        m_sampleIntervalMs = 250;  // 默认 250ms 采样一次
    }

    // 每帧调用 (用于 FPS 计算)
    void TickFrame() {
        m_frameCount++;
        ULONGLONG now = GetTickCount64();
        ULONGLONG dt = now - m_fpsLastTime;
        if (dt >= 1000) {
            m_currentFps = (float)(m_frameCount * 1000.0 / dt);
            m_frameCount = 0;
            m_fpsLastTime = now;
        }
    }

    // 是否应该采样 (按采样间隔)
    bool ShouldSample() const {
        return (GetTickCount64() - m_lastSampleTime) >= m_sampleIntervalMs;
    }

    // 执行一次采样
    void Sample() {
        ULONGLONG now = GetTickCount64();

        // ── CPU 使用率 ──
        FILETIME ftCreate, ftExit, ftKernel, ftUser;
        FILETIME sysIdle, sysKernel, sysUser;

        if (GetProcessTimes(m_hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser) &&
            GetSystemTimes(&sysIdle, &sysKernel, &sysUser))
        {
            ULARGE_INTEGER curKernel, curUser;
            curKernel.LowPart = ftKernel.dwLowDateTime;
            curKernel.HighPart = ftKernel.dwHighDateTime;
            curUser.LowPart = ftUser.dwLowDateTime;
            curUser.HighPart = ftUser.dwHighDateTime;

            ULARGE_INTEGER sysCurIdle, sysCurKernel, sysCurUser;
            sysCurIdle.LowPart = sysIdle.dwLowDateTime;
            sysCurIdle.HighPart = sysIdle.dwHighDateTime;
            sysCurKernel.LowPart = sysKernel.dwLowDateTime;
            sysCurKernel.HighPart = sysKernel.dwHighDateTime;
            sysCurUser.LowPart = sysUser.dwLowDateTime;
            sysCurUser.HighPart = sysUser.dwHighDateTime;

            // 时间差 (100-nanosecond units)
            ULONGLONG procDelta = (curKernel.QuadPart - m_lastKernelTime.QuadPart) +
                                  (curUser.QuadPart - m_lastUserTime.QuadPart);
            ULONGLONG sysDelta = (sysCurKernel.QuadPart - m_sysLastKernel.QuadPart) +
                                 (sysCurUser.QuadPart - m_sysLastUser.QuadPart);

            if (sysDelta > 0) {
                // 单核心百分比 (不除以核心数)
                float cpu = (float)(procDelta * 100.0 / sysDelta);
                // 夹在合理范围内
                m_currentCpu = std::clamp(cpu, 0.0f, 100.0f * GetProcessorCount());
            }

            m_lastKernelTime = curKernel;
            m_lastUserTime = curUser;
            m_sysLastIdle = sysCurIdle;
            m_sysLastKernel = sysCurKernel;
            m_sysLastUser = sysCurUser;
        }

        // ── 内存占用 ──
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(m_hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            m_currentMemoryMB = (float)(pmc.WorkingSetSize / (1024.0 * 1024.0));
            m_currentPrivateMB = (float)(pmc.PrivateUsage / (1024.0 * 1024.0));
        }

        // ── 写入环形缓冲 ──
        m_history[m_historyIndex].cpuPercent = m_currentCpu;
        m_history[m_historyIndex].memoryMB = m_currentMemoryMB;
        m_history[m_historyIndex].fps = m_currentFps;
        m_history[m_historyIndex].privateMB = m_currentPrivateMB;

        m_historyIndex = (m_historyIndex + 1) % kPerfHistorySize;
        if (m_historyCount < kPerfHistorySize) {
            m_historyCount++;
        }

        m_lastSampleTime = now;
    }

    // ── 读取当前值 ──
    float GetCpuPercent()    const { return m_currentCpu; }
    float GetMemoryMB()      const { return m_currentMemoryMB; }
    float GetPrivateMB()     const { return m_currentPrivateMB; }
    float GetFps()           const { return m_currentFps; }

    // ── 历史数据 (用于绘制曲线) ──
    size_t GetHistoryCount() const { return m_historyCount; }
    const PerfSample& GetSample(size_t idx) const {
        // idx 0 = 最旧, GetHistoryCount()-1 = 最新
        if (m_historyCount < kPerfHistorySize) {
            return m_history[idx % kPerfHistorySize];
        } else {
            return m_history[(m_historyIndex + idx) % kPerfHistorySize];
        }
    }

    // 设置采样间隔 (毫秒)
    void SetSampleInterval(UINT ms) { m_sampleIntervalMs = ms; }

private:
    // 获取处理器核心数 (缓存)
    static int GetProcessorCount() {
        static int count = 0;
        if (count == 0) {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            count = si.dwNumberOfProcessors;
        }
        return count;
    }

    HANDLE          m_hProcess = nullptr;

    // CPU 采样
    ULARGE_INTEGER  m_lastKernelTime = {};
    ULARGE_INTEGER  m_lastUserTime = {};
    ULARGE_INTEGER  m_sysLastIdle = {};
    ULARGE_INTEGER  m_sysLastKernel = {};
    ULARGE_INTEGER  m_sysLastUser = {};
    ULONGLONG       m_lastSampleTime = 0;
    UINT            m_sampleIntervalMs = 250;

    // FPS 计算
    ULONGLONG       m_fpsLastTime = 0;
    UINT            m_frameCount = 0;
    float           m_currentFps = 0.0f;

    // 当前值
    float           m_currentCpu = 0.0f;
    float           m_currentMemoryMB = 0.0f;
    float           m_currentPrivateMB = 0.0f;

    // 环形缓冲
    PerfSample      m_history[kPerfHistorySize];
    size_t          m_historyIndex = 0;
    size_t          m_historyCount = 0;
};
