/*
 * GameMatrix.h — 游戏视角矩阵 & 世界坐标转屏幕坐标（完全复刻 Obs_Dll 项目算法）
 *
 * 坐标系约定：Unreal Engine 左手坐标系
 *   Pitch=Yaw=Roll=0 时：前=+X, 右=+Y, 上=+Z
 *
 * 依赖：<cmath>（sinf/cosf/tanf）
 *
 * 编码：UTF-8 无 BOM
 */

#pragma once

#include <cmath>

/* ================================================================
 *  一、基础数学结构体
 * ================================================================ */

struct FVector {
    float X, Y, Z;

    FVector() : X(0), Y(0), Z(0) {}
    FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

    // 向量减法
    FVector operator-(const FVector& v) const {
        return FVector(X - v.X, Y - v.Y, Z - v.Z);
    }

    // 点积 (Dot Product)
    float Dot(const FVector& v) const {
        return X * v.X + Y * v.Y + Z * v.Z;
    }
};

struct FVector2D {
    float X, Y;
    FVector2D() : X(0), Y(0) {}
    FVector2D(float x, float y) : X(x), Y(y) {}
};

/* ================================================================
 *  二、4x4 矩阵（行优先 M[row][col]）
 *     Pitch=Yaw=Roll=0 时：
 *     M[0] = 前向量 (Forward, +X)
 *     M[1] = 右向量 (Right,   +Y)
 *     M[2] = 上向量 (Up,      +Z)
 *     M[3] = 平移
 * ================================================================ */

struct FMatrix {
    float M[4][4];

    FMatrix() {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                M[i][j] = 0.0f;
    }
};

/* ================================================================
 *  三、BuildViewMatrix — 构建 View 矩阵
 *
 *  逐元素复刻 Obs_Dll 中 FRotator::GetMatrix（FRotator.cpp L3-38），
 *  增加相机位置的逆变换 M[3]（等价于 -(CameraPos · 各轴)）。
 *
 *  参数：
 *    CameraPos   : 相机世界坐标 (X, Y, Z)
 *    Pitch       : 俯仰角（度），-90° ~ +90°
 *    Yaw         : 偏航角（度），0° ~ 360°
 *    Roll        : 翻滚角（度），FPS游戏通常为 0°
 *
 *  返回：4x4 View 矩阵（左手系，行向量 vec*M）
 *
 *  公式（行优先，与 Obs_Dll 完全一致）：
 *
 *    CP=cos(Pitch), SP=sin(Pitch), CY=cos(Yaw), SY=sin(Yaw),
 *    CR=cos(Roll),  SR=sin(Roll)
 *
 *    M[0] = [  CP*CY,                  CP*SY,                 SP,         0.0  ]  ← 前向量 (Forward)
 *    M[1] = [  SR*SP*CY - CR*SY,       SR*SP*SY + CR*CY,      -SR*CP,     0.0  ]  ← 右向量 (Right)
 *    M[2] = [ -(CR*SP*CY + SR*SY),      CY*SR - CR*SP*SY,     CR*CP,      0.0  ]  ← 上向量 (Up)
 *    M[3] = [ -(CamPos·Forward),       -(CamPos·Right),       -(CamPos·Up), 1.0]  ← 平移
 *
 * ================================================================ */

