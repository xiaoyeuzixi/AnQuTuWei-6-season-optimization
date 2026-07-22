#pragma once
/*
 * HoloUI.h — Dark Purple Tech UI Toolkit for Dear ImGui
 *
 * Provides custom drawing utilities and widgets for a unified dark purple
 * technology aesthetic. All rendering is done through ImGui's ImDrawList API.
 *
 * Palette: Deep purple-black backgrounds (#0d0d1a / #121026),
 *          purple primary scale (#1a0a2e → #2d1b4e → #4a2d7a → #6b3fa0),
 *          violet accents (#7c3aed / #a78bfa / #c4b5fd).
 *
 * Features:
 *   - Neon glow effects (purple layered glow)
 *   - Dynamic scanline / grid / circuit-pattern background textures
 *   - Pulse ring and radar sweep animations
 *   - Frosted glass panels with corner brackets
 *   - Custom styled buttons, checkboxes, separators, headers
 *   - Time-based smooth animations (lerp, pulse, ease-in-out)
 *
 * Usage:
 *   #include "HoloUI.h"
 *   // In your render function:
 *   auto* dl = ImGui::GetWindowDrawList();
 *   HoloPanel(dl, p1, p2, "TITLE");
 *   if (HoloButton("Click Me", ImVec2(120, 32))) { ... }
 */

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Windows.h defines min/max macros that break std::min/std::max.
// Use NOMINOMIN guard or manual clamp to avoid the conflict.
#ifndef HoloMin
#define HoloMin(a,b) ((a)<(b)?(a):(b))
#endif

// ═══════════════════════════════════════════════════════════
//  Dark Purple Tech Color Palette (Unified Monochrome Violet)
// ═══════════════════════════════════════════════════════════
namespace Holo {

// ── Background Bases: deep purple-black, avoids colorkey transparency ──
inline constexpr float BG_DEEP[4]     = { 0.051f, 0.051f, 0.102f, 0.97f };  // #0D0D1A
inline constexpr float BG_DARK[4]     = { 0.059f, 0.039f, 0.102f, 0.95f };  // #0F0A1A
inline constexpr float BG_PANEL[4]    = { 0.071f, 0.063f, 0.149f, 0.88f };  // #121026
inline constexpr float BG_PANEL_HL[4] = { 0.102f, 0.039f, 0.180f, 0.80f };  // #1A0A2E

// ── Primary Purple Scale: dark → mid → light (core UI hierarchy) ──
inline constexpr float PURPLE_DARKEST[4] = { 0.102f, 0.039f, 0.180f, 1.000f }; // #1A0A2E
inline constexpr float BG_FRAME[4]      = { 0.176f, 0.106f, 0.306f, 0.60f };  // #2D1B4E
inline constexpr float PURPLE_MID[4]    = { 0.290f, 0.176f, 0.478f, 1.000f }; // #4A2D7A
inline constexpr float PURPLE_LIGHT[4]  = { 0.420f, 0.247f, 0.627f, 1.000f }; // #6B3FA0

// ── Accent Violets (bright highlights / interactive elements) ──
inline constexpr float VIOLET[4]       = { 0.486f, 0.227f, 0.929f, 1.000f };  // #7C3AED — primary accent
inline constexpr float VIOLET_LIGHT[4] = { 0.655f, 0.545f, 0.980f, 1.000f };  // #A78BFA — secondary accent
inline constexpr float VIOLET_PALE[4]  = { 0.769f, 0.710f, 0.992f, 1.000f };  // #C4B5FD — tertiary accent (glow)

// ── Dimmed variants for non-interactive / subtle elements ──
inline constexpr float VIOLET_DIM[4]   = { 0.290f, 0.176f, 0.478f, 1.000f };  // #4A2D7A (mid purple)
inline constexpr float VIOLET_SUBTLE[4]= { 0.420f, 0.247f, 0.627f, 1.000f };  // #6B3FA0 (light purple)

// ── Legacy aliases (backward compat — all mapped to unified purple scale) ──
//    Old magenta/cyan dual-accent → monochromatic purple
inline constexpr float MAGENTA[4]       = { 0.420f, 0.247f, 0.627f, 1.000f };  // → PURPLE_LIGHT
inline constexpr float MAGENTA_BRIGHT[4]= { 0.655f, 0.545f, 0.980f, 1.000f };  // → VIOLET_LIGHT
inline constexpr float MAGENTA_DIM[4]   = { 0.176f, 0.106f, 0.306f, 1.000f };  // → BG_FRAME
inline constexpr float CYAN[4]          = { 0.486f, 0.227f, 0.929f, 1.000f };  // → VIOLET
inline constexpr float CYAN_BRIGHT[4]   = { 0.769f, 0.710f, 0.992f, 1.000f };  // → VIOLET_PALE
inline constexpr float CYAN_DIM[4]      = { 0.290f, 0.176f, 0.478f, 1.000f };  // → PURPLE_MID
inline constexpr float ELECTRIC_BLUE[4] = { 0.769f, 0.710f, 0.992f, 1.000f };  // → VIOLET_PALE
inline constexpr float PURPLE[4]        = { 0.655f, 0.545f, 0.980f, 1.000f };  // → VIOLET_LIGHT
inline constexpr float PURPLE_DIM[4]    = { 0.420f, 0.247f, 0.627f, 1.000f };  // → PURPLE_LIGHT

// ── Semantic Colors (desaturated per spec) ──
inline constexpr float GREEN_NEON[4] = { 0.133f, 0.773f, 0.369f, 1.000f };  // #22C55E success
inline constexpr float AMBER[4]      = { 0.961f, 0.620f, 0.043f, 1.000f };  // #F59E0B warning
inline constexpr float RED_ALERT[4]  = { 0.937f, 0.267f, 0.267f, 1.000f };  // #EF4444 error

// ── Text Gradient (per spec) ──
inline constexpr float TEXT_TITLE[4] = { 1.000f, 1.000f, 1.000f, 1.000f };  // #FFFFFF titles/headings
inline constexpr float TEXT_BRIGHT[4]= { 0.878f, 0.878f, 0.878f, 1.000f };  // #E0E0E0 body text
inline constexpr float TEXT_DIM[4]   = { 0.627f, 0.627f, 0.722f, 1.000f };  // #A0A0B8 auxiliary text
inline constexpr float TEXT_GLOW[4]  = { 0.769f, 0.710f, 0.992f, 1.000f };  // #C4B5FD violet glow tint

// ── Color helpers ──
inline ImU32 ToU32(const float c[4]) {
    return IM_COL32((int)(c[0]*255), (int)(c[1]*255), (int)(c[2]*255), (int)(c[3]*255));
}
inline ImU32 ToU32(const float c[4], float alphaMul) {
    return IM_COL32((int)(c[0]*255), (int)(c[1]*255), (int)(c[2]*255),
                    (int)(HoloMin(1.0f, c[3] * alphaMul) * 255));
}
inline ImVec4 ToVec4(const float c[4]) {
    return ImVec4(c[0], c[1], c[2], c[3]);
}
inline ImVec4 ToVec4(const float c[4], float alphaMul) {
    return ImVec4(c[0], c[1], c[2], HoloMin(1.0f, c[3] * alphaMul));
}

} // namespace Holo

