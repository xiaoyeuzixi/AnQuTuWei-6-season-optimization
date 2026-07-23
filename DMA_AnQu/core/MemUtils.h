#pragma once
/*
 * core/MemUtils.h — 内存工具函数集合
 *
 * 从 main.h 抽离的内存读取与游戏状态管理工具:
 *   - HEARTBEAT_TIMEOUT, MAX_ERROR_COUNT  常量
 *   - SafeRead, SafeReadBulk              安全内存读取
 *   - IsGameProcessAlive, IsHeartbeatTimeout, UpdateHeartbeat  心跳管理
 *   - ResetGameState, SetGameProcess, IsBaseValid              游戏状态管理
 *   - ReadActorLocation, ReadComponentToWorldLocation          坐标读取 (含 ACE 加密回退)
 *
 * 依赖: Mem.h (mem), Offset.h, GameState.h (gs), GameMatrix.h (FVector),
 *       ESPUtils.h (ace_decrypt_*), core/Math.h
 */

#include "../Mem.h"
#include "../Offset.h"
#include "../GameMatrix.h"
#include "../ESPUtils.h"
#include "GameState.h"
#include "Math.h"
#include <Windows.h>
#include <cmath>

// 心跳超时阈值 (ms)
inline constexpr DWORD HEARTBEAT_TIMEOUT = 5000;

// 错误重置阈值（连续失败 N 次后强制重置状态）
inline constexpr int MAX_ERROR_COUNT = 10;

// 安全读取模板 — 带异常处理和边界检查
template<typename T>
inline bool SafeRead(DWORD64 address, T* out) {
    if (!out || address < 0x10000 || address > 0x7FFFFFFFFF) {
        return false;
    }
    try {
        *out = mem.Read<T>(address);
        return true;
    } catch (...) {
        InterlockedIncrement(&gs.gameProcess.errorCount);
        return false;
    }
}

// 安全读取批量数据
inline bool SafeReadBulk(DWORD64 address, void* buffer, DWORD size) {
    if (!buffer || address < 0x10000 || size == 0 || size > 1024 * 1024) {
        return false;
    }
    try {
        return mem.Read(address, buffer, size);
    } catch (...) {
        InterlockedIncrement(&gs.gameProcess.errorCount);
        return false;
    }
}

// 检查游戏进程是否存活
inline bool IsGameProcessAlive() {
    if (!gs.gameProcess.processHandle) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(gs.gameProcess.processHandle, &exitCode)) {
        return false;
    }
    return exitCode == STILL_ACTIVE;
}

// 检查心跳是否超时
inline bool IsHeartbeatTimeout() {
    auto now = GetTickCount64();
    return (now - gs.gameProcess.lastHeartbeat) > HEARTBEAT_TIMEOUT;
}

// 更新心跳时间戳
inline void UpdateHeartbeat() {
    gs.gameProcess.lastHeartbeat = GetTickCount64();
    InterlockedExchange(&gs.gameProcess.errorCount, 0);
}

// 重置游戏状态（游戏退出/重开时调用）
inline void ResetGameState() {
    gs.gameProcess.isRunning = false;
    gs.gameProcess.isPaused = false;
    gs.gameProcess.processId = 0;
    if (gs.gameProcess.processHandle) {
        CloseHandle(gs.gameProcess.processHandle);
        gs.gameProcess.processHandle = nullptr;
    }
    gs.world = 0;
    gs.base = 0;
    InterlockedExchange(&gs.dirty.playersDirty, 1);
    InterlockedExchange(&gs.dirty.bonesDirty, 1);
    InterlockedExchange(&gs.dirty.infoDirty, 1);
    InterlockedExchange(&gs.dirty.combatDirty, 1);
}

// 设置游戏进程信息
inline void SetGameProcess(DWORD pid, HANDLE handle) {
    ResetGameState();
    gs.gameProcess.processId = pid;
    gs.gameProcess.processHandle = handle;
    gs.gameProcess.isRunning = true;
    UpdateHeartbeat();
}

