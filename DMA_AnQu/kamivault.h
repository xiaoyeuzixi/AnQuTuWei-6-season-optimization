/**
 * kamivault.h — KamiVault 卡密验证 SDK（单头文件，零依赖）
 *
 * 只需 #include "kamivault.h" 并链接 winhttp.lib advapi32.lib
 * 编译：MSVC /std:c++17 /utf-8
 *
 * ┌──────────────────────────────────────────────────┐
 * │  快速开始                                         │
 * │                                                  │
 * │  kv::Client client("192.144.142.27", 80,         │
 * │                     "你的APIKey");                │
 * │                                                  │
 * │  kv::Session s = client.login("卡密");            │
 * │  if (s.ok()) {                                   │
 * │      // 启动自动心跳                              │
 * │      kv::Heartbeat hb;                            │
 * │      hb.start(client, s.token, []{               │
 * │          // 被踢下线回调                           │
 * │      });                                          │
 * │                                                  │
 * │      // ... 你的程序逻辑 ...                     │
 * │                                                  │
 * │      hb.stop();       // 停止心跳                 │
 * │      client.logout(s.token); // 主动登出          │
 * │  }                                               │
 * └──────────────────────────────────────────────────┘
 *
 * 后端 API 路径：
 *   POST /v1/session/login    — 卡密验证登录
 *   POST /v1/session/heartbeat — 心跳续期
 *   POST /v1/session/logout    — 主动登出
 *   POST /v1/session/devices   — 查询在线设备
 *
 * 版本：v2.0 — 2026-06-27
 * 匹配后端：SessionController + SessionService (Spring Boot 3)
 */

#pragma once

#include <windows.h>
#include <winhttp.h>
#include <intrin.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>
#include <sstream>
#include <random>
#include <iomanip>
#include <cstring>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

 // ================================================================
 //  内部实现（用户不需要关心）
 // ================================================================
namespace kv {
    namespace detail {

        // ======================== SHA256（纯实现，无外部依赖） ========================

        class SHA256 {
        public:
            SHA256() { reset(); }

            void reset() {
                m_dataLen = 0;
                m_bitLen = 0;
                m_state[0] = 0x6a09e667; m_state[1] = 0xbb67ae85;
                m_state[2] = 0x3c6ef372; m_state[3] = 0xa54ff53a;
                m_state[4] = 0x510e527f; m_state[5] = 0x9b05688c;
                m_state[6] = 0x1f83d9ab; m_state[7] = 0x5be0cd19;
                memset(m_data, 0, 64);
            }

            void update(const uint8_t* data, size_t len) {
                for (size_t i = 0; i < len; i++) {
                    m_data[m_dataLen++] = data[i];
                    if (m_dataLen == 64) {
                        transform();
                        m_bitLen += 512;
                        m_dataLen = 0;
                    }
                }
            }

            std::string final() {
                uint32_t i = m_dataLen;
                if (m_dataLen < 56) {
                    m_data[i++] = 0x80;
                    while (i < 56) m_data[i++] = 0x00;
                }
                else {
                    m_data[i++] = 0x80;
                    while (i < 64) m_data[i++] = 0x00;
                    transform();
                    memset(m_data, 0, 56);
                }
                m_bitLen += (uint64_t)m_dataLen * 8;
                m_data[63] = (uint8_t)(m_bitLen);
                m_data[62] = (uint8_t)(m_bitLen >> 8);
                m_data[61] = (uint8_t)(m_bitLen >> 16);
                m_data[60] = (uint8_t)(m_bitLen >> 24);
                m_data[59] = (uint8_t)(m_bitLen >> 32);
                m_data[58] = (uint8_t)(m_bitLen >> 40);
                m_data[57] = (uint8_t)(m_bitLen >> 48);
                m_data[56] = (uint8_t)(m_bitLen >> 56);
                transform();
                std::string result;
                for (int j = 0; j < 8; j++) {
                    for (i = 0; i < 4; i++) {
                        char hex[4];
                        sprintf_s(hex, "%02x", ((m_state[j] >> (24 - i * 8)) & 0xFF));
                        result += hex;
                    }
                }
                return result;
            }

