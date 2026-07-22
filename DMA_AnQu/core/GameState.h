#pragma once
/*
 * core/GameState.h — 游戏全局状态聚合
 *
 * 集中管理所有全局数据结构和互斥锁，替代原 main.h 中的全局变量。
 * 结构体定义与 main.h 中完全一致，确保向后兼容。
 *
 * 依赖: GameMatrix.h (FVector), Windows.h, STL
 */

#include "../GameMatrix.h"
#include <Windows.h>
#include "Math.h"  // CameraData
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <cstdint>

// ═══════════════════════════════════════
//  前置声明 & 类型
// ═══════════════════════════════════════
enum PlayerType { PT_UNKNOWN = 0, PT_AI = 1, PT_PLAYER = 2 };

struct PlayerEntry {
    DWORD64 pawn; DWORD64 mesh; DWORD64 state; int teamId;
    FVector pos;       // rootcomponent Location (用于人机方框 / 绘制全部)
    std::string clazz; // 类名 (用于"绘制全部"模式)
};

struct NearbyEntry {
    DWORD64 actor = 0;      // Actor 地址，供物资线程去重/缓存
    DWORD64 root = 0;       // RootComponent 缓存，避免重复读
    FVector pos;            // 世界坐标
    std::string className;  // 原始类名 (ThreadActors存储, ThreadNearby查kItemNameMap)
    std::string displayName;// 预计算显示名，渲染线程不再每帧查 kItemNameMap/裁剪前缀
    int rarity = 0;         // 稀有度整数
    uint32_t flags = 0;     // RelativeLocation flags，便于诊断加密/明文位置
    bool labelSkip = false; // 口袋/空手等无效物资在元数据阶段标记，避免重复进入绘制路径
};

// 预计算的骨骼世界坐标 (ThreadESP 缓存, 低频更新)
struct BoneCache {
    DWORD64 mesh;           // 用于校验 (mesh地址变了=玩家重生)
    FVector worldBones[14]; // 14根骨骼的世界坐标
    FVector rootPos;        // Root (bone 0) 世界坐标, 避免 ThreadESP 中 GetBoneWithRotation
    FVector headPos;        // Head (bone 16) 世界坐标, 避免 ThreadESP 中 GetBoneWithRotation
    int age = 0;            // 帧老化计数器
    uint64_t lastUpdateMs = 0; // 最后一次真实骨骼 DMA 更新时间，用于避免旧骨骼导致偏框
    bool hasSkeleton = false; // true=worldBones[14] 完整；false=仅 root/head 用于精准方框
};

// 世界坐标 ESP 数据 (工作线程存世界坐标, 渲染线程做 W2S)
// 参考 PUBG_DMA: 工作线程零投影, 渲染线程用最新相机实时 W2S
struct WorldEntry {
    DWORD64 pawn;
    DWORD64 mesh;
    int teamId;
    bool isAI;
    bool hasBones;
    FVector worldTop, worldBot;   // 方框顶/底 世界坐标
    FVector worldBones[14];       // 骨骼世界坐标
    std::string clazz;            // DrawAll 模式用
};

// 玩家信息 (ThreadInfo 异步 DMA, 渲染线程按 pawn 查找)
struct PlayerInfo {
    std::string nameStr;
    std::string weaponName;
    int weaponAmmo = -1;      // 当前武器弹匣子弹数 (-1=未读取)
    int weaponMaxAmmo = -1;   // 弹匣容量
    float armorDur = -1.f;    // 护甲(背心)耐久度 (-1=未读取)
    float armorMax = -1.f;    // 最大护甲耐久
    int   armorLevel = 0;     // 护甲(背心)等级 (0=无, 1-6)
    float helmetDur = -1.f;   // 头盔耐久度 (-1=未读取)
    float helmetMax = -1.f;   // 最大头盔耐久
    int   helmetLevel = 0;    // 头盔等级 (0=无, 1-6)
};

// EFactionType 枚举 (SDK 验证)
enum : uint8_t {
    FT_None = 0, FT_NormalPMC = 1, FT_NormalScav = 2, FT_PlayerScav = 3,
    FT_Bear = 4, FT_Usec = 5, FT_RebelForce = 6, FT_Trapper = 7,
    FT_Boss = 8, FT_BossFollower = 9, FT_Arena = 10, FT_Abyss = 11,
    FT_Spectator = 12, FT_MAX = 13
};

// 战斗数据 (ThreadCombat 采集, 仅保留雷达所需字段)
struct CombatEntry {
    DWORD64 pawn = 0;
    uint8_t factionType = 0;
};

// 雷达轨迹数据
struct RadarTrailEntry {
    static constexpr int TRAIL_MAX = 10;
    DWORD64 pawn = 0;
    FVector currentPos;
    float   yaw = 0.f;
    bool    isAiming = false;
    uint8_t factionType = 0;
    FVector trail[TRAIL_MAX];
    int     trailHead = 0;
    int     trailCount = 0;
};

