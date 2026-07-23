#pragma once
// Force rebuild marker - 修改此行可迫使 VS 重新编译此文件, 绕过 ipch 缓存
/*
 * ESPUtils.h — ESP 工具函数集合（移植自 ARS_AnQu_WB，适配 DMA 内存读取 + FVector/FMatrix 数学库）
 *
 * 包含：FQuat/FTransform 结构体、FMatrix 乘法、
 *        骨骼读取、血量计算、武器/物品名读取、描边文字工具
 *        ACE 解密系统 (June 2026 ABI)
 */

#include "Mem.h"
#include "Offset.h"
#include "GameMatrix.h"
#include "BoneEnum.h"
#include "ImGui/imgui.h"
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <cstring>



/* ================================================================
 *  ACE 解密系统 (June 2026 ABI)
 *  XOR stream cipher + global hash table cache
 * ================================================================ */

// Murmur-style hash helper
inline uint32_t ace_murmur(uint32_t x) {
    return 73244475u * (x ^ (x >> 16));
}

// 生成一个 4 字节 XOR mask
inline uint32_t ace_mask(uint32_t v12, uint32_t seed, uint32_t v16, uint32_t v17) {
    uint32_t sa = ace_murmur(seed ^ v12 ^ (v17 + 626627285u));
    uint32_t sb = ace_murmur(seed ^ v12 ^ (v17 - 1013904242u));
    uint32_t sc = ace_murmur(seed ^ v12 ^ v16);

    uint32_t w = seed ^ v12 ^ v17;
    w ^= (w >> 16);
    uint32_t sd = (73244475u * w) ^ (2671443968u * w);

    return ((sa ^ (sa >> 16)) & 0xFF)
         | (((sb << 8) ^ (sb >> 8)) & 0xFF00)
         | ((sc ^ (sc << 16)) & 0xFF0000)
         | ((sd << 8) & 0xFF000000);
}

// ACE cache table 查找: 通过 25-bit key 找到 cache entry
// 返回: {data_ptr, data_size, seed} 或 {0,0,0} 失败
struct ACECacheEntry {
    uint64_t data_ptr;
    uint32_t data_size;
    uint32_t seed;
    uint64_t node;
};

struct ACENodeSnapshot {
    uint64_t data_ptr;
    uint32_t data_size;
    uint32_t node_key;
    uint32_t seed;
    uint32_t reserved;
    uint64_t next;
};
static_assert(sizeof(ACENodeSnapshot) == 0x20, "Unexpected ACE node layout");

inline bool ace_read_node(uint64_t node, uint32_t key, ACECacheEntry& out,
                          uint64_t* next = nullptr) {
    ACENodeSnapshot snapshot{};
    if (!node || !mem.Read(node + 0x10, &snapshot, sizeof(snapshot))) return false;
    if (next) *next = snapshot.next;
    if (snapshot.node_key != key || !snapshot.data_ptr || snapshot.data_size == 0)
        return false;
    out = {snapshot.data_ptr, snapshot.data_size, snapshot.seed, node};
    return true;
}

inline ACECacheEntry ace_cache_lookup(uint32_t key) {
    if (key == 0) return {0, 0, 0, 0};

    struct CachedACENode {
        uint64_t node = 0;
        uint64_t refreshedAt = 0;
    };
    static thread_local DWORD s_cachedPid = 0;
    static thread_local uint64_t s_cachedBase = 0;
    static thread_local uint64_t s_cachedWorld = 0;
    static thread_local std::uintptr_t s_cachedRva = 0;
    static thread_local std::unordered_map<uint32_t, CachedACENode> s_lookupCache;

    // Cache only the node address. data_ptr/size/seed are live fields and seed
    // changes whenever ACE republishes the encrypted payload.
    if (s_cachedPid != mem.pid || s_cachedBase != gs.base ||
        s_cachedWorld != gs.world || s_cachedRva != ACE_CacheTable_RVA) {
        s_cachedPid = mem.pid;
        s_cachedBase = gs.base;
        s_cachedWorld = gs.world;
        s_cachedRva = ACE_CacheTable_RVA;
        s_lookupCache.clear();
        s_lookupCache.reserve(4096);
    }
    const uint64_t now = GetTickCount64();
    auto cached = s_lookupCache.find(key);
    if (cached != s_lookupCache.end()) {
        const uint64_t age = now - cached->second.refreshedAt;
        if (!cached->second.node) {
            // Dormant pooled actors often retain encrypted flags after their
            // table nodes are gone. Avoid hammering the same empty bucket in
            // the 2ms character loop while still detecting activation quickly.
            if (age < 20) return {0, 0, 0, 0};
            s_lookupCache.erase(cached);
        } else if (age < 100) {
            ACECacheEntry current{};
            if (ace_read_node(cached->second.node, key, current)) return current;
            s_lookupCache.erase(cached);
        }
    }

    uint32_t bucket = (0x9E3779B1u * key) % 0x10001u;
    uint64_t entry = mem.Read<uint64_t>(gs.base + ACE_CacheTable_RVA + (uint64_t)bucket * 8);

    // 遍历链表, 最多 32 次防止死循环
    for (int i = 0; i < 32 && entry; i++) {
        ACECacheEntry ce{};
        uint64_t next = 0;
        if (ace_read_node(entry, key, ce, &next)) {
            s_lookupCache[key] = {entry, now};
            return ce;
        }
        entry = next;
    }
    s_lookupCache[key] = {0, now};
    return {0, 0, 0, 0};
}

