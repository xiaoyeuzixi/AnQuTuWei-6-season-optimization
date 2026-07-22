#pragma once
/*
 * core/PerfDraw.h — 性能曲线图绘制模块
 *
 * 功能:
 *   - CPU 使用率折线图 (0-100%)
 *   - 内存使用折线图 (自适应范围)
 *   - FPS 折线图 (0-240 或自适应)
 *
 * 设计:
 *   - Header-only, inline 实现
 *   - 使用 ImGui ImDrawList 手动绘制，轻量高效
 *   - 每条曲线不同颜色，显示当前值 + 峰值
 *   - 曲线图宽度约 200px，高度约 60px
 *
 * 依赖:
 *   - ImGui/imgui.h
 *   - core/PerfMonitor.h
 *   - HoloUI.h (颜色主题)
 */

#include "../ImGui/imgui.h"
#include "PerfMonitor.h"
#include "../HoloUI.h"
#include <cstdio>
#include <algorithm>

namespace PerfDraw {

enum ChartType {
    CHART_CPU = 0,
    CHART_MEM = 1,
    CHART_FPS = 2
};

inline float GetSampleValue(const PerfSample& s, ChartType type) {
    switch (type) {
        case CHART_CPU: return s.cpuPercent;
        case CHART_MEM: return s.memoryMB;
        case CHART_FPS: return s.fps;
        default: return 0.0f;
    }
}

inline float CalcPeak(const PerfMonitor& perf, ChartType type) {
    float peak = 0.0f;
    size_t count = perf.GetHistoryCount();
    for (size_t i = 0; i < count; i++) {
        float v = GetSampleValue(perf.GetSample(i), type);
        if (v > peak) peak = v;
    }
    return peak;
}

inline float CalcMin(const PerfMonitor& perf, ChartType type) {
    if (perf.GetHistoryCount() == 0) return 0.0f;
    float minVal = GetSampleValue(perf.GetSample(0), type);
    size_t count = perf.GetHistoryCount();
    for (size_t i = 1; i < count; i++) {
        float v = GetSampleValue(perf.GetSample(i), type);
        if (v < minVal) minVal = v;
    }
    return minVal;
}

inline void DrawLineChart(ImDrawList* dl,
                          const ImVec2& pos, float w, float h,
                          const PerfMonitor& perf,
                          ChartType type,
                          float yMin, float yMax,
                          ImU32 lineColor,
                          const char* label,
                          const char* valueFmt,
                          float currentValue)
{
    ImVec2 p1 = pos;
    ImVec2 p2 = ImVec2(pos.x + w, pos.y + h);

    dl->AddRectFilled(p1, p2, Holo::ToU32(Holo::BG_PANEL, 0.6f), 4.0f);
    dl->AddRect(p1, p2, Holo::ToU32(Holo::CYAN_DIM, 0.4f), 4.0f, 0, 1.0f);

    ImU32 gridColor = Holo::ToU32(Holo::CYAN_DIM, 0.15f);
    for (int i = 1; i < 3; i++) {
        float y = p1.y + h * (float)i / 3.0f;
        dl->AddLine(ImVec2(p1.x + 2, y), ImVec2(p2.x - 2, y), gridColor, 1.0f);
    }

    size_t count = perf.GetHistoryCount();
    if (count >= 2) {
        float range = yMax - yMin;
        if (range <= 0.0f) range = 1.0f;

        ImVec2 points[64];
        size_t pointCount = 0;
        float stepX = (w - 8.0f) / (float)(kPerfHistorySize - 1);

        for (size_t i = 0; i < count; i++) {
            float v = GetSampleValue(perf.GetSample(i), type);
            float t = (v - yMin) / range;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            float x = p1.x + 4.0f + (float)i * stepX;
            float y = p2.y - 4.0f - t * (h - 8.0f);
            points[pointCount++] = ImVec2(x, y);
        }

        if (pointCount >= 2) {
            ImVec2 fillPts[66];
            fillPts[0] = ImVec2(points[0].x, p2.y - 4.0f);
            for (size_t i = 0; i < pointCount; i++) {
                fillPts[i + 1] = points[i];
            }
            fillPts[pointCount + 1] = ImVec2(points[pointCount - 1].x, p2.y - 4.0f);

            ImU32 fillColor = (lineColor & 0x00FFFFFF) | (30 << IM_COL32_A_SHIFT);
            dl->AddConvexPolyFilled(fillPts, (int)(pointCount + 2), fillColor);
        }

        dl->AddPolyline(points, (int)pointCount, lineColor, 0, 2.0f);

        if (pointCount > 0) {
            ImVec2 last = points[pointCount - 1];
            dl->AddCircleFilled(last, 3.0f, lineColor);
            dl->AddCircle(last, 5.0f, (lineColor & 0x00FFFFFF) | (80 << IM_COL32_A_SHIFT), 0, 1.5f);
        }
    }

    dl->AddText(ImVec2(p1.x + 6, p1.y + 3),
                Holo::ToU32(Holo::TEXT_DIM, 0.9f),
                label);

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), valueFmt, currentValue);
    ImVec2 valSize = ImGui::CalcTextSize(valBuf);
    dl->AddText(ImVec2(p2.x - valSize.x - 6, p1.y + 3),
                lineColor,
                valBuf);

    float peak = CalcPeak(perf, type);
    char peakBuf[48];
    snprintf(peakBuf, sizeof(peakBuf), "peak: ");
    size_t peakLabelLen = strlen(peakBuf);
    snprintf(peakBuf + peakLabelLen, sizeof(peakBuf) - peakLabelLen, valueFmt, peak);
    ImVec2 peakSize = ImGui::CalcTextSize(peakBuf);
    dl->AddText(ImVec2(p2.x - peakSize.x - 6, p2.y - 14),
                Holo::ToU32(Holo::TEXT_DIM, 0.7f),
                peakBuf);
}