        private:
            uint8_t  m_data[64];
            uint32_t m_dataLen;
            uint64_t m_bitLen;
            uint32_t m_state[8];

            static uint32_t ROTR(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
            static uint32_t CH(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
            static uint32_t MAJ(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
            static uint32_t EP0(uint32_t x) { return ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22); }
            static uint32_t EP1(uint32_t x) { return ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25); }
            static uint32_t SIG0(uint32_t x) { return ROTR(x, 7) ^ ROTR(x, 18) ^ (x >> 3); }
            static uint32_t SIG1(uint32_t x) { return ROTR(x, 17) ^ ROTR(x, 19) ^ (x >> 10); }

            static const uint32_t K[64];

            void transform() {
                uint32_t m[64];
                for (uint32_t i = 0, j = 0; i < 16; i++, j += 4)
                    m[i] = ((uint32_t)m_data[j] << 24) | ((uint32_t)m_data[j + 1] << 16) |
                    ((uint32_t)m_data[j + 2] << 8) | (uint32_t)m_data[j + 3];
                for (uint32_t i = 16; i < 64; i++)
                    m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

                uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
                uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

                for (uint32_t i = 0; i < 64; i++) {
                    uint32_t t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
                    uint32_t t2 = EP0(a) + MAJ(a, b, c);
                    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
                }
                m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
                m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
            }
        };

        const uint32_t SHA256::K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };

        inline std::string sha256(const std::string& input) {
            SHA256 ctx;
            ctx.update(reinterpret_cast<const uint8_t*>(input.c_str()), input.size());
            return ctx.final();
        }

        // ======================== 设备指纹 ========================

        inline std::string getCPUId() {
            int cpuInfo[4] = { 0 };
            __cpuid(cpuInfo, 0);
            char buf[33] = { 0 };
            sprintf_s(buf, "%08X%08X%08X%08X", cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
            return std::string(buf);
        }

        inline std::string getMachineGuid() {
            HKEY hKey;
            char guid[128] = { 0 };
            DWORD size = sizeof(guid);
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                "SOFTWARE\\Microsoft\\Cryptography", 0,
                KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
                RegQueryValueExA(hKey, "MachineGuid", nullptr, nullptr,
                    reinterpret_cast<LPBYTE>(guid), &size);
                RegCloseKey(hKey);
            }
            return std::string(guid);
        }

        inline std::string getComputerName() {
            char name[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
            DWORD size = sizeof(name);
            GetComputerNameA(name, &size);
            return std::string(name);
        }

        // 设备指纹 = SHA256(CPUID | MachineGuid | ComputerName)
        inline std::string getDeviceFingerprint() {
            std::string raw = getCPUId() + "|" + getMachineGuid() + "|" + getComputerName();
            return sha256(raw);
        }

        inline std::string getOSInfo() {
            // 简化版本信息返回
            return "Windows";
        }

        // ======================== 敏感信息掩码 ========================

        // 将敏感字符串掩码显示：保留前 showFirst 位 + *** + 后 showLast 位
        // 例如 "4b165482b3f643d4b37fb5aa58a191f9" → "4b16***58a191f9"
        inline std::string maskSecret(const std::string& secret, int showFirst = 4, int showLast = 4) {
            if (secret.empty()) return "(空)";
            if (secret.size() <= showFirst + showLast) return secret;  // 太短不掩码
            return secret.substr(0, showFirst) + "***" + secret.substr(secret.size() - showLast);
        }

        // ======================== HTTP POST (WinHTTP) ========================

        struct HttpResp {
            int         code = 0;
            std::string body;
            bool        ok = false;
        };

        inline HttpResp httpPost(const std::string& host, int port,
            const std::string& path,
            const std::string& jsonBody,
            bool useSSL = false,
            const std::string& extraHeader = "",
            const std::string& contentType = "application/json") {
            HttpResp r;
            if (!port) port = useSSL ? 443 : 80;

            HINTERNET hSession = WinHttpOpen(L"KamiVault/2.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession) return r;

            // host → wide string（使用 MultiByteToWideChar 正确处理 UTF-8）
            int wlen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
            std::wstring whost(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &whost[0], wlen);

            HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(),
                (INTERNET_PORT)port, 0);
            if (!hConnect) { WinHttpCloseHandle(hSession); return r; }

            // path → wide string
            int wplen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
            std::wstring wpath(wplen, 0);
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wplen);

            DWORD flags = useSSL ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return r; }