// ═══════════════════════════════════════════════════════════
//  Animation Helpers
// ═══════════════════════════════════════════════════════════
namespace HoloAnim {

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Smooth pulse: 0→1→0 oscillation
inline float Pulse(float speed = 2.0f, float minVal = 0.0f, float maxVal = 1.0f) {
    float t = (sinf((float)ImGui::GetTime() * speed) + 1.0f) * 0.5f;
    return Lerp(minVal, maxVal, t);
}

// Ease in-out cubic
inline float EaseInOut(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

// Ease out quad (deceleration)
inline float EaseOut(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

// Sawtooth wave 0→1 (for scanning effects)
inline float Sawtooth(float speed = 1.0f) {
    float t = fmodf((float)ImGui::GetTime() * speed, 1.0f);
    return t;
}

// Triangle wave 0→1→0
inline float Triangle(float speed = 1.0f) {
    float t = fmodf((float)ImGui::GetTime() * speed, 2.0f);
    return t < 1.0f ? t : 2.0f - t;
}

// Frame-rate independent smooth approach
inline float Approach(float current, float target, float speed = 0.15f) {
    float dt = ImGui::GetIO().DeltaTime;
    float t = 1.0f - powf(1.0f - speed, dt * 60.0f);
    return Lerp(current, target, t);
}

// Simple float storage for animated values (per-ID, persists across frames)
struct AnimState {
    float value = 0.0f;
    void Reset() { value = 0.0f; }
};

// Global anim state map (using ImGui storage for persistence)
inline float GetAnim(const char* key, float target, float speed = 0.15f) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID id = ImHashStr(key);
    float* val = storage->GetFloatRef(id, 0.0f);
    *val = Approach(*val, target, speed);
    return *val;
}

inline float GetAnimID(ImGuiID id, float target, float speed = 0.15f) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float* val = storage->GetFloatRef(id, 0.0f);
    *val = Approach(*val, target, speed);
    return *val;
}

} // namespace HoloAnim

// ═══════════════════════════════════════════════════════════
//  Custom Drawing Functions
// ═══════════════════════════════════════════════════════════

// ── Glow Rectangle: layered semi-transparent fills for neon glow effect ──
// ★优化: 4层→2层, 减少 50% AddRectFilled 调用
inline void HoloGlowRect(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 color,
                          float glowSize = 6.0f, float rounding = 4.0f) {
    if (!dl) return;
    for (int i = 2; i >= 1; i--) {
        float expand = glowSize * (float)i * 0.5f;
        float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) * (0.10f * i);
        ImU32 glowCol = (color & 0x00FFFFFF) | ((ImU32)(a) << IM_COL32_A_SHIFT);
        dl->AddRectFilled(
            ImVec2(p1.x - expand, p1.y - expand),
            ImVec2(p2.x + expand, p2.y + expand),
            glowCol, rounding + expand * 0.5f);
    }
}

// ── Glow Line: layered semi-transparent lines for neon glow ──
inline void HoloGlowLine(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 color,
                          float thickness = 1.0f, float glowSize = 3.0f) {
    if (!dl) return;
    for (int i = 3; i >= 1; i--) {
        float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) * (0.08f * i);
        ImU32 glowCol = (color & 0x00FFFFFF) | ((ImU32)(a) << IM_COL32_A_SHIFT);
        dl->AddLine(p1, p2, glowCol, thickness + glowSize * (float)i * 0.5f);
    }
    dl->AddLine(p1, p2, color, thickness);
}

// ── Glow Circle: layered semi-transparent circles for neon glow ──
inline void HoloGlowCircle(ImDrawList* dl, ImVec2 center, float radius, ImU32 color,
                            float glowSize = 4.0f, int segments = 24) {
    if (!dl) return;
    for (int i = 3; i >= 1; i--) {
        float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) * (0.06f * i);
        ImU32 glowCol = (color & 0x00FFFFFF) | ((ImU32)(a) << IM_COL32_A_SHIFT);
        dl->AddCircle(center, radius + glowSize * (float)i * 0.5f, glowCol,
                      segments, 1.0f + glowSize * (float)i * 0.3f);
    }
    dl->AddCircle(center, radius, color, segments, 2.0f);
}

// ── Scanlines: horizontal lines for CRT/holographic texture ──
// ★优化: 默认间距 3→8, 减少 ~60% AddRectFilled 调用
inline void HoloScanlines(ImDrawList* dl, ImVec2 p1, ImVec2 p2,
                           float alpha = 0.04f, float spacing = 8.0f) {
    if (!dl) return;
    ImU32 col = IM_COL32(167, 139, 250, (int)(alpha * 255));        // violet #A78BFA
    float y = p1.y;
    while (y < p2.y) {
        dl->AddRectFilled(ImVec2(p1.x, y), ImVec2(p2.x, y + 1.0f), col);
        y += spacing;
    }
    // Moving scanline (bright sweep)
    float sweep = HoloAnim::Triangle(0.3f);
    float sweepY = p1.y + (p2.y - p1.y) * sweep;
    ImU32 sweepCol = IM_COL32(196, 181, 253, (int)(0.12f * 255));   // pale violet #C4B5FD
    dl->AddRectFilled(ImVec2(p1.x, sweepY - 1), ImVec2(p2.x, sweepY + 2), sweepCol);
}

