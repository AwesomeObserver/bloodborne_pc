#ifndef BB_CRASH_WIN_H
#define BB_CRASH_WIN_H
#ifdef _WIN32
#include <windows.h>
#include <stddef.h>
void crash_win_init(void);
void crash_win_set_guest(const void *base, size_t size);
void crash_win_dump(EXCEPTION_POINTERS *exception);
#endif
#endif