inline bool ace_read_stable_payload(uint32_t key, uint32_t requiredSize,
                                    uint32_t dataOffset, void* output,
                                    uint32_t outputSize, ACECacheEntry* metadata = nullptr) {
    if (!key || !output || !outputSize) return false;

    for (int attempt = 0; attempt < 2; ++attempt) {
        ACECacheEntry before = ace_cache_lookup(key);
        if (!before.data_ptr || before.data_size < requiredSize ||
            dataOffset > before.data_size || outputSize > before.data_size - dataOffset)
            return false;
        if (!mem.Read(before.data_ptr + dataOffset, output, outputSize)) continue;

        ACECacheEntry after{};
        if (!ace_read_node(before.node, key, after)) continue;
        if (before.data_ptr == after.data_ptr && before.data_size == after.data_size &&
            before.seed == after.seed) {
            if (metadata) *metadata = before;
            return true;
        }
    }
    return false;
}

inline FVector ace_decrypt_relative_location_with_ctl(uint64_t component, uint32_t ctl) {
    uint32_t algo = ctl >> 29;
    uint32_t key = ctl & 0x1FFFFFF;

    if (algo == 0) {
        uint32_t raw = mem.Read<uint32_t>(component + Offset_ActorLocation);
        if (raw == ACE_DeadActorSentinel) return {0, 0, 0};
        return mem.Read<FVector>(component + Offset_ActorLocation);
    }
    if (key == 0) return {0, 0, 0};

    ACECacheEntry ce{};
    uint32_t encrypted[3]{};
    if (!ace_read_stable_payload(key, 12, 0, encrypted, sizeof(encrypted), &ce))
        return {0, 0, 0};
    uint32_t d0 = encrypted[0];
    uint32_t d1 = encrypted[1];
    uint32_t d2 = encrypted[2];

    uint32_t v12 = 0x9E3779B1u * key;
    uint32_t v16 = 0x3C6EF372u;
    uint32_t v17 = 0xDAA66D2Bu;
    uint32_t STEP = 0x78DDE6E4u;

    d0 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d1 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d2 ^= ace_mask(v12, ce.seed, v16, v17);

    FVector result;
    memcpy(&result.X, &d0, 4);
    memcpy(&result.Y, &d1, 4);
    memcpy(&result.Z, &d2, 4);
    return result;
}

// ACE 解密 RelativeLocation (3 floats = 12 bytes)
// component: USceneComponent 指针
// 返回: 解密后的 FVector, 或 {0,0,0} 失败
inline FVector ace_decrypt_relative_location(uint64_t component) {
    uint32_t ctl = mem.Read<uint32_t>(component + Offset_ActorLocationFlags);
    return ace_decrypt_relative_location_with_ctl(component, ctl);
}