            // 30 秒超时
            DWORD timeout = 30000;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
            WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

            // 构建请求头：Content-Type + 可选额外头
            std::wstring headers = L"Content-Type: " +
                std::wstring(contentType.begin(), contentType.end()) + L"\r\n";
            if (!extraHeader.empty()) {
                int ehLen = MultiByteToWideChar(CP_UTF8, 0, extraHeader.c_str(), -1, nullptr, 0);
                std::wstring wExtra(ehLen, 0);
                MultiByteToWideChar(CP_UTF8, 0, extraHeader.c_str(), -1, &wExtra[0], ehLen);
                // MultiByteToWideChar with -1 includes the null terminator; shrink by 1
                if (!wExtra.empty() && wExtra.back() == L'\0') wExtra.pop_back();
                headers += wExtra;
            }

            BOOL bResult = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1,
                (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size(),
                (DWORD)jsonBody.size(), 0);

            if (bResult && WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD statusCode = 0, sz = sizeof(statusCode);
                WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz,
                    WINHTTP_NO_HEADER_INDEX);
                r.code = (int)statusCode;
                r.ok = (statusCode == 200);

                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                    std::vector<char> buf(avail + 1, 0);
                    DWORD bytesRead = 0;
                    if (WinHttpReadData(hRequest, buf.data(), avail, &bytesRead)) {
                        r.body.append(buf.data(), bytesRead);
                    }
                }
            }

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return r;
        }

        // ======================== JSON 微型解析 ========================

        // 提取字符串值：{"key":"value"} → value
        inline std::string jStr(const std::string& j, const std::string& key) {
            std::string needle = "\"" + key + "\"";
            size_t pos = j.find(needle);
            if (pos == std::string::npos) return "";

            pos = j.find(':', pos + needle.size());
            if (pos == std::string::npos) return "";
            pos++;

            // 跳过空白
            while (pos < j.size() && (j[pos] == ' ' || j[pos] == '\t' || j[pos] == '\n')) pos++;
            if (pos >= j.size()) return "";

            if (j[pos] == '"') {
                pos++;
                size_t end = j.find('"', pos);
                if (end == std::string::npos) return "";
                return j.substr(pos, end - pos);
            }
            else {
                // 数值或布尔
                size_t end = pos;
                while (end < j.size() && j[end] != ',' && j[end] != '}' &&
                    j[end] != ']' && j[end] != ' ' && j[end] != '\n') {
                    end++;
                }
                return j.substr(pos, end - pos);
            }
        }

        // 提取布尔值
        inline bool jBool(const std::string& j, const std::string& key) {
            std::string needle = "\"" + key + "\":";
            size_t pos = j.find(needle);
            if (pos == std::string::npos) return false;
            pos += needle.size();
            while (pos < j.size() && (j[pos] == ' ' || j[pos] == '\t')) pos++;
            return (pos + 4 <= j.size() && j.substr(pos, 4) == "true");
        }

        // 提取整数值
        inline int jInt(const std::string& j, const std::string& key) {
            std::string needle = "\"" + key + "\":";
            size_t pos = j.find(needle);
            if (pos == std::string::npos) return 0;
            pos += needle.size();
            while (pos < j.size() && (j[pos] == ' ' || j[pos] == '\t')) pos++;
            std::string num;
            while (pos < j.size() && j[pos] >= '0' && j[pos] <= '9') num += j[pos++];
            if (num.empty()) return 0;
            try { return std::stoi(num); }
            catch (...) { return 0; }
        }

        // ======================== XOR 加解密 ========================

        inline void xorEncrypt(std::vector<uint8_t>& data, const std::string& key) {
            if (key.empty()) return;
            for (size_t i = 0; i < data.size(); i++) {
                data[i] ^= static_cast<uint8_t>(key[i % key.size()]);
            }
        }

        inline std::string xorEncryptString(const std::string& data, const std::string& key) {
            std::string result = data;
            for (size_t i = 0; i < result.size(); i++) {
                result[i] ^= key[i % key.size()];
            }
            return result;
        }

        // ======================== Base64 编解码 ========================

        inline std::string base64Encode(const std::string& input) {
            static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string result;
            int i = 0, j = 0;
            unsigned char char3[3], char4[4];
            for (size_t pos = 0; pos < input.size(); pos++) {
                char3[i++] = input[pos];
                if (i == 3) {
                    char4[0] = (char3[0] & 0xFC) >> 2;
                    char4[1] = ((char3[0] & 0x03) << 4) + ((char3[1] & 0xF0) >> 4);
                    char4[2] = ((char3[1] & 0x0F) << 2) + ((char3[2] & 0xC0) >> 6);
                    char4[3] = char3[2] & 0x3F;
                    for (i = 0; i < 4; i++) result += table[char4[i]];
                    i = 0;
                }
            }
            if (i > 0) {
                for (j = i; j < 3; j++) char3[j] = '\0';
                char4[0] = (char3[0] & 0xFC) >> 2;
                char4[1] = ((char3[0] & 0x03) << 4) + ((char3[1] & 0xF0) >> 4);
                char4[2] = ((char3[1] & 0x0F) << 2) + ((char3[2] & 0xC0) >> 6);
                for (j = 0; j < i + 1; j++) result += table[char4[j]];
                while (i++ < 3) result += '=';
            }
            return result;
        }

        inline std::string base64Decode(const std::string& input) {
            static const int8_t table[256] = {
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
                52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
                -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
                15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
                -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
                41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
            };
            std::string result;
            int i = 0;
            unsigned char char4[4];
            for (size_t pos = 0; pos < input.size(); pos++) {
                if (input[pos] == '=') break;
                int8_t val = table[(unsigned char)input[pos]];
                if (val < 0) continue;
                char4[i++] = (unsigned char)val;
                if (i == 4) {
                    result += (char)((char4[0] << 2) | (char4[1] >> 4));
                    result += (char)((char4[1] << 4) | (char4[2] >> 2));
                    result += (char)((char4[2] << 6) | char4[3]);
                    i = 0;
                }
            }
            if (i > 0) {
                for (int j = i; j < 4; j++) char4[j] = 0;
                result += (char)((char4[0] << 2) | (char4[1] >> 4));
                if (i >= 3) result += (char)((char4[1] << 4) | (char4[2] >> 2));
            }
            return result;
        }

        // ======================== Nonce 生成（动态密钥派生） ========================

        // 生成 16 字节随机 nonce（hex 编码，32 字符），用于动态密钥派生
        inline std::string genNonce() {
            std::random_device rd;
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            ss << std::setw(8) << rd();
            ss << std::setw(8) << rd();
            ss << std::setw(8) << rd();
            ss << std::setw(8) << rd();
            return ss.str();  // 32 hex chars = 16 bytes
        }

        // ======================== 内嵌资源读取 ========================

        inline std::vector<uint8_t> readResource(HMODULE hModule, int resourceId,
            const std::wstring& resourceType) {
            HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCEW(resourceId), resourceType.c_str());
            if (!hRes) return {};

            HGLOBAL hMem = LoadResource(hModule, hRes);
            if (!hMem) return {};

            DWORD size = SizeofResource(hModule, hRes);
            void* data = LockResource(hMem);
            if (!data || size == 0) return {};

            return std::vector<uint8_t>(static_cast<uint8_t*>(data),
                static_cast<uint8_t*>(data) + size);
        }

        inline std::string readResourceString(HMODULE hModule, int resourceId) {
            auto data = readResource(hModule, resourceId, L"CONFIG");
            if (data.empty()) return "";
            return std::string(data.begin(), data.end());
        }

    }
} // namespace kv::detail


