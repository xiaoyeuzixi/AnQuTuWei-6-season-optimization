/**
 * @file      dma_kapi.h
 * @brief     DMA 统一读写封装 — 全局上下文, 零参数调用
 * @details   后台自动管理 DMA 设备句柄与目标进程 PID,
 *            所有读写操作无需传入上下文参数。
 *
 * 快速上手:
 * @code
 *   #include "dma_kapi.h"
 *   int main() {
 *       DMA_Init();                                  // 自动打开FPGA
 *       DMA_PidFromName("notepad.exe");              // 自动获取+设置PID
 *       ULONG64 base = DMA_GetModuleBase("notepad.exe");
 *
 *       INT64 val = DMA_ReadI64(base + 0x100);      // 基础类型读
 *       PlayerInfo info = DMA_ReadStruct(base, PlayerInfo);  // 自定义结构体
 *       int hp = DMA_ReadField(base + 0x200, PlayerInfo, health); // 单字段
 *
 *       DMA_WriteI32(base + 0x400, 42);             // 基础类型写
 *
 *       // 无需手动 DMA_Close() — 程序退出时自动清理
 *   }
 * @endcode
 */

#pragma once
#ifndef DMA_KAPI_H
#define DMA_KAPI_H

#include <Windows.h>
#include <cstring>
#include <cstdio>
#include "leechcore.h"
#include "vmmdll.h"

// ─────────────────────────────────────────────
//  编译时配置
// ─────────────────────────────────────────────
#ifndef DMA_DEFAULT_DEVICE
    #define DMA_DEFAULT_DEVICE  "FPGA"
#endif
#ifndef DMA_DEFAULT_FLAGS
    #define DMA_DEFAULT_FLAGS   0
#endif

// ─────────────────────────────────────────────
//  全局内部上下文 (后台自动管理)
// ─────────────────────────────────────────────
typedef struct {
    VMM_HANDLE  hVMM;
    DWORD       pid;
    CHAR        device[64];
    DWORD       flags;
    BOOL        ready;
} _DMA_CTX;

static _DMA_CTX __dma = { 0 };

/// 自动清理守卫 — 程序退出时自动关闭DMA
static struct _DMA_AutoClose {
    ~_DMA_AutoClose() {
        if (__dma.hVMM) { VMMDLL_Close(__dma.hVMM); __dma.hVMM = NULL; }
    }
} __dma_guard;

// ─────────────────────────────────────────────
//  1. 设备管理 — DMA句柄自动获取 / 自动清理
// ─────────────────────────────────────────────

/// 自动初始化DMA设备 (默认FPGA) — 退出时自动关闭, 无需手动调 DMA_Close
inline BOOL DMA_Init() {
    memset(&__dma, 0, sizeof(__dma));
    LPCSTR argv[] = { "", "-device", DMA_DEFAULT_DEVICE };
    __dma.hVMM = VMMDLL_Initialize(3, argv);
    if (!__dma.hVMM) return FALSE;
    strncpy_s(__dma.device, DMA_DEFAULT_DEVICE, _TRUNCATE);
    __dma.flags = DMA_DEFAULT_FLAGS;
    __dma.ready = TRUE;
    return TRUE;
}

/// 自定义参数初始化
inline BOOL DMA_InitEx(LPCSTR device, DWORD flags) {
    memset(&__dma, 0, sizeof(__dma));
    LPCSTR argv[] = { "", "-device", device };
    __dma.hVMM = VMMDLL_Initialize(3, argv);
    if (!__dma.hVMM) return FALSE;
    strncpy_s(__dma.device, device, _TRUNCATE);
    __dma.flags = flags;
    __dma.ready = TRUE;
    return TRUE;
}

/// 关闭DMA设备
inline VOID DMA_Close() {
    if (__dma.hVMM) { VMMDLL_Close(__dma.hVMM); __dma.hVMM = NULL; }
    __dma.pid = 0;
    __dma.ready = FALSE;
}

/// 检查DMA是否就绪
inline BOOL DMA_IsValid() { return __dma.ready && __dma.hVMM != NULL; }

// ─────────────────────────────────────────────
//  2. 进程操作 — PID自动获取与设置
// ─────────────────────────────────────────────