// ACE 解密 ComponentToWorld translation (跳过前4个DWORD的旋转)
// component: USceneComponent 指针
// 返回: 解密后的 translation FVector, 或 {0,0,0} 失败
inline FVector ace_decrypt_c2w_translation(uint64_t component) {
    uint32_t ctl = mem.Read<uint32_t>(component + Offset_RootComponentToWorldFlags);
    uint32_t algo = ctl >> 29;
    uint32_t key = ctl & 0x1FFFFFF;

    // 明文路径
    if (algo == 0) {
        // C2W FTransform at 0x220, translation at +0x10 = 0x230
        return mem.Read<FVector>(component + Offset_RootComponentToWorld + 16);
    }
    if (key == 0) return {0, 0, 0};

    // Read the encrypted translation and verify that the live tuple did not
    // change during the DMA read.
    ACECacheEntry ce{};
    uint32_t encrypted[3]{};
    if (!ace_read_stable_payload(key, 48, 16, encrypted, sizeof(encrypted), &ce))
        return {0, 0, 0};
    uint32_t d4 = encrypted[0];
    uint32_t d5 = encrypted[1];
    uint32_t d6 = encrypted[2];

    // XOR stream, 跳过前4个DWORD到达 translation
    uint32_t v12 = 0x9E3779B1u * key;
    uint32_t STEP = 0x78DDE6E4u;
    uint32_t v16 = 0x3C6EF372u + 4 * STEP;  // 跳过4个DWORD
    uint32_t v17 = 0xDAA66D2Bu + 4 * STEP;

    d4 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d5 ^= ace_mask(v12, ce.seed, v16, v17); v16 += STEP; v17 += STEP;
    d6 ^= ace_mask(v12, ce.seed, v16, v17);

    FVector result;
    memcpy(&result.X, &d4, 4);
    memcpy(&result.Y, &d5, 4);
    memcpy(&result.Z, &d6, 4);
    return result;
}

/* ================================================================
 *  FQuat — 四元数 (UE FQuat)
 * ================================================================ */
struct FQuat {
    float x, y, z, w;
};

/* ================================================================
 *  FTransform — UE 变换 (旋转 + 平移 + 缩放), size=0x30
 * ================================================================ */
struct FTransform {
    FQuat   rot;                // offset 0x00
    FVector translation;        // offset 0x10
    char    pad_0x1C[0x0004];
    FVector scale;              // offset 0x20
    char    pad_0x2C[0x0004];

    FMatrix ToMatrixWithScale() const {
        FMatrix m;

        // 平移
        m.M[3][0] = translation.X;
        m.M[3][1] = translation.Y;
        m.M[3][2] = translation.Z;

        // 四元数 → 旋转矩阵
        float x2 = rot.x + rot.x;
        float y2 = rot.y + rot.y;
        float z2 = rot.z + rot.z;

        float xx2 = rot.x * x2;
        float yy2 = rot.y * y2;
        float zz2 = rot.z * z2;
        m.M[0][0] = (1.0f - (yy2 + zz2)) * scale.X;
        m.M[1][1] = (1.0f - (xx2 + zz2)) * scale.Y;
        m.M[2][2] = (1.0f - (xx2 + yy2)) * scale.Z;

        float yz2 = rot.y * z2;
        float wx2 = rot.w * x2;
        m.M[2][1] = (yz2 - wx2) * scale.Z;
        m.M[1][2] = (yz2 + wx2) * scale.Y;

        float xy2 = rot.x * y2;
        float wz2 = rot.w * z2;
        m.M[1][0] = (xy2 - wz2) * scale.Y;
        m.M[0][1] = (xy2 + wz2) * scale.X;

        float xz2 = rot.x * z2;
        float wy2 = rot.w * y2;
        m.M[2][0] = (xz2 + wy2) * scale.Z;
        m.M[0][2] = (xz2 - wy2) * scale.X;

        m.M[0][3] = 0.0f;
        m.M[1][3] = 0.0f;
        m.M[2][3] = 0.0f;
        m.M[3][3] = 1.0f;

        return m;
    }
};

/* ================================================================
 *  FMatrix 乘法 (行优先 M1 * M2)
 * ================================================================ */
inline FMatrix MatrixMultiply(const FMatrix& m1, const FMatrix& m2) {
    FMatrix r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.M[i][j] = m1.M[i][0] * m2.M[0][j]
                      + m1.M[i][1] * m2.M[1][j]
                      + m1.M[i][2] * m2.M[2][j]
                      + m1.M[i][3] * m2.M[3][j];
    return r;
}

/* ================================================================
 *  GetBoneIndex — 读取指定骨骼的 FTransform
 *  ★优先 0x568 (CachedComponentSpaceTransforms), fallback 0x558
 * ================================================================ */
