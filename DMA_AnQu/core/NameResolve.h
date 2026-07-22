#pragma once
/*
 * core/NameResolve.h — 名字解析 & 类型判定模块
 *
 * 从 main.h 抽离的名字解析相关函数:
 *   - GetNameAnQu()       FName 解密获取名字
 *   - GetObjectName()     通过 UObject 获取对象名称
 *   - ContainsI()         大小写不敏感字符串包含（零分配优化）
 *   - IsHumanoidClass()   判断是否是人形角色类
 *   - GetHumanoidType()   获取人形角色类型
 *
 * 依赖: core/Decryption.h (FNameDecrypt, GetXorKey, GetCachedNameKey)
 *       core/GameState.h (gs.base)
 *       ../GameMatrix.h
 *       ../Mem.h (mem)
 *       ../Offset.h (BaseName)
 *       <string>, <string_view>, <cctype>, <cstdint>, <cstring>
 */

#include "../Mem.h"
#include "../Offset.h"
#include "../GameMatrix.h"
#include "Decryption.h"
#include "GameState.h"
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <cctype>

inline std::string GetNameAnQu(int key) {
    unsigned int chunkOffset = ((unsigned int)key) >> 16;
    unsigned short nameOffset = (unsigned short)key;
    static DWORD64 cachedGnamesBase = 0;
    static DWORD64 cachedChunkBase = 0;
    static unsigned int cachedChunkOffset = 0xFFFFFFFF;
    DWORD64 currentGnamesBase = gs.base + BaseName;
    DWORD64 pool_chunk;
    if (chunkOffset == cachedChunkOffset && cachedGnamesBase == currentGnamesBase && cachedChunkBase) {
        pool_chunk = cachedChunkBase;
    } else {
        pool_chunk = mem.Read<unsigned long long>(currentGnamesBase + ((unsigned long long)(chunkOffset + 2) * 8));
        if (!pool_chunk) return "";
        cachedChunkBase = pool_chunk;
        cachedChunkOffset = chunkOffset;
        cachedGnamesBase = currentGnamesBase;
    }
    unsigned long long entry_offset = pool_chunk + (unsigned long)(2 * nameOffset);
    unsigned short name_entry = mem.Read<unsigned short>(entry_offset);

    unsigned long nameLength = name_entry >> 6;

    char buff[1028];

    if (nameLength && nameLength > 0 && nameLength < 1024) {
        mem.Read(entry_offset + 2, buff, nameLength);
        buff[nameLength] = '\0';

        FNameDecrypt(buff, nameLength);
        return std::string(buff);
    }
    return "";
}

inline std::string GetObjectName(uint64_t moduleBase, uint64_t objAddr) {
    if (!moduleBase || !objAddr || objAddr < 0x10000) return "";
    uint64_t fname = mem.Read<uint64_t>(objAddr + 0x20);
    if (!fname) return "";
    uint32_t compIdx = (uint32_t)(fname & 0xFFFFFFFF);
    uint32_t index = compIdx & 0xFFFF;
    uint32_t number = (compIdx >> 16) & 0xFFFF;
    uint32_t fnameNum = (uint32_t)(fname >> 32);
    if (number > 0xFFFF) return "";
    uint64_t gnameBase = moduleBase + BaseName;
    uint64_t chunkPtr = mem.Read<uint64_t>(gnameBase + (number + 2) * 8);
    if (!chunkPtr) return "";
    uint64_t entryAddr = chunkPtr + index * 2ULL;
    BYTE _buf[2 + 512];
    mem.Read(entryAddr, _buf, 2 + 512);
    uint16_t header = _buf[0] | (_buf[1] << 8);
    bool isWide = header & 1;
    uint32_t len = header >> 6;
    if (len == 0 || len > 255) return "";
    uint32_t readSize = isWide ? len * 2 : len;
    BYTE* cipher = _buf + 2;
    uint8_t key = GetXorKey();
    std::string result;
    if (isWide) {
        for (uint32_t i = 0; i < len * 2; i++) cipher[i] ^= key;
        for (uint32_t i = 0; i < len; i++) result += (char)cipher[i * 2];
    } else {
        for (uint32_t i = 0; i < len; i++) result += (char)(cipher[i] ^ key);
    }
    if (fnameNum != 0) result += "_" + std::to_string(fnameNum);
    return result;
}

