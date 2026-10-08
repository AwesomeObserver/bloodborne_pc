#ifndef BB_CRASH_WIN_H
#define BB_CRASH_WIN_H
#ifdef _WIN32
#include <windows.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BB_CRASH_ASSERT_EXCEPTION ((DWORD)0xe0424201u)
void crash_win_init(void);
void crash_win_set_guest(const void *base, size_t size);
void crash_win_dump(EXCEPTION_POINTERS *exception);
/* Capture a deliberate fatal stop without raising/reclassifying a CPU fault. */
void crash_win_dump_fatal(const char *reason);
#ifdef __cplusplus
}
#endif
#endif
#endif
