/* Preserve the failing thread and loaded module versions when a Windows driver crashes.
 * DbgHelp runs on a dedicated thread, with a bounded wait on the faulting thread. */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <wchar.h>
#include "crash_win.h"

typedef BOOL (WINAPI *WriteDump)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);
static WriteDump write_dump;
static HANDLE requested, completed;
static EXCEPTION_RECORD record;
static CONTEXT context;
static EXCEPTION_POINTERS pointers = {&record, &context};
static DWORD fault_thread, dump_error;
static wchar_t dump_directory[1024];
static char dump_path_utf8[4096];

static DWORD WINAPI dump_worker(void *unused) {
    (void)unused;
    WaitForSingleObject(requested, INFINITE);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t path[1200];
    _snwprintf(path, sizeof(path)/sizeof(path[0]),
        L"%ls\\bb-crash-%04u%02u%02u-%02u%02u%02u-%lu.dmp", dump_directory,
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        GetCurrentProcessId());
    WideCharToMultiByte(CP_UTF8, 0, path, -1, dump_path_utf8, sizeof(dump_path_utf8), NULL, NULL);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        dump_error = GetLastError();
    } else {
        MINIDUMP_EXCEPTION_INFORMATION info = {fault_thread, &pointers, FALSE};
        if (!write_dump(GetCurrentProcess(), GetCurrentProcessId(), file,
                MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules, &info, NULL, NULL)) {
            dump_error = GetLastError();
            CloseHandle(file);
            DeleteFileW(path); // discard only this newly created, incomplete dump
        } else {
            CloseHandle(file);
        }
    }
    SetEvent(completed);
    return 0;
}

void crash_win_init(void) {
    const unsigned char *base = (const unsigned char *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS64 *pe = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    printf("Runtime build: PE timestamp=%08lx, image_size=0x%lx\n",
        pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage);
    wchar_t setting[8];
    if (GetEnvironmentVariableW(L"BB_CRASH_DUMP", setting, 8) && setting[0] == L'0') return;
    DWORD length = GetEnvironmentVariableW(L"BB_CRASH_DIR", dump_directory, 1024);
    if (length >= 1024) return;
    if (!length) {
        CreateDirectoryW(L"logs", NULL);
        wcscpy(dump_directory, L"logs\\crashes");
    }
    CreateDirectoryW(dump_directory, NULL);
    /* Preload before entering the guest/driver, avoiding LoadLibrary in a fault handler. */
    HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dbghelp) return;
    write_dump = (WriteDump)GetProcAddress(dbghelp, "MiniDumpWriteDump");
    if (!write_dump) return;
    requested = CreateEventW(NULL, FALSE, FALSE, NULL);
    completed = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!requested || !completed) {
        if (requested) CloseHandle(requested);
        if (completed) CloseHandle(completed);
        requested = completed = NULL;
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, dump_worker, NULL, 0, NULL);
    if (!thread) {
        CloseHandle(requested);
        CloseHandle(completed);
        requested = completed = NULL;
        return;
    }
    CloseHandle(thread);
}

void crash_win_dump(EXCEPTION_POINTERS *exception) {
    if (!requested || !completed) return;
    record = *exception->ExceptionRecord;
    context = *exception->ContextRecord;
    fault_thread = GetCurrentThreadId();
    SetEvent(requested);
    if (WaitForSingleObject(completed, 10000) != WAIT_OBJECT_0) {
        fprintf(stderr, "Crash dump: timed out after 10 seconds\n");
    } else if (dump_error) {
        fprintf(stderr, "Crash dump: failed (Windows error 0x%08lx)\n", dump_error);
    } else {
        fprintf(stderr, "Crash dump: %s\n", dump_path_utf8);
    }
}
#endif
