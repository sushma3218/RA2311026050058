#ifndef NOVA_DIAG_H
#define NOVA_DIAG_H

/*
 * Nova Language Compiler - Diagnostics
 * Pretty-printed, colour-coded error and warning messages.
 */

#include "lexer.h"

typedef enum {
    DIAG_NOTE,
    DIAG_WARN,
    DIAG_ERROR,
    DIAG_FATAL,
} DiagLevel;

void diag_emit(DiagLevel level, const SrcLoc *loc, const char *fmt, ...);

/* Shorthand macros */
#define DIAG_NOTE(loc, ...)  diag_emit(DIAG_NOTE,  loc, __VA_ARGS__)
#define DIAG_WARN(loc, ...)  diag_emit(DIAG_WARN,  loc, __VA_ARGS__)
#define DIAG_ERR(loc, ...)   diag_emit(DIAG_ERROR, loc, __VA_ARGS__)
#define DIAG_FATAL(loc, ...) diag_emit(DIAG_FATAL, loc, __VA_ARGS__)

/* Terminal colour support (auto-disabled if not a TTY) */
void diag_use_color(int enable);

#endif /* NOVA_DIAG_H */