// ── Grid Background: subtle tech grid pattern ──
// ★优化: 移除交点圆(原~2000个AddCircleFilled), 加大默认间距 30→60
inline void HoloGridBG(ImDrawList* dl, ImVec2 p1, ImVec2 p2,
                        float cellSize = 60.0f, float alpha = 0.035f) {
    if (!dl) return;
    ImU32 col = IM_COL32(107, 63, 160, (int)(alpha * 255));       // violet #6B3FA0 grid
    // Vertical lines
    for (float x = p1.x; x <= p2.x; x += cellSize)
        dl->AddLine(ImVec2(x, p1.y), ImVec2(x, p2.y), col, 1.0f);
    // Horizontal lines
    for (float y = p1.y; y <= p2.y; y += cellSize)
        dl->AddLine(ImVec2(p1.x, y), ImVec2(p2.x, y), col, 1.0f);
}

// ── Particles: flowing particle texture ──
struct HoloParticle {
    float x, y;      // position (0-1 normalized)
    float vx, vy;    // velocity
    float size;      // particle size
    float phase;     // animation phase offset
};
inline std::vector<HoloParticle> g_HoloParticles;
inline bool g_HoloParticlesInit = false;

inline void HoloInitParticles(int count = 40) {
    g_HoloParticles.resize(count);
    for (auto& p : g_HoloParticles) {
        p.x = (float)(rand() % 1000) / 1000.0f;
        p.y = (float)(rand() % 1000) / 1000.0f;
        p.vx = ((float)(rand() % 100) / 1000.0f - 0.05f) * 0.3f;
        p.vy = ((float)(rand() % 100) / 1000.0f - 0.05f) * 0.3f;
        p.size = 0.8f + (float)(rand() % 30) / 10.0f;
        p.phase = (float)(rand() % 628) / 100.0f;
    }
    g_HoloParticlesInit = true;
}

inline void HoloParticles(ImDrawList* dl, ImVec2 p1, ImVec2 p2,
                           int count = 40, ImU32 color = 0) {
    if (!dl) return;
    if (!g_HoloParticlesInit || (int)g_HoloParticles.size() < count)
        HoloInitParticles(count);

    if (color == 0) color = IM_COL32(167, 139, 250, 120);           // violet #A78BFA particles

    float w = p2.x - p1.x;
    float h = p2.y - p1.y;
    float dt = ImGui::GetIO().DeltaTime;

    for (int i = 0; i < count && i < (int)g_HoloParticles.size(); i++) {
        auto& p = g_HoloParticles[i];
        // Update position
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        // Wrap around
        if (p.x < 0) p.x += 1.0f; if (p.x > 1) p.x -= 1.0f;
        if (p.y < 0) p.y += 1.0f; if (p.y > 1) p.y -= 1.0f;

        float px = p1.x + p.x * w;
        float py = p1.y + p.y * h;
        // Pulsing alpha
        float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 2.0f + p.phase);
        ImU32 col = (color & 0x00FFFFFF) |
                    ((ImU32)((float)((color >> IM_COL32_A_SHIFT) & 0xFF) * pulse) << IM_COL32_A_SHIFT);
        dl->AddCircleFilled(ImVec2(px, py), p.size, col);
        // Faint trail
        dl->AddCircle(ImVec2(px, py), p.size + 1.5f,
                      (color & 0x00FFFFFF) | ((ImU32)(30 * pulse) << IM_COL32_A_SHIFT),
                      8, 0.5f);
    }
}

// ── Pulse Ring: expanding ring animation ──
inline void HoloPulseRing(ImDrawList* dl, ImVec2 center, float baseRadius,
                           ImU32 color, float speed = 2.0f, int maxRings = 3) {
    if (!dl) return;
    float t = (float)ImGui::GetTime() * speed;
    for (int i = 0; i < maxRings; i++) {
        float phase = fmodf(t + (float)i / maxRings, 1.0f);
        float radius = baseRadius + phase * 25.0f;
        float alpha = (1.0f - phase) * 0.6f;
        ImU32 col = (color & 0x00FFFFFF) |
                    ((ImU32)(alpha * 255) << IM_COL32_A_SHIFT);
        dl->AddCircle(center, radius, col, 32, 1.5f);
    }
}

// ── Radar Sweep: rotating radar line ──
inline void HoloRadarSweep(ImDrawList* dl, ImVec2 center, float radius,
                            float speed = 1.0f, ImU32 color = 0) {
    if (!dl) return;
    if (color == 0) color = IM_COL32(124, 58, 237, 80);               // violet radar

    float angle = (float)ImGui::GetTime() * speed;
    // Sweep trail (gradient arc)
    for (int i = 0; i < 20; i++) {
        float a = angle - (float)i * 0.05f;
        float alpha = (1.0f - (float)i / 20.0f) * 0.3f;
        ImU32 col = (color & 0x00FFFFFF) |
                    ((ImU32)(alpha * 255) << IM_COL32_A_SHIFT);
        float x = center.x + cosf(a) * radius;
        float y = center.y + sinf(a) * radius;
        dl->AddLine(center, ImVec2(x, y), col, 1.0f);
    }
    // Main sweep line
    float mx = center.x + cosf(angle) * radius;
    float my = center.y + sinf(angle) * radius;
    HoloGlowLine(dl, center, ImVec2(mx, my), color, 2.0f, 4.0f);
}

