#include "app/timing.h"

#include <windows.h>
#include <shlobj.h>
#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace ciniva::timing {

static ULONGLONG g_created = 0;
static std::mutex g_mutex;
static FILE* g_file = nullptr;

static ULONGLONG now_100ns() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

void init() {
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        g_created = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    else
        g_created = now_100ns();

    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        std::wstring dir = std::wstring(base) + L"\\Ciniva";
        CreateDirectoryW(dir.c_str(), nullptr);
        g_file = _wfsopen((dir + L"\\ciniva.log").c_str(), L"w", _SH_DENYWR);
        CoTaskMemFree(base);
    }
}

double since_process_start_ms() {
    return static_cast<double>(now_100ns() - g_created) / 10000.0;
}

static void write_line(const char* text) {
    std::lock_guard lock(g_mutex);
    char line[1200];
    std::snprintf(line, sizeof line, "[%9.1f ms] %s\n", since_process_start_ms(), text);
    OutputDebugStringA(line);
    if (g_file) {
        std::fputs(line, g_file);
        std::fflush(g_file);
    }
}

double now_seconds() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}

void mark(const char* what) { write_line(what); }

void log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    write_line(buf);
}

}