static FMatrix BuildViewMatrix(
    const FVector& CameraPos,   /* 相机世界坐标 (X, Y, Z) */
    float Pitch,                /* 俯仰角（度），-90° ~ +90° */
    float Yaw,                  /* 偏航角（度），0° ~ 360° */
    float Roll)                 /* 翻滚角（度），FPS游戏通常为 0° */
{
    const float DEG2RAD = 3.1415926535897932f / 180.0f;

    float radPitch = Pitch * DEG2RAD;
    float radYaw   = Yaw   * DEG2RAD;
    float radRoll  = Roll  * DEG2RAD;

    float SP = std::sinf(radPitch);
    float CP = std::cosf(radPitch);
    float SY = std::sinf(radYaw);
    float CY = std::cosf(radYaw);
    float SR = std::sinf(radRoll);
    float CR = std::cosf(radRoll);

    FMatrix mat;

    // 行 0 — 前向量 (Forward)
    mat.M[0][0] = CP * CY;
    mat.M[0][1] = CP * SY;
    mat.M[0][2] = SP;
    mat.M[0][3] = 0.0f;

    // 行 1 — 右向量 (Right)
    mat.M[1][0] = SR * SP * CY - CR * SY;
    mat.M[1][1] = SR * SP * SY + CR * CY;
    mat.M[1][2] = -SR * CP;
    mat.M[1][3] = 0.0f;

    // 行 2 — 上向量 (Up)
    mat.M[2][0] = -(CR * SP * CY + SR * SY);
    mat.M[2][1] = CY * SR - CR * SP * SY;
    mat.M[2][2] = CR * CP;
    mat.M[2][3] = 0.0f;

    // 行 3 — 平移（相机位置逆变换）
    FVector axisX(mat.M[0][0], mat.M[0][1], mat.M[0][2]);  // Forward
    FVector axisY(mat.M[1][0], mat.M[1][1], mat.M[1][2]);  // Right
    FVector axisZ(mat.M[2][0], mat.M[2][1], mat.M[2][2]);  // Up

    mat.M[3][0] = -(CameraPos.Dot(axisX));
    mat.M[3][1] = -(CameraPos.Dot(axisY));
    mat.M[3][2] = -(CameraPos.Dot(axisZ));
    mat.M[3][3] = 1.0f;

    return mat;
}


/* ================================================================
 *  四、WorldToScreen — 世界坐标转屏幕坐标
 *
 *  完全复刻 Obs_Dll 中 APlayerCameraManager::WorldtoScreen（UObject.cpp L180-232）
 *
 *  参数：
 *    WorldLocation : 目标世界坐标 (X, Y, Z) — 要投影的3D点
 *    CameraPos     : 相机世界坐标 (X, Y, Z)
 *    CameraPitch   : 相机俯仰角（度），-90° ~ +90°
 *    CameraYaw     : 相机偏航角（度），0° ~ 360°
 *    CameraRoll    : 相机翻滚角（度），FPS游戏通常为 0°
 *    FOV           : 视场角（度），水平FOV
 *    ScreenWidth   : 屏幕宽度（像素），如 1920
 *    ScreenHeight  : 屏幕高度（像素），如 1080
 *    outScreen     : [输出] 屏幕像素坐标 (X, Y)
 *
 *  返回：true = 在屏幕内可绘制, false = 屏幕外/后方/太远
 *
 *  算法步骤（与 Obs_Dll 逐行对应）：
 *    1. L189-192: 欧拉角 → 旋转矩阵 → 提取前/右/上轴向量
 *    2. L194:    vDelta = WorldLocation - CameraPos
 *    3. L195:    点积投影到相机空间
 *                  vTransformed.X = vDelta · Right   （水平偏移）
 *                  vTransformed.Y = vDelta · Up      （垂直偏移）
 *                  vTransformed.Z = vDelta · Forward  （深度）
 *    4. L205-213: 深度剔除（Z < 1 或 Z > 100000）
 *    5. L222-223: 透视投影（水平FOV，焦距 = centerX / tan(FOV/2)，X/Y 共用）
 *                  screenX = centerX + (水平偏移 × 焦距) / 深度
 *                  screenY = centerY - (垂直偏移 × 焦距) / 深度
 *    6. L226-228: 屏幕边界检查
 *
 * ================================================================ */

