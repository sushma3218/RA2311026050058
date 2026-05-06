/*
 * Nova Language Compiler - Lexer Implementation
 */
#include "lexer.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

/* ── Keyword table ────────────────────────────────────────────────────────── */
typedef struct { const char *word; TokenKind kind; } KwEntry;
static const KwEntry KEYWORDS[] = {
    {"fn",     TOK_FN},    {"let",    TOK_LET},   {"mut",    TOK_MUT},
    {"return", TOK_RETURN},{"if",     TOK_IF},     {"else",   TOK_ELSE},
    {"while",  TOK_WHILE}, {"for",    TOK_FOR},    {"in",     TOK_IN},
    {"import", TOK_IMPORT},{"extern", TOK_EXTERN}, {"struct", TOK_STRUCT},
    {"enum",   TOK_ENUM},  {"as",     TOK_AS},     {"null",   TOK_NULL},
    {"true",   TOK_BOOL_LIT},{"false",TOK_BOOL_LIT},
    /* types */
    {"i8",     TOK_TYPE_I8},  {"i16",  TOK_TYPE_I16},{"i32",  TOK_TYPE_I32},
    {"i64",    TOK_TYPE_I64}, {"u8",   TOK_TYPE_U8}, {"u16",  TOK_TYPE_U16},
    {"u32",    TOK_TYPE_U32}, {"u64",  TOK_TYPE_U64},
    {"f32",    TOK_TYPE_F32}, {"f64",  TOK_TYPE_F64},
    {"bool",   TOK_TYPE_BOOL},{"str",  TOK_TYPE_STR},{"void", TOK_TYPE_VOID},
    {NULL, TOK_EOF}
};

/* ── Helpers ──────────────────────────────────────────────────────────────── */
static char peek_char(Lexer *lex) {
    if (lex->pos >= lex->src_len) return '\0';
    return lex->src[lex->pos];
}

static char peek2(Lexer *lex) {
    if (lex->pos + 1 >= lex->src_len) return '\0';
    return lex->src[lex->pos + 1];
}

static char advance(Lexer *lex) {
    char c = lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else           { lex->col++; }
    return c;
}

static int match(Lexer *lex, char expected) {
    if (peek_char(lex) == expected) { advance(lex); return 1; }
    return 0;
}

static SrcLoc current_loc(const Lexer *lex) {
    SrcLoc loc;
    loc.filename = lex->filename;
    loc.line     = lex->line;
    loc.col      = lex->col;
    loc.offset   = (uint32_t)lex->pos;
    return loc;
}

static void skip_whitespace_and_comments(Lexer *lex) {
    for (;;) {
        char c = peek_char(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
        } else if (c == '/' && peek2(lex) == '/') {
            /* line comment */
            while (peek_char(lex) != '\n' && peek_char(lex) != '\0')
                advance(lex);
        } else if (c == '/' && peek2(lex) == '*') {
            /* block comment */
            advance(lex); advance(lex);
            while (lex->pos < lex->src_len) {
                if (peek_char(lex) == '*' && peek2(lex) == '/') {
                    advance(lex); advance(lex); break;
                }
                advance(lex);
            }
        } else {
            break;
        }
    }
}

/* ── Intern string literal ────────────────────────────────────────────────── */
static char *intern_string(Lexer *lex, const char *s, size_t len) {
    if (lex->string_count >= lex->string_cap) {
        lex->string_cap = lex->string_cap ? lex->string_cap * 2 : 16;
        lex->strings = realloc(lex->strings, lex->string_cap * sizeof(char *));
        if (!lex->strings) { perror("realloc"); exit(1); }
    }
    char *buf = malloc(len + 1);
    if (!buf) { perror("malloc"); exit(1); }
    /* process escape sequences */
    size_t di = 0;
    for (size_t si = 0; si < len; si++) {
        if (s[si] == '\\' && si + 1 < len) {
            si++;
            switch (s[si]) {
                case 'n':  buf[di++] = '\n'; break;
                case 't':  buf[di++] = '\t'; break;
                case 'r':  buf[di++] = '\r'; break;
                case '\\': buf[di++] = '\\'; break;
                case '"':  buf[di++] = '"';  break;
                case '0':  buf[di++] = '\0'; break;
                default:   buf[di++] = '\\'; buf[di++] = s[si]; break;
            }
        } else {
            buf[di++] = s[si];
        }
    }
    buf[di] = '\0';
    lex->strings[lex->string_count++] = buf;
    return buf;
}