/// 从进程名自动获取PID并设为当前目标
inline BOOL DMA_PidFromName(LPCSTR procName) {
    if (!DMA_IsValid()) return FALSE;
    DWORD pid = 0;
    if (!VMMDLL_PidGetFromName(__dma.hVMM, procName, &pid)) return FALSE;
    __dma.pid = pid;
    return TRUE;
}

/// 手动设置目标PID
inline VOID DMA_SetPid(DWORD pid) { __dma.pid = pid; }

/// 获取当前目标PID
inline DWORD DMA_GetPid() { return __dma.pid; }

// ─────────────────────────────────────────────
//  3. 模块操作
// ─────────────────────────────────────────────

/// 获取模块基址 (UTF-8)
inline ULONG64 DMA_GetModuleBase(LPCSTR moduleName) {
    if (!DMA_IsValid() || __dma.pid == 0) return 0;
    return VMMDLL_ProcessGetModuleBaseU(__dma.hVMM, __dma.pid, moduleName);
}

/// 获取导出函数地址
inline ULONG64 DMA_GetProcAddress(LPCSTR moduleName, LPCSTR funcName) {
    if (!DMA_IsValid() || __dma.pid == 0) return 0;
    return VMMDLL_ProcessGetProcAddressU(__dma.hVMM, __dma.pid, moduleName, funcName);
}

// ─────────────────────────────────────────────
//  4. 原始字节读写
// ─────────────────────────────────────────────

inline BOOL DMA_ReadRaw(ULONG64 addr, PVOID buf, DWORD size) {
    if (!DMA_IsValid() || __dma.pid == 0) return FALSE;
    return VMMDLL_MemRead(__dma.hVMM, __dma.pid, addr, (PBYTE)buf, size);
}

inline BOOL DMA_WriteRaw(ULONG64 addr, PVOID buf, DWORD size) {
    if (!DMA_IsValid() || __dma.pid == 0) return FALSE;
    return VMMDLL_MemWrite(__dma.hVMM, __dma.pid, addr, (PBYTE)buf, size);
}

// ─────────────────────────────────────────────
//  5. 模板类型安全读写 (支持任意POD类型/自定义结构体)
// ─────────────────────────────────────────────

template<typename T>
inline T DMA_Read(ULONG64 addr) {
    T value{};
    DMA_ReadRaw(addr, &value, sizeof(T));
    return value;
}

template<typename T>
inline BOOL DMA_Write(ULONG64 addr, const T& value) {
    return DMA_WriteRaw(addr, (PVOID)&value, sizeof(T));
}

// ─────────────────────────────────────────────
//  6. 快捷命名类型读写
// ─────────────────────────────────────────────

// 有符号整数
inline INT8   DMA_ReadI8  (ULONG64 a) { return DMA_Read<INT8>(a);   }
inline INT16  DMA_ReadI16 (ULONG64 a) { return DMA_Read<INT16>(a);  }
inline INT32  DMA_ReadI32 (ULONG64 a) { return DMA_Read<INT32>(a);  }
inline INT64  DMA_ReadI64 (ULONG64 a) { return DMA_Read<INT64>(a);  }
inline BOOL   DMA_WriteI8 (ULONG64 a, INT8  v) { return DMA_Write<INT8>(a, v);  }
inline BOOL   DMA_WriteI16(ULONG64 a, INT16 v) { return DMA_Write<INT16>(a, v); }
inline BOOL   DMA_WriteI32(ULONG64 a, INT32 v) { return DMA_Write<INT32>(a, v); }
inline BOOL   DMA_WriteI64(ULONG64 a, INT64 v) { return DMA_Write<INT64>(a, v); }

// 无符号整数
inline UINT8  DMA_ReadU8  (ULONG64 a) { return DMA_Read<UINT8>(a);   }
inline UINT16 DMA_ReadU16 (ULONG64 a) { return DMA_Read<UINT16>(a);  }
inline UINT32 DMA_ReadU32 (ULONG64 a) { return DMA_Read<UINT32>(a);  }
inline UINT64 DMA_ReadU64 (ULONG64 a) { return DMA_Read<UINT64>(a);  }
inline BOOL   DMA_WriteU8 (ULONG64 a, UINT8  v) { return DMA_Write<UINT8>(a, v);  }
inline BOOL   DMA_WriteU16(ULONG64 a, UINT16 v) { return DMA_Write<UINT16>(a, v); }
inline BOOL   DMA_WriteU32(ULONG64 a, UINT32 v) { return DMA_Write<UINT32>(a, v); }
inline BOOL   DMA_WriteU64(ULONG64 a, UINT64 v) { return DMA_Write<UINT64>(a, v); }

