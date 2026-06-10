// raylib_stub.h -- minimal stubs for raylib symbols used by game.c and fen.c
// only compiled when building the test target (no actual rendering needed)
#ifndef RAYLIB_STUB_H
#define RAYLIB_STUB_H

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct { float x; float y; } Vector2;

#define LOG_INFO    3
#define LOG_ERROR   4
#define LOG_WARNING 5
#define LOG_DEBUG   2

static inline void TraceLog(int level, const char *fmt, ...) {
    (void)level;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
}

#endif // RAYLIB_STUB_H
