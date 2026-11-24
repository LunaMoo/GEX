// GEX - Graphical Explorer
#include "Util.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cwctype>

namespace gex
{
    std::string ToUtf8(const wchar_t* w, int len)
    {
        if (!w) return {};
        if (len < 0) len = (int)wcslen(w);
        if (len == 0) return {};
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
        std::string out((size_t)n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), n, nullptr, nullptr);
        return out;
    }

    std::string ToUtf8(const std::wstring& w) { return ToUtf8(w.c_str(), (int)w.size()); }

    std::wstring ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring out((size_t)n, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
        return out;
    }

    std::string FormatSize(uint64_t b)
    {
        char buf[64];
        const double d = (double)b;
        if (b < 1024ull)                      snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
        else if (b < 1024ull * 1024)          snprintf(buf, sizeof(buf), "%.1f KB", d / 1024.0);
        else if (b < 1024ull * 1024 * 1024)   snprintf(buf, sizeof(buf), "%.1f MB", d / (1024.0 * 1024.0));
        else if (b < 1024ull * 1024 * 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f GB", d / (1024.0 * 1024.0 * 1024.0));
        else                                  snprintf(buf, sizeof(buf), "%.2f TB", d / (1024.0 * 1024.0 * 1024.0 * 1024.0));
        return buf;
    }

    std::string FormatCount(uint64_t n)
    {
        std::string s = std::to_string(n);
        int insertPosition = (int)s.length() - 3;
        while (insertPosition > 0) { s.insert((size_t)insertPosition, ","); insertPosition -= 3; }
        return s;
    }

    std::string FormatFileTime(uint64_t ft)
    {
        if (ft == 0) return "";
        FILETIME f; f.dwLowDateTime = (DWORD)(ft & 0xffffffff); f.dwHighDateTime = (DWORD)(ft >> 32);
        FILETIME lf; SYSTEMTIME st;
        if (!::FileTimeToLocalFileTime(&f, &lf) || !::FileTimeToSystemTime(&lf, &st)) return "";
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        return buf;
    }

    uint64_t NowFileTime()
    {
        FILETIME f; ::GetSystemTimeAsFileTime(&f);
        return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime;
    }

    std::string FormatAgo(uint64_t ft, uint64_t now)
    {
        if (ft == 0) return "";
        if (ft > now) return "just now";
        const uint64_t secs = (now - ft) / 10000000ull;
        char buf[64];
        if (secs < 60)              snprintf(buf, sizeof(buf), "%llu s ago", (unsigned long long)secs);
        else if (secs < 3600)       snprintf(buf, sizeof(buf), "%llu min ago", (unsigned long long)(secs / 60));
        else if (secs < 86400)      snprintf(buf, sizeof(buf), "%llu h ago", (unsigned long long)(secs / 3600));
        else                        snprintf(buf, sizeof(buf), "%llu d ago", (unsigned long long)(secs / 86400));
        return buf;
    }

    std::string ToLowerAscii(std::string s)
    {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return s;
    }

    bool ContainsNoCase(const std::string& haystack, const std::string& needleLower)
    {
        if (needleLower.empty()) return true;
        if (needleLower.size() > haystack.size()) return false;
        const size_t n = needleLower.size();
        for (size_t i = 0; i + n <= haystack.size(); ++i)
        {
            size_t j = 0;
            for (; j < n; ++j)
            {
                char c = haystack[i + j];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c != needleLower[j]) break;
            }
            if (j == n) return true;
        }
        return false;
    }

    std::string ExtensionOf(const std::string& name)
    {
        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size()) return {};
        if (name.size() - dot > 16) return {}; // not a real extension
        return ToLowerAscii(name.substr(dot + 1));
    }

    std::wstring NormalizeDirPath(std::wstring p)
    {
        for (auto& c : p) if (c == L'/') c = L'\\';
        while (p.size() > 3 && p.back() == L'\\') p.pop_back();
        if (p.size() == 2 && p[1] == L':') p += L'\\';
        return p;
    }

    std::wstring ParentPath(const std::wstring& path)
    {
        std::wstring p = NormalizeDirPath(path);
        if (p.size() <= 3 && p.size() >= 2 && p[1] == L':') return p; // drive root
        const size_t pos = p.find_last_of(L'\\');
        if (pos == std::wstring::npos) return p;
        std::wstring parent = p.substr(0, pos);
        if (parent.size() == 2 && parent[1] == L':') parent += L'\\';
        return parent;
    }

    std::wstring FileNameOf(const std::wstring& path)
    {
        std::wstring p = NormalizeDirPath(path);
        const size_t pos = p.find_last_of(L'\\');
        if (pos == std::wstring::npos) return p;
        if (pos + 1 >= p.size()) return p;
        return p.substr(pos + 1);
    }

    std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
    {
        if (dir.empty()) return name;
        if (dir.back() == L'\\') return dir + name;
        return dir + L"\\" + name;
    }

    static bool WEqNoCase(const wchar_t* a, const wchar_t* b, size_t n)
    {
        return ::CompareStringOrdinal(a, (int)n, b, (int)n, TRUE) == CSTR_EQUAL;
    }

    bool PathEquals(const std::wstring& a, const std::wstring& b)
    {
        std::wstring na = NormalizeDirPath(a), nb = NormalizeDirPath(b);
        return na.size() == nb.size() && WEqNoCase(na.c_str(), nb.c_str(), na.size());
    }

    bool PathStartsWith(const std::wstring& path, const std::wstring& prefix)
    {
        std::wstring p = NormalizeDirPath(path), pre = NormalizeDirPath(prefix);
        if (pre.size() > p.size()) return false;
        if (!WEqNoCase(p.c_str(), pre.c_str(), pre.size())) return false;
        if (p.size() == pre.size()) return true;
        return pre.back() == L'\\' || p[pre.size()] == L'\\';
    }

    bool IsProcessElevated()
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        TOKEN_ELEVATION elev{}; DWORD sz = 0;
        BOOL ok = ::GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &sz);
        ::CloseHandle(token);
        return ok && elev.TokenIsElevated;
    }

    std::wstring ExeDirectory()
    {
        wchar_t buf[MAX_PATH * 4];
        DWORD n = ::GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        return ParentPath(std::wstring(buf, n));
    }

    std::wstring AppDataDirectory()
    {
        PWSTR p = nullptr;
        std::wstring out;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)))
        {
            out = JoinPath(p, L"GEX");
            ::CoTaskMemFree(p);
            ::CreateDirectoryW(out.c_str(), nullptr);
        }
        else
            out = ExeDirectory();
        return out;
    }
}
