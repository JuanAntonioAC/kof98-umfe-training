#include "log.h"
#include <stdarg.h>
#include <stdio.h>

static FILE* g_log;

void log_open(const wchar_t* path) {
    if (g_log || !path) return;
    g_log = _wfopen(path, L"a");
}

void log_line(const char* text) {
    if (!g_log || !text) return;
    fprintf(g_log, "%s\n", text);
    fflush(g_log);
}

void log_fmt(const char* fmt, ...) {
    va_list ap;
    if (!g_log || !fmt) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}