inline void DrawPerfCharts(ImDrawList* dl, const ImVec2& pos, float chartW, float chartH, float spacing) {
    float x = pos.x;
    float y = pos.y;

    DrawLineChart(dl, ImVec2(x, y), chartW, chartH,
                  g_Perf, CHART_CPU,
                  0.0f, 100.0f,
                  Holo::ToU32(Holo::AMBER, 0.9f),
                  "CPU %", "%.1f%%",
                  g_Perf.GetCpuPercent());

    x += chartW + spacing;

    float memMin = CalcMin(g_Perf, CHART_MEM);
    float memMax = CalcPeak(g_Perf, CHART_MEM);
    if (memMax - memMin < 10.0f) {
        memMax = memMin + 10.0f;
    }
    if (memMin > 0) {
        memMin = memMin - 5.0f;
        if (memMin < 0.0f) memMin = 0.0f;
    }
    memMax += 5.0f;

    DrawLineChart(dl, ImVec2(x, y), chartW, chartH,
                  g_Perf, CHART_MEM,
                  memMin, memMax,
                  Holo::ToU32(Holo::GREEN_NEON, 0.9f),
                  "MEM MB", "%.0f",
                  g_Perf.GetMemoryMB());

    x += chartW + spacing;

    float fpsPeak = CalcPeak(g_Perf, CHART_FPS);
    float fpsMax;
    if (fpsPeak > 200.0f) {
        fpsMax = 240.0f;
    } else {
        fpsMax = fpsPeak * 1.2f;
        if (fpsMax < 120.0f) fpsMax = 120.0f;
    }

    DrawLineChart(dl, ImVec2(x, y), chartW, chartH,
                  g_Perf, CHART_FPS,
                  0.0f, fpsMax,
                  Holo::ToU32(Holo::CYAN, 0.9f),
                  "FPS", "%.0f",
                  g_Perf.GetFps());
}

} // namespace PerfDraw
