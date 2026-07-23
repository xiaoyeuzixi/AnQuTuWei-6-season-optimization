#pragma once
#include <cstdint>

// ════════════════════════════════════════════════════════════════
//  AnQu (UAGame.exe) 内存偏移定义 — 完整类名, 无缩写
// ════════════════════════════════════════════════════════════════

// ── 国服 ──
constexpr std::uintptr_t BaseWorld_CN = 0xAACB9B8;  // UWorld 全局指针相对模块基址偏移
constexpr std::uintptr_t BaseName_CN =  0xB2F4240;    // FNamePool (GNames) 基址相对模块基址偏移
constexpr std::uintptr_t NameKey_CN =   0xB2AC14C;     // FName 解密密钥字节相对模块基址偏移

// ── 国际服 ──
constexpr std::uintptr_t BaseWorld_GL = 0xACB88B8;
constexpr std::uintptr_t BaseName_GL = 0xB4E2FC0;
constexpr std::uintptr_t NameKey_GL = 0xB49A9CC;

// ── 运行时选中版本 (main.cpp 中根据用户选择赋值) ──
extern std::uintptr_t BaseWorld;
extern std::uintptr_t BaseName;
extern std::uintptr_t NameKey;

// ── UWorld / GameInstance / PlayerController 链路 ──
inline constexpr std::uintptr_t Offset_GameInstance       = 0x180;  // UWorld → UGameInstance*
inline constexpr std::uintptr_t Offset_GamePlayer         = 0x38;   // UGameInstance → APlayerController*
inline constexpr std::uintptr_t Offset_PlayerController   = 0x30;   // ULocalPlayer → APlayerController*

// ── AActor 根组件 & 变换 ──
inline constexpr std::uintptr_t Offset_RootComponent      = 0x170;  // AActor → USceneComponent* (根组件)
inline constexpr std::uintptr_t Offset_ActorLocation      = 0x0170;  // USceneComponent → RelativeLocation FVector (12字节, ACE加密)
inline constexpr std::uintptr_t Offset_ActorLocationFlags = 0x017C;  // USceneComponent → RelativeLocation ACE控制DWORD (bit31-29=算法, bit24-0=cache key)

// ── RootComponent ComponentToWorld (SceneComponent 基类, 与 Mesh 的 CTW 偏移不同) ──
// IDA sub_140B20E7C 确认: 数据@+0x220(544), 标志@+0x250(592)
inline constexpr std::uintptr_t Offset_RootComponentToWorld      = 0x0220;  // RootComponent → ComponentToWorld FTransform (48字节)
inline constexpr std::uintptr_t Offset_RootComponentToWorldFlags = 0x0250;  // RootComponent → ComponentToWorld ACE标志
inline constexpr std::uintptr_t Offset_ActorPitch         = 0x200;  // 旋转 Pitch
inline constexpr std::uintptr_t Offset_ActorYaw           = 0x204;  // 旋转 Yaw
inline constexpr std::uintptr_t Offset_ActorRoll          = 0x1FC;  // 旋转 Roll

// ── ULevel → Actors 数组 ──
inline constexpr std::uintptr_t Offset_PersistentLevel    = 0x30;   // UWorld → ULevel* (当前关卡)
inline constexpr std::uintptr_t Offset_LevelActors        = 0x98;   // ULevel → TArray<AActor*> Actors

// ── 骨骼 & 网格 (ASkeletalMeshComponent) ──
// ★Mesh 的 ComponentToWorld 也在 0x220 (和 RootComponent 一样, 都是 USceneComponent 子类)
//   0x598 不是 CTW, 是其他字段 (bone_diag.log 验证)
inline constexpr std::uintptr_t Offset_ActorMesh              = 0x0388;  // ACharacter → USkeletalMeshComponent*
inline constexpr std::uintptr_t Offset_BoneArray              = 0x0558;  // USkeletalMeshComponent → 骨骼坐标数组 (bone_diag.log 验证: 0x558有指针, 0x518=0)
inline constexpr std::uintptr_t Offset_BoneArray2             = 0x0568;  // USkeletalMeshComponent → 第二骨骼数组 (0x558+0x10)
inline constexpr std::uintptr_t Offset_ComponentToWorld       = 0x0220;  // ★已废弃 — 使用 Offset_RootComponentToWorld (相同偏移)
inline constexpr std::uintptr_t Offset_ComponentToWorldFlags  = 0x0250;  // ★已废弃 — 使用 Offset_RootComponentToWorldFlags (相同偏移)
inline constexpr std::uintptr_t Offset_SkeletalMesh           = 0x0528;  // USkeletalMeshComponent → USkeletalMesh*
inline constexpr std::uintptr_t Offset_BoneNames              = 0x01F0;  // USkeletalMesh → RefSkeleton 骨骼名数组 (TArray<FMeshBoneInfo>)