// 指针 & DWORD/QWORD
inline ULONG64 DMA_ReadPtr  (ULONG64 a) { return DMA_Read<ULONG64>(a); }
inline DWORD   DMA_ReadDWORD(ULONG64 a) { return DMA_Read<DWORD>(a);   }
inline QWORD   DMA_ReadQWORD(ULONG64 a) { return DMA_Read<QWORD>(a);   }
inline BOOL    DMA_WritePtr  (ULONG64 a, ULONG64 v) { return DMA_Write<ULONG64>(a, v); }
inline BOOL    DMA_WriteDWORD(ULONG64 a, DWORD v)   { return DMA_Write<DWORD>(a, v);   }
inline BOOL    DMA_WriteQWORD(ULONG64 a, QWORD v)   { return DMA_Write<QWORD>(a, v);   }

// 浮点
inline FLOAT  DMA_ReadFloat (ULONG64 a) { return DMA_Read<FLOAT>(a);  }
inline DOUBLE DMA_ReadDouble(ULONG64 a) { return DMA_Read<DOUBLE>(a); }
inline BOOL   DMA_WriteFloat (ULONG64 a, FLOAT  v) { return DMA_Write<FLOAT>(a, v);  }
inline BOOL   DMA_WriteDouble(ULONG64 a, DOUBLE v) { return DMA_Write<DOUBLE>(a, v); }

// 字节 / 布尔
inline BYTE DMA_ReadByte (ULONG64 a) { return DMA_Read<BYTE>(a); }
inline BOOL DMA_ReadBool (ULONG64 a) { return DMA_Read<BOOL>(a); }
inline BOOL DMA_WriteByte(ULONG64 a, BYTE v) { return DMA_Write<BYTE>(a, v); }
inline BOOL DMA_WriteBool(ULONG64 a, BOOL v) { return DMA_Write<BOOL>(a, v); }

// ─────────────────────────────────────────────
//  7. 字符串 / 数组
// ─────────────────────────────────────────────

/// 读 ANSI 字符串
inline SIZE_T DMA_ReadString(ULONG64 addr, PCHAR buf, SIZE_T bufSize) {
    if (!DMA_IsValid() || __dma.pid == 0 || !buf || bufSize == 0) return 0;
    memset(buf, 0, bufSize);
    SIZE_T n = 0; CHAR ch;
    for (SIZE_T i = 0; i < bufSize - 1; i++) {
        if (!DMA_ReadRaw(addr + i, &ch, 1)) break;
        buf[i] = ch; n++;
        if (ch == '\0') break;
    }
    buf[bufSize - 1] = '\0';
    return n;
}

/// 读宽字符串 (UTF-16LE)
inline SIZE_T DMA_ReadWString(ULONG64 addr, PWCHAR buf, SIZE_T charCount) {
    if (!DMA_IsValid() || __dma.pid == 0 || !buf || charCount == 0) return 0;
    memset(buf, 0, charCount * sizeof(WCHAR));
    SIZE_T n = 0; WCHAR ch;
    for (SIZE_T i = 0; i < charCount - 1; i++) {
        if (!DMA_ReadRaw(addr + i * sizeof(WCHAR), &ch, sizeof(WCHAR))) break;
        buf[i] = ch; n++;
        if (ch == L'\0') break;
    }
    buf[charCount - 1] = L'\0';
    return n;
}

/// 读连续内存数组
inline BOOL DMA_ReadArray(ULONG64 addr, PVOID buf, DWORD size) { return DMA_ReadRaw(addr, buf, size); }
inline BOOL DMA_WriteArray(ULONG64 addr, PVOID buf, DWORD size) { return DMA_WriteRaw(addr, buf, size); }

/// 读字节集 — 读取指定数量的原始字节到缓冲区
/// 用法:
///   BYTE buf[256];
///   DMA_ReadBytes(addr, buf, 256);              // 读256字节到数组
///   DMA_ReadBytes(addr, myVec.data(), size);    // 读入 vector<BYTE>
inline BOOL DMA_ReadBytes(ULONG64 addr, PVOID buf, DWORD size) { return DMA_ReadRaw(addr, buf, size); }

