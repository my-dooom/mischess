// raylib.h -- minimal stub for the test target (no rendering, no window)
#ifndef RAYLIB_H
#define RAYLIB_H

#include <math.h>
#include <stdbool.h>
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

#endif // RAYLIB_H
