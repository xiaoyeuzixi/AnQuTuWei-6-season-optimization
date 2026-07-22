#pragma once
/*
 * Mem.h — DMA 内存读写单例
 * 移植自 KAKA PUBG DMA 项目的 Memory 类
 *
 * 用法：
 *   mem.Init("UAGame.exe");
 *   auto base = mem.GetBase("UAGame.exe");
 *   auto h = mem.CreateScatter();
 *   mem.AddScatter(h, addr1, &buf1);
 *   mem.ExecuteScatter(h);
 *   mem.CloseScatter(h);
 */

#include <Windows.h>
#include <vector>
#include <string>
#include "vmmdll.h"
#include "leechcore.h"

// ======================== Memory 单例 ========================
class Mem {
public:
    VMM_HANDLE   hVMM = nullptr;
    DWORD        pid  = 0;
    DWORD64      base = 0;
    std::string  process;

    // ── 初始化 ──
    bool Init(const char* procName) {
        process = procName;
        LPCSTR args[] = {"", "-device", "fpga://algo=0"};
        hVMM = VMMDLL_Initialize(3, args);
        if (!hVMM) return false;
        return WaitProcess(procName);
    }

    bool WaitProcess(const char* name) {
        while (true) {
            if (VMMDLL_PidGetFromName(hVMM, (LPSTR)name, &pid) && pid) break;
            printf("Waiting for %s...\n", name);
            Sleep(5000);
        }
        base = GetBase(name);
        return base != 0;
    }

    void Close() { if (hVMM) { VMMDLL_Close(hVMM); hVMM = nullptr; } }
    ~Mem() { Close(); }

    // ── 基础读写 ──
    bool Read(DWORD64 addr, void* buf, DWORD size) const {
        return VMMDLL_MemReadEx(hVMM, pid, addr, (PBYTE)buf, size, nullptr,
            VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO);
    }
    template<typename T> T Read(DWORD64 addr) const {
        T v{}; Read(addr, &v, sizeof(T)); return v;
    }
    bool Write(DWORD64 addr, void* buf, DWORD size) const {
        return VMMDLL_MemWrite(hVMM, pid, addr, (PBYTE)buf, size);
    }

    // ── 模块基址 ──
    DWORD64 GetBase(const char* mod) const {
        PVMMDLL_MAP_MODULEENTRY e;
        if (VMMDLL_Map_GetModuleFromNameU(hVMM, pid, (LPSTR)mod, &e, 0))
            return e->vaBase;
        return 0;
    }
    DWORD64 GetBaseSize(const char* mod) const {
        PVMMDLL_MAP_MODULEENTRY e;
        if (VMMDLL_Map_GetModuleFromNameU(hVMM, pid, (LPSTR)mod, &e, 0))
            return e->cbImageSize;
        return 0;
    }

    // ── Scatter 批量读 ──
    // 对齐调试版：实时读取，不走 VMM 旧缓存。
    // 之前改成缓存读取会造成坐标/物资数据滞后，表现为 FPS 很高但画面一顿一顿。
    VMMDLL_SCATTER_HANDLE CreateScatter() const {
        return VMMDLL_Scatter_Initialize(hVMM, pid,
            VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO);
    }
    void AddScatter(VMMDLL_SCATTER_HANDLE h, DWORD64 addr, void* buf, DWORD size) {
        VMMDLL_Scatter_PrepareEx(h, addr, size, (PBYTE)buf, nullptr);
    }
    template<typename T>
    void AddScatter(VMMDLL_SCATTER_HANDLE h, DWORD64 addr, T* buf) {
        AddScatter(h, addr, buf, sizeof(T));
    }
    void ExecuteScatter(VMMDLL_SCATTER_HANDLE h) {
        VMMDLL_Scatter_ExecuteRead(h);
        VMMDLL_Scatter_Clear(h, pid, VMMDLL_FLAG_NOCACHE);
    }
    void CloseScatter(VMMDLL_SCATTER_HANDLE h) { VMMDLL_Scatter_CloseHandle(h); }

    // ── 缓存刷新 ──
    void RefreshTlb()   { VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL, 0); }
    void RefreshMem()   { VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL, 0); }
    void RefreshAll()   { VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_ALL, 0); }
};

// ── 全局单例 ──
inline Mem mem;
