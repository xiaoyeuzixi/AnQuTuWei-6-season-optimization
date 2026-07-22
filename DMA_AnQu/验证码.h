#pragma once
#include <string>
#include <cstdio>
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

// ============================================================
// 验证码类 - 基于北京时间日期 + 密钥 + SHA256哈希
// 验证码格式：12位小写字母+数字组合
// ============================================================
class 验证码
{
private:
	// 密钥（必须与生成端保持一致）
	static const std::string 密钥;

	// 字符集（小写字母+数字）
	static const char 字符集[];
	static const int 字符集大小;

public:
	// 验证输入的验证码是否匹配今天的验证码
	static bool 验证(const std::string& 输入的验证码)
	{
		if (输入的验证码.length() != 12)
			return false;

		// 检查每个字符是否在合法字符集内
		for (char c : 输入的验证码)
		{
			bool 合法 = false;
			for (int i = 0; i < 字符集大小; i++)
			{
				if (c == 字符集[i])
				{
					合法 = true;
					break;
				}
			}
			if (!合法) return false;
		}

		// 取今天北京日期 → 密钥混淆哈希 → 比对
		int y, m, d;
		取北京时间(y, m, d);
		char 日期字符串[32] = { 0 };
		sprintf_s(日期字符串, "%04d-%02d-%02d", y, m, d);
		std::string 今天验证码 = 密钥日期混淆(日期字符串);
		return 输入的验证码 == 今天验证码;
	}

	// 获取北京时间（年月日）
	static void 取北京时间(int& year, int& month, int& day)
	{
		SYSTEMTIME st;
		GetSystemTime(&st); // UTC

		// UTC → UTC+8（北京时间）
		FILETIME ft;
		SystemTimeToFileTime(&st, &ft);
		ULARGE_INTEGER uli;
		uli.LowPart = ft.dwLowDateTime;
		uli.HighPart = ft.dwHighDateTime;
		uli.QuadPart += 8ULL * 60 * 60 * 10000000;
		ft.dwLowDateTime = uli.LowPart;
		ft.dwHighDateTime = uli.HighPart;
		FileTimeToSystemTime(&ft, &st);

		year = st.wYear;
		month = st.wMonth;
		day = st.wDay;
	}

private:
	// 密钥+日期哈希混淆（与生成端算法完全一致）
	static std::string 密钥日期混淆(const std::string& 日期)
	{
		// 使用与生成端相同的混淆方式：密钥 + "|" + 日期 + "|" + 密钥
		std::string 混淆数据 = 密钥 + "|" + 日期 + "|" + 密钥;
		return 哈希取12位字符(混淆数据);
	}

	// SHA256 → 生成12位小写字母+数字组合
	static std::string 哈希取12位字符(const std::string& 数据)
	{
		HCRYPTPROV hProv = NULL;
		HCRYPTHASH hHash = NULL;
		BYTE rgbHash[32] = { 0 };
		DWORD cbHash = 32;
		char buf[13] = { 0 };  // 12位 + 结束符

		if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		{
			if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_NEWKEYSET))
				return "000000000000";
		}
		if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
		{
			CryptReleaseContext(hProv, 0);
			return "000000000000";
		}
		CryptHashData(hHash, (const BYTE*)数据.c_str(), (DWORD)数据.length(), 0);
		CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0);
		CryptDestroyHash(hHash);
		CryptReleaseContext(hProv, 0);

		// 使用哈希值生成12位字符
		// 每5位哈希值生成一个字符（2^5=32，覆盖36字符）
		ULONGLONG 值 = 0;
		int 位计数 = 0;
		int 索引 = 0;

		for (int i = 0; i < 12; i++)
		{
			// 当不足5位时，从哈希中取新的字节
			if (位计数 < 5)
			{
				值 = (值 << 8) | rgbHash[索引++];
				位计数 += 8;
				索引 %= 32;  // 循环使用哈希值
			}

			// 取低5位（0-31）
			int 字符索引 = (值 >> (位计数 - 5)) & 0x1F;
			位计数 -= 5;

			// 映射到字符集（0-35，但5位只有0-31，刚好在36字符内）
			buf[i] = 字符集[字符索引];
		}

		return std::string(buf);
	}
};

// 静态成员变量定义
const std::string 验证码::密钥 = "dadadanb";
const char 验证码::字符集[] = "0123456789abcdefghijklmnopqrstuvwxyz";
const int 验证码::字符集大小 = 36;

// ============================================================
// 验证码心跳类 - 在主循环中检测验证码是否过期
// ============================================================
class 验证码心跳
{
public:
	验证码心跳(const std::string& 已验证码)
		: m_验证码(已验证码)
	{
		int y, m, d;
		验证码::取北京时间(y, m, d);
		m_记录日期 = y * 10000 + m * 100 + d;
		m_已过期 = false;
	}

	// 返回 true = 有效，false = 已过期
	bool 心跳检测()
	{
		if (m_已过期)
			return false;

		int y, m, d;
		验证码::取北京时间(y, m, d);
		if ((y * 10000 + m * 100 + d) != m_记录日期)
		{
			m_已过期 = true;
			return false;
		}
		return true;
	}

	bool 是否有效() const { return !m_已过期; }

private:
	std::string m_验证码;	// 已验证的验证码
	int m_记录日期;		// 验证成功时的北京日期（YYYYMMDD）
	bool m_已过期 = false;
};