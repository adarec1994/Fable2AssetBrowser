#pragma once

#include <algorithm>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace DebugTrace {

inline const std::string& log_path() {
    static const std::string p = [] {
#ifdef _WIN32
        char buf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string s(buf);
        const size_t sl = s.find_last_of("\\/");
        if (sl != std::string::npos) s.resize(sl + 1);
        else s.clear();
        return s + "level_debug.log";
#else
        return std::string("level_debug.log");
#endif
    }();
    return p;
}

inline void log(const char* format, ...) {
    if (!format) return;
    char message[4096] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
#ifdef _WIN32
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char line[4352] = {};
    const int length = std::snprintf(
        line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds,
        (unsigned long)GetCurrentThreadId(), message);
    if (length <= 0) return;
    HANDLE file = CreateFileA(
        log_path().c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    const DWORD bytes =
        (DWORD)std::min<int>(length, (int)sizeof(line) - 1);
    WriteFile(file, line, bytes, &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
#else
    FILE* file = std::fopen(log_path().c_str(), "a");
    if (!file) return;
    std::fprintf(file, "%s\n", message);
    std::fflush(file);
    std::fclose(file);
#endif
}

#ifdef _WIN32
inline void log_exception(const char* tag, EXCEPTION_POINTERS* ep) {
    const DWORD code = ep && ep->ExceptionRecord
        ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr = ep && ep->ExceptionRecord
        ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    const HMODULE base = GetModuleHandleA(nullptr);
    log("%s: exception 0x%08lX at %p (module base %p, offset 0x%llX)",
        tag, (unsigned long)code, addr, (void*)base,
        (unsigned long long)((uintptr_t)addr - (uintptr_t)base));
    void* frames[32] = {};
    const USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        log("%s: frame %02u %p (offset 0x%llX)", tag, i, frames[i],
            (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)base));
    }
}

inline LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* ep) {
    log_exception("CRASH", ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

inline LONG WINAPI vectored_filter(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (c == EXCEPTION_ACCESS_VIOLATION ||
        c == EXCEPTION_ILLEGAL_INSTRUCTION ||
        c == EXCEPTION_STACK_OVERFLOW ||
        c == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        c == 0xC0000409u) {
        log_exception("FATAL", ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

inline void abort_handler(int) {
    log("CRASH: abort() called (assert or debug-CRT failure)");
    const HMODULE base = GetModuleHandleA(nullptr);
    void* frames[32] = {};
    const USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        log("CRASH: frame %02u %p (offset 0x%llX)", i, frames[i],
            (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)base));
    }
}

inline void install_crash_handler() {
    SetUnhandledExceptionFilter(unhandled_filter);
    AddVectoredExceptionHandler(1, vectored_filter);
    std::signal(SIGABRT, abort_handler);
    log("=== session start (crash handler installed) ===");
}
#else
inline void install_crash_handler() {}
#endif

}
