#pragma once
/*
 * HoloSplash.h - VEX Opening Animation (Dark Purple Tech)
 *
 * Four-layer animated intro:
 *   1. Deep dark background + flowing particle system
 *   2. V->E->X sequential glowing text reveal with pulse halo (#a78bfa -> #7c3aed)
 *   3. Particles converging from screen edges toward VEX center
 *   4. Expanding glow rings after letters complete, fade-transition to main UI
 *
 * Duration: ~2.0s  |  VEX letters large, centered, fully visible, never clipped
 *
 * Interface:
 *   HoloSplash::Init(w, h)   - call once at startup
 *   HoloSplash::Update()     - call every frame
 *   HoloSplash::IsActive()   - returns false when animation finished
 *   HoloSplash::Render(w, h) - call every frame while active
 */
#include "imgui/imgui.h"
#include <cmath>
#include <vector>
#include <cstdlib>
#include <cfloat>

namespace HoloSplash {

// ════════════════════════════════════════════════════════════
//  Timing
// ════════════════════════════════════════════════════════════
static constexpr float DURATION   = 2.0f;   // total animation seconds
static constexpr float FADE_START = 0.85f;  // start final fade-out at 85%

// ════════════════════════════════════════════════════════════
//  State
// ════════════════════════════════════════════════════════════
inline float g_StartTime   = 0.0f;
inline bool  g_Initialized  = false;
inline bool  g_Active       = true;
inline float g_ScreenW      = 1920.0f;
inline float g_ScreenH      = 1080.0f;

// Cached VEX layout (recalculated on Init)
inline float g_FontSize     = 160.0f;
inline float g_VexStartX    = 0.0f;
inline float g_VexCenterY   = 0.0f;
inline float g_VexTotalW    = 0.0f;
inline float g_VexHeight    = 0.0f;
inline float g_LetterW[3]   = {0, 0, 0};
inline float g_LetterStartX[3] = {0, 0, 0};

// ════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════
inline static float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline static float EaseIO(float t) {
    return t < 0.5f ? 4.f * t * t * t : 1.f - powf(-2.f * t + 2.f, 3.f) * 0.5f;
}
inline static float EaseOut(float t) {
    return 1.f - powf(1.f - t, 3.f);
}
inline static float Rand01() {
    return (float)(rand() % 10000) / 9999.f;
}

// ════════════════════════════════════════════════════════════
//  Layer 1: Background Flowing Particles
// ════════════════════════════════════════════════════════════
struct BgParticle {
    float x, y;        // normalized 0-1 position
    float vx, vy;      // normalized velocity
    float size;
    float phase;       // for pulse animation
};
inline std::vector<BgParticle> s_bgParticles;
inline bool s_bgReady = false;

inline void InitBgParticles(int count = 60) {
    s_bgParticles.resize(count);
    for (auto& p : s_bgParticles) {
        p.x = Rand01();
        p.y = Rand01();
        p.vx = (Rand01() - 0.5f) * 0.12f;
        p.vy = (Rand01() - 0.5f) * 0.12f;
        p.size = 0.8f + Rand01() * 2.0f;
        p.phase = Rand01() * 6.283f;
    }
    s_bgReady = true;
}

inline void DrawBgParticles(ImDrawList* dl, float w, float h, float alpha) {
    if (!s_bgReady || !dl || alpha < 0.01f) return;
    float dt = ImGui::GetIO().DeltaTime;
    float time = (float)ImGui::GetTime();
    for (auto& p : s_bgParticles) {
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.x < 0) p.x += 1.f;
        if (p.x > 1) p.x -= 1.f;
        if (p.y < 0) p.y += 1.f;
        if (p.y > 1) p.y -= 1.f;
        float px = p.x * w;
        float py = p.y * h;
        float pulse = 0.3f + 0.7f * (0.5f + 0.5f * sinf(time * 1.5f + p.phase));
        int a = (int)(pulse * alpha * 90);
        if (a < 2) continue;
        // #A78BFA violet particles
        dl->AddCircleFilled(ImVec2(px, py), p.size, IM_COL32(167, 139, 250, a));
        // Soft glow ring
        int ra = a / 5;
        if (ra > 1)
            dl->AddCircle(ImVec2(px, py), p.size + 2.5f, IM_COL32(167, 139, 250, ra), 8, 0.8f);
    }
}

// ════════════════════════════════════════════════════════════
//  Layer 3: Convergence Particles (screen edges -> VEX center)
// ════════════════════════════════════════════════════════════
struct ConvParticle {
    float startX, startY;   // screen pixel start (on edge)
    float targetX, targetY; // screen pixel target (near VEX)
    float size;
    float speed;            // 0-1 normalized arrival time
};
inline std::vector<ConvParticle> s_convParticles;
inline bool s_convReady = false;

