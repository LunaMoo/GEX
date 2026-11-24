// GEX - Graphical Explorer
// Small string / formatting helpers shared by all modules.
#pragma once
#include <string>
#include <cstdint>

namespace gex
{
    std::string  ToUtf8(const std::wstring& w);
    std::string  ToUtf8(const wchar_t* w, int len = -1);
    std::wstring ToWide(const std::string& s);

    // "1.23 GB", "512 B"
    std::string FormatSize(uint64_t bytes);
    // "1,234,567"
    std::string FormatCount(uint64_t n);
    // FILETIME (as uint64) -> "2026-11-24 03:22"
    std::string FormatFileTime(uint64_t ft);
    // FILETIME delta to now -> "5 s ago", "3 min ago", "2 h ago", "4 d ago"
    std::string FormatAgo(uint64_t ft, uint64_t now);
    // Current time as FILETIME uint64 (UTC)
    uint64_t NowFileTime();

    // Case-insensitive (ASCII + basic Unicode via lowercase) contains check on UTF-8 strings
    bool ContainsNoCase(const std::string& haystack, const std::string& needleLower);
    std::string ToLowerAscii(std::string s);

    // Lowercase extension without the dot, "" when none. Works on UTF-8 names.
    std::string ExtensionOf(const std::string& name);

    // Path helpers (wide)
    std::wstring ParentPath(const std::wstring& path);   // "C:\a\b" -> "C:\a", "C:\a" -> "C:\"
    std::wstring FileNameOf(const std::wstring& path);   // "C:\a\b" -> "b"
    std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
    bool         PathStartsWith(const std::wstring& path, const std::wstring& prefix); // case-insensitive, component aware
    bool         PathEquals(const std::wstring& a, const std::wstring& b);
    std::wstring NormalizeDirPath(std::wstring p);       // strip trailing '\' except for "C:\"

    bool IsProcessElevated();
    std::wstring ExeDirectory();
    std::wstring AppDataDirectory(); // %APPDATA%\GEX (created)
}
