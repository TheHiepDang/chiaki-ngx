// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <string>
#include <cstdio>
#include <cwchar>

namespace ChiakiCrash {
using DumpFunction = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);
struct State {
    std::wstring textPath, dumpPath;
    std::string instructions, tag = "[App]";
    DumpFunction dump = nullptr;
    volatile LONG handling = 0;
};
inline State &state() { static auto *s = new State; return *s; }
inline LONG WINAPI handle(EXCEPTION_POINTERS *exception) {
    State &s = state();
    if (InterlockedExchange(&s.handling, 1)) return EXCEPTION_EXECUTE_HANDLER;
    // Paths, instructions and DbgHelp were prepared before the exception.
    // Do not call Qt or acquire application locks in this handler.
    HANDLE text = CreateFileW(s.textPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    char details[256];
    int size = std::snprintf(details, sizeof(details),
        "%s[Crash] Unhandled native exception: code=0x%08lx address=%p thread=%lu\r\n", s.tag.c_str(),
        exception->ExceptionRecord->ExceptionCode,
        exception->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    DWORD written;
    if (text != INVALID_HANDLE_VALUE) {
        WriteFile(text, details, DWORD(size > 0 ? size : 0), &written, nullptr);
        WriteFile(text, s.instructions.data(), DWORD(s.instructions.size()), &written, nullptr);
        FlushFileBuffers(text);
    }
    bool saved = false;
    HANDLE dump = CreateFileW(s.dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        if (s.dump) saved = s.dump(GetCurrentProcess(), GetCurrentProcessId(), dump,
            MiniDumpNormal, &info, nullptr, nullptr);
        FlushFileBuffers(dump);
        CloseHandle(dump);
        if (!saved) DeleteFileW(s.dumpPath.c_str());
    }
    if (text != INVALID_HANDLE_VALUE) {
        const char *result = saved ? "Native minidump saved. Review/share it privately.\r\n"
                                  : "Native minidump unavailable; submit this report and logs.\r\n";
        WriteFile(text, result, DWORD(std::char_traits<char>::length(result)), &written, nullptr);
        FlushFileBuffers(text);
        CloseHandle(text);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
inline void install(const std::wstring &base, const std::string &instructions, const char *tag = "[App]") {
    State &s = state();
    s.textPath = base + L"-crash.txt";
    s.dumpPath = base + L"-crash.dmp";
    s.instructions = instructions;
    s.tag = tag;
    HMODULE library = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (library) s.dump = reinterpret_cast<DumpFunction>(GetProcAddress(library, "MiniDumpWriteDump"));
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(handle);
}
inline void installWorkerFromEnvironment() {
    wchar_t directory[32768] = {};
    DWORD size = GetEnvironmentVariableW(L"CHIAKI_DIAGNOSTIC_LOG_DIR", directory, 32768);
    if (!size || size >= 32768) return;
    SYSTEMTIME time; GetSystemTime(&time);
    wchar_t name[128];
    std::swprintf(name, 128, L"/chiaki_ngx_worker_%04u%02u%02u_%02u%02u%02u_%lu",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, GetCurrentProcessId());
    install(std::wstring(directory) + name,
        "Report: https://github.com/TheHiepDang/chiaki-ngx/issues/new\r\n"
        "Attach this crash.txt and matching chiaki_ngx_*.log / chiaki_session_*.log.\r\n"
        "Find these in Settings > General > Log Directory > Open.\r\n"
        "Include reproduction steps, GPU/driver, and RTX/Eye Candy settings.\r\n"
        "For SM86 FG also attach matching loader_*.jsonl / backend_*.jsonl from the release's runtimes/sm86-proxy/dlssg_sm86/logs/.\r\n"
        "Crash dumps can contain private data; review/share them privately.\r\n", "[RTX]");
}
}
#endif