/* ── Number lexing ────────────────────────────────────────────────────────── */
static Token lex_number(Lexer *lex) {
    SrcLoc loc = current_loc(lex);
    const char *start = lex->src + lex->pos;
    int is_float = 0;
    int64_t ival = 0;
    double  fval = 0.0;

    /* Hex literal */
    if (peek_char(lex) == '0' && (peek2(lex) == 'x' || peek2(lex) == 'X')) {
        advance(lex); advance(lex);
        while (isxdigit((unsigned char)peek_char(lex))) {
            char c = advance(lex);
            ival = ival * 16 + (isdigit((unsigned char)c) ? c - '0' :
                                tolower((unsigned char)c) - 'a' + 10);
        }
        Token t = {TOK_INT_LIT, loc, start,
                   (size_t)(lex->src + lex->pos - start), {0}};
        t.val.ival = ival;
        return t;
    }

    /* Binary literal */
    if (peek_char(lex) == '0' && (peek2(lex) == 'b' || peek2(lex) == 'B')) {
        advance(lex); advance(lex);
        while (peek_char(lex) == '0' || peek_char(lex) == '1') {
            char c = advance(lex);
            ival = ival * 2 + (c - '0');
        }
        Token t = {TOK_INT_LIT, loc, start,
                   (size_t)(lex->src + lex->pos - start), {0}};
        t.val.ival = ival;
        return t;
    }

    /* Decimal / float */
    while (isdigit((unsigned char)peek_char(lex))) {
        ival = ival * 10 + (advance(lex) - '0');
    }
    if (peek_char(lex) == '.' && peek2(lex) != '.') {
        is_float = 1;
        advance(lex); /* consume '.' */
        fval = (double)ival;
        double frac = 0.1;
        while (isdigit((unsigned char)peek_char(lex))) {
            fval += (advance(lex) - '0') * frac;
            frac *= 0.1;
        }
    }
    /* Exponent */
    if ((peek_char(lex) == 'e' || peek_char(lex) == 'E') && is_float) {
        advance(lex);
        int exp_sign = 1;
        if (peek_char(lex) == '+') advance(lex);
        else if (peek_char(lex) == '-') { advance(lex); exp_sign = -1; }
        int exp = 0;
        while (isdigit((unsigned char)peek_char(lex)))
            exp = exp * 10 + (advance(lex) - '0');
        double mult = 1.0;
        for (int i = 0; i < exp; i++) mult *= 10.0;
        if (exp_sign < 0) fval /= mult; else fval *= mult;
    }

    Token t;
    t.loc   = loc;
    t.start = start;
    t.len   = (size_t)(lex->src + lex->pos - start);
    if (is_float) {
        t.kind      = TOK_FLOAT_LIT;
        t.val.fval  = fval;
    } else {
        t.kind      = TOK_INT_LIT;
        t.val.ival  = ival;
    }
    return t;
}

/* ── Identifier / keyword lexing ─────────────────────────────────────────── */
static Token lex_ident(Lexer *lex) {
    SrcLoc loc = current_loc(lex);
    const char *start = lex->src + lex->pos;
    while (isalnum((unsigned char)peek_char(lex)) || peek_char(lex) == '_')
        advance(lex);
    size_t len = (size_t)(lex->src + lex->pos - start);

    /* keyword lookup */
    for (const KwEntry *kw = KEYWORDS; kw->word; kw++) {
        if (strlen(kw->word) == len && memcmp(kw->word, start, len) == 0) {
            Token t = {kw->kind, loc, start, len, {0}};
            if (kw->kind == TOK_BOOL_LIT)
                t.val.bval = (start[0] == 't') ? 1 : 0;
            return t;
        }
    }
    Token t = {TOK_IDENT, loc, start, len, {0}};
    return t;
}

/* ── String literal ───────────────────────────────────────────────────────── */
static Token lex_string(Lexer *lex) {
    SrcLoc loc = current_loc(lex);
    advance(lex); /* opening '"' */
    const char *str_start = lex->src + lex->pos;
    while (peek_char(lex) != '"' && peek_char(lex) != '\0') {
        if (peek_char(lex) == '\\') advance(lex); /* skip escape */
        advance(lex);
    }
    size_t str_len = (size_t)(lex->src + lex->pos - str_start);
    if (peek_char(lex) == '"') advance(lex);
    else DIAG_ERR(&loc, "unterminated string literal");

    char *s = intern_string(lex, str_start, str_len);
    Token t = {TOK_STRING_LIT, loc, str_start, str_len, {0}};
    t.val.sval = s;
    return t;
}

