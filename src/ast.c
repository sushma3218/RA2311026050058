/*
 * Nova Language Compiler - Arena Allocator & AST Utilities
 */
#include "ast.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define ARENA_BLOCK_SIZE (1024 * 1024) /* 1 MiB per block */

/* ── Arena ────────────────────────────────────────────────────────────────── */
Arena *arena_new(void) {
    Arena *a = calloc(1, sizeof(Arena));
    if (!a) { perror("calloc"); exit(1); }
    return a;
}

static ArenaBlock *block_new(size_t min_size) {
    size_t cap = min_size > ARENA_BLOCK_SIZE ? min_size : ARENA_BLOCK_SIZE;
    ArenaBlock *b = malloc(sizeof(ArenaBlock));
    if (!b) { perror("malloc"); exit(1); }
    b->data = malloc(cap);
    if (!b->data) { perror("malloc"); exit(1); }
    b->used = 0;
    b->cap  = cap;
    b->next = NULL;
    return b;
}

void *arena_alloc(Arena *a, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;
    if (!a->head || a->head->used + size > a->head->cap) {
        ArenaBlock *b = block_new(size);
        b->next = a->head;
        a->head = b;
    }
    void *p = a->head->data + a->head->used;
    a->head->used += size;
    a->total_allocated += size;
    return p;
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = arena_alloc(a, len);
    memcpy(d, s, len);
    return d;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    free(a);
}

/* ── AST node constructors ────────────────────────────────────────────────── */
ASTNode *ast_node_new(Arena *a, NodeKind kind, SrcLoc loc) {
    ASTNode *n = arena_alloc(a, sizeof(ASTNode));
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->loc  = loc;
    return n;
}

Type *type_new(Arena *a, TypeKind kind) {
    Type *t = arena_alloc(a, sizeof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    return t;
}

Type *type_primitive(Arena *a, TypeKind k) {
    return type_new(a, k);
}

Type *type_ptr(Arena *a, Type *inner) {
    Type *t = type_new(a, TY_PTR);
    t->ptr_to = inner;
    return t;
}

Type *type_array(Arena *a, Type *elem, int64_t sz) {
    Type *t = type_new(a, TY_ARRAY);
    t->array.elem = elem;
    t->array.size = sz;
    return t;
}

Type *type_unresolved(Arena *a, const char *name) {
    Type *t = type_new(a, TY_UNRESOLVED);
    t->name = arena_strdup(a, name);
    return t;
}

int type_is_integer(const Type *t) {
    if (!t) return 0;
    return t->kind >= TY_I8 && t->kind <= TY_U64;
}

int type_is_float(const Type *t) {
    if (!t) return 0;
    return t->kind == TY_F32 || t->kind == TY_F64;
}

int type_is_numeric(const Type *t) {
    return type_is_integer(t) || type_is_float(t);
}

int type_equals(const Type *a, const Type *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
        case TY_PTR:
            return type_equals(a->ptr_to, b->ptr_to);
        case TY_ARRAY:
            return a->array.size == b->array.size &&
                   type_equals(a->array.elem, b->array.elem);
        case TY_STRUCT: case TY_ENUM: case TY_UNRESOLVED:
            return strcmp(a->name, b->name) == 0;
        default:
            return 1; /* same kind, no sub-structure */
    }
}

const char *type_name(const Type *t) {
    if (!t) return "<null>";
    static char buf[128];
    switch (t->kind) {
        case TY_VOID:  return "void";
        case TY_BOOL:  return "bool";
        case TY_I8:    return "i8";
        case TY_I16:   return "i16";
        case TY_I32:   return "i32";
        case TY_I64:   return "i64";
        case TY_U8:    return "u8";
        case TY_U16:   return "u16";
        case TY_U32:   return "u32";
        case TY_U64:   return "u64";
        case TY_F32:   return "f32";
        case TY_F64:   return "f64";
        case TY_STR:   return "str";
        case TY_PTR:
            snprintf(buf, sizeof(buf), "*%s", type_name(t->ptr_to));
            return buf;
        case TY_ARRAY:
            if (t->array.size < 0)
                snprintf(buf, sizeof(buf), "[]%s", type_name(t->array.elem));
            else
                snprintf(buf, sizeof(buf), "[%lld]%s",
                         (long long)t->array.size, type_name(t->array.elem));
            return buf;
        case TY_STRUCT: case TY_ENUM: case TY_UNRESOLVED:
            return t->name ? t->name : "<unnamed>";
        case TY_FN:    return "fn(...)";
        default:       return "<unknown-type>";
    }
}

