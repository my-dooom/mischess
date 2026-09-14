#ifndef LOGGER_H
#define LOGGER_H
#include <stdarg.h>
#include <stdio.h>

#include "raylib.h"

// ANSI colour codes; \x1b rather than the GCC-only \e so MSVC accepts it
#define ANSI_GREEN "\x1b[1;32m"
#define ANSI_RED "\x1b[1;31m"
#define ANSI_YELLOW "\x1b[1;33m"
#define ANSI_RESET "\x1b[0m"

// Custom logging funtion
void LogColored(int msgType, const char *text, va_list args) {
    switch (msgType) {
    case LOG_INFO:
        printf("[" ANSI_GREEN "INFO" ANSI_RESET "] : ");
        break;
    case LOG_ERROR:
        printf("[" ANSI_RED "ERROR" ANSI_RESET "]: ");
        break;
    case LOG_WARNING:
        printf("[" ANSI_YELLOW "WARN" ANSI_RESET "] : ");
        break;
    case LOG_DEBUG:
        printf("[DEBUG]: ");
        break;
    default:
        break;
    }

    vprintf(text, args);
    printf("\n");
}

#endif // LOGGER_H