// ── Corner Brackets: sci-fi corner decorations ──
inline void HoloCornerBrackets(ImDrawList* dl, ImVec2 p1, ImVec2 p2,
                                float len, ImU32 color, float thick = 2.0f) {
    if (!dl) return;
    // Top-left
    dl->AddLine(p1, ImVec2(p1.x + len, p1.y), color, thick);
    dl->AddLine(p1, ImVec2(p1.x, p1.y + len), color, thick);
    // Top-right
    dl->AddLine(ImVec2(p2.x, p1.y), ImVec2(p2.x - len, p1.y), color, thick);
    dl->AddLine(ImVec2(p2.x, p1.y), ImVec2(p2.x, p1.y + len), color, thick);
    // Bottom-left
    dl->AddLine(ImVec2(p1.x, p2.y), ImVec2(p1.x + len, p2.y), color, thick);
    dl->AddLine(ImVec2(p1.x, p2.y), ImVec2(p1.x, p2.y - len), color, thick);
    // Bottom-right
    dl->AddLine(p2, ImVec2(p2.x - len, p2.y), color, thick);
    dl->AddLine(p2, ImVec2(p2.x, p2.y - len), color, thick);
}

// ── Holographic Panel: frosted glass panel with glow border ──
inline void HoloPanel(ImDrawList* dl, ImVec2 p1, ImVec2 p2,
                       const char* title = nullptr, float rounding = 6.0f,
                       ImU32 accentColor = 0) {
    if (!dl) return;
    if (accentColor == 0) accentColor = Holo::ToU32(Holo::CYAN, 0.7f);

    // Background: semi-transparent frosted glass
    dl->AddRectFilled(p1, p2, Holo::ToU32(Holo::BG_PANEL, 0.88f), rounding);

    // Subtle inner gradient (top lighter)
    float panelH = p2.y - p1.y;
    if (panelH > 0) {
        dl->AddRectFilledMultiColor(
            p1, ImVec2(p2.x, p1.y + panelH * 0.4f),
            Holo::ToU32(Holo::BG_PANEL_HL, 0.30f),
            Holo::ToU32(Holo::BG_PANEL_HL, 0.30f),
            Holo::ToU32(Holo::BG_PANEL, 0.0f),
            Holo::ToU32(Holo::BG_PANEL, 0.0f));
    }

    // Scanline texture (★优化: 间距已加大, 减少调用)
    HoloScanlines(dl, p1, p2, 0.025f, 8.0f);

    // Glow border
    HoloGlowRect(dl, p1, p2, accentColor, 3.0f, rounding);
    dl->AddRect(p1, p2, accentColor, rounding, 0, 1.5f);

    // Corner brackets
    HoloCornerBrackets(dl, p1, p2, 10.0f, accentColor, 2.0f);

    // Title bar
    if (title) {
        float titleH = 24.0f;
        // Title background strip
        dl->AddRectFilled(p1, ImVec2(p2.x, p1.y + titleH),
                          Holo::ToU32(Holo::BG_FRAME, 0.70f), rounding, ImDrawFlags_RoundCornersTop);
        // Title accent line
        dl->AddLine(ImVec2(p1.x, p1.y + titleH), ImVec2(p2.x, p1.y + titleH),
                    accentColor, 1.0f);
        // Title text with glow
        ImVec2 textSize = ImGui::CalcTextSize(title);
        ImVec2 textPos(p1.x + 12.0f, p1.y + (titleH - textSize.y) * 0.5f);
        // Glow text (★优化: 4层描边→1层阴影)
        ImU32 glowText = (accentColor & 0x00FFFFFF) | (60 << IM_COL32_A_SHIFT);
        dl->AddText(ImVec2(textPos.x + 1, textPos.y + 1), glowText, title);
        dl->AddText(textPos, Holo::ToU32(Holo::TEXT_BRIGHT), title);

        // Title decoration: small dots on the right
        for (int i = 0; i < 3; i++) {
            float dx = p2.x - 14.0f - i * 8.0f;
            float dy = p1.y + titleH * 0.5f;
            float pulse = HoloAnim::Pulse(2.0f + i * 0.5f, 0.3f, 1.0f);
            ImU32 dotCol = (accentColor & 0x00FFFFFF) |
                           ((ImU32)(pulse * 200) << IM_COL32_A_SHIFT);
            dl->AddCircleFilled(ImVec2(dx, dy), 2.5f, dotCol);
        }
    }
}

// ── Holographic Progress Bar ──
inline void HoloProgressBar(ImDrawList* dl, ImVec2 p1, ImVec2 p2, float fraction,
                             ImU32 color = 0, float rounding = 3.0f) {
    if (!dl) return;
    if (color == 0) color = Holo::ToU32(Holo::CYAN, 0.9f);

    fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);

    // Background
    dl->AddRectFilled(p1, p2, IM_COL32(13, 10, 26, 200), rounding);   // deep violet-black
    // Border
    dl->AddRect(p1, p2, Holo::ToU32(Holo::VIOLET_DIM, 0.5f), rounding, 0, 1.0f);

    // Fill
    if (fraction > 0.0f) {
        float fillW = (p2.x - p1.x) * fraction;
        // Gradient fill
        dl->AddRectFilledMultiColor(
            p1, ImVec2(p1.x + fillW, p2.y),
            color,
            (color & 0x00FFFFFF) | (180 << IM_COL32_A_SHIFT),
            (color & 0x00FFFFFF) | (180 << IM_COL32_A_SHIFT),
            color);
        // Glow on fill edge
        dl->AddLine(ImVec2(p1.x + fillW, p1.y), ImVec2(p1.x + fillW, p2.y),
                    IM_COL32(255, 255, 255, 200), 2.0f);
    }

    // Animated scanline on the fill
    float scan = HoloAnim::Sawtooth(0.5f);
    float scanX = p1.x + (p2.x - p1.x) * scan;
    if (scanX < p1.x + (p2.x - p1.x) * fraction) {
        dl->AddLine(ImVec2(scanX, p1.y), ImVec2(scanX, p2.y),
                    IM_COL32(255, 255, 255, 80), 1.0f);
    }
}

// ═══════════════════════════════════════════════════════════
//  Custom Widgets
// ═══════════════════════════════════════════════════════════