inline void InitConvParticles(float w, float h, int count = 80) {
    s_convParticles.clear();
    s_convParticles.reserve(count);
    // VEX text bounding box
    float vexL = g_VexStartX;
    float vexR = g_VexStartX + g_VexTotalW;
    float vexT = g_VexCenterY - g_VexHeight * 0.5f;
    float vexB = g_VexCenterY + g_VexHeight * 0.5f;
    for (int i = 0; i < count; i++) {
        ConvParticle c;
        // Start on a random screen edge
        int edge = rand() % 4;
        switch (edge) {
            case 0: c.startX = 0;          c.startY = Rand01() * h; break;  // left
            case 1: c.startX = w;          c.startY = Rand01() * h; break;  // right
            case 2: c.startX = Rand01() * w; c.startY = 0;          break;  // top
            default: c.startX = Rand01() * w; c.startY = h;          break;  // bottom
        }
        // Target: random point within VEX text area
        c.targetX = vexL + Rand01() * (vexR - vexL);
        c.targetY = vexT + Rand01() * (vexB - vexT);
        c.size = 1.0f + Rand01() * 2.5f;
        c.speed = 0.6f + Rand01() * 0.4f;
        s_convParticles.push_back(c);
    }
    s_convReady = true;
}

inline void DrawConvParticles(ImDrawList* dl, float progress, float alpha) {
    if (!s_convReady || !dl || alpha < 0.01f) return;
    // Particles converge during 0-50% of animation, then fade out by 70%
    float convT = ClampF(progress / 0.50f, 0.f, 1.f);
    float convE = EaseIO(convT);
    float pAlpha = alpha * (1.f - ClampF((progress - 0.50f) / 0.20f, 0.f, 1.f));
    for (auto& p : s_convParticles) {
        float t = ClampF(convE * p.speed, 0.f, 1.f);
        t = EaseIO(t);
        float x = p.startX + (p.targetX - p.startX) * t;
        float y = p.startY + (p.targetY - p.startY) * t;
        int a = (int)(pAlpha * 200);
        if (a < 3) continue;
        // #C4B5FD pale violet convergence particles
        dl->AddCircleFilled(ImVec2(x, y), p.size, IM_COL32(196, 181, 253, a));
        int ra = a / 4;
        if (ra > 1)
            dl->AddCircle(ImVec2(x, y), p.size + 2.f, IM_COL32(196, 181, 253, ra), 6, 0.7f);
    }
}

// ════════════════════════════════════════════════════════════
//  Layer 2: V-E-X Sequential Glowing Text Reveal
// ════════════════════════════════════════════════════════════
//  Each letter reveals sequentially with pulse halo color shift
//  #A78BFA (light violet) <-> #7C3AED (primary violet)
//  After full reveal: glowing stroke + dark-to-light purple gradient fill
// ════════════════════════════════════════════════════════════