// ================================================================
//  公开 API
// ================================================================
namespace kv {

    // ======================== 登录结果 ========================

    struct Session {
        bool        success = false;
        std::string message;               // 服务器消息
        std::string token;                  // session_token（心跳/登出必须携带）
        int         cardId = 0; // 卡密 ID
        int         programId = 0; // 程序 ID
        std::string loginAt;                // 登录时间（ISO 8601）
        std::string expiresAt;              // 过期时间（ISO 8601）
        int         heartbeatInterval = 60;  // 心跳间隔秒数
        int         heartbeatTimeout = 180; // 心跳超时秒数
        std::string cardExpireTime;           // 卡密到期时间（ISO 8601）

        bool ok() const { return success; }
    };

    // ======================== 心跳结果 ========================

    struct HeartbeatResult {
        bool        success = false;
        bool        kicked = false;   // 是否被踢下线
        std::string expiresAt;          // 新的过期时间
        std::string reason;             // 被踢原因（如果 kicked=true）
    };

    // ======================== 客户端 ========================

    class Client {
    public:
        /**
         * 构造客户端
         * @param host    服务器地址（不含 http://，如 "192.144.142.27"）
         * @param port    端口（默认 80）
         * @param apiKey  后端注册的 API Key
         * @param useSSL  是否使用 HTTPS
         */
        Client(std::string host, int port, std::string apiKey, bool useSSL = false)
            : m_host(std::move(host)), m_port(port), m_apiKey(std::move(apiKey)),
            m_ssl(useSSL), m_basePath("/v1/session") {
        }