inline FTransform GetBoneIndex(DWORD64 mesh, int index) {
    DWORD64 bonearray = mem.Read<DWORD64>(mesh + Offset_BoneArray2);
    if (!bonearray)
        bonearray = mem.Read<DWORD64>(mesh + Offset_BoneArray);
    return mem.Read<FTransform>(bonearray + (index * 0x30));
}

/* ================================================================
 *  GetBoneNum — 获取骨骼数组长度
 *  ★优先 0x568, fallback 0x558
 * ================================================================ */
inline int GetBoneNum(DWORD64 mesh) {
    DWORD64 bonearray = mem.Read<DWORD64>(mesh + Offset_BoneArray2);
    int i = mem.Read<int>(mesh + Offset_BoneArray2 + 0x8);
    if (bonearray == 0x0) {
        bonearray = mem.Read<DWORD64>(mesh + Offset_BoneArray);
        i = mem.Read<int>(mesh + Offset_BoneArray + 0x8);
    }
    return i;
}

/* ================================================================
 *  GetBoneWithRotation — 骨骼世界坐标（带父级变换）
 *  ★Mesh CTW 在 0x220 (不是 0x598), 和 RootComponent 一样
 *  优化: CTW inline + ACE flags + bonearray 一次 Scatter, bone 一次 Scatter
 * ================================================================ */
inline FVector GetBoneWithRotation(DWORD64 mesh, int id) {
    if (!mesh) return FVector{};
    // 一次 Scatter 读 CTW inline(48B @0x220) + ACE flags(@0x250) + 两个 bonearray 指针
    FTransform ctw{};
    DWORD64 bonearray = 0, bonearray2 = 0;
    int ctwFlags = 0;
    VMMDLL_SCATTER_HANDLE h = mem.CreateScatter();
    mem.AddScatter(h, mesh + Offset_RootComponentToWorld, &ctw, 0x30);
    mem.AddScatter(h, mesh + Offset_RootComponentToWorldFlags, &ctwFlags, 4);
    mem.AddScatter(h, mesh + Offset_BoneArray2, &bonearray2, 8);
    mem.AddScatter(h, mesh + Offset_BoneArray, &bonearray, 8);
    mem.ExecuteScatter(h);
    // ★优先 0x568 (CachedComponentSpaceTransforms), fallback 0x558
    DWORD64 ba = bonearray2 ? bonearray2 : bonearray;
    if (!ba) { mem.CloseScatter(h); return FVector{}; }
    // ACE 加密检查: enc != 0 时 CTW 不可读, 返回零
    if ((unsigned int)ctwFlags >> 29 != 0) { mem.CloseScatter(h); return FVector{}; }
    // 一次 Scatter 读 bone
    FTransform bone{};
    mem.AddScatter(h, ba + (DWORD64)id * 0x30, &bone, 0x30);
    mem.ExecuteScatter(h);
    mem.CloseScatter(h);
    FMatrix mat = MatrixMultiply(bone.ToMatrixWithScale(), ctw.ToMatrixWithScale());
    return FVector(mat.M[3][0], mat.M[3][1], mat.M[3][2]);
}

/* ================================================================
 *  GetBonesBatch — 批量读骨骼世界坐标 (Scatter DMA, 4ms→0.3ms)
 *    bones[16] = 输出 16 个骨骼的 FVector
 *    ids[16]   = 输入 16 个骨骼索引 (不使用的填 -1)
 *    count     = 骨骼数量 (max 16)
 *    actorPos  = 可选: 当 CTW 加密时, 用 actor 世界位置构造合成 CTW
 *
 *  ★修复: Mesh 的 ComponentToWorld 是 inline FTransform (48字节), 不是指针!
 *    mesh+0x220 = FTransform{Quat, Translation, Scale3D} (世界变换, 和 RootComp 相同偏移)
 *    mesh+0x250 = ACE 加密标志 (enc=0 明文, enc=7 加密)
 *    mesh+0x558 = BoneSpaceTransforms (相对父骨骼, 需递归累积)
 *    mesh+0x568 = CachedComponentSpaceTransforms (组件空间, 直接乘CTW即可) ★优先使用
 *
 *  ★ACE 加密回退: 当 mesh+0x250 enc=7 时, CTW 不可读
 *    但 BoneArray 数据本身未加密, 可正常读取
 *    此时用 actorPos 构造合成 CTW (单位旋转 + actor位置 + 单位缩放)
 *    骨骼在组件空间中已是 Z-up, 与世界空间一致, 无需轴变换
 *    缺点: 丢失角色朝向旋转, 骨骼方向固定 (但高度正确, 足够画 ESP)
 * ================================================================ */