inline void DrawVEX(ImDrawList* dl, float progress, float alpha) {
    if (!dl || alpha < 0.01f) return;

    ImFont* font = ImGui::GetIO().Fonts->Fonts.Size > 0
                       ? ImGui::GetIO().Fonts->Fonts[0]
                       : nullptr;
    if (!font) return;

    const char* letters[3] = {"V", "E", "X"};
    // Sequential reveal windows (V first, then E, then X)
    const float revealStart[3] = {0.10f, 0.28f, 0.46f};
    const float revealEnd[3]   = {0.30f, 0.48f, 0.66f};

    float fontSize = g_FontSize;
    float time = (float)ImGui::GetTime();

    for (int i = 0; i < 3; i++) {
        // Per-letter reveal progress
        float revT = ClampF((progress - revealStart[i]) / (revealEnd[i] - revealStart[i]), 0.f, 1.f);
        if (revT <= 0.f) continue;
        float revE = EaseIO(revT);
        float letterAlpha = alpha * revE;

        float lx = g_LetterStartX[i];
        float ly = g_VexCenterY - g_VexHeight * 0.5f;

        // ── Pulse halo color: blend #A78BFA <-> #7C3AED ──
        float pulsePhase = time * 3.0f + (float)i * 1.8f;
        float pulse = 0.5f + 0.5f * sinf(pulsePhase);
        int hr = (int)(167.f + (124.f - 167.f) * pulse);  // R: 167 <-> 124
        int hg = (int)(139.f + (58.f  - 139.f) * pulse);  // G: 139 <-> 58
        int hb = (int)(250.f + (237.f - 250.f) * pulse);  // B: 250 <-> 237

        // ── Layer 1: Large soft outer glow (6 offset directions) ──
        for (int ring = 5; ring >= 1; ring--) {
            float off = (float)ring * 3.0f;
            int ga = (int)(letterAlpha * 20.f * (6 - ring) / 5);
            if (ga < 2) continue;
            ImU32 glowCol = IM_COL32(hr, hg, hb, ga);
            // 8-direction offset for soft blur effect
            dl->AddText(font, fontSize, ImVec2(lx - off, ly),       glowCol, letters[i]);
            dl->AddText(font, fontSize, ImVec2(lx + off, ly),       glowCol, letters[i]);
            dl->AddText(font, fontSize, ImVec2(lx, ly - off),       glowCol, letters[i]);
            dl->AddText(font, fontSize, ImVec2(lx, ly + off),       glowCol, letters[i]);
            dl->AddText(font, fontSize, ImVec2(lx - off*0.7f, ly - off*0.7f), glowCol, letters[i]);
            dl->AddText(font, fontSize, ImVec2(lx + off*0.7f, ly - off*0.7f), glowCol, letters[i]);
            dl->AddText(font, fontSize, ImVec2(lx - off*0.7f, ly + off*0.7f), glowCol, letters[i]);
            dl->AddText(font, fontSize, ImVec2(lx + off*0.7f, ly + off*0.7f), glowCol, letters[i]);
        }

        // ── Layer 2: Medium tight glow (violet) ──
        int midA = (int)(letterAlpha * 100.f);
        ImU32 midGlow = IM_COL32(hr, hg, hb, midA);
        for (int ox = -2; ox <= 2; ox++) {
            for (int oy = -2; oy <= 2; oy++) {
                if (ox == 0 && oy == 0) continue;
                if (abs(ox) + abs(oy) > 3) continue;
                dl->AddText(font, fontSize, ImVec2(lx + (float)ox, ly + (float)oy), midGlow, letters[i]);
            }
        }

        // ── Layer 3: Core text — gradient fill simulation ──
        // Dark purple base (#4A2D7A) for the "dark-to-light gradient" effect
        ImU32 darkFill = IM_COL32(74, 45, 122, (int)(letterAlpha * 200));
        dl->AddText(font, fontSize, ImVec2(lx, ly), darkFill, letters[i]);

        // Light violet overlay (#C4B5FD) with partial alpha — simulates gradient
        ImU32 lightFill = IM_COL32(196, 181, 253, (int)(letterAlpha * 160));
        dl->AddText(font, fontSize, ImVec2(lx, ly), lightFill, letters[i]);

        // Bright white-violet core for readability and "glowing stroke" look
        ImU32 coreCol = IM_COL32(240, 235, 255, (int)(letterAlpha * 255));
        dl->AddText(font, fontSize, ImVec2(lx, ly), coreCol, letters[i]);

        // ── Reveal sweep: a bright flash during the reveal phase ──
        if (revT < 1.f) {
            float sweepAlpha = (1.f - revT) * 0.6f;
            ImU32 sweepCol = IM_COL32(255, 255, 255, (int)(alpha * sweepAlpha * 255));
            dl->AddText(font, fontSize, ImVec2(lx, ly), sweepCol, letters[i]);
        }
    }
}

// ════════════════════════════════════════════════════════════
//  Layer 4: Expanding Glow Rings (after letters complete)
// ════════════════════════════════════════════════════════════
inline void DrawGlowExpand(ImDrawList* dl, float cx, float cy, float progress, float alpha) {
    if (!dl || progress < 0.60f || alpha < 0.01f) return;
    float expandT = ClampF((progress - 0.60f) / 0.35f, 0.f, 1.f);
    // Multiple staggered rings
    for (int r = 0; r < 4; r++) {
        float ringT = ClampF(expandT - r * 0.10f, 0.f, 1.f);
        if (ringT <= 0.f) continue;
        float ringE = EaseOut(ringT);
        float maxR = g_ScreenW * 0.4f;  // expand to 40% of screen width
        float radius = ringE * maxR;
        if (radius < 5.f) continue;
        int a = (int)(alpha * (1.f - ringE) * 120);
        if (a < 3) continue;
        // #A78BFA violet rings
        ImU32 ringCol = IM_COL32(167, 139, 250, a);
        dl->AddCircle(ImVec2(cx, cy), radius, ringCol, 64, 2.5f);
        // Soft glow around ring
        for (int g = 1; g <= 3; g++) {
            int ga = a / (g * 3);
            if (ga < 2) continue;
            dl->AddCircle(ImVec2(cx, cy), radius + (float)g * 3.f,
                          IM_COL32(167, 139, 250, ga), 64, 1.f);
        }
    }
}

// ════════════════════════════════════════════════════════════
//  Background Frame & Ambient Glow
// ════════════════════════════════════════════════════════════
inline void DrawBackground(ImDrawList* dl, float w, float h, float alpha) {
    if (!dl) return;
    // Deep base fill #0D0D1A
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(13, 13, 26, (int)(alpha * 255)));

    // Subtle radial center glow (#4A2D7A dark purple)
    float cx = w * 0.5f, cy = h * 0.5f;
    float maxR = w * 0.35f;
    for (int i = 10; i >= 1; i--) {
        float r = (float)i / 10.f;
        int a = (int)(alpha * 10.f * (1.f - r));
        if (a < 2) continue;
        dl->AddCircleFilled(ImVec2(cx, cy), r * maxR, IM_COL32(74, 45, 122, a));
    }
}

// ════════════════════════════════════════════════════════════
//  Fade-out vignette (smooth transition to main UI)
// ════════════════════════════════════════════════════════════
inline void DrawFadeVignette(ImDrawList* dl, float w, float h, float progress, float alpha) {
    if (!dl || progress < FADE_START) return;
    float va = EaseIO((progress - FADE_START) / (1.f - FADE_START));
    // Full-screen dark overlay that increases opacity
    int overlayA = (int)(va * 230);
    if (overlayA > 2)
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(13, 13, 26, overlayA));
}

// ════════════════════════════════════════════════════════════
//  Public Interface
// ════════════════════════════════════════════════════════════

inline void CalcVexLayout(float w, float h) {
    ImFont* font = ImGui::GetIO().Fonts->Fonts.Size > 0
                       ? ImGui::GetIO().Fonts->Fonts[0]
                       : nullptr;
    if (!font) return;

    // Font size: 15% of screen height, clamped to 80-300px
    g_FontSize = ClampF(h * 0.15f, 80.f, 300.f);

    // Measure each letter
    const char* letters[3] = {"V", "E", "X"};
    float spacing = g_FontSize * 0.06f;  // small gap between letters
    g_VexTotalW = 0;
    g_VexHeight = 0;
    for (int i = 0; i < 3; i++) {
        ImVec2 sz = font->CalcTextSizeA(g_FontSize, FLT_MAX, 0.0f, letters[i]);
        g_LetterW[i] = sz.x;
        if (sz.y > g_VexHeight) g_VexHeight = sz.y;
        g_VexTotalW += sz.x;
    }
    g_VexTotalW += spacing * 2;  // gaps between 3 letters

    g_VexStartX = w * 0.5f - g_VexTotalW * 0.5f;
    g_VexCenterY = h * 0.5f;

    // Calculate each letter's start X position
    float cursor = g_VexStartX;
    for (int i = 0; i < 3; i++) {
        g_LetterStartX[i] = cursor;
        cursor += g_LetterW[i] + spacing;
    }
}

inline void Init(float w, float h) {
    g_ScreenW = w;
    g_ScreenH = h;
    g_StartTime = (float)ImGui::GetTime();
    g_Initialized = true;
    g_Active = true;

    CalcVexLayout(w, h);
    InitBgParticles(60);
    InitConvParticles(w, h, 80);
}

inline void Update() {
    if (!g_Initialized) return;
    float elapsed = (float)ImGui::GetTime() - g_StartTime;
    if (elapsed >= DURATION) g_Active = false;
}

inline bool IsActive() {
    return g_Initialized && g_Active;
}

inline void Render(float w, float h) {
    if (!g_Initialized) return;
    float elapsed = (float)ImGui::GetTime() - g_StartTime;
    float progress = ClampF(elapsed / DURATION, 0.f, 1.f);

    // Overall alpha curve:
    //   0.00-0.06: fade in
    //   0.06-0.85: full opacity
    //   0.85-1.00: fade out
    float alpha;
    if (progress < 0.06f)
        alpha = EaseIO(progress / 0.06f);
    else if (progress > FADE_START)
        alpha = 1.f - EaseIO((progress - FADE_START) / (1.f - FADE_START));
    else
        alpha = 1.f;

    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    // ── Layer 0: Deep background fill + ambient center glow ──
    DrawBackground(dl, w, h, alpha);

    // ── Layer 1: Flowing background particles ──
    DrawBgParticles(dl, w, h, alpha);

    // ── Layer 3: Convergence particles (early phase, fades by 70%) ──
    DrawConvParticles(dl, progress, alpha);

    // ── Layer 2: V-E-X sequential glowing text reveal ──
    DrawVEX(dl, progress, alpha);

    // ── Layer 4: Expanding glow rings (after 60% progress) ──
    DrawGlowExpand(dl, w * 0.5f, h * 0.5f, progress, alpha);

    // ── Fade-out vignette (smooth transition to main UI) ──
    DrawFadeVignette(dl, w, h, progress, alpha);
}

} // namespace HoloSplash