        /**
         * 设置自定义 API 基础路径
         * 默认 /v1/session，如果后端改了路径可以这里调整
         */
        void setBasePath(const std::string& p) { m_basePath = p; }

        /**
         * 设置 XOR 加密密钥，设置后请求和响应均会被 XOR 加密
         * 密钥为空则不加密（默认）
         */
        void setXorKey(const std::string& key) { m_xorKey = key; }

        /**
         * 卡密验证登录
         * POST /v1/session/login
         * 请求体: { api_key, card_key, device_fingerprint, device_name, os_info }
         * 响应体: { success, message, session_token, card_id, program_id,
         *           login_at, expires_at, heartbeat_interval, heartbeat_timeout }
         */
        Session login(const std::string& cardKey) {
            Session s;
            std::string fingerprint = detail::getDeviceFingerprint();
            std::string deviceName = detail::getComputerName();
            std::string osInfo = detail::getOSInfo();

            std::string json = "{"
                "\"api_key\":\"" + m_apiKey + "\","
                "\"card_key\":\"" + cardKey + "\","
                "\"device_fingerprint\":\"" + fingerprint + "\","
                "\"device_name\":\"" + deviceName + "\","
                "\"os_info\":\"" + osInfo + "\""
                "}";

            auto r = send(m_basePath + "/login", json);

            if (r.code == 0) {
                s.message = "无法连接服务器";
                return s;
            }

            s.success = detail::jBool(r.body, "success");
            s.message = detail::jStr(r.body, "message");
            if (s.message.empty()) s.message = s.success ? "验证成功" : "验证失败";

            if (s.success) {
                s.token = detail::jStr(r.body, "session_token");
                s.cardId = detail::jInt(r.body, "card_id");
                s.programId = detail::jInt(r.body, "program_id");
                s.loginAt = detail::jStr(r.body, "login_at");
                s.expiresAt = detail::jStr(r.body, "expires_at");
                s.heartbeatInterval = detail::jInt(r.body, "heartbeat_interval");
                s.heartbeatTimeout = detail::jInt(r.body, "heartbeat_timeout");
                if (!s.heartbeatInterval)  s.heartbeatInterval = 60;
                if (!s.heartbeatTimeout)   s.heartbeatTimeout = 180;
                s.cardExpireTime = detail::jStr(r.body, "card_expire_time");
            }

            return s;
        }