/* ── Main scan function ───────────────────────────────────────────────────── */
static Token scan_one(Lexer *lex) {
    skip_whitespace_and_comments(lex);
    if (lex->pos >= lex->src_len) {
        SrcLoc loc = current_loc(lex);
        Token t = {TOK_EOF, loc, lex->src + lex->pos, 0, {0}};
        return t;
    }

    char c = peek_char(lex);

    if (isdigit((unsigned char)c)) return lex_number(lex);
    if (isalpha((unsigned char)c) || c == '_') return lex_ident(lex);
    if (c == '"') return lex_string(lex);

    SrcLoc loc = current_loc(lex);
    const char *start = lex->src + lex->pos;
    advance(lex);

#define TOK1(k) do { Token t = {k, loc, start, 1, {0}}; return t; } while(0)
#define TOK2(next, k2, k1) do { \
    if (match(lex, next)) { Token t = {k2, loc, start, 2, {0}}; return t; } \
    TOK1(k1); } while(0)

    switch (c) {
        case '(': TOK1(TOK_LPAREN);
        case ')': TOK1(TOK_RPAREN);
        case '{': TOK1(TOK_LBRACE);
        case '}': TOK1(TOK_RBRACE);
        case '[': TOK1(TOK_LBRACKET);
        case ']': TOK1(TOK_RBRACKET);
        case ';': TOK1(TOK_SEMICOLON);
        case ':': TOK1(TOK_COLON);
        case ',': TOK1(TOK_COMMA);
        case '@': TOK1(TOK_AT);
        case '#': TOK1(TOK_HASH);
        case '~': TOK1(TOK_TILDE);
        case '^': TOK1(TOK_CARET);
        case '%': TOK1(TOK_PERCENT);
        case '.': TOK2('.', TOK_DOTDOT, TOK_DOT);
        case '+': TOK2('=', TOK_PLUS_ASSIGN, TOK_PLUS);
        case '*': TOK2('=', TOK_STAR_ASSIGN, TOK_STAR);
        case '!': TOK2('=', TOK_NEQ, TOK_BANG);
        case '=': {
            if (match(lex, '=')) { Token t = {TOK_EQ, loc, start, 2, {0}}; return t; }
            if (match(lex, '>')) { Token t = {TOK_FAT_ARROW, loc, start, 2, {0}}; return t; }
            TOK1(TOK_ASSIGN);
        }
        case '<': {
            if (match(lex, '<')) { Token t = {TOK_LSHIFT, loc, start, 2, {0}}; return t; }
            TOK2('=', TOK_LEQ, TOK_LT);
        }
        case '>': {
            if (match(lex, '>')) { Token t = {TOK_RSHIFT, loc, start, 2, {0}}; return t; }
            TOK2('=', TOK_GEQ, TOK_GT);
        }
        case '-': {
            if (match(lex, '>')) { Token t = {TOK_ARROW, loc, start, 2, {0}}; return t; }
            TOK2('=', TOK_MINUS_ASSIGN, TOK_MINUS);
        }
        case '/': TOK2('=', TOK_SLASH_ASSIGN, TOK_SLASH);
        case '&': TOK2('&', TOK_AND, TOK_AMP);
        case '|': TOK2('|', TOK_OR,  TOK_PIPE);
        default: {
            DIAG_ERR(&loc, "unexpected character '%c' (0x%02x)", c, (unsigned char)c);
            lex->had_error = 1;
            Token t = {TOK_ERROR, loc, start, 1, {0}};
            return t;
        }
    }
#undef TOK1
#undef TOK2
}

/* ── Public API ───────────────────────────────────────────────────────────── */
void lexer_init(Lexer *lex, const char *src, size_t len, const char *filename) {
    memset(lex, 0, sizeof(*lex));
    lex->src      = src;
    lex->src_len  = len;
    lex->filename = filename;
    lex->line     = 1;
    lex->col      = 1;
}

Token lexer_next(Lexer *lex) { return scan_one(lex); }

Token lexer_peek(Lexer *lex) {
    size_t   saved_pos  = lex->pos;
    uint32_t saved_line = lex->line;
    uint32_t saved_col  = lex->col;
    Token t = scan_one(lex);
    lex->pos  = saved_pos;
    lex->line = saved_line;
    lex->col  = saved_col;
    return t;
}

void lexer_free(Lexer *lex) {
    for (size_t i = 0; i < lex->string_count; i++)
        free(lex->strings[i]);
    free(lex->strings);
}

/* ── Token name lookup ────────────────────────────────────────────────────── */
static const char *TOKEN_NAMES[TOK_COUNT] = {
    "INT_LIT","FLOAT_LIT","STRING_LIT","BOOL_LIT","IDENT",
    "fn","let","mut","return","if","else","while","for","in",
    "import","extern","struct","enum","as","null",
    "i8","i16","i32","i64","u8","u16","u32","u64","f32","f64","bool","str","void",
    "+","-","*","/","%","&","|","^","~","!","<",">","<<",">>",
    "&&","||","==","!=","<=",">=","=","+=","-=","*=","/=","->","=>",".","..",
    "(",")","{","}","[","]",";",":","," ,"@","#",
    "EOF","ERROR"
};

const char *token_kind_name(TokenKind k) {
    if (k < 0 || k >= TOK_COUNT) return "<bad>";
    return TOKEN_NAMES[k];
}

void token_print(const Token *t) {
    printf("[%s", token_kind_name(t->kind));
    if (t->kind == TOK_IDENT || t->kind == TOK_INT_LIT ||
        t->kind == TOK_FLOAT_LIT || t->kind == TOK_STRING_LIT) {
        printf(" `%.*s`", (int)t->len, t->start);
    }
    printf(" @%u:%u]", t->loc.line, t->loc.col);
}