// 撤离点
struct ExtractionPoint {
    FVector pos;
    std::string name;
};

// 威胁数据 (ThreadThreat 采集, 仅保留雷达所需字段)
struct ThreatEntry {
    DWORD64 pawn = 0;
    bool    isAiming = false;
    float   yaw = 0.f;
};

// 物资地址队列 (ThreadActors 生产 → ThreadNearbyItems 消费, 解耦 actor 扫描与物品 DMA)
struct ItemScanRequest {
    std::vector<std::pair<DWORD64, std::string>> nearbyActors;
};

// 加密物资解密请求 (ThreadNearbyItems 生产 → ThreadItemDecrypt 消费)
struct ItemDecryptRequest {
    std::vector<NearbyEntry> items;
    uint64_t sequence = 0;
};

// 角色缓存
struct ActorCache {
    bool ok = false; bool isH = false;
    std::string cls, type; int ptype = 0; DWORD64 root = 0;
};

// Dirty Flag 机制 — 减少冗余计算
struct DirtyFlags {
    volatile long playersDirty = 1;
    volatile long bonesDirty = 1;
    volatile long cameraDirty = 1;
    volatile long infoDirty = 1;
    volatile long combatDirty = 1;
    volatile long itemsDirty = 1;
    DWORD lastActorCount = 0;
    DWORD lastPlayerCount = 0;
};

// 游戏进程状态
struct GameProcessState {
    DWORD processId = 0;                    // 游戏进程 ID
    HANDLE processHandle = nullptr;         // 游戏进程句柄
    volatile bool isRunning = false;        // 游戏是否正在运行
    volatile bool isPaused = false;         // 是否暂停（游戏加载中）
    volatile LONG errorCount = 0;           // 错误计数（连续失败次数）
    volatile DWORD64 lastValidBase = 0;     // 上次有效的模块基址
    DWORD64 lastHeartbeat = 0;              // 上次心跳时间戳
};

// ═══════════════════════════════════════
//  GameState — 全局游戏状态聚合
// ═══════════════════════════════════════
struct GameState {
    // 基础指针 & 屏幕尺寸
    DWORD64 world = 0;
    DWORD64 base = 0;
    int screenW = 1920;
    int screenH = 1080;

    // 相机数据
    CameraData camera;
    std::shared_mutex camMutex;

    // 玩家列表
    std::shared_mutex dataMutex;
    std::shared_ptr<std::vector<PlayerEntry>> players;

    // DrawAll 对象列表（与真人/AI分离，避免骨骼/信息线程遍历海量静态对象）
    std::shared_mutex drawAllMutex;
    std::shared_ptr<std::vector<PlayerEntry>> drawAllObjects;

    // 附近物品
    std::shared_mutex nearbyMutex;
    std::shared_ptr<std::vector<NearbyEntry>> nearbyItems;

    // 骨骼缓存
    std::shared_mutex boneMutex;
    std::unordered_map<DWORD64, BoneCache> boneCache;

    // 世界坐标 ESP 数据
    std::shared_mutex screenMutex;
    std::shared_ptr<std::vector<WorldEntry>> worldData;

    // 玩家信息
    std::shared_mutex infoMutex;
    std::unordered_map<DWORD64, PlayerInfo> playerInfo;

    // 战斗数据
    std::shared_mutex combatMutex;
    std::unordered_map<DWORD64, CombatEntry> combatData;

    // 雷达轨迹数据
    std::shared_mutex radarMutex;
    std::unordered_map<DWORD64, RadarTrailEntry> radarData;

    // 威胁数据
    std::shared_mutex threatMutex;
    std::unordered_map<DWORD64, ThreatEntry> threatData;

    // 撤离点
    std::shared_mutex extractionMutex;
    std::vector<ExtractionPoint> extractions;

    // 物品扫描请求
    std::shared_mutex itemReqMutex;
    std::shared_ptr<ItemScanRequest> itemReq;

    // 物品位置缓存
    std::unordered_map<DWORD64, FVector> itemPosCache;
    std::shared_mutex itemCacheMutex;

    // 加密物资解密请求
    std::shared_mutex itemDecryptMutex;
    std::shared_ptr<ItemDecryptRequest> itemDecryptReq;

    // 角色缓存
    std::unordered_map<DWORD64, ActorCache> cache;
    DWORD64 cacheBase = 0;

    // 脏标记 & 进程状态
    DirtyFlags dirty;
    GameProcessState gameProcess;

    GameState()
        : players(std::make_shared<std::vector<PlayerEntry>>())
        , drawAllObjects(std::make_shared<std::vector<PlayerEntry>>())
        , nearbyItems(std::make_shared<std::vector<NearbyEntry>>())
        , worldData(std::make_shared<std::vector<WorldEntry>>())
        , itemReq(std::make_shared<ItemScanRequest>())
        , itemDecryptReq(std::make_shared<ItemDecryptRequest>())
    {
    }
};

extern GameState gs;