inline bool ContainsI(std::string_view haystack, const char* needle) {
    if (!needle || haystack.empty()) return false;
    size_t nlen = strlen(needle);
    if (nlen > haystack.size()) return false;
    constexpr size_t STACK_BUF = 128;
    char stackBuf[STACK_BUF];
    static thread_local char* heapBuf = nullptr;
    static thread_local size_t heapCap = 0;
    char* buf;
    size_t hlen = haystack.size();
    if (hlen < STACK_BUF) {
        buf = stackBuf;
    } else {
        if (heapCap < hlen + 1) {
            heapCap = hlen + 64;
            heapBuf = (char*)realloc(heapBuf, heapCap);
        }
        buf = heapBuf;
    }
    for (size_t i = 0; i < hlen; ++i) {
        char c = haystack[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }
    buf[hlen] = '\0';
    char first = (needle[0] >= 'A' && needle[0] <= 'Z') ? (needle[0] + 32) : needle[0];
    for (size_t i = 0; i <= hlen - nlen; ++i) {
        if (buf[i] != first) continue;
        size_t j;
        for (j = 1; j < nlen; ++j) {
            char c = needle[j];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (buf[i + j] != c) break;
        }
        if (j == nlen) return true;
    }
    return false;
}

inline bool IsHumanoidClass(std::string_view className) {
    if (className.empty()) return false;
    char first = className[0];
    if (first >= 'A' && first <= 'Z') first += 32;
    switch (first) {
        case 'a': {
            if (ContainsI(className, "ain")) return false;
            if (ContainsI(className, "ail")) return false;
            if (ContainsI(className, "ai3")) return false;
            if (ContainsI(className, "air")) return false;
            if (ContainsI(className, "ai")) return true;
            return false;
        }
        case 'b': {
            if (ContainsI(className, "boss")) return true;
            return false;
        }
        case 'c': {
            if (ContainsI(className, "container")) return false;
            if (ContainsI(className, "camp")) return true;
            return false;
        }
        case 'f': {
            if (ContainsI(className, "follower")) return true;
            return false;
        }
        case 'l': {
            if (ContainsI(className, "localplayer")) return true;
            return false;
        }
        case 'm': {
            if (ContainsI(className, "main")) return false;
            return false;
        }
        case 'n': {
            if (ContainsI(className, "nails")) return false;
            if (ContainsI(className, "nai")) return false;
            if (ContainsI(className, "npc14")) return false;
            if (ContainsI(className, "npc")) return true;
            return false;
        }
        case 'o': {
            if (ContainsI(className, "obplayer")) return true;
            return false;
        }
        case 'p': {
            if (ContainsI(className, "pmc")) return !ContainsI(className, "ai");
            if (ContainsI(className, "player")) return true;
            return false;
        }
        case 'r': {
            if (ContainsI(className, "rai")) return false;
            if (ContainsI(className, "rnpc")) return false;
            return false;
        }
        case 's': {
            if (ContainsI(className, "scav")) return true;
            if (ContainsI(className, "spectator")) return true;
            if (ContainsI(className, "sgcharacter")) return !ContainsI(className, "ai");
            return false;
        }
        case 'u': {
            if (ContainsI(className, "uamp")) return false;
            if (ContainsI(className, "uamcharacter")) return !ContainsI(className, "ai");
            return false;
        }
        case 'v': {
            if (ContainsI(className, "vestcontainer")) return false;
            return false;
        }
        default:
            return false;
    }
}

inline const char* GetHumanoidType(std::string_view className) {
    if (ContainsI(className, "uamplayerstate")) return "\xE6\x9C\xAA\xE7\x9F\xA5";
    bool isAI = ContainsI(className, "camp") || ContainsI(className, "follower")
        || ContainsI(className, "boss") || ContainsI(className, "scav")
        || ContainsI(className, "npc") || ContainsI(className, "ai");
    bool isPlayer = ContainsI(className, "uamcharacter")
        || (ContainsI(className, "sgcharacter") && !ContainsI(className, "ai"))
        || (ContainsI(className, "pmc") && !ContainsI(className, "ai"))
        || ContainsI(className, "spectator") || ContainsI(className, "obplayer")
        || ContainsI(className, "localplayer") || ContainsI(className, "player");
    if (isAI && isPlayer) return "\xE7\x8E\xA9\xE5\xAE\xB6\xE6\x89\xAE\xE6\xbc\x94\xE4\xBA\xBA\xE6\x9C\xBA";
    if (isAI) return "\xE4\xBA\xBA\xE6\x9C\xBA";
    if (isPlayer) return "\xE7\x8E\xA9\xE5\xAE\xB6";
    return "\xE6\x9C\xAA\xE7\x9F\xA5";
}
