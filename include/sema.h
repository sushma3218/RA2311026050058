#ifndef NOVA_SEMA_H
#define NOVA_SEMA_H

/*
 * Nova Language Compiler - Semantic Analysis
 * Type checking, name resolution, and scope management.
 */

#include "ast.h"

/* ── Symbol kinds ─────────────────────────────────────────────────────────── */
typedef enum {
    SYM_VAR,
    SYM_FN,
    SYM_PARAM,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_EXTERN_FN,
} SymbolKind;

typedef struct Symbol Symbol;
struct Symbol {
    SymbolKind  kind;
    const char *name;
    Type       *type;
    ASTNode    *decl;  /* back-pointer to declaration node */
    int         is_mut;
    Symbol     *next;  /* hash chain */
};

/* ── Scope ────────────────────────────────────────────────────────────────── */
#define SCOPE_HASH_SIZE 64
typedef struct Scope Scope;
struct Scope {
    Symbol *buckets[SCOPE_HASH_SIZE];
    Scope  *parent;
};

/* ── Semantic analyser ────────────────────────────────────────────────────── */
typedef struct {
    Arena   *arena;
    Scope   *current_scope;
    Type    *current_fn_ret; /* expected return type of enclosing function */
    int      had_error;
    unsigned error_count;
} Sema;

/* ── Public API ───────────────────────────────────────────────────────────── */
void sema_init(Sema *s, Arena *arena);
int  sema_analyse(Sema *s, ASTNode *module);
void sema_free(Sema *s);

Symbol *scope_lookup(Scope *sc, const char *name);

#endif /* NOVA_SEMA_H */