// ── Holographic Button ──
inline bool HoloButton(const char* label, const ImVec2& size = ImVec2(0, 0),
                       ImU32 glowColor = 0) {
    if (glowColor == 0) glowColor = Holo::ToU32(Holo::CYAN, 0.9f);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);

    ImVec2 sz;
    sz.x = (size.x > 0) ? size.x : (labelSize.x + style.FramePadding.x * 2.0f + 16.0f);
    sz.y = (size.y > 0) ? size.y : (labelSize.y + style.FramePadding.y * 2.0f + 4.0f);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
    ImGui::ItemSize(sz, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = ImGui::ItemHoverable(bb, id, ImGuiItemFlags_None);
    bool active = (hovered && g.IO.MouseDown[0]) || g.ActiveId == id;

    // Animation state
    float hoverAnim = HoloAnim::GetAnimID(id, hovered ? 1.0f : 0.0f, 0.20f);
    float pressAnim = HoloAnim::GetAnimID(id | 0x80000000, active ? 1.0f : 0.0f, 0.30f);

    auto* dl = ImGui::GetWindowDrawList();

    // Background: frosted glass with gradient
    ImU32 bgCol = Holo::ToU32(Holo::BG_FRAME, 0.40f + hoverAnim * 0.30f);
    ImU32 bgHover = Holo::ToU32(Holo::VIOLET_DIM, 0.15f + hoverAnim * 0.25f);

    float rounding = 4.0f;
    dl->AddRectFilled(bb.Min, bb.Max, bgCol, rounding);
    if (hoverAnim > 0.01f) {
        dl->AddRectFilledMultiColor(
            bb.Min, bb.Max,
            bgHover, bgHover,
            Holo::ToU32(Holo::MAGENTA_DIM, 0.15f + hoverAnim * 0.20f),
            Holo::ToU32(Holo::MAGENTA_DIM, 0.15f + hoverAnim * 0.20f));
    }

    // Glow border (intensifies on hover)
    // ★优化: 仅 hover 时画 GlowRect, 非 hover 只画普通边框
    float glowAlpha = 0.3f + hoverAnim * 0.6f - pressAnim * 0.1f;
    ImU32 borderCol = (glowColor & 0x00FFFFFF) |
                      ((ImU32)(glowAlpha * 255) << IM_COL32_A_SHIFT);
    if (hoverAnim > 0.01f)
        HoloGlowRect(dl, bb.Min, bb.Max, borderCol, 2.0f + hoverAnim * 2.0f, rounding);
    dl->AddRect(bb.Min, bb.Max, borderCol, rounding, 0, 1.5f);

    // Press flash
    if (pressAnim > 0.01f) {
        dl->AddRectFilled(bb.Min, bb.Max,
                          IM_COL32(255, 255, 255, (int)(pressAnim * 40)), rounding);
    }

    // Corner accents (★优化: 仅 hover 时画, 8条线→非hover时省略)
    if (hoverAnim > 0.01f) {
        float cl = 6.0f;
        ImU32 cornerCol = (glowColor & 0x00FFFFFF) |
                          ((ImU32)((0.4f + hoverAnim * 0.5f) * 255) << IM_COL32_A_SHIFT);
        dl->AddLine(bb.Min, ImVec2(bb.Min.x + cl, bb.Min.y), cornerCol, 2.0f);
        dl->AddLine(bb.Min, ImVec2(bb.Min.x, bb.Min.y + cl), cornerCol, 2.0f);
        dl->AddLine(ImVec2(bb.Max.x, bb.Min.y), ImVec2(bb.Max.x - cl, bb.Min.y), cornerCol, 2.0f);
        dl->AddLine(ImVec2(bb.Max.x, bb.Min.y), ImVec2(bb.Max.x, bb.Min.y + cl), cornerCol, 2.0f);
        dl->AddLine(ImVec2(bb.Min.x, bb.Max.y), ImVec2(bb.Min.x + cl, bb.Max.y), cornerCol, 2.0f);
        dl->AddLine(ImVec2(bb.Min.x, bb.Max.y), ImVec2(bb.Min.x, bb.Max.y - cl), cornerCol, 2.0f);
        dl->AddLine(bb.Max, ImVec2(bb.Max.x - cl, bb.Max.y), cornerCol, 2.0f);
        dl->AddLine(bb.Max, ImVec2(bb.Max.x, bb.Max.y - cl), cornerCol, 2.0f);
    }

    // Text with glow
    ImVec2 textPos(bb.Min.x + (sz.x - labelSize.x) * 0.5f,
                   bb.Min.y + (sz.y - labelSize.y) * 0.5f);
    ImU32 textGlow = (glowColor & 0x00FFFFFF) |
                     ((ImU32)(hoverAnim * 80) << IM_COL32_A_SHIFT);
    if (hoverAnim > 0.01f) {
        dl->AddText(ImVec2(textPos.x + 1, textPos.y), textGlow, label);
        dl->AddText(ImVec2(textPos.x - 1, textPos.y), textGlow, label);
        dl->AddText(ImVec2(textPos.x, textPos.y + 1), textGlow, label);
        dl->AddText(ImVec2(textPos.x, textPos.y - 1), textGlow, label);
    }
    dl->AddText(textPos, Holo::ToU32(Holo::TEXT_BRIGHT), label);

    // Handle interaction
    bool clicked = false;
    if (hovered && g.IO.MouseClicked[0]) {
        ImGui::SetActiveID(id, window);
        ImGui::FocusWindow(window);
    }
    if (g.ActiveId == id) {
        if (g.IO.MouseReleased[0]) {
            if (hovered) clicked = true;
            ImGui::ClearActiveID();
        }
    }

    return clicked;
}

