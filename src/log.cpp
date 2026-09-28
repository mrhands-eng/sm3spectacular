#include "log.hpp"

#include "config.hpp"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace sm3spectacular {
namespace {

std::mutex g_logMutex;
FILE* g_log = nullptr;

void writeTimestampUnlocked(FILE* f) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ", (unsigned)st.wYear, (unsigned)st.wMonth,
            (unsigned)st.wDay, (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
            (unsigned)st.wMilliseconds);
}

void vlogfUnlocked(const char* fmt, va_list args) {
    if (!g_log) {
        return;
    }
    writeTimestampUnlocked(g_log);
    vfprintf(g_log, fmt, args);
    fputc('\n', g_log);
}

}  // namespace

void logInit() {
    std::lock_guard lock(g_logMutex);
    if (g_log) {
        return;
    }
    const auto path = moduleDirectory() + L"\\sm3spectacular.log";
    g_log = _wfopen(path.c_str(), L"w");
    if (g_log) {
        setvbuf(g_log, nullptr, _IONBF, 0);
        writeTimestampUnlocked(g_log);
        fprintf(g_log, "SM3 Spectacular Edition by zryuyu — log started\n");
    }
}

void logf(const char* fmt, ...) {
    std::lock_guard lock(g_logMutex);
    va_list args;
    va_start(args, fmt);
    vlogfUnlocked(fmt, args);
    va_end(args);
}

}  // namespace sm3spectacular