        /**
         * 心跳续期
         * POST /v1/session/heartbeat
         * 请求体: { session_token }
         * 响应体: { success, kicked, expires_at, reason }
         */
        HeartbeatResult heartbeat(const std::string& token) {
            HeartbeatResult h;
            if (token.empty()) return h;

            std::string json = "{\"session_token\":\"" + token + "\"}";
            auto r = send(m_basePath + "/heartbeat", json);

            if (r.code == 0) return h;

            h.success = detail::jBool(r.body, "success");
            h.kicked = detail::jBool(r.body, "kicked");
            h.expiresAt = detail::jStr(r.body, "expires_at");
            h.reason = detail::jStr(r.body, "reason");

            return h;
        }

        /**
         * 主动登出
         * POST /v1/session/logout
         * 请求体: { session_token }
         */
        void logout(const std::string& token) {
            if (token.empty()) return;
            send(m_basePath + "/logout", "{\"session_token\":\"" + token + "\"}");
        }

        /**
         * 查询当前卡密的在线设备列表
         * POST /v1/session/devices
         * 请求体: { session_token }
         */
        HeartbeatResult queryDevices(const std::string& token) {
            if (token.empty()) return {};
            std::string json = "{\"session_token\":\"" + token + "\"}";
            auto r = send(m_basePath + "/devices", json);
            HeartbeatResult h;
            h.success = detail::jBool(r.body, "success");
            return h;
        }

        // 访问器
        const std::string& host()   const { return m_host; }
        int                port()   const { return m_port; }
        const std::string& apiKey() const { return m_apiKey; }
        bool               ssl()    const { return m_ssl; }
        const std::string& basePath() const { return m_basePath; }

    private:
        std::string m_host;
        int         m_port;
        std::string m_apiKey;
        bool        m_ssl;
        std::string m_basePath;
        std::string m_xorKey;

        detail::HttpResp send(const std::string& path, const std::string& json) {
            if (m_xorKey.empty()) {
                return detail::httpPost(m_host, m_port, path, json, m_ssl);
            }
            // 动态加密：每次请求生成随机 nonce
            std::string nonce = detail::genNonce();
            std::string dynKey = detail::sha256(m_xorKey + nonce);
            // 请求：JSON → XOR(dynamicKey) → Base64 → text/plain
            std::string xored = detail::xorEncryptString(json, dynKey);
            std::string encoded = detail::base64Encode(xored);
            std::string headers = "X-Encrypt: xor\r\nX-Nonce: " + nonce + "\r\n";
            auto r = detail::httpPost(m_host, m_port, path, encoded, m_ssl,
                headers, "text/plain");
            // 响应：Base64 → XOR(dynamicKey) → 原始 JSON
            if (!r.body.empty()) {
                std::string decoded = detail::base64Decode(r.body);
                r.body = detail::xorEncryptString(decoded, dynKey);
            }
            return r;
        }
    };

    // ======================== 心跳保活（后台线程） ========================

    class Heartbeat {
    public:
        Heartbeat() = default;
        ~Heartbeat() { stop(); }

        // 不可拷贝
        Heartbeat(const Heartbeat&) = delete;
        Heartbeat& operator=(const Heartbeat&) = delete;