// ── Holographic Checkbox ──
inline bool HoloCheckbox(const char* label, bool* v, ImU32 glowColor = 0) {
    if (glowColor == 0) glowColor = Holo::ToU32(Holo::CYAN, 0.9f);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);

    float boxSz = 16.0f;
    ImVec2 sz(boxSz + style.ItemInnerSpacing.x + labelSize.x,
              (boxSz > labelSize.y) ? boxSz : labelSize.y);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
    const ImRect boxBB(pos, ImVec2(pos.x + boxSz, pos.y + boxSz));
    ImGui::ItemSize(sz, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = ImGui::ItemHoverable(bb, id, ImGuiItemFlags_None);
    bool active = (hovered && g.IO.MouseDown[0]) || g.ActiveId == id;

    float hoverAnim = HoloAnim::GetAnimID(id, hovered ? 1.0f : 0.0f, 0.20f);

    auto* dl = ImGui::GetWindowDrawList();

    // Checkbox box background
    ImU32 boxBg = Holo::ToU32(Holo::BG_DEEP, 0.80f);
    dl->AddRectFilled(boxBB.Min, boxBB.Max, boxBg, 3.0f);

    // Glow border (★优化: 仅 hover 时画 GlowRect, 非 hover 只画普通边框)
    float glowAlpha = 0.25f + hoverAnim * 0.5f;
    ImU32 borderCol = (glowColor & 0x00FFFFFF) |
                      ((ImU32)(glowAlpha * 255) << IM_COL32_A_SHIFT);
    if (hoverAnim > 0.01f)
        HoloGlowRect(dl, boxBB.Min, boxBB.Max, borderCol, 1.5f + hoverAnim * 1.5f, 3.0f);
    dl->AddRect(boxBB.Min, boxBB.Max, borderCol, 3.0f, 0, 1.5f);

    // Checkmark (animated appearance)
    if (*v) {
        float checkAnim = HoloAnim::GetAnimID(id | 0x40000000, 1.0f, 0.25f);
        ImVec2 c1(boxBB.Min.x + 4.0f, boxBB.Min.y + boxSz * 0.5f);
        ImVec2 c2(boxBB.Min.x + boxSz * 0.4f, boxBB.Min.y + boxSz - 4.0f);
        ImVec2 c3(boxBB.Min.x + boxSz - 4.0f, boxBB.Min.y + 4.0f);

        // ★优化: 移除 3 层发光偏移, 只画 1 层主勾标
        ImU32 checkCol = (glowColor & 0x00FFFFFF) |
                         ((ImU32)(checkAnim * 255) << IM_COL32_A_SHIFT);
        dl->AddLine(c1, c2, checkCol, 2.5f);
        dl->AddLine(c2, c3, checkCol, 2.5f);
        // ★优化: 移除 PulseRing (每帧 2 个 AddCircle)
    } else {
        HoloAnim::GetAnimID(id | 0x40000000, 0.0f, 0.25f);
    }

    // Label text with subtle glow
    ImVec2 textPos(boxBB.Max.x + style.ItemInnerSpacing.x,
                   pos.y + (boxSz - labelSize.y) * 0.5f);
    if (hoverAnim > 0.01f) {
        ImU32 textGlow = (glowColor & 0x00FFFFFF) |
                         ((ImU32)(hoverAnim * 50) << IM_COL32_A_SHIFT);
        dl->AddText(ImVec2(textPos.x + 1, textPos.y), textGlow, label);
        dl->AddText(ImVec2(textPos.x - 1, textPos.y), textGlow, label);
    }
    dl->AddText(textPos, *v ? Holo::ToU32(Holo::TEXT_BRIGHT) : Holo::ToU32(Holo::TEXT_DIM), label);

    // Handle interaction
    bool clicked = false;
    if (hovered && g.IO.MouseClicked[0]) {
        ImGui::SetActiveID(id, window);
    }
    if (g.ActiveId == id) {
        if (g.IO.MouseReleased[0]) {
            if (hovered) { *v = !*v; clicked = true; }
            ImGui::ClearActiveID();
        }
    }
    return clicked;
}

// ── Holographic Separator ──
inline void HoloSeparator(ImU32 color = 0, float thickness = 1.0f) {
    if (color == 0) color = Holo::ToU32(Holo::CYAN, 0.4f);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImVec2 pos = window->DC.CursorPos;
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p1(pos.x, pos.y);
    ImVec2 p2(pos.x + w, pos.y);

    auto* dl = ImGui::GetWindowDrawList();

    // Glow line
    HoloGlowLine(dl, p1, p2, color, thickness, 2.0f);

    // Decorative endpoints
    dl->AddCircleFilled(p1, 2.5f, color);
    dl->AddCircleFilled(p2, 2.5f, color);
    // ★优化: 移除动画扫描点 (每帧 Sawtooth + 2 次绘制调用)

    ImGui::ItemSize(ImVec2(w, thickness + 4.0f), 0);
    window->DC.CursorPos.y += 4.0f;
}

// ── Holographic Section Header ──
inline void HoloHeader(const char* label, ImU32 color = 0) {
    if (color == 0) color = Holo::ToU32(Holo::CYAN, 0.8f);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImVec2 labelSize = ImGui::CalcTextSize(label);
    ImVec2 pos = window->DC.CursorPos;
    float w = ImGui::GetContentRegionAvail().x;

    auto* dl = ImGui::GetWindowDrawList();

    // Background strip
    dl->AddRectFilled(
        ImVec2(pos.x - 4, pos.y - 2),
        ImVec2(pos.x + w + 4, pos.y + labelSize.y + 6),
        Holo::ToU32(Holo::BG_FRAME, 0.50f), 3.0f);

    // Left accent bar
    dl->AddRectFilled(
        ImVec2(pos.x - 4, pos.y - 2),
        ImVec2(pos.x - 1, pos.y + labelSize.y + 6),
        color, 2.0f);
    HoloGlowRect(dl,
        ImVec2(pos.x - 4, pos.y - 2),
        ImVec2(pos.x - 1, pos.y + labelSize.y + 6),
        color, 3.0f, 2.0f);

    // Text with glow (★优化: 4层描边→1层阴影)
    ImU32 textGlow = (color & 0x00FFFFFF) | (60 << IM_COL32_A_SHIFT);
    dl->AddText(ImVec2(pos.x + 7, pos.y + 2), textGlow, label);
    dl->AddText(ImVec2(pos.x + 6, pos.y + 1), Holo::ToU32(Holo::TEXT_BRIGHT), label);

    // Right decorative line
    float lineStart = pos.x + 6 + labelSize.x + 10;
    float lineEnd = pos.x + w + 4;
    if (lineEnd > lineStart) {
        HoloGlowLine(dl, ImVec2(lineStart, pos.y + labelSize.y * 0.5f),
                     ImVec2(lineEnd, pos.y + labelSize.y * 0.5f),
                     Holo::ToU32(Holo::VIOLET_DIM, 0.3f), 1.0f, 1.0f);
    }

    ImGui::ItemSize(ImVec2(w, labelSize.y + 8.0f), 0);
    window->DC.CursorPos.y += 4.0f;
}