// ── 玩家状态 & 队伍 (ASGCharacter → APlayerState) ──
inline constexpr std::uintptr_t Offset_PlayerState            = 0x0348;  // ASGCharacter → APlayerState*
inline constexpr std::uintptr_t Offset_APawn                  = 0x0398;  // APlayerController → APawn*
inline constexpr std::uintptr_t Offset_PlayerNamePrivate      = 0x03F8;  // APlayerState → 玩家名 FString
inline constexpr std::uintptr_t Offset_TeamId                 = 0x0598;  // APlayerState → 队伍ID int32

// ── 血量系统 (ASGCharacter → UAbilitySystemComponent) ──
inline constexpr std::uintptr_t Offset_AbilitySystemComponent = 0x17C8;  // ASGCharacter → UAbilitySystemComponent* (SDK: 0x17C8)
inline constexpr std::uintptr_t Offset_AbilitySetData         = 0x0188;  // UAbilitySystemComponent → 属性集子指针
inline constexpr std::uintptr_t Offset_HealthAttributeArray   = 0x0030;  // 属性集 → 血量数组指针 (7部位血量)

// ── 武器系统 (ASGCharacter → USGWeaponManagerComponent) ──
inline constexpr std::uintptr_t Offset_WeaponManagerComponent = 0x1908;  // ASGCharacter → USGWeaponManagerComponent* (SDK: 0x1908)
inline constexpr std::uintptr_t Offset_CurrentWeapon          = 0x01F8;  // USGWeaponManagerComponent → ASGInventory* (当前武器)

// ── 物资容器 (ASGInventory 子类 → 容器组件) ──
inline constexpr std::uintptr_t Offset_StorageContainer       = 0x0910;  // 容器Actor → USGInventoryContainerMgrComponent*
inline constexpr std::uintptr_t Offset_InventoryBag            = 0x0140;  // USGInventoryContainerMgrComponent → 物品列表 TArray

// ── 物品名 & 数据读取 (ASGInventory → USGInventoryCommonDataComponent) ──
inline constexpr std::uintptr_t Offset_CommonDataComponent    = 0x0760;  // ASGInventory → USGInventoryCommonDataComponent*
inline constexpr std::uintptr_t Offset_DisplayName            = 0x0140;  // USGInventoryCommonDataComponent → DisplayName FText

// ── 物品价值 & 属性 (USGInventoryCommonDataComponent 内字段) ──
inline constexpr std::uintptr_t Offset_TotalCount             = 0x0104;  // int32  堆叠数量
inline constexpr std::uintptr_t Offset_StandardPrice          = 0x010C;  // int32  标准价格 (原价)
inline constexpr std::uintptr_t Offset_Rarity                 = 0x0110;  // int32  稀有度
inline constexpr std::uintptr_t Offset_Durability             = 0x0118;  // float  当前耐久度
inline constexpr std::uintptr_t Offset_DurabilityMax          = 0x011C;  // float  最大耐久度
inline constexpr std::uintptr_t Offset_SellPrice              = 0x0134;  // uint32 卖出价格 (回收价)
inline constexpr std::uintptr_t Offset_Weight                 = 0x01DC;  // float  重量

// ── ASGInventory 基类字段 ──
inline constexpr std::uintptr_t Offset_ItemId                 = 0x06C0;  // uint64 物品类型ID
inline constexpr std::uintptr_t Offset_ArmorLevel             = 0x06C8;  // uint32 护甲等级 (1-6)
inline constexpr std::uintptr_t Offset_InventoryType          = 0x0768;  // enum ESGInventoryType 物品类型

// ── 武器弹药链 (ASGInventory(武器) → USGWeaponAmmoComponent) ──
// 调用链: pawn → WeaponManagerComponent(0x1908) → CurrentWeapon(0x1F8) → WeaponAmmoComponent(0xBC8) → ClipAmmoCount(0x12C)
inline constexpr std::uintptr_t Offset_WeaponAmmoComponent    = 0x0BC8;  // ASGInventory(武器) → USGWeaponAmmoComponent*
inline constexpr std::uintptr_t Offset_ClipAmmoCount          = 0x012C;  // int32  弹匣内剩余子弹数
inline constexpr std::uintptr_t Offset_MaxAmmoCount           = 0x0108;  // int32  弹匣容量 (OriginalClipAmmoCount)

// ── 玩家装备库存组件 (ASGCharacter → USGCharacterInventoryComponent) ──
inline constexpr std::uintptr_t Offset_CharacterInventoryComponent = 0x1A78;  // ASGCharacter → USGCharacterInventoryManagerComponent* (SDK: 0x1A78)

