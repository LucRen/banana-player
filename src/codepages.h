// Single-byte code pages (Windows-125x, ISO-8859-x, KOI8, the DOS pages...) to and from UTF-8, by tables built in
// (codepage_tables.h). BananaOS ships no glibc gconv modules, so iconv_open fails for every code page on the handheld:
// a TXT in Windows-1250 showed its bytes as "?" whatever encoding was picked (issue #12). iconv stays the way for
// anything these tables don't cover (a multi-byte page: Shift-JIS, GBK...), where the system has it.
#pragma once
#include <string>

namespace bp
{
	// a code page these tables know, by any of its names ("CP1250", "windows-1250", "iso-8859-2"...; case and the
	// spelling's punctuation don't matter)
	bool knownCodePage(const std::string& name);
	// bytes in that page as UTF-8. strict: false at a byte the page leaves undefined (else U+FFFD for it and on).
	// False too for a page the tables don't know
	bool codePageToUtf8(const std::string& name, const char* p, size_t n, std::string& out, bool strict = false);
	// UTF-8 in that page (a search's text, to find it in the file's bytes); false when a character isn't in it
	bool utf8ToCodePage(const std::string& name, const std::string& utf8, std::string& out);
}
