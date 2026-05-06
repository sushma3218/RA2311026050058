/*
 * Nova Language Compiler - Diagnostics Implementation
 */
#define _POSIX_C_SOURCE 200112L
#include "diag.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <io.h>
  #define ISATTY(fd) _isatty(fd)
#else
  #include <unistd.h>
  #define ISATTY(fd) isatty(fd)
#endif

static int g_use_color = -1;

void diag_use_color(int enable) { g_use_color = enable; }

static int should_color(void) {
    if (g_use_color == -1) return ISATTY(2);
    return g_use_color;
}

static const char *level_color(DiagLevel lv) {
    if (!should_color()) return "";
    switch (lv) {
        case DIAG_NOTE:  return "\033[36m";
        case DIAG_WARN:  return "\033[33m";
        case DIAG_ERROR: return "\033[31m";
        case DIAG_FATAL: return "\033[35m";
    }
    return "";
}

static const char *level_label(DiagLevel lv) {
    switch (lv) {
        case DIAG_NOTE:  return "note";
        case DIAG_WARN:  return "warning";
        case DIAG_ERROR: return "error";
        case DIAG_FATAL: return "fatal";
    }
    return "unknown";
}

void diag_emit(DiagLevel level, const SrcLoc *loc, const char *fmt, ...) {
    const char *clr   = level_color(level);
    const char *label = level_label(level);
    const char *rst   = should_color() ? "\033[0m" : "";
    const char *bld   = should_color() ? "\033[1m"  : "";

    if (loc && loc->filename)
        fprintf(stderr, "%s%s:%u:%u:%s ", bld, loc->filename, loc->line, loc->col, rst);

    fprintf(stderr, "%s%s%s%s: ", bld, clr, label, rst);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    if (level == DIAG_FATAL) exit(1);
}