// ── Holographic Stat Box ──
inline void HoloStatBox(const char* label, const char* value,
                         ImVec2 size = ImVec2(0, 0), ImU32 accentColor = 0) {
    if (accentColor == 0) accentColor = Holo::ToU32(Holo::CYAN, 0.8f);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImVec2 labelSize = ImGui::CalcTextSize(label);
    ImVec2 valueSize = ImGui::CalcTextSize(value);
    ImVec2 sz;
    sz.x = (size.x > 0) ? size.x : ((labelSize.x > valueSize.x) ? labelSize.x : valueSize.x) + 20.0f;
    sz.y = (size.y > 0) ? size.y : labelSize.y + valueSize.y + 16.0f;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
    ImGui::ItemSize(sz, 0);

    auto* dl = ImGui::GetWindowDrawList();

    // Panel background
    dl->AddRectFilled(bb.Min, bb.Max, Holo::ToU32(Holo::BG_PANEL, 0.70f), 4.0f);

    // Top accent line
    dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y), ImVec2(bb.Max.x, bb.Min.y + 2),
                      accentColor, 2.0f);
    HoloGlowRect(dl, ImVec2(bb.Min.x, bb.Min.y), ImVec2(bb.Max.x, bb.Min.y + 2),
                 accentColor, 2.0f, 0.0f);

    // Scanline
    HoloScanlines(dl, bb.Min, bb.Max, 0.02f, 4.0f);

    // Border
    dl->AddRect(bb.Min, bb.Max, Holo::ToU32(Holo::VIOLET_DIM, 0.3f), 4.0f, 0, 1.0f);

    // Label (dim, top)
    dl->AddText(ImVec2(bb.Min.x + 10, bb.Min.y + 6),
                Holo::ToU32(Holo::TEXT_DIM), label);

    // Value (bright, center, with glow) (★优化: 4层描边→1层阴影)
    ImVec2 valPos(bb.Min.x + 10, bb.Min.y + 6 + labelSize.y + 2);
    ImU32 valGlow = (accentColor & 0x00FFFFFF) | (50 << IM_COL32_A_SHIFT);
    dl->AddText(ImVec2(valPos.x + 1, valPos.y + 1), valGlow, value);
    dl->AddText(valPos, Holo::ToU32(Holo::TEXT_BRIGHT), value);

    // Pulse dot (top-right corner)
    float pulse = HoloAnim::Pulse(2.0f, 0.3f, 1.0f);
    ImU32 dotCol = (accentColor & 0x00FFFFFF) |
                   ((ImU32)(pulse * 200) << IM_COL32_A_SHIFT);
    dl->AddCircleFilled(ImVec2(bb.Max.x - 8, bb.Min.y + 8), 3.0f, dotCol);
}

// ── Holographic Title Bar (top navigation light bar) ──
inline void HoloTitleBar(const char* title, ImVec2 pos, ImVec2 size,
                          ImU32 accentColor = 0) {
    if (accentColor == 0) accentColor = Holo::ToU32(Holo::CYAN, 0.9f);

    auto* dl = ImGui::GetWindowDrawList();
    if (!dl) return;

    ImVec2 p1 = pos;
    ImVec2 p2 = ImVec2(pos.x + size.x, pos.y + size.y);

    // Background gradient
    dl->AddRectFilledMultiColor(
        p1, p2,
        Holo::ToU32(Holo::BG_FRAME, 0.85f),
        Holo::ToU32(Holo::BG_FRAME, 0.85f),
        Holo::ToU32(Holo::BG_DEEP, 0.60f),
        Holo::ToU32(Holo::BG_DEEP, 0.60f));

    // Bottom glowing line (the "light bar")
    HoloGlowLine(dl, ImVec2(p1.x, p2.y), ImVec2(p2.x, p2.y), accentColor, 2.0f, 4.0f);

    // Scanline texture
    HoloScanlines(dl, p1, p2, 0.03f, 3.0f);

    // Title text with glow (★优化: 4层描边→1层阴影)
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImVec2 textPos(p1.x + 16.0f, p1.y + (size.y - titleSize.y) * 0.5f);
    ImU32 textGlow = (accentColor & 0x00FFFFFF) | (80 << IM_COL32_A_SHIFT);
    dl->AddText(ImVec2(textPos.x + 1, textPos.y + 1), textGlow, title);
    dl->AddText(textPos, Holo::ToU32(Holo::TEXT_BRIGHT), title);

    // Right side: animated status indicators
    float rx = p2.x - 20.0f;
    for (int i = 0; i < 5; i++) {
        float pulse = HoloAnim::Pulse(1.5f + i * 0.3f, 0.2f, 1.0f);
        ImU32 dotCol = (accentColor & 0x00FFFFFF) |
                       ((ImU32)(pulse * 180) << IM_COL32_A_SHIFT);
        dl->AddRectFilled(
            ImVec2(rx - i * 6.0f, p1.y + size.y * 0.5f - 4.0f),
            ImVec2(rx - i * 6.0f + 3.0f, p1.y + size.y * 0.5f + 4.0f),
            dotCol, 1.0f);
    }

    // Corner brackets
    HoloCornerBrackets(dl, p1, p2, 12.0f, accentColor, 2.0f);
}

