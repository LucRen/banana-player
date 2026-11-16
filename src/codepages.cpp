#include "codepages.h"
#include "codepage_tables.h"
#include <cctype>
#include <cstdint>

namespace bp
{
	namespace
	{
		// a name with case and punctuation dropped: "Windows-1250", "WINDOWS_1250" and "windows1250" are one
		std::string squeeze(const std::string& s)
		{
			std::string o;
			for (char c : s)
				if (isalnum((unsigned char)c)) o += (char)toupper((unsigned char)c);
			return o;
		}

		const uint16_t* findPage(const std::string& name)
		{
			std::string want = squeeze(name);
			if (want.empty()) return nullptr;
			for (const CodePageTable& t : CODE_PAGES)
				for (const char* const* n = t.names; *n; n++)
					if (squeeze(*n) == want) return t.high;
			return nullptr;
		}

		void putUtf8(std::string& o, uint32_t c)
		{
			if (c < 0x80) o += (char)c;
			else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
			else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
		}
	}

	bool knownCodePage(const std::string& name) { return findPage(name) != nullptr; }

	bool codePageToUtf8(const std::string& name, const char* p, size_t n, std::string& out, bool strict)
	{
		const uint16_t* high = findPage(name);
		if (!high) return false;
		out.clear();
		out.reserve(n + n / 2);
		for (size_t i = 0; i < n; i++)
		{
			uint8_t b = (uint8_t)p[i];
			if (b < 0x80) { out += (char)b; continue; }
			uint16_t u = high[b - 0x80];
			if (!u)
			{
				if (strict) return false;
				u = 0xFFFD;	// (a byte this page doesn't have: a mark for it, and on)
			}
			putUtf8(out, u);
		}
		return true;
	}

	bool utf8ToCodePage(const std::string& name, const std::string& utf8, std::string& out)
	{
		const uint16_t* high = findPage(name);
		if (!high) return false;
		out.clear();
		for (size_t i = 0; i < utf8.size();)
		{
			uint8_t c = (uint8_t)utf8[i];
			uint32_t cp;
			int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
			if (!len || i + len > utf8.size()) return false;
			cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
			for (int k = 1; k < len; k++) cp = (cp << 6) | ((uint8_t)utf8[i + k] & 0x3F);
			i += len;
			if (cp < 0x80) { out += (char)cp; continue; }
			int at = -1;
			for (int b = 0; b < 128 && at < 0; b++)
				if (high[b] == cp) at = b;
			if (at < 0) return false;	// (not in this page: the search can't match it)
			out += (char)(0x80 + at);
		}
		return true;
	}
}