static bool WorldToScreen(
    const FVector& WorldLocation,   /* 目标世界坐标 (X, Y, Z) — 要投影的3D点 */
    const FVector& CameraPos,       /* 相机世界坐标 (X, Y, Z) */
    float CameraPitch,              /* 相机俯仰角（度），-90° ~ +90° */
    float CameraYaw,                /* 相机偏航角（度），0° ~ 360° */
    float CameraRoll,               /* 相机翻滚角（度），FPS游戏通常为 0° */
    float FOV,                      /* 视场角（度），水平FOV */
    float ScreenWidth,              /* 屏幕宽度（像素），如 1920 */
    float ScreenHeight,             /* 屏幕高度（像素），如 1080 */
    FVector2D& outScreen)           /* [输出] 屏幕像素坐标 (X, Y) */
{


    // ---- 步骤 1：欧拉角 → 旋转矩阵 → 提取轴向量 （Obs_Dll L189-192）----
    FMatrix rotMat = BuildViewMatrix(CameraPos, CameraPitch, CameraYaw, CameraRoll);

    FVector vAxisX(rotMat.M[0][0], rotMat.M[0][1], rotMat.M[0][2]); // 前向量 (Forward)
    FVector vAxisY(rotMat.M[1][0], rotMat.M[1][1], rotMat.M[1][2]); // 右向量 (Right)
    FVector vAxisZ(rotMat.M[2][0], rotMat.M[2][1], rotMat.M[2][2]); // 上向量 (Up)


    // ---- 步骤 2：目标相对相机位置差 （Obs_Dll L194）----
    FVector vDelta = WorldLocation - CameraPos;

    // ---- 步骤 3：点积投影到相机空间 （Obs_Dll L195）----
    FVector vTransformed(vDelta.Dot(vAxisY),  // X: 水平偏移 = dot(vDelta, Right)
                         vDelta.Dot(vAxisZ),  // Y: 垂直偏移 = dot(vDelta, Up)
                         vDelta.Dot(vAxisX)); // Z: 深度     = dot(vDelta, Forward)



    // ---- 步骤 4：深度剔除 （Obs_Dll L205-213）----
    if (vTransformed.Z < 1.0f) {

        return false;
    }
    if (vTransformed.Z > 100000.0f) {

        return false;
    }

    // ---- 步骤 5：透视投影到屏幕坐标 （Obs_Dll L217-223）----
    float centerX = ScreenWidth  / 2.0f;
    float centerY = ScreenHeight / 2.0f;

    const float DEG2RAD = 3.1415926535897932f / 180.0f;
    float tanHalfFOV = std::tanf((FOV * DEG2RAD) / 2.0f);

    // 焦距（像素）：pin-hole 相机模型，X/Y 两轴共用同一焦距
    float scale = centerX / tanHalfFOV;
    outScreen.X = centerX + vTransformed.X * scale / vTransformed.Z;
    outScreen.Y = centerY - vTransformed.Y * scale / vTransformed.Z;



    // ---- 步骤 6：屏幕边界检查 （Obs_Dll L226-228）----
    if (outScreen.X < 0.0f || outScreen.Y < 0.0f ||
        outScreen.X > ScreenWidth || outScreen.Y > ScreenHeight) {

        return false;
    }


    return true;
}


///* ================================================================
// *  五、使用示例
// *
// *  FVector2D screenPos;
// *  FVector   targetPos(1000.f, 2000.f, 50.f);    // 目标世界坐标
// *  FVector   cameraPos(500.f, 1000.f, 170.f);    // 相机世界坐标
// *
// *  if (WorldToScreen(
// *      targetPos, cameraPos,
// *      5.f,       /* 相机俯仰角 Pitch（度） */
// *      90.f,      /* 相机偏航角 Yaw（度）   */
// *      0.f,       /* 相机翻滚角 Roll（度）  */
// *      90.f,      /* 视场角 FOV（度）       */
// *      1920.f,    /* 屏幕宽度（像素）      */
// *      1080.f,    /* 屏幕高度（像素）      */
// *      screenPos  /* [输出] 屏幕坐标       */))
// *  {
// *      DrawAt(screenPos.x, screenPos.y);
// *  }
// *
// *================================================================ */


