#pragma once
#include <Windows.h>
#include <array>
#include "Mem.h"

class DMAKey {
    static constexpr int N = 256;
    DWORD64 m_addr = 0;
    int m_pid  = 0;
    std::array<uint8_t, N*2/8> m_kb{};
    std::array<uint8_t, N/8>   m_ed{};

public:
    bool Init() {
        DWORD pidWl = 0;
        DWORD cnt; PVMMDLL_PROCESS_INFORMATION all;
        if (VMMDLL_ProcessGetInformationAll(mem.hVMM, &all, &cnt)) {
            for (DWORD i = 0; i < cnt; i++) {
                if (strcmp(all[i].szName, "winlogon.exe") == 0) { pidWl = all[i].dwPID; break; }
            }
            VMMDLL_MemFree(all);
        }
        if (!pidWl) return false;
        m_pid = pidWl | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;

        PVMMDLL_MAP_EAT eat = nullptr;
        if (VMMDLL_Map_GetEATU(mem.hVMM, m_pid, (LPSTR)"win32kbase.sys", &eat) && eat) {
            for (int i = 0; i < (int)eat->cMap; i++) {
                if (strcmp(eat->pMap[i].uszFunction, "gafAsyncKeyState") == 0) {
                    m_addr = eat->pMap[i].vaFunction; break;
                }
            }
            VMMDLL_MemFree(eat);
        }
        return m_addr != 0;
    }

    bool IsReady() const { return m_addr != 0; }

    void Update() {
        if (!m_addr) return;
        auto prev = m_kb;
        VMMDLL_MemReadEx(mem.hVMM, m_pid, m_addr, (PBYTE)m_kb.data(), 64, nullptr,
            VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO);
        for (int vk = 0; vk < N; vk++) {
            int bi = (vk * 2) / 8, bo = (vk * 2) % 8;
            if ((m_kb[bi] & (1 << bo)) && !(prev[bi] & (1 << bo)))
                m_ed[vk / 8] |= (1 << (vk % 8));
        }
    }

    // 同时检测主机(DMA)和副机(本机)键盘
    bool Down(int vk) const {
        bool local = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (!m_addr) return local;
        int bi = (vk * 2) / 8, bo = (vk * 2) % 8;
        return ((m_kb[bi] & (1 << bo)) != 0) || local;
    }

    // 同时检测主机(DMA)和副机(本机)键盘按下事件
    bool Pressed(int vk) {
        bool local = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (!m_addr) return local;
        bool r = (m_ed[vk / 8] & (1 << (vk % 8))) != 0;
        m_ed[vk / 8] &= ~(1 << (vk % 8));
        return r || local;
    }
};

inline DMAKey g_DMAKey;