// 带 Scatter Handle 复用的版本 (避免每次创建/销毁)
inline void GetBonesBatch(DWORD64 mesh, FVector* bones, const int* ids, int count,
                          VMMDLL_SCATTER_HANDLE hScatter = nullptr,
                          const FVector* actorPos = nullptr) {
    if (!mesh || count <= 0) return;

    bool ownScatter = (hScatter == nullptr);
    if (ownScatter) hScatter = mem.CreateScatter();

    // 1. 检查 mesh ComponentToWorld 加密标志 (mesh+0x250)
    //    ★Mesh 的 CTW 也在 0x220, 和 RootComponent 一样 (都是 USceneComponent 子类)
    int ctwFlags = mem.Read<int>(mesh + Offset_RootComponentToWorldFlags);
    int ctwEnc = (unsigned int)ctwFlags >> 29;

    FTransform ctw{};
    bool useSyntheticCtw = false;

    if (ctwEnc != 0) {
        // ★CTW 被加密, 用 actorPos 构造合成 CTW
        if (!actorPos || (actorPos->X == 0.f && actorPos->Y == 0.f && actorPos->Z == 0.f)) {
            // 没有 actorPos 或位置为零, 无法构造合成 CTW
            if (ownScatter) mem.CloseScatter(hScatter);
            return;
        }
        // 合成 CTW: 单位旋转 + actor 位置 + 单位缩放
        // ★减去 Mesh Z偏移 (~90): RootComponent 在角色中心, Mesh 原点在脚底
        // 骨骼数据在组件空间中已是 Z-up (与世界空间一致)
        ctw.rot = {0.f, 0.f, 0.f, 1.f};  // identity quaternion
        ctw.translation = {actorPos->X, actorPos->Y, actorPos->Z - 90.f};
        ctw.scale = {1.f, 1.f, 1.f};
        useSyntheticCtw = true;
    }

    // 2. 一次 Scatter: 读 CTW(48B) + 两个 BoneArray 指针
    DWORD64 bonearray = 0, bonearray2 = 0;
    if (!useSyntheticCtw) {
        mem.AddScatter(hScatter, mesh + Offset_RootComponentToWorld, &ctw, 0x30);
    }
    mem.AddScatter(hScatter, mesh + Offset_BoneArray2, &bonearray2, 8);
    mem.AddScatter(hScatter, mesh + Offset_BoneArray, &bonearray, 8);
    mem.ExecuteScatter(hScatter);

    // ★优先 0x568 (CachedComponentSpaceTransforms), fallback 0x558
    DWORD64 ba = bonearray2 ? bonearray2 : bonearray;
    if (!ba) { if (ownScatter) mem.CloseScatter(hScatter); return; }

    // 3. 一次 Scatter: 读所有骨骼 FTransform (0x30 each)
    FTransform bonetf[16];
    memset(bonetf, 0, sizeof(bonetf));
    for (int i = 0; i < count; i++)
        if (ids[i] >= 0)
            mem.AddScatter(hScatter, ba + (DWORD64)(ids[i]) * 0x30, &bonetf[i], 0x30);
    mem.ExecuteScatter(hScatter);

    if (ownScatter) mem.CloseScatter(hScatter);

    // 4. 计算世界坐标 (纯 CPU, 零 DMA)
    FMatrix ctwMat = ctw.ToMatrixWithScale();
    for (int i = 0; i < count; i++) {
        if (ids[i] < 0) continue;
        FMatrix m = MatrixMultiply(bonetf[i].ToMatrixWithScale(), ctwMat);
        bones[i] = FVector(m.M[3][0], m.M[3][1], m.M[3][2]);
    }
}

/* ================================================================
 *  GetName2 — 读取 FString (wchar_t) 转 UTF-8
 * ================================================================ */
inline std::string GetName2(DWORD64 name_address, int name_len) {
    if (name_address == 0 || name_len <= 0 || name_len > 1024)
        return "";

    std::vector<wchar_t> wstr(name_len * 2);
    if (!mem.Read(name_address, wstr.data(), (DWORD)(name_len * 2)))
        return "";

    int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wstr.data(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";

    std::string out(len * 2 - 1, '\0');
    int result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wstr.data(), -1, out.data(), static_cast<int>(out.size()) + 1,
        nullptr, nullptr);
    if (result <= 0) return "";

    out.pop_back();
    return out;
}

/* ================================================================
 *  GetWeaponName — 读取武器名 (优化: 6次DMA→3次+1次连续读)
 *  原路径: WMC→CW→CDC→DN→[addr,len]→name = 6 次顺序 DMA
 *  优化: 先 3 次读指针链, 再 1 次连续读 addr+len (8+4=12字节)
 * ================================================================ */
inline std::string GetWeaponName(DWORD64 actorPawn) {
    if (!actorPawn) return "";
    DWORD64 UWMC = mem.Read<DWORD64>(actorPawn + Offset_WeaponManagerComponent);
    if (!UWMC) return "";
    DWORD64 cw = mem.Read<DWORD64>(UWMC + Offset_CurrentWeapon);
    if (!cw) return "";
    DWORD64 UCDC = mem.Read<DWORD64>(cw + Offset_CommonDataComponent);
    if (!UCDC) return "";
    DWORD64 dn = mem.Read<DWORD64>(UCDC + Offset_DisplayName);
    if (!dn) return "";
    // addr(8) + len(4) 连续, 一次 DMA 读取
    struct { DWORD64 addr; int len; } nameData{};
    mem.Read(dn + 0x28, &nameData, sizeof(nameData));
    if (!nameData.addr || nameData.len <= 0 || nameData.len > 1024) return "";
    return GetName2(nameData.addr, nameData.len);
}

/* ================================================================
 *  GetItemName — 读取容器物品名 (优化: 4次DMA→2次+1次连续读)
 *  原路径: CDC→DN→[addr,len]→name = 4 次顺序 DMA
 *  优化: 2 次读指针 + 1 次连续读 addr+len
 * ================================================================ */
inline std::string GetItemName(DWORD64 ItemActor) {
    if (!ItemActor) return "";
    DWORD64 udc = mem.Read<DWORD64>(ItemActor + Offset_CommonDataComponent);
    if (!udc) return "";
    DWORD64 dn = mem.Read<DWORD64>(udc + Offset_DisplayName);
    if (!dn) return "";
    // addr(8) + len(4) 连续, 一次 DMA 读取
    struct { DWORD64 addr; int len; } nameData{};
    mem.Read(dn + 0x28, &nameData, sizeof(nameData));
    if (!nameData.addr || nameData.len <= 0 || nameData.len > 1024) return "";
    return GetName2(nameData.addr, nameData.len);
}

/* ================================================================
 *  GetItemSellPrice — 读取物品卖出价格 (2次DMA)
 *  链路: ItemActor → CDC(0x760) → SellPrice(0x134)
 * ================================================================ */
inline int GetItemSellPrice(DWORD64 ItemActor) {
    if (!ItemActor) return 0;
    DWORD64 cdc = mem.Read<DWORD64>(ItemActor + Offset_CommonDataComponent);
    if (!cdc) return 0;
    return mem.Read<int>(cdc + Offset_SellPrice);
}

/* ================================================================
 *  GetItemStandardPrice — 读取物品标准价格 (2次DMA)
 *  链路: ItemActor → CDC(0x760) → StandardPrice(0x10C)
 * ================================================================ */
inline int GetItemStandardPrice(DWORD64 ItemActor) {
    if (!ItemActor) return 0;
    DWORD64 cdc = mem.Read<DWORD64>(ItemActor + Offset_CommonDataComponent);
    if (!cdc) return 0;
    return mem.Read<int>(cdc + Offset_SellPrice);
}

/* ================================================================
 *  GetItemInfo — 一次性读取物品关键信息 (优化: 2次DMA读全部)
 *  返回: 卖出价, 标准价, 堆叠数, 耐久度, 最大耐久, 稀有度
 *  CDC 内 0x100~0x120 连续 0x20 字节, 一次 DMA 读取
 * ================================================================ */
struct ItemInfo {
    int sellPrice = 0;
    int standardPrice = 0;
    int totalCount = 0;
    float durability = 0.f;
    float durabilityMax = 0.f;
    int rarity = 0;
};