/// 写字节集
inline BOOL DMA_WriteBytes(ULONG64 addr, const PVOID buf, DWORD size) { return DMA_WriteRaw(addr, (PVOID)buf, size); }

// ─────────────────────────────────────────────
//  8. 自定义结构体读写 — 用户封装类型
// ─────────────────────────────────────────────

/// 字段偏移量
#ifndef DMA_FIELD_OFFSET
    #define DMA_FIELD_OFFSET(TYPE, FIELD) ((ULONG64)(ULONG_PTR)&(((TYPE*)0)->FIELD))
#endif

/// 读完整结构体
/// 用法: PlayerInfo info = DMA_ReadStruct(addr, PlayerInfo);
#define DMA_ReadStruct(addr, TYPE)              DMA_Read<TYPE>(addr)

/// 写完整结构体
/// 用法: DMA_WriteStruct(addr, PlayerInfo, info);
#define DMA_WriteStruct(addr, TYPE, val)        DMA_Write<TYPE>(addr, val)

/// 读结构体单个字段 (带类型检查)
/// 用法: int hp = DMA_ReadField(addr, PlayerInfo, health);
#define DMA_ReadField(addr, TYPE, FIELD)        DMA_Read<decltype(((TYPE*)0)->FIELD)>(addr + DMA_FIELD_OFFSET(TYPE, FIELD))

/// 写结构体单个字段
/// 用法: DMA_WriteField(addr, PlayerInfo, health, 999);
#define DMA_WriteField(addr, TYPE, FIELD, val)  DMA_Write<decltype(((TYPE*)0)->FIELD)>(addr + DMA_FIELD_OFFSET(TYPE, FIELD), val)

/// 读结构体数组
#define DMA_ReadStructArray(addr, buf, count)   DMA_ReadArray(addr, buf, (DWORD)(sizeof((buf)[0]) * (count)))
#define DMA_WriteStructArray(addr, buf, count)  DMA_WriteArray(addr, buf, (DWORD)(sizeof((buf)[0]) * (count)))

// ─────────────────────────────────────────────
//  9. 链式指针追踪 (多级解引用)
// ─────────────────────────────────────────────

/// 多级指针读: DMA_ChainRead(base, {0x10, 0x20, 0x8}) → 读取 [[[base+0x10]] + 0x20] + 0x8
template<size_t N>
inline ULONG64 DMA_ChainRead(ULONG64 base, const ULONG64(&offsets)[N]) {
    ULONG64 addr = base;
    for (size_t i = 0; i < N; i++) {
        addr = DMA_ReadPtr(addr + offsets[i]);
        if (addr == 0) return 0;
    }
    return addr;
}

/// 多级指针→结构体: DMA_ChainStruct<PlayerInfo>(base, {0x10, 0x8})
template<typename T, size_t N>
inline T DMA_ChainStruct(ULONG64 base, const ULONG64(&offsets)[N]) {
    ULONG64 addr = DMA_ChainRead(base, offsets);
    if (addr == 0) return T{};
    return DMA_Read<T>(addr);
}

// ─────────────────────────────────────────────
//  10. 散列(Scatter)批量读写
// ─────────────────────────────────────────────

/// 一步散列读
inline DWORD DMA_ReadScatter(PPMEM_SCATTER mems, DWORD count, DWORD flags) {
    if (!DMA_IsValid() || __dma.pid == 0 || !mems) return 0;
    return VMMDLL_MemReadScatter(__dma.hVMM, __dma.pid, mems, count, flags);
}

/// 一步散列写
inline DWORD DMA_WriteScatter(PPMEM_SCATTER mems, DWORD count) {
    if (!DMA_IsValid() || __dma.pid == 0 || !mems) return 0;
    return VMMDLL_MemWriteScatter(__dma.hVMM, __dma.pid, mems, count);
}

