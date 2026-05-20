#ifndef LOGGER_H
#define LOGGER_H
#include <stdarg.h>
#include <stdio.h>

#include "raylib.h"

// Custom logging funtion
void LogColored(int msgType, const char *text, va_list args) {
    switch (msgType) {
    case LOG_INFO:
        printf("[\e[1;32mINFO\e[0m] : ");
        break;
    case LOG_ERROR:
        printf("[\e[1;31mERROR\e[0m]: ");
        break;
    case LOG_WARNING:
        printf("[\e[1;33mWARN\e[0m] : ");
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