// ── Holographic Slider (wraps ImGui::SliderInt with custom styling) ──
inline bool HoloSliderInt(const char* label, int* v, int v_min, int v_max,
                           ImU32 glowColor = 0) {
    if (glowColor == 0) glowColor = Holo::ToU32(Holo::VIOLET, 0.9f);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, Holo::ToVec4(Holo::BG_DEEP, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Holo::ToVec4(Holo::VIOLET_DIM, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Holo::ToVec4(Holo::VIOLET_DIM, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Holo::ToVec4(Holo::VIOLET));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    bool changed = ImGui::SliderInt(label, v, v_min, v_max);

    ImGui::PopStyleColor(5);

    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        HoloGlowRect(dl, min, max, glowColor, 2.0f, 3.0f);
    }

    return changed;
}

// ── Holographic Color Edit (wraps ImGui::ColorEdit3 with custom styling) ──
inline bool HoloColorEdit3(const char* label, float col[3], ImU32 glowColor = 0) {
    if (glowColor == 0) glowColor = Holo::ToU32(Holo::VIOLET, 0.6f);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, Holo::ToVec4(Holo::BG_DEEP, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Holo::ToVec4(Holo::VIOLET_DIM, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Holo::ToVec4(Holo::VIOLET_DIM, 0.50f));

    bool changed = ImGui::ColorEdit3(label, col, ImGuiColorEditFlags_NoInputs);

    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered()) {
        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        HoloGlowRect(dl, min, max, glowColor, 1.5f, 3.0f);
    }

    return changed;
}

// ── Holographic Small Button ──
inline bool HoloSmallButton(const char* label, ImU32 glowColor = 0) {
    if (glowColor == 0) glowColor = Holo::ToU32(Holo::VIOLET, 0.7f);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);

    ImVec2 sz(labelSize.x + style.FramePadding.x * 2.0f + 8.0f,
              labelSize.y + style.FramePadding.y * 2.0f);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + sz.x, pos.y + sz.y));
    ImGui::ItemSize(sz, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = ImGui::ItemHoverable(bb, id, ImGuiItemFlags_None);
    float hoverAnim = HoloAnim::GetAnimID(id, hovered ? 1.0f : 0.0f, 0.20f);

    auto* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(bb.Min, bb.Max,
                      Holo::ToU32(Holo::BG_FRAME, 0.40f + hoverAnim * 0.30f), 3.0f);

    float glowAlpha = 0.2f + hoverAnim * 0.5f;
    ImU32 borderCol = (glowColor & 0x00FFFFFF) |
                      ((ImU32)(glowAlpha * 255) << IM_COL32_A_SHIFT);
    if (hoverAnim > 0.01f)
        HoloGlowRect(dl, bb.Min, bb.Max, borderCol, 2.0f, 3.0f);
    dl->AddRect(bb.Min, bb.Max, borderCol, 3.0f, 0, 1.0f);

    ImVec2 textPos(bb.Min.x + (sz.x - labelSize.x) * 0.5f,
                   bb.Min.y + (sz.y - labelSize.y) * 0.5f);
    dl->AddText(textPos, Holo::ToU32(hoverAnim > 0.5f ? Holo::TEXT_BRIGHT : Holo::TEXT_DIM),
                label);

    bool clicked = false;
    if (hovered && g.IO.MouseClicked[0]) {
        ImGui::SetActiveID(id, window);
    }
    if (g.ActiveId == id && g.IO.MouseReleased[0]) {
        if (hovered) clicked = true;
        ImGui::ClearActiveID();
    }
    return clicked;
}

// ── Holographic Tab Bar styling helpers ──
inline void HoloPushTabStyle() {
    ImGui::PushStyleColor(ImGuiCol_Tab, Holo::ToVec4(Holo::BG_FRAME, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, Holo::ToVec4(Holo::VIOLET_DIM, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, Holo::ToVec4(Holo::VIOLET_DIM, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, Holo::ToVec4(Holo::BG_DEEP, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, Holo::ToVec4(Holo::BG_PANEL_HL, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));
}

inline void HoloPopTabStyle() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
}

// ── Holographic Loading Spinner (radar sweep style) ──
inline void HoloSpinner(ImVec2 center, float radius, float speed = 2.0f,
                         ImU32 color = 0) {
    if (color == 0) color = Holo::ToU32(Holo::VIOLET, 0.9f);
    auto* dl = ImGui::GetWindowDrawList();
    if (!dl) return;

    dl->AddCircle(center, radius, Holo::ToU32(Holo::VIOLET_DIM, 0.3f), 32, 1.0f);
    HoloRadarSweep(dl, center, radius, speed, color);
    dl->AddCircleFilled(center, 3.0f, color);
}

// ── Animated number display (digit roll effect) ──
inline void HoloAnimatedNumber(ImDrawList* dl, int value, ImVec2 pos,
                                ImU32 color = 0) {
    if (color == 0) color = Holo::ToU32(Holo::VIOLET, 0.95f);
    if (!dl) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);

    ImU32 glow = (color & 0x00FFFFFF) | (50 << IM_COL32_A_SHIFT);
    // ★优化: 4层描边→1层阴影
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), glow, buf);
    dl->AddText(pos, color, buf);
}

// ── Full holographic window background ──
// ★优化: 移除 HoloParticles(60次调用), HoloGridBG 间距加大, HoloScanlines 间距加大
inline void HoloWindowBackground(ImDrawList* dl, ImVec2 winPos, ImVec2 winSize,
                                  float titleBarH = 36.0f) {
    if (!dl) return;

    ImVec2 p1 = winPos;
    ImVec2 p2 = ImVec2(winPos.x + winSize.x, winPos.y + winSize.y);

    // Deep space background
    dl->AddRectFilled(p1, p2, Holo::ToU32(Holo::BG_DEEP, 0.96f));

    // Grid background (已加大间距)
    HoloGridBG(dl, p1, p2, 60.0f, 0.03f);

    // Scanlines (已加大间距)
    HoloScanlines(dl, p1, p2, 0.02f, 8.0f);

    // Edge glow (top and bottom) — unified violet (2层代替8层)
    for (int i = 0; i < 3; i++) {
        float a = 0.15f * (1.0f - (float)i / 3.0f);
        ImU32 col = IM_COL32(167, 139, 250, (int)(a * 255));       // violet #A78BFA
        dl->AddLine(ImVec2(p1.x, p1.y + titleBarH + i), ImVec2(p2.x, p1.y + titleBarH + i), col, 1.0f);
    }
}