#pragma once
/*
 * core/Math.h — 相机投影与矩阵运算
 *
 * 从 main.h 抽离的纯数学模块:
 *   - CameraData          相机数据聚合
 *   - CachedCameraMatrix  旋转矩阵缓存 (避免每帧重复计算)
 *   - MatrixRotation      欧拉角 → 旋转矩阵
 *   - GetCachedRotationMatrix  带缓存的旋转矩阵获取
 *   - AnQuWorldToScreen   世界坐标 → 屏幕坐标 (零 DMA, 使用缓存相机数据)
 *   - W2S                 AnQuWorldToScreen 的 bool 包装
 *
 * 依赖: GameMatrix.h (FVector/FVector2D/FMatrix), <cmath>
 */

#include "../GameMatrix.h"
#include <cmath>

// ═══════════════════════════════════════
//  CameraData — 相机数据 (DMA 读取后缓存)
// ═══════════════════════════════════════
struct CameraData {
    DWORD64    cameraMgr;
    FVector    camLoc;
    FVector    camRot;
    float      camFov;
    FVector    localPos;
    int        localTeamId;
};

// The overlay may cover a full monitor while the game renders into a client
// area that starts below a title bar or at a non-zero desktop coordinate.
// Keep that mapping separate from the camera math so fullscreen still uses the
// zero-offset fast path.
struct ProjectionViewport {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

inline ProjectionViewport g_ProjectionViewport;

inline void SetProjectionViewport(int left, int top, int width, int height, bool valid) {
    g_ProjectionViewport.left = left;
    g_ProjectionViewport.top = top;
    g_ProjectionViewport.width = width;
    g_ProjectionViewport.height = height;
    g_ProjectionViewport.valid = valid && width > 0 && height > 0;
}

// ═══════════════════════════════════════
//  CachedCameraMatrix — 旋转矩阵缓存
//  避免每帧重复计算相同 camRot 的旋转矩阵
// ═══════════════════════════════════════
struct CachedCameraMatrix {
    FVector    camRot;
    FMatrix    rotMatrix;
    DWORD64    timestamp;
    bool       valid;
};

inline CachedCameraMatrix g_CachedMatrix;

// ── 前向声明 ──
inline FMatrix MatrixRotation(const FVector& rot);

// ═══════════════════════════════════════
//  GetCachedRotationMatrix
//  当 camRot 未变化时复用上次计算的矩阵
// ═══════════════════════════════════════
inline const FMatrix& GetCachedRotationMatrix(const FVector& rot) {
    if (!g_CachedMatrix.valid ||
        g_CachedMatrix.camRot.X != rot.X ||
        g_CachedMatrix.camRot.Y != rot.Y ||
        g_CachedMatrix.camRot.Z != rot.Z) {
        g_CachedMatrix.rotMatrix = MatrixRotation(rot);
        g_CachedMatrix.camRot    = rot;
        g_CachedMatrix.valid     = true;
    }
    return g_CachedMatrix.rotMatrix;
}

// ═══════════════════════════════════════
//  MatrixRotation — 欧拉角 (度) → 旋转矩阵
//  Pitch=rot.X, Yaw=rot.Y, Roll=rot.Z
// ═══════════════════════════════════════
inline FMatrix MatrixRotation(const FVector& rot) {
    float aa = rot.X * 3.1415926535897932f / 180.f;
    float bb = rot.Y * 3.1415926535897932f / 180.f;
    float cc = rot.Z * 3.1415926535897932f / 180.f;
    float a = sinf(aa), b = cosf(aa), c = sinf(bb), d = cosf(bb), e = sinf(cc), f = cosf(cc);
    FMatrix m;
    m.M[0][0] = b * d;             m.M[0][1] = b * c;             m.M[0][2] = a;             m.M[0][3] = 0.f;
    m.M[1][0] = e * a * d - f * c; m.M[1][1] = e * a * c + f * d; m.M[1][2] = -e * b;        m.M[1][3] = 0.f;
    m.M[2][0] = -(f * a * d + e * c); m.M[2][1] = d * e - f * a * c; m.M[2][2] = f * b;       m.M[2][3] = 0.f;
    m.M[3][0] = 0.f;               m.M[3][1] = 0.f;               m.M[3][2] = 0.f;           m.M[3][3] = 1.f;
    return m;
}

// ═══════════════════════════════════════
//  AnQuWorldToScreen — 世界坐标 → 屏幕坐标
//  使用缓存的相机数据 (零 DMA)
//  每次调用都实时计算矩阵 — 不做帧级缓存, 确保相机移动时坐标始终精确
//  ★优化: 缓存 fov→scale 和 cx/cy, 避免每次调用都算 tanf
// ═══════════════════════════════════════
inline FVector2D AnQuWorldToScreen(const FVector& world, const CameraData& cam, int sw, int sh) {
    FVector2D out;
    const FMatrix& m = GetCachedRotationMatrix(cam.camRot);
    FVector AX(m.M[0][0], m.M[0][1], m.M[0][2]);
    FVector AY(m.M[1][0], m.M[1][1], m.M[1][2]);
    FVector AZ(m.M[2][0], m.M[2][1], m.M[2][2]);
    FVector DA = world - cam.camLoc;
    FVector vTransformed(DA.Dot(AY), DA.Dot(AZ), DA.Dot(AX));

    // Reject invalid camera/projection inputs before dividing. A stale zero-sized
    // swap-chain or FOV can otherwise produce huge screen offsets.
    const int viewportW = g_ProjectionViewport.valid ? g_ProjectionViewport.width : sw;
    const int viewportH = g_ProjectionViewport.valid ? g_ProjectionViewport.height : sh;
    const int viewportLeft = g_ProjectionViewport.valid ? g_ProjectionViewport.left : 0;
    const int viewportTop = g_ProjectionViewport.valid ? g_ProjectionViewport.top : 0;
    if (vTransformed.Z < 1.f || viewportW <= 0 || viewportH <= 0 ||
        !std::isfinite(cam.camFov) || cam.camFov <= 1.f || cam.camFov >= 179.f) {
        return out;
    }

    // ★优化: 缓存 scale/cx/cy, fov 和屏幕尺寸不变时跳过 tanf
    static float s_cachedFov = -1.0f;
    static float s_cachedScale = 0.0f;
    static int   s_cachedSw = 0;
    static int   s_cachedSh = 0;
    static float s_cx = 0.0f, s_cy = 0.0f;
    if (s_cachedFov != cam.camFov || s_cachedSw != viewportW || s_cachedSh != viewportH) {
        s_cachedFov = cam.camFov;
        s_cachedSw = viewportW;
        s_cachedSh = viewportH;
        s_cx = viewportW / 2.f;
        s_cy = viewportH / 2.f;
        s_cachedScale = s_cx / tanf(cam.camFov * 3.1415926535897932f / 360.f);
    }

    out.X = viewportLeft + s_cx + vTransformed.X * s_cachedScale / vTransformed.Z;
    out.Y = viewportTop + s_cy - vTransformed.Y * s_cachedScale / vTransformed.Z;
    if (!std::isfinite(out.X) || !std::isfinite(out.Y)) return FVector2D{};
    return out;
}