// 验证模块基址是否有效
inline bool IsBaseValid() {
    if (!gs.base || gs.base < 0x100000) return false;
    try {
        volatile BYTE test = mem.Read<BYTE>(gs.base + 0x1000);
        return true;
    } catch (...) {
        return false;
    }
}

// ═══════════════════════════════════════
//  坐标读取 (ACE 加密回退)
//  1. RelativeLocation (encType=0): 直接读 = 明文
//  2. RelativeLocation (encType!=0): 回退到 RootComponent ComponentToWorld
//  3. Root CTW (encType!=0): 回退到 Mesh ComponentToWorld
//  4. 全部加密: 返回零向量
// ═══════════════════════════════════════
inline FVector ReadActorLocation(DWORD64 rootComp, DWORD64 actorPtr = 0) {
    if (!rootComp) return {};

    // 1. ★ACE 解密 RelativeLocation (新: 优先尝试真正的解密)
    FVector acePos = ace_decrypt_relative_location(rootComp);
    auto isCoord = [](float v) -> bool {
        return !std::isnan(v) && !std::isinf(v) &&
               std::abs(v) > 0.01f && std::abs(v) < 500000.f;
    };
    if (isCoord(acePos.X) && isCoord(acePos.Y) && isCoord(acePos.Z) &&
        std::abs(acePos.Z) < 10000.f &&
        (std::abs(acePos.X) > 10.f || std::abs(acePos.Y) > 10.f)) {
        return acePos;
    }

    // 2. ★ACE 解密 RootComponent 的 ComponentToWorld translation
    FVector ctwPos = ace_decrypt_c2w_translation(rootComp);
    if (isCoord(ctwPos.X) && isCoord(ctwPos.Y) && isCoord(ctwPos.Z) &&
        std::abs(ctwPos.Z) < 10000.f &&
        (std::abs(ctwPos.X) > 10.f || std::abs(ctwPos.Y) > 10.f)) {
        return ctwPos;
    }

    // 3. 旧回退: RL 加密且 ACE 解密失败, 尝试 Mesh 的 ComponentToWorld
    //    ★Mesh CTW 也在 0x220 (和 RootComponent 一样, 都是 USceneComponent 子类)
    if (actorPtr) {
        DWORD64 mesh = mem.Read<DWORD64>(actorPtr + Offset_ActorMesh);
        if (mesh) {
            int meshCtwFlags = mem.Read<int>(mesh + Offset_RootComponentToWorldFlags);
            int meshCtwEnc = (unsigned int)meshCtwFlags >> 29;
            if (meshCtwEnc == 0) {
                FVector p = mem.Read<FVector>(mesh + Offset_RootComponentToWorld + 16);
                if (!std::isnan(p.X) && (std::abs(p.X) > 1.f || std::abs(p.Y) > 1.f))
                    return p;
            }
        }
    }

    // 4. 全部加密, 尝试读 Bounds
    {
        static const DWORD64 boundsOffsets[] = { 0x278, 0x280, 0x2A0, 0x2B0, 0x2C0 };
        for (size_t bi = 0; bi < 5; bi++) {
            FVector bOrigin = mem.Read<FVector>(rootComp + boundsOffsets[bi]);
            if (!std::isnan(bOrigin.X) && !std::isnan(bOrigin.Y) && !std::isnan(bOrigin.Z) &&
                std::abs(bOrigin.X) > 0.1f && std::abs(bOrigin.X) < 1000000.f &&
                std::abs(bOrigin.Y) > 0.1f && std::abs(bOrigin.Y) < 1000000.f &&
                std::abs(bOrigin.Z) > 0.1f && std::abs(bOrigin.Z) < 1000000.f) {
                return bOrigin;
            }
        }
    }

    // 5. ★ targeted pointer check: 只检查诊断确定有明文组件的偏移
    //    诊断发现 BP_ImpactEffect_HD_C 的明文组件在 Actor+0x4F0/0x6E0/0x6E8/0x6F0/0x870
    //    Root+0x688 也有明文组件
    //    全扫描太慢(800+次DMA), 只查关键偏移
    if (actorPtr) {
        auto isCoord = [](float v) -> bool {
            return !std::isnan(v) && !std::isinf(v) &&
                   std::abs(v) > 0.01f && std::abs(v) < 500000.f;
        };
        static const DWORD64 actorPtrOffsets[] = { 0x3A0, 0x3D8, 0x4F0, 0x6E0, 0x6E8, 0x6F0, 0x870 };
        for (auto poff : actorPtrOffsets) {
            DWORD64 ptr = mem.Read<DWORD64>(actorPtr + poff);
            if (ptr < 0x10000 || ptr > 0x7FFFFFFFFFFF) continue;
            int pFlags = mem.Read<int>(ptr + Offset_ActorLocationFlags);
            if (((unsigned int)pFlags >> 29) == 0) {
                FVector p = mem.Read<FVector>(ptr + Offset_ActorLocation);
                if (isCoord(p.X) && isCoord(p.Y) && isCoord(p.Z))
                    return p;
            }
        }
        // Root+0x688
        {
            DWORD64 ptr = mem.Read<DWORD64>(rootComp + 0x688);
            if (ptr >= 0x10000 && ptr <= 0x7FFFFFFFFFFF) {
                int pFlags = mem.Read<int>(ptr + Offset_ActorLocationFlags);
                if (((unsigned int)pFlags >> 29) == 0) {
                    FVector p = mem.Read<FVector>(ptr + Offset_ActorLocation);
                    if (isCoord(p.X) && isCoord(p.Y) && isCoord(p.Z))
                        return p;
                }
            }
        }
    }

    // 6. Bounds.Origin fallback: 诊断发现 Root+0x6DC 有 FBoxSphereBounds.Origin
    //    对 Helmet/Vest 物品, 这是唯一能找到的坐标来源
    {
        FVector bOrigin = mem.Read<FVector>(rootComp + 0x6DC);
        if (!std::isnan(bOrigin.X) && !std::isnan(bOrigin.Y) && !std::isnan(bOrigin.Z) &&
            std::abs(bOrigin.X) > 0.01f && std::abs(bOrigin.X) < 500000.f &&
            std::abs(bOrigin.Y) > 0.01f && std::abs(bOrigin.Y) < 500000.f &&
            std::abs(bOrigin.Z) > 0.01f && std::abs(bOrigin.Z) < 500000.f) {
            return bOrigin;
        }
    }

    return {};
}

