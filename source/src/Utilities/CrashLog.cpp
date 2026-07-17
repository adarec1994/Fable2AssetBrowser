#include "CrashLog.h"

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstdlib>
#include <io.h>

#pragma comment(lib, "dbghelp.lib")

namespace CrashLog {

namespace {

void write_stack(FILE* f, CONTEXT* context) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    char symbol_buffer[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;

    for (int depth = 0; depth < 64; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
                         context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) break;

        DWORD64 displacement = 0;
        const char* name = "?";
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement,
                        symbol)) {
            name = symbol->Name;
        }
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD line_disp = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_disp,
                                 &line)) {
            std::fprintf(f, "  %02d %s +0x%llx  (%s:%lu)\n", depth, name,
                         (unsigned long long)displacement, line.FileName,
                         line.LineNumber);
        } else {
            std::fprintf(f, "  %02d %s +0x%llx\n", depth, name,
                         (unsigned long long)displacement);
        }
    }
    SymCleanup(process);
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) {
    FILE* f = nullptr;
    if (fopen_s(&f, "crash_log.txt", "w") == 0 && f) {
        std::fprintf(f, "Unhandled exception 0x%08lX at 0x%p\n",
                     info->ExceptionRecord->ExceptionCode,
                     info->ExceptionRecord->ExceptionAddress);
        write_stack(f, info->ContextRecord);
        std::fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}

void Install() {
    
    
    if (_fileno(stderr) < 0 || GetStdHandle(STD_ERROR_HANDLE) == nullptr) {
        FILE* redirected = nullptr;
        freopen_s(&redirected, "crash_stderr.log", "w", stderr);
    }
#ifdef _DEBUG
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    SetUnhandledExceptionFilter(unhandled_filter);
}

}

#else

namespace CrashLog {
void Install() {}
}

#endif
