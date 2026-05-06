#ifndef NOVA_PARSER_H
#define NOVA_PARSER_H

/*
 * Nova Language Compiler - Parser
 * Recursive-descent parser producing a typed AST.
 */

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer   *lex;
    Arena   *arena;
    Token    current;
    Token    lookahead;
    int      had_error;
    int      panic_mode;
    unsigned error_count;
} Parser;

/* ── Public API ───────────────────────────────────────────────────────────── */
void     parser_init(Parser *p, Lexer *lex, Arena *arena);
ASTNode *parser_parse_module(Parser *p, const char *module_name);

#endif /* NOVA_PARSER_H */