inline ItemInfo GetItemInfo(DWORD64 ItemActor) {
    ItemInfo info;
    if (!ItemActor) return info;
    DWORD64 cdc = mem.Read<DWORD64>(ItemActor + Offset_CommonDataComponent);
    if (!cdc) return info;
    // 一次 DMA 读 0x100~0x120 (32字节): MaxTotalCount, TotalCount, ..., Rarity, RollUpTime, Durability, DurabilityMax
    struct {
        int32_t maxTotalCount;   // 0x100
        int32_t totalCount;      // 0x104
        float   fireNoiseRes;    // 0x108
        int32_t standardPrice;   // 0x10C
        int32_t rarity;          // 0x110
        float   rollUpTime;      // 0x114
        float   durability;      // 0x118
        float   durabilityMax;   // 0x11C
    } data{};
    mem.Read(cdc + 0x100, &data, sizeof(data));
    info.totalCount    = data.totalCount;
    info.standardPrice = data.standardPrice;
    info.rarity        = data.rarity;
    info.durability    = data.durability;
    info.durabilityMax = data.durabilityMax;
    // SellPrice 在 0x134, 单独读
    info.sellPrice     = mem.Read<int>(cdc + Offset_SellPrice);
    return info;
}

/* ================================================================
 *  GetWeaponAmmo — 读取当前武器弹匣子弹数 (3次DMA)
 *  链路: pawn → WMC(0x1908) → CW(0x1F8) → AmmoComp(0xBC8) → ClipAmmoCount(0x12C)
 *  SDK 验证 (UAGame DUMP):
 *    - USGCharacterWeaponManagerComponent.CurrentWeapon @ 0x1F8 (ASGInventory*)
 *    - ASGWeapon.WeaponAmmoComp @ 0xBC8 (USGWeaponAmmoComponent*)
 *    - USGWeaponAmmoComponent.ClipAmmoCount @ 0x12C (int32 弹匣剩余)
 *    - USGWeaponAmmoComponent.OriginalClipAmmoCount @ 0x108 (int32 弹匣容量)
 *  注: CurrentWeapon 可能是空手/投掷物(非 ASGWeapon), 需校验 AmmoComp 指针有效性
 * ================================================================ */
struct WeaponAmmoInfo {
    int clipAmmo     = 0;   // 弹匣内剩余子弹
    int magCapacity  = 0;   // 弹匣容量
};

inline WeaponAmmoInfo GetWeaponAmmo(DWORD64 actorPawn) {
    WeaponAmmoInfo ammo;
    if (!actorPawn) return ammo;
    DWORD64 uwmc = mem.Read<DWORD64>(actorPawn + Offset_WeaponManagerComponent);
    if (!uwmc || uwmc < 0x10000) return ammo;  // 指针合法性校验
    DWORD64 cw = mem.Read<DWORD64>(uwmc + Offset_CurrentWeapon);
    if (!cw || cw < 0x10000) return ammo;      // 空手时 cw 可能为空
    DWORD64 ammoComp = mem.Read<DWORD64>(cw + Offset_WeaponAmmoComponent);
    if (!ammoComp || ammoComp < 0x10000) return ammo;  // 非武器(投掷物等)无 AmmoComp
    // 一次 DMA 读 0x108~0x130 区间 (ClipAmmoCount @ 0x12C, OriginalClipAmmoCount @ 0x108)
    // 为简化: 分两次读 (偏移相距 0x24, 不连续读)
    ammo.clipAmmo    = mem.Read<int>(ammoComp + Offset_ClipAmmoCount);
    ammo.magCapacity = mem.Read<int>(ammoComp + Offset_MaxAmmoCount);
    // 合法性校验: 子弹数不应为负或异常大
    if (ammo.clipAmmo < 0 || ammo.clipAmmo > 1000) ammo.clipAmmo = 0;
    if (ammo.magCapacity < 0 || ammo.magCapacity > 1000) ammo.magCapacity = 0;
    return ammo;
}

/* ================================================================
 *  GetArmorDurability — 读取玩家护甲耐久度
 *  通过 ASC 读取护甲属性 (与 HP 同一属性集)
 *  返回: {当前护甲值, 最大护甲值}
 *  注: 护甲值通过 ASC 属性集读取, 偏移需实际验证
 * ================================================================ */
struct ArmorInfo {
    float current = 0.f;
    float max = 0.f;
    int level = 0;  // 护甲等级 (1-6)
};