// ── 护甲管理组件 (ASGCharacter → USGCharacterArmorManagerComponent) ──
// SDK: USGCharacterArmorManagerComponent 内
//   ArmorList (TArray<ASGInventory*>) @ 0x278
//   GetHelmet()/GetVest() 等返回 ArmorList 中对应槽位的 ASGInventory*
inline constexpr std::uintptr_t Offset_ArmorManagerComponent = 0x1AA0;  // ASGCharacter → USGCharacterArmorManagerComponent* (SDK: 0x1AA0)
inline constexpr std::uintptr_t Offset_ArmorList             = 0x0278;  // USGCharacterArmorManagerComponent → TArray<ASGInventory*> ArmorList

// ── 相机缓存 (APlayerController → APlayerCameraManager) ──
inline constexpr std::uintptr_t Offset_CameraManager          = 0x03B0;  // APlayerController → APlayerCameraManager*
inline constexpr std::uintptr_t Offset_CameraCache            = 0x20E0;  // APlayerCameraManager → 相机缓存 (位置/旋转/FOV)
inline constexpr std::uintptr_t Offset_UObjectFNameIndex       = 0x0020;  // UObject → FName index (用于类名解析)
inline constexpr std::uintptr_t Offset_RelativeLocation       = 0x017C;  // (废弃, 与 Offset_ActorLocationFlags 相同)

// ── 战斗辅助系统偏移 (SDK DUMP 验证) ──
// 瞄准预警
inline constexpr std::uintptr_t Offset_AnimInstance           = 0x01C8;  // USkeletalMeshComponent → UAnimInstance*
inline constexpr std::uintptr_t Offset_bIsAiming              = 0x0A64;  // UAnimInstance → bIsAiming (bool)

// 角色阵营 & 状态 (ASGCharacter 内)
inline constexpr std::uintptr_t Offset_FactionType            = 0x1650;  // ASGCharacter → EFactionType (uint8)
inline constexpr std::uintptr_t Offset_CharacterSex           = 0x1651;  // ASGCharacter → ECharacterSex (uint8)
inline constexpr std::uintptr_t Offset_GID                    = 0x1648;  // ASGCharacter → GID (uint64)
inline constexpr std::uintptr_t Offset_DesiredFOV             = 0x1B38;  // ASGCharacter → DesiredFOV (float)
inline constexpr std::uintptr_t Offset_HealthDirect           = 0x1BF4;  // ASGCharacter → Health (float, SDK: 0x1BF4)
inline constexpr std::uintptr_t Offset_HealthComponent        = 0x1A58;  // ASGCharacter → USGCharacterHealthComponent*
inline constexpr std::uintptr_t Offset_DBNOComponent          = 0x1910;  // ASGCharacter → USGCharacterDBNOComponent* (SDK: DeathComponent 0x1910)
inline constexpr std::uintptr_t Offset_SGCharacterMovement    = 0x16E8;  // ASGCharacter → USGCharacterMovementComponent*

// 移动状态 (UCharacterMovementComponent 内)
inline constexpr std::uintptr_t Offset_MovementMode           = 0x01CC;  // UCharacterMovementComponent → EMovementMode (uint8)
inline constexpr std::uintptr_t Offset_bIsCrouched            = 0x0440;  // UCharacterMovementComponent → bIsCrouched (bool)

// 投掷物类型 (ASGThrowableProjectile 内)
inline constexpr std::uintptr_t Offset_ThrowableType          = 0x05E8;  // ASGThrowableProjectile → ESGThrowSubType (uint8)
inline constexpr std::uintptr_t Offset_ThrowableFaction       = 0x0600;  // ASGThrowableProjectile → EFactionType (uint8)

// 控制器旋转 (AController)
inline constexpr std::uintptr_t Offset_ControlRotation        = 0x0380;  // AController → ControlRotation (FRotator)
inline constexpr std::uintptr_t Offset_PlayerControllerRot    = 0x0438;  // APlayerController → ControlRotation (FRotator, 备用)

// ── ACE 加密系统 ──
// 算法在国服/国际服完全一致, 仅 CacheTable RVA 不同
// 运行时根据 g_IsGL 选择对应的 RVA (main.cpp 启动时赋值)
inline std::uintptr_t ACE_CacheTable_RVA = 0xAF8A3D0;  // 默认国际服, main.cpp 中按版本切换
inline constexpr std::uintptr_t ACE_CacheTable_RVA_GL  = 0xAF8A3D0;  // 国际服
  inline constexpr std::uintptr_t ACE_CacheTable_RVA_CN  = 0xAD964F0;  // 国服
inline constexpr uint32_t       ACE_DeadActorSentinel = 0xFF8B6D2B; // 死亡actor哨兵值