        /**
         * 启动后台心跳线程
         * @param client      客户端实例
         * @param token       会话令牌
         * @param onKicked    被踢下线回调（可选）
         * @param intervalSec 心跳间隔秒数（0 = 使用 Session 中的值，默认 60）
         * @param maxFailures 连续失败多少次触发 onKicked（默认 5）
         */
        void start(Client& client, const std::string& token,
            std::function<void(const HeartbeatResult&)> onKicked = nullptr,
            int intervalSec = 0, int maxFailures = 5) {
            stop();
            m_client = &client;
            m_token = token;
            m_onKicked = std::move(onKicked);
            m_interval = intervalSec > 0 ? intervalSec : 60;
            m_maxFailures = maxFailures;
            m_failed = 0;
            m_running = true;
            m_thread = std::thread(&Heartbeat::run, this);
        }

        /** 简化版启动（无结果回调） */
        void start(Client& client, const std::string& token,
            std::function<void()> onKickedSimple,
            int intervalSec = 0) {
            start(client, token,
                [onKickedSimple](const HeartbeatResult&) { onKickedSimple(); },
                intervalSec);
        }

        /** 停止心跳线程 */
        void stop() {
            m_running = false;
            if (m_thread.joinable()) m_thread.join();
        }

        bool isRunning() const { return m_running; }

    private:
        Client* m_client = nullptr;
        std::string                         m_token;
        std::function<void(const HeartbeatResult&)> m_onKicked;
        int                                 m_interval = 60;
        int                                 m_maxFailures = 5;
        int                                 m_failed = 0;
        std::atomic<bool>                   m_running{ false };
        std::thread                         m_thread;

        void run() {
            while (m_running) {
                // 按秒级粒度等待，便于随时响应 stop()
                for (int i = 0; i < m_interval && m_running; i++)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                if (!m_running) break;

                auto h = m_client->heartbeat(m_token);
                if (h.success && !h.kicked) {
                    m_failed = 0;
                }
                else if (h.kicked) {
                    // 被踢下线，立即回调
                    if (m_onKicked) m_onKicked(h);
                    m_running = false;
                    return;
                }
                else {
                    m_failed++;
                    if (m_failed > m_maxFailures) {
                        if (m_onKicked) m_onKicked(h);
                        m_running = false;
                        return;
                    }
                }
            }
        }
    };

    // ======================== 配置结构（兼容旧版 VerifyConfig） ========================

    struct VerifyConfig {
        std::string apiHost;            // 服务器地址（不含 http://）
        int         apiPort = 80;   // 端口
        std::string apiPath = "/v1/session";  // API 路径前缀
        std::string apiKey;             // API Key
        bool        useSSL = false;
        std::string xorKey;             // XOR 加密密钥（可选）
    };

    inline VerifyConfig parseConfig(const std::string& json) {
        VerifyConfig cfg;
        cfg.apiHost = detail::jStr(json, "api_host");
        cfg.apiPort = detail::jInt(json, "api_port");
        cfg.apiPath = detail::jStr(json, "api_path");
        cfg.apiKey = detail::jStr(json, "api_key");
        cfg.useSSL = detail::jBool(json, "use_ssl");
        cfg.xorKey = detail::jStr(json, "xor_key");
        if (cfg.apiPath.empty()) cfg.apiPath = "/v1/session";
        if (!cfg.apiPort)        cfg.apiPort = cfg.useSSL ? 443 : 80;
        return cfg;
    }

    // ======================== 旧版兼容结构 ========================

    struct LoginResult {
        bool        success = false;
        std::string sessionToken;
        std::string message;
        int         heartbeatInterval = 60;
        int         heartbeatTimeout = 180;
    };

    // ======================== 旧版兼容函数 ========================

    inline LoginResult verifyCard(VerifyConfig& config, const std::string& cardKey) {
        LoginResult result = { false, "", "", 60, 180 };
        Client client(config.apiHost, config.apiPort, config.apiKey, config.useSSL);
        if (!config.apiPath.empty()) client.setBasePath(config.apiPath);
        Session s = client.login(cardKey);
        result.success = s.success;
        result.sessionToken = s.token;
        result.message = s.message;
        result.heartbeatInterval = s.heartbeatInterval;
        result.heartbeatTimeout = s.heartbeatTimeout;
        return result;
    }

} // namespace kv