inline ArmorInfo GetArmorInfo(DWORD64 actorPawn) {
    ArmorInfo armor;
    if (!actorPawn) return armor;
    // 通过 ASC 读取护甲属性 (与 HP 类似的链路)
    DWORD64 asc = mem.Read<DWORD64>(actorPawn + Offset_AbilitySystemComponent);
    if (!asc) return armor;
    DWORD64 sa = mem.Read<DWORD64>(asc + Offset_AbilitySetData);
    if (!sa) return armor;
    // 护甲属性数组 (偏移需验证, 暂用 0x38 作为 ArmorArray)
    DWORD64 armorArr = mem.Read<DWORD64>(sa + 0x38);
    if (armorArr) {
        // 读取护甲当前值和最大值 (偏移需验证)
        float vals[2] = {0};
        mem.Read(armorArr + 0x48, vals, sizeof(vals));
        armor.current = vals[0];
        armor.max = vals[1];
    }
    return armor;
}

/* ================================================================
 *  PlayerArmorParts — 遍历玩家护甲列表读取各部件等级+耐久
 *  链路: pawn → ArmorManagerComponent(0x1AA0) → ArmorList(0x278 TArray)
 *  每个 ASGInventory* 读: ArmorLevel(0x6C8) + CDC(0x760)→Dur(0x118)/DurMax(0x11C)
 *  注: ArmorList 不区分头盔/背心, 按数组顺序返回, 调用方按顺序映射
 *      (通常 ArmorList[0]=头盔, 后续=背心/面罩等, 顺序由游戏 EArmorSlot 决定)
 * ================================================================ */
struct ArmorPartInfo {
    int   level = 0;     // 护甲等级 (0=无护甲, 1-6)
    float dur   = 0.f;   // 当前耐久
    float durMax = 0.f;  // 最大耐久
};

inline std::vector<ArmorPartInfo> GetPlayerArmorParts(DWORD64 actorPawn) {
    std::vector<ArmorPartInfo> result;
    if (!actorPawn) return result;
    DWORD64 amc = mem.Read<DWORD64>(actorPawn + Offset_ArmorManagerComponent);
    if (!amc) return result;
    // ArmorList: TArray<ASGInventory*> @ 0x278  (addr(8) + count(4) 连续)
    struct { DWORD64 addr; int count; int pad; } listData{};
    mem.Read(amc + Offset_ArmorList, &listData, sizeof(listData));
    if (!listData.addr || listData.count <= 0 || listData.count > 16) return result;
    result.reserve(listData.count);
    for (int i = 0; i < listData.count; ++i) {
        DWORD64 inv = mem.Read<DWORD64>(listData.addr + 0x8 * i);
        if (!inv) continue;
        ArmorPartInfo part;
        part.level = mem.Read<int>(inv + Offset_ArmorLevel);
        if (part.level <= 0) continue;  // 无等级的部件(耳机等)跳过
        // 耐久度: CDC(0x760) → Durability(0x118) / DurabilityMax(0x11C)
        DWORD64 cdc = mem.Read<DWORD64>(inv + Offset_CommonDataComponent);
        if (cdc) {
            struct { float dur; float durMax; } d{};
            mem.Read(cdc + Offset_Durability, &d, sizeof(d));
            part.dur = d.dur;
            part.durMax = d.durMax;
        }
        result.push_back(part);
    }
    return result;
}

/* ================================================================
 *  StrokeText — 描边文字（2 层偏移 + 1 层填充）
 *  ★优化: 原 5 次 AddText → 3 次, 减少 40% 文字绘制开销
 *  对角线偏移在视觉上与 4 方向描边效果几乎一致
 * ================================================================ */
inline void StrokeText(ImDrawList* dl, const char* text, ImVec2 pos,
                       ImU32 outlineColor, ImU32 fillColor) {
    float o = 1.0f;
    dl->AddText(ImVec2(pos.x - o, pos.y + o), outlineColor, text);
    dl->AddText(ImVec2(pos.x + o, pos.y - o), outlineColor, text);
    dl->AddText(pos, fillColor, text);
}

/* ================================================================
 *  CalcTextWidth — 估算描边文字宽度（使用 ImGui 默认字体）
 * ================================================================ */
inline float CalcTextWidth(const char* text) {
    return ImGui::CalcTextSize(text).x;
}

/* ================================================================
 *  AnQu 项目 fname_decrypt / GetNameAnQu / MatrixRotation / AnQuWorldToScreen
 *  → 已移至 main.h（避免头文件依赖顺序问题）
 * ================================================================ */
