#ifndef NOVA_LEXER_H
#define NOVA_LEXER_H

/*
 * Nova Language Compiler - Lexer
 * Tokenizes Nova DSL source code into a stream of tokens.
 */

#include <stddef.h>
#include <stdint.h>

/* ── Token Types ──────────────────────────────────────────────────────────── */
typedef enum {
    /* Literals */
    TOK_INT_LIT,      /* 42, 0xFF, 0b1010 */
    TOK_FLOAT_LIT,    /* 3.14, 1.0e-3      */
    TOK_STRING_LIT,   /* "hello"            */
    TOK_BOOL_LIT,     /* true / false       */

    /* Identifiers & keywords */
    TOK_IDENT,

    /* Keywords */
    TOK_FN,           /* fn     */
    TOK_LET,          /* let    */
    TOK_MUT,          /* mut    */
    TOK_RETURN,       /* return */
    TOK_IF,           /* if     */
    TOK_ELSE,         /* else   */
    TOK_WHILE,        /* while  */
    TOK_FOR,          /* for    */
    TOK_IN,           /* in     */
    TOK_IMPORT,       /* import */
    TOK_EXTERN,       /* extern */
    TOK_STRUCT,       /* struct */
    TOK_ENUM,         /* enum   */
    TOK_AS,           /* as     */
    TOK_NULL,         /* null   */

    /* Primitive types */
    TOK_TYPE_I8,  TOK_TYPE_I16, TOK_TYPE_I32, TOK_TYPE_I64,
    TOK_TYPE_U8,  TOK_TYPE_U16, TOK_TYPE_U32, TOK_TYPE_U64,
    TOK_TYPE_F32, TOK_TYPE_F64,
    TOK_TYPE_BOOL, TOK_TYPE_STR, TOK_TYPE_VOID,

    /* Operators */
    TOK_PLUS,         /* +  */
    TOK_MINUS,        /* -  */
    TOK_STAR,         /* *  */
    TOK_SLASH,        /* /  */
    TOK_PERCENT,      /* %  */
    TOK_AMP,          /* &  */
    TOK_PIPE,         /* |  */
    TOK_CARET,        /* ^  */
    TOK_TILDE,        /* ~  */
    TOK_BANG,         /* !  */
    TOK_LT,           /* <  */
    TOK_GT,           /* >  */
    TOK_LSHIFT,       /* << */
    TOK_RSHIFT,       /* >> */
    TOK_AND,          /* && */
    TOK_OR,           /* || */
    TOK_EQ,           /* == */
    TOK_NEQ,          /* != */
    TOK_LEQ,          /* <= */
    TOK_GEQ,          /* >= */
    TOK_ASSIGN,       /* =  */
    TOK_PLUS_ASSIGN,  /* += */
    TOK_MINUS_ASSIGN, /* -= */
    TOK_STAR_ASSIGN,  /* *= */
    TOK_SLASH_ASSIGN, /* /= */
    TOK_ARROW,        /* -> */
    TOK_FAT_ARROW,    /* => */
    TOK_DOT,          /* .  */
    TOK_DOTDOT,       /* .. */

    /* Delimiters */
    TOK_LPAREN,   /* ( */
    TOK_RPAREN,   /* ) */
    TOK_LBRACE,   /* { */
    TOK_RBRACE,   /* } */
    TOK_LBRACKET, /* [ */
    TOK_RBRACKET, /* ] */
    TOK_SEMICOLON,/* ; */
    TOK_COLON,    /* : */
    TOK_COMMA,    /* , */
    TOK_AT,       /* @ */
    TOK_HASH,     /* # */

    /* Special */
    TOK_EOF,
    TOK_ERROR,

    TOK_COUNT
} TokenKind;

/* ── Source Location ──────────────────────────────────────────────────────── */
typedef struct {
    const char *filename;
    uint32_t    line;
    uint32_t    col;
    uint32_t    offset; /* byte offset from start of file */
} SrcLoc;

/* ── Token ────────────────────────────────────────────────────────────────── */
typedef struct {
    TokenKind kind;
    SrcLoc    loc;
    const char *start; /* pointer into source buffer – NOT NUL terminated */
    size_t     len;

    /* Decoded values for literals */
    union {
        int64_t  ival;
        double   fval;
        uint8_t  bval; /* 0 or 1 for bool */
        char    *sval; /* decoded string */
    } val;
} Token;

/* ── Lexer state ──────────────────────────────────────────────────────────── */
typedef struct {
    const char *src;      /* entire source text            */
    size_t      src_len;
    size_t      pos;      /* current read position (bytes) */
    uint32_t    line;
    uint32_t    col;
    const char *filename;
    int         had_error;

    /* Intern table for string literals (heap-allocated NUL-terminated) */
    char      **strings;
    size_t      string_count;
    size_t      string_cap;
} Lexer;

/* ── Public API ───────────────────────────────────────────────────────────── */
void  lexer_init(Lexer *lex, const char *src, size_t len, const char *filename);
Token lexer_next(Lexer *lex);
Token lexer_peek(Lexer *lex);
void  lexer_free(Lexer *lex);

const char *token_kind_name(TokenKind k);
void        token_print(const Token *t);

#endif /* NOVA_LEXER_H */