/* ── AST pretty-printer ───────────────────────────────────────────────────── */
static void indent_print(int indent) {
    for (int i = 0; i < indent; i++) fputs("  ", stdout);
}

void ast_print(const ASTNode *n, int indent) {
    if (!n) { indent_print(indent); puts("<null>"); return; }
    indent_print(indent);
    switch (n->kind) {
        case NODE_MODULE:
            printf("Module(%s)\n", n->module.name);
            for (size_t i = 0; i < n->module.count; i++)
                ast_print(n->module.decls[i], indent + 1);
            break;
        case NODE_FN_DECL:
            printf("FnDecl(%s) -> %s\n", n->fn_decl.name,
                   type_name(n->fn_decl.ret_type));
            for (size_t i = 0; i < n->fn_decl.param_count; i++)
                ast_print(n->fn_decl.params[i], indent + 1);
            if (n->fn_decl.body) ast_print(n->fn_decl.body, indent + 1);
            break;
        case NODE_PARAM:
            printf("Param(%s : %s)\n", n->param.name, type_name(n->param.type));
            break;
        case NODE_LET:
            printf("Let(%s%s)\n", n->let.is_mut ? "mut " : "", n->let.name);
            if (n->let.init) ast_print(n->let.init, indent + 1);
            break;
        case NODE_BLOCK:
            printf("Block[%zu]\n", n->block.count);
            for (size_t i = 0; i < n->block.count; i++)
                ast_print(n->block.stmts[i], indent + 1);
            break;
        case NODE_RETURN:
            printf("Return\n");
            if (n->ret.value) ast_print(n->ret.value, indent + 1);
            break;
        case NODE_IF:
            printf("If\n");
            ast_print(n->if_stmt.cond,       indent + 1);
            ast_print(n->if_stmt.then_block,  indent + 1);
            if (n->if_stmt.else_block) ast_print(n->if_stmt.else_block, indent + 1);
            break;
        case NODE_WHILE:
            printf("While\n");
            ast_print(n->while_stmt.cond, indent + 1);
            ast_print(n->while_stmt.body, indent + 1);
            break;
        case NODE_FOR_RANGE:
            printf("ForRange(%s)\n", n->for_range.var);
            ast_print(n->for_range.from, indent + 1);
            ast_print(n->for_range.to,   indent + 1);
            ast_print(n->for_range.body, indent + 1);
            break;
        case NODE_BINOP:
            printf("BinOp(%s)\n", token_kind_name(n->binop.op));
            ast_print(n->binop.left,  indent + 1);
            ast_print(n->binop.right, indent + 1);
            break;
        case NODE_UNOP:
            printf("UnOp(%s)\n", token_kind_name(n->unop.op));
            ast_print(n->unop.operand, indent + 1);
            break;
        case NODE_CALL:
            printf("Call\n");
            ast_print(n->call.callee, indent + 1);
            for (size_t i = 0; i < n->call.arg_count; i++)
                ast_print(n->call.args[i], indent + 1);
            break;
        case NODE_IDENT_REF:
            printf("Ident(%s)\n", n->ident.name);
            break;
        case NODE_INT_LIT:
            printf("IntLit(%lld)\n", (long long)n->int_lit.ival);
            break;
        case NODE_FLOAT_LIT:
            printf("FloatLit(%g)\n", n->float_lit.fval);
            break;
        case NODE_STRING_LIT:
            printf("StringLit(\"%s\")\n", n->string_lit.sval);
            break;
        case NODE_BOOL_LIT:
            printf("BoolLit(%s)\n", n->bool_lit.bval ? "true" : "false");
            break;
        case NODE_ASSIGN:
            printf("Assign\n");
            ast_print(n->assign.target, indent + 1);
            ast_print(n->assign.value,  indent + 1);
            break;
        case NODE_EXTERN_FN:
            printf("ExternFn(%s)\n", n->extern_fn.name);
            break;
        case NODE_STRUCT_DECL:
            printf("StructDecl(%s)\n", n->struct_decl.name);
            break;
        default:
            printf("Node(%d)\n", n->kind);
    }
}