/// 散列辅助器 (RAII)
class DMA_Scatter {
    VMMDLL_SCATTER_HANDLE m_h;
public:
    DMA_Scatter() : m_h(NULL) {}
    BOOL Init(DWORD flags) {
        if (!DMA_IsValid() || __dma.pid == 0) return FALSE;
        m_h = VMMDLL_Scatter_Initialize(__dma.hVMM, __dma.pid, flags);
        return m_h != NULL;
    }
    BOOL Prepare(QWORD va, DWORD cb)                { return m_h && VMMDLL_Scatter_Prepare(m_h, va, cb); }
    BOOL PrepareEx(QWORD va, DWORD cb, PBYTE buf, PDWORD pcb = NULL) { return m_h && VMMDLL_Scatter_PrepareEx(m_h, va, cb, buf, pcb); }
    BOOL Execute()                                   { return m_h && VMMDLL_Scatter_ExecuteRead(m_h); }
    BOOL Read(QWORD va, DWORD cb, PBYTE buf, PDWORD pcb = NULL) { return m_h && VMMDLL_Scatter_Read(m_h, va, cb, buf, pcb); }
    VOID Close() { if (m_h) { VMMDLL_Scatter_CloseHandle(m_h); m_h = NULL; } }
    ~DMA_Scatter() { Close(); }
};

// ─────────────────────────────────────────────
//  11. 工具函数
// ─────────────────────────────────────────────

/// 虚拟地址→物理地址
inline BOOL DMA_Virt2Phys(ULONG64 va, PULONG64 pa) {
    if (!DMA_IsValid() || __dma.pid == 0) return FALSE;
    return VMMDLL_MemVirt2Phys(__dma.hVMM, __dma.pid, va, pa);
}

/// 十六进制转储 (调试用)
inline VOID DMA_DumpHex(PVOID data, DWORD size, LPCSTR label = NULL) {
    const BYTE* p = (const BYTE*)data;
    if (label) printf("[%s] %u bytes:\n", label, size);
    for (DWORD i = 0; i < size; i++) {
        printf("%02X ", p[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (size % 16 != 0) printf("\n");
}

#endif 
// DMA_KAPI_H


//using namespace std;

//// ── 自定义结构体 (自己封装的类型) ──
//#pragma pack(push, 1)
//typedef struct {
//    INT32   health;
//    INT32   maxHealth;
//    INT32   team;
//    FLOAT   x, y, z;
//    CHAR    name[32];
//} PlayerInfo;
//#pragma pack(pop)
//
//int main() {
//    // 1. 自动打开DMA (句柄后台自动获取)
//    if (!DMA_Init()) { cout << "DMA初始化失败" << endl; return -1; }
//
//    // 2. 自动获取PID并设置
//    if (!DMA_PidFromName("notepad.exe")) { cout << "notepad.exe 未运行" << endl; return -1; }
//    cout << "PID: " << DMA_GetPid() << endl;
//
//    // 3. 获取模块基址
//    ULONG64 base = DMA_GetModuleBase("notepad.exe");
//    cout << "Base: 0x" << hex << base << dec << endl;
//
//    // 4. 读基础类型
//    INT64  v64 = DMA_ReadI64(base + 0x18);
//    INT32  v32 = DMA_ReadI32(base + 0x100);
//    FLOAT  f32 = DMA_ReadFloat(base + 0x200);
//    cout << "I64=" << v64 << " I32=" << v32 << " Float=" << f32 << endl;
//
//    // 5. 读自定义结构体
//    PlayerInfo info = DMA_ReadStruct(base + 0x1000, PlayerInfo);
//    cout << "HP=" << info.health << "/" << info.maxHealth
//         << " Pos=(" << info.x << "," << info.y << "," << info.z << ")"
//         << " Name=" << info.name << endl;
//
//    // 6. 读结构体单个字段 (无需读整个结构体)
//    int hp = DMA_ReadField(base + 0x1000, PlayerInfo, health);
//    cout << "HP(单字段): " << hp << endl;
//
//    // 7. 链式指针读 (多级解引用)
//    ULONG64 ptr = DMA_ChainRead(base, { 0x10, 0x8, 0x0 });
//    cout << "ChainRead: 0x" << hex << ptr << dec << endl;
//
//    // 8. 写内存
//    DMA_WriteI32(base + 0x400, 42);
//
//    // 9. 字符串
//    CHAR name[64];
//    DMA_ReadString(base + 0x300, name, sizeof(name));
//    cout << "String: " << name << endl;
//
//  
//
//
//
//    return 0;
//}