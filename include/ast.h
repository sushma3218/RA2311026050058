#ifndef NOVA_AST_H
#define NOVA_AST_H

/*
 * Nova Language Compiler - Abstract Syntax Tree
 * Defines every node type used in the AST.
 */

#include "lexer.h"
#include <stddef.h>
#include <stdint.h>

/* ── Type system ──────────────────────────────────────────────────────────── */
typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_I8, TY_I16, TY_I32, TY_I64,
    TY_U8, TY_U16, TY_U32, TY_U64,
    TY_F32, TY_F64,
    TY_STR,
    TY_PTR,    /* pointer to inner type */
    TY_ARRAY,  /* [N]T or []T (unsized) */
    TY_STRUCT,
    TY_ENUM,
    TY_FN,     /* function pointer */
    TY_UNRESOLVED, /* name not yet resolved */
    TY_ERROR,
} TypeKind;

typedef struct Type Type;
struct Type {
    TypeKind kind;
    union {
        /* TY_PTR */
        Type *ptr_to;
        /* TY_ARRAY */
        struct { Type *elem; int64_t size; /* -1 = slice */ } array;
        /* TY_STRUCT / TY_ENUM / TY_UNRESOLVED */
        char *name;
        /* TY_FN */
        struct {
            Type  **params;
            size_t  param_count;
            Type   *ret;
        } fn;
    };
};

/* ── Node kinds ───────────────────────────────────────────────────────────── */
typedef enum {
    /* Declarations */
    NODE_MODULE,
    NODE_FN_DECL,
    NODE_PARAM,
    NODE_LET,
    NODE_STRUCT_DECL,
    NODE_ENUM_DECL,
    NODE_IMPORT,
    NODE_EXTERN_FN,

    /* Statements */
    NODE_BLOCK,
    NODE_RETURN,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR_RANGE,
    NODE_EXPR_STMT,
    NODE_ASSIGN,
    NODE_COMPOUND_ASSIGN,  /* +=, -=, *=, /= */

    /* Expressions */
    NODE_BINOP,
    NODE_UNOP,
    NODE_CALL,
    NODE_INDEX,
    NODE_FIELD,
    NODE_CAST,
    NODE_IDENT_REF,
    NODE_INT_LIT,
    NODE_FLOAT_LIT,
    NODE_STRING_LIT,
    NODE_BOOL_LIT,
    NODE_NULL_LIT,
    NODE_ARRAY_LIT,
    NODE_STRUCT_INIT,
    NODE_STRUCT_FIELD_INIT,

    NODE_COUNT
} NodeKind;

/* ── Forward declaration ──────────────────────────────────────────────────── */
typedef struct ASTNode ASTNode;

/* ── AST Node ─────────────────────────────────────────────────────────────── */
struct ASTNode {
    NodeKind kind;
    SrcLoc   loc;
    Type    *type;   /* filled in by semantic analysis */

    union {
        /* NODE_MODULE */
        struct {
            ASTNode **decls;
            size_t    count;
            char     *name;
        } module;

        /* NODE_FN_DECL */
        struct {
            char     *name;
            ASTNode **params;
            size_t    param_count;
            Type     *ret_type;
            ASTNode  *body;       /* NULL for extern */
            int       is_pub;
            int       is_extern;
        } fn_decl;

        /* NODE_PARAM */
        struct {
            char *name;
            Type *type;
            int   is_mut;
        } param;

        /* NODE_LET */
        struct {
            char    *name;
            Type    *annot;   /* optional type annotation */
            ASTNode *init;
            int      is_mut;
        } let;

        /* NODE_STRUCT_DECL */
        struct {
            char     *name;
            char    **field_names;
            Type    **field_types;
            size_t    field_count;
        } struct_decl;

        /* NODE_ENUM_DECL */
        struct {
            char   *name;
            char  **variants;
            size_t  variant_count;
        } enum_decl;

        /* NODE_IMPORT */
        struct {
            char *path;
            char *alias;
        } import;

        /* NODE_EXTERN_FN */
        struct {
            char  *name;
            Type **param_types;
            size_t param_count;
            Type  *ret_type;
            int    variadic;
        } extern_fn;

        /* NODE_BLOCK */
        struct {
            ASTNode **stmts;
            size_t    count;
        } block;

        /* NODE_RETURN */
        struct { ASTNode *value; } ret;

        /* NODE_IF */
        struct {
            ASTNode *cond;
            ASTNode *then_block;
            ASTNode *else_block; /* may be NULL or another NODE_IF */
        } if_stmt;

        /* NODE_WHILE */
        struct {
            ASTNode *cond;
            ASTNode *body;
        } while_stmt;

        /* NODE_FOR_RANGE */
        struct {
            char    *var;
            ASTNode *from;
            ASTNode *to;
            ASTNode *body;
        } for_range;

        /* NODE_EXPR_STMT */
        struct { ASTNode *expr; } expr_stmt;

        /* NODE_ASSIGN / NODE_COMPOUND_ASSIGN */
        struct {
            ASTNode  *target;
            ASTNode  *value;
            TokenKind op; /* TOK_ASSIGN for plain, else +=,-=,*=,/= */
        } assign;

        /* NODE_BINOP */
        struct {
            TokenKind op;
            ASTNode  *left;
            ASTNode  *right;
        } binop;

        /* NODE_UNOP */
        struct {
            TokenKind op;
            ASTNode  *operand;
            int       postfix; /* 0 = prefix */
        } unop;

        /* NODE_CALL */
        struct {
            ASTNode  *callee;
            ASTNode **args;
            size_t    arg_count;
        } call;

        /* NODE_INDEX */
        struct {
            ASTNode *array;
            ASTNode *index;
        } index;

        /* NODE_FIELD */
        struct {
            ASTNode *object;
            char    *field;
        } field;

        /* NODE_CAST */
        struct {
            ASTNode *expr;
            Type    *to;
        } cast;

        /* NODE_IDENT_REF */
        struct {
            char    *name;
            ASTNode *resolved; /* set by sema */
        } ident;

        /* Literals */
        struct { int64_t  ival; } int_lit;
        struct { double   fval; } float_lit;
        struct { char    *sval; } string_lit;
        struct { uint8_t  bval; } bool_lit;

        /* NODE_ARRAY_LIT */
        struct {
            ASTNode **elems;
            size_t    count;
        } array_lit;

        /* NODE_STRUCT_INIT */
        struct {
            char     *struct_name;
            ASTNode **fields;  /* NODE_STRUCT_FIELD_INIT nodes */
            size_t    count;
        } struct_init;

        /* NODE_STRUCT_FIELD_INIT */
        struct {
            char    *name;
            ASTNode *value;
        } field_init;
    };
};

/* ── Arena allocator used by the AST ─────────────────────────────────────── */
typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock {
    uint8_t     *data;
    size_t       used;
    size_t       cap;
    ArenaBlock  *next;
};

typedef struct {
    ArenaBlock *head;
    size_t      total_allocated;
} Arena;

Arena  *arena_new(void);
void   *arena_alloc(Arena *a, size_t size);
char   *arena_strdup(Arena *a, const char *s);
void    arena_free(Arena *a);

ASTNode *ast_node_new(Arena *a, NodeKind kind, SrcLoc loc);
Type    *type_new(Arena *a, TypeKind kind);
Type    *type_primitive(Arena *a, TypeKind k);
Type    *type_ptr(Arena *a, Type *inner);
Type    *type_array(Arena *a, Type *elem, int64_t sz);
Type    *type_unresolved(Arena *a, const char *name);
int      type_equals(const Type *a, const Type *b);
int      type_is_integer(const Type *t);
int      type_is_float(const Type *t);
int      type_is_numeric(const Type *t);
const char *type_name(const Type *t);

void ast_print(const ASTNode *node, int indent);

#endif /* NOVA_AST_H */
