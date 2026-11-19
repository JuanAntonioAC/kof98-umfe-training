#ifndef KOF98_TRAIN_LOG_H
#define KOF98_TRAIN_LOG_H
#include <stddef.h>
void log_open(const wchar_t* path);
void log_line(const char* text);
void log_fmt(const char* fmt, ...);
#endif