inline FVector ReadActorLocationKnownFlags(DWORD64 rootComp, DWORD64 actorPtr, uint32_t knownLocationFlags) {
    (void)actorPtr;
    if (!rootComp) return {};
    auto isCoord = [](float v) -> bool {
        return !std::isnan(v) && !std::isinf(v) &&
               std::abs(v) > 0.01f && std::abs(v) < 500000.f;
    };

    // ThreadItems 已经批量读过 RelativeLocationFlags，解密线程直接复用，
    // 避免每个物资再次 mem.Read(root+flags) + 再查 ACE 链表。
    FVector p = ace_decrypt_relative_location_with_ctl(rootComp, knownLocationFlags);
    if (isCoord(p.X) && isCoord(p.Y) && isCoord(p.Z) && std::abs(p.Z) < 10000.f) {
        return p;
    }

    // 物资后台线程不能在单个物资上走完整 fallback。
    // 完整 fallback 会扫 Mesh/Bounds/多个候选指针，NOCACHE 下 300 个物资可卡 10 秒。
    // 无效时保持旧缓存，下一轮继续尝试，不阻塞玩家/骨骼实时数据。
    return {};
}

inline FVector ReadComponentToWorldLocation(DWORD64 meshComp) {
    if (!meshComp) return {};
    // ★Mesh CTW 也在 0x220, 和 RootComponent 一样
    return mem.Read<FVector>(meshComp + Offset_RootComponentToWorld + 16);
}
