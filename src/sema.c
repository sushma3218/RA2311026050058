/*
 * Nova Language Compiler - Semantic Analysis Implementation
 * Type checking, name resolution, and scope management.
 */
#include "sema.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Scope / symbol table ─────────────────────────────────────────────────── */
static uint32_t hash_name(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static Scope *scope_new(Arena *a, Scope *parent) {
    Scope *sc = arena_alloc(a, sizeof(Scope));
    memset(sc, 0, sizeof(*sc));
    sc->parent = parent;
    return sc;
}

Symbol *scope_lookup(Scope *sc, const char *name) {
    for (; sc; sc = sc->parent) {
        uint32_t idx = hash_name(name) % SCOPE_HASH_SIZE;
        for (Symbol *sym = sc->buckets[idx]; sym; sym = sym->next)
            if (strcmp(sym->name, name) == 0) return sym;
    }
    return NULL;
}

static Symbol *scope_lookup_local(Scope *sc, const char *name) {
    uint32_t idx = hash_name(name) % SCOPE_HASH_SIZE;
    for (Symbol *sym = sc->buckets[idx]; sym; sym = sym->next)
        if (strcmp(sym->name, name) == 0) return sym;
    return NULL;
}

static Symbol *scope_define(Sema *s, const char *name, SymbolKind kind,
                             Type *type, ASTNode *decl, int is_mut) {
    if (scope_lookup_local(s->current_scope, name)) {
        SrcLoc loc = decl ? decl->loc : (SrcLoc){NULL,0,0,0};
        DIAG_ERR(&loc, "redefinition of '%s'", name);
        s->had_error = 1; s->error_count++;
        return NULL;
    }
    Symbol *sym  = arena_alloc(s->arena, sizeof(Symbol));
    sym->kind    = kind;
    sym->name    = name;
    sym->type    = type;
    sym->decl    = decl;
    sym->is_mut  = is_mut;
    uint32_t idx = hash_name(name) % SCOPE_HASH_SIZE;
    sym->next    = s->current_scope->buckets[idx];
    s->current_scope->buckets[idx] = sym;
    return sym;
}

static void push_scope(Sema *s) {
    s->current_scope = scope_new(s->arena, s->current_scope);
}
static void pop_scope(Sema *s) {
    if (s->current_scope->parent)
        s->current_scope = s->current_scope->parent;
}

/* ── Type resolution ──────────────────────────────────────────────────────── */
static Type *resolve_type(Sema *s, Type *t) {
    if (!t) return NULL;
    if (t->kind == TY_UNRESOLVED) {
        Symbol *sym = scope_lookup(s->current_scope, t->name);
        if (!sym) {
            DIAG_ERR(NULL, "unknown type '%s'", t->name);
            s->had_error = 1; s->error_count++;
            return type_primitive(s->arena, TY_ERROR);
        }
        if (sym->kind == SYM_STRUCT) {
            Type *rt = type_new(s->arena, TY_STRUCT);
            rt->name = arena_strdup(s->arena, t->name);
            return rt;
        }
        if (sym->kind == SYM_ENUM) {
            Type *rt = type_new(s->arena, TY_ENUM);
            rt->name = arena_strdup(s->arena, t->name);
            return rt;
        }
        DIAG_ERR(NULL, "'%s' is not a type", t->name);
        s->had_error = 1; s->error_count++;
        return type_primitive(s->arena, TY_ERROR);
    }
    if (t->kind == TY_PTR)   { t->ptr_to = resolve_type(s, t->ptr_to); }
    if (t->kind == TY_ARRAY) { t->array.elem = resolve_type(s, t->array.elem); }
    return t;
}

/* ── Forward declarations ─────────────────────────────────────────────────── */
static Type *sema_expr(Sema *s, ASTNode *n);
static void  sema_stmt(Sema *s, ASTNode *n);
static void  sema_block(Sema *s, ASTNode *n);

/* ── Expression type-checker ──────────────────────────────────────────────── */
static Type *sema_expr(Sema *s, ASTNode *n) {
    if (!n) return type_primitive(s->arena, TY_VOID);
    switch (n->kind) {

    case NODE_INT_LIT:
        n->type = type_primitive(s->arena, TY_I64);
        return n->type;

    case NODE_FLOAT_LIT:
        n->type = type_primitive(s->arena, TY_F64);
        return n->type;

    case NODE_BOOL_LIT:
        n->type = type_primitive(s->arena, TY_BOOL);
        return n->type;

    case NODE_STRING_LIT:
        n->type = type_primitive(s->arena, TY_STR);
        return n->type;

    case NODE_NULL_LIT:
        n->type = type_ptr(s->arena, type_primitive(s->arena, TY_VOID));
        return n->type;

    case NODE_IDENT_REF: {
        Symbol *sym = scope_lookup(s->current_scope, n->ident.name);
        if (!sym) {
            DIAG_ERR(&n->loc, "undefined symbol '%s'", n->ident.name);
            s->had_error = 1; s->error_count++;
            n->type = type_primitive(s->arena, TY_ERROR);
            return n->type;
        }
        n->ident.resolved = sym->decl;
        n->type = sym->type;
        return n->type;
    }

    case NODE_BINOP: {
        Type *lt = sema_expr(s, n->binop.left);
        Type *rt = sema_expr(s, n->binop.right);
        TokenKind op = n->binop.op;
        /* Comparison operators always produce bool */
        if (op == TOK_EQ || op == TOK_NEQ || op == TOK_LT || op == TOK_GT ||
            op == TOK_LEQ || op == TOK_GEQ || op == TOK_AND || op == TOK_OR) {
            n->type = type_primitive(s->arena, TY_BOOL);
            return n->type;
        }
        /* Arithmetic: use left type (coercion is handled in codegen) */
        if (!type_equals(lt, rt)) {
            if (type_is_numeric(lt) && type_is_numeric(rt)) {
                /* implicit numeric promotion - use wider type */
                n->type = type_is_float(lt) || type_is_float(rt)
                          ? type_primitive(s->arena, TY_F64)
                          : type_primitive(s->arena, TY_I64);
            } else {
                DIAG_ERR(&n->loc, "type mismatch in binary expression: '%s' vs '%s'",
                         type_name(lt), type_name(rt));
                s->had_error = 1; s->error_count++;
                n->type = lt;
            }
        } else {
            n->type = lt;
        }
        return n->type;
    }

    case NODE_UNOP: {
        Type *t = sema_expr(s, n->unop.operand);
        if (n->unop.op == TOK_BANG) n->type = type_primitive(s->arena, TY_BOOL);
        else                         n->type = t;
        return n->type;
    }

    case NODE_CALL: {
        Type *callee_t = sema_expr(s, n->call.callee);
        /* Resolve function type */
        const char *fn_name = (n->call.callee->kind == NODE_IDENT_REF)
                              ? n->call.callee->ident.name : "<expr>";
        Symbol *sym = NULL;
        if (n->call.callee->kind == NODE_IDENT_REF)
            sym = scope_lookup(s->current_scope, fn_name);

        if (sym && (sym->kind == SYM_FN || sym->kind == SYM_EXTERN_FN)) {
            /* Check arg count for non-variadic */
            ASTNode *decl = sym->decl;
            if (decl) {
                size_t expected_params = (decl->kind == NODE_FN_DECL)
                                         ? decl->fn_decl.param_count
                                         : decl->extern_fn.param_count;
                int variadic = (decl->kind == NODE_FN_DECL) ? 0 : decl->extern_fn.variadic;
                if (!variadic && n->call.arg_count != expected_params) {
                    DIAG_ERR(&n->loc, "function '%s' expects %zu args, got %zu",
                             fn_name, expected_params, n->call.arg_count);
                    s->had_error = 1; s->error_count++;
                }
            }
            n->type = (sym->kind == SYM_FN && sym->decl)
                      ? sym->decl->fn_decl.ret_type
                      : (callee_t && callee_t->kind == TY_FN ? callee_t->fn.ret : type_primitive(s->arena, TY_VOID));
            if (!n->type) n->type = type_primitive(s->arena, TY_VOID);
        } else {
            n->type = type_primitive(s->arena, TY_VOID);
        }
        /* Type-check arguments */
        for (size_t i = 0; i < n->call.arg_count; i++)
            sema_expr(s, n->call.args[i]);
        return n->type;
    }

    case NODE_INDEX: {
        Type *arr_t = sema_expr(s, n->index.array);
        sema_expr(s, n->index.index);
        if (arr_t && arr_t->kind == TY_ARRAY)
            n->type = arr_t->array.elem;
        else if (arr_t && arr_t->kind == TY_PTR)
            n->type = arr_t->ptr_to;
        else
            n->type = type_primitive(s->arena, TY_ERROR);
        return n->type;
    }

    case NODE_FIELD: {
        Type *obj_t = sema_expr(s, n->field.object);
        /* Look up struct field */
        if (obj_t && obj_t->kind == TY_STRUCT) {
            Symbol *ssym = scope_lookup(s->current_scope, obj_t->name);
            if (ssym && ssym->decl && ssym->decl->kind == NODE_STRUCT_DECL) {
                ASTNode *sd = ssym->decl;
                for (size_t i = 0; i < sd->struct_decl.field_count; i++) {
                    if (strcmp(sd->struct_decl.field_names[i], n->field.field) == 0) {
                        n->type = sd->struct_decl.field_types[i];
                        return n->type;
                    }
                }
                DIAG_ERR(&n->loc, "struct '%s' has no field '%s'",
                         obj_t->name, n->field.field);
                s->had_error = 1; s->error_count++;
            }
        }
        n->type = type_primitive(s->arena, TY_ERROR);
        return n->type;
    }

    case NODE_CAST: {
        sema_expr(s, n->cast.expr);
        n->cast.to = resolve_type(s, n->cast.to);
        n->type    = n->cast.to;
        return n->type;
    }

    case NODE_ASSIGN: {
        Type *lt = sema_expr(s, n->assign.target);
        Type *rt = sema_expr(s, n->assign.value);
        if (!type_equals(lt, rt) && !type_is_numeric(lt)) {
            DIAG_ERR(&n->loc, "assignment type mismatch: '%s' = '%s'",
                     type_name(lt), type_name(rt));
            s->had_error = 1; s->error_count++;
        }
        n->type = lt;
        return n->type;
    }

    case NODE_ARRAY_LIT: {
        Type *elem_t = NULL;
        for (size_t i = 0; i < n->array_lit.count; i++) {
            Type *t = sema_expr(s, n->array_lit.elems[i]);
            if (!elem_t) elem_t = t;
        }
        if (!elem_t) elem_t = type_primitive(s->arena, TY_VOID);
        n->type = type_array(s->arena, elem_t, (int64_t)n->array_lit.count);
        return n->type;
    }

    default:
        n->type = type_primitive(s->arena, TY_VOID);
        return n->type;
    }
}

/* ── Statement type-checker ───────────────────────────────────────────────── */
static void sema_stmt(Sema *s, ASTNode *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_LET: {
        Type *init_t = n->let.init ? sema_expr(s, n->let.init) : NULL;
        Type *ty = n->let.annot ? resolve_type(s, n->let.annot) : init_t;
        if (!ty) ty = type_primitive(s->arena, TY_VOID);
        if (n->let.annot && init_t && !type_equals(ty, init_t)) {
            if (!type_is_numeric(ty) || !type_is_numeric(init_t)) {
                DIAG_WARN(&n->loc, "type annotation '%s' differs from initializer '%s'",
                          type_name(ty), type_name(init_t));
            }
        }
        n->type = ty;
        scope_define(s, n->let.name, SYM_VAR, ty, n, n->let.is_mut);
        break;
    }
    case NODE_RETURN: {
        Type *ret_t = n->ret.value ? sema_expr(s, n->ret.value)
                                   : type_primitive(s->arena, TY_VOID);
        if (s->current_fn_ret && !type_equals(ret_t, s->current_fn_ret)) {
            if (!type_is_numeric(ret_t) || !type_is_numeric(s->current_fn_ret)) {
                DIAG_ERR(&n->loc, "return type mismatch: expected '%s', got '%s'",
                         type_name(s->current_fn_ret), type_name(ret_t));
                s->had_error = 1; s->error_count++;
            }
        }
        break;
    }
    case NODE_IF:
        sema_expr(s, n->if_stmt.cond);
        sema_block(s, n->if_stmt.then_block);
        if (n->if_stmt.else_block) {
            if (n->if_stmt.else_block->kind == NODE_IF)
                sema_stmt(s, n->if_stmt.else_block);
            else
                sema_block(s, n->if_stmt.else_block);
        }
        break;
    case NODE_WHILE:
        sema_expr(s, n->while_stmt.cond);
        sema_block(s, n->while_stmt.body);
        break;
    case NODE_FOR_RANGE: {
        sema_expr(s, n->for_range.from);
        sema_expr(s, n->for_range.to);
        push_scope(s);
        Type *idx_t = type_primitive(s->arena, TY_I64);
        ASTNode *dummy = ast_node_new(s->arena, NODE_LET, n->loc);
        scope_define(s, n->for_range.var, SYM_VAR, idx_t, dummy, 0);
        sema_block(s, n->for_range.body);
        pop_scope(s);
        break;
    }
    case NODE_BLOCK:
        sema_block(s, n);
        break;
    case NODE_EXPR_STMT:
        sema_expr(s, n->expr_stmt.expr);
        break;
    default:
        sema_expr(s, n);
        break;
    }
}

static void sema_block(Sema *s, ASTNode *n) {
    if (!n || n->kind != NODE_BLOCK) { sema_stmt(s, n); return; }
    push_scope(s);
    for (size_t i = 0; i < n->block.count; i++)
        sema_stmt(s, n->block.stmts[i]);
    pop_scope(s);
}

/* ── First pass: register all top-level names ─────────────────────────────── */
static void sema_first_pass(Sema *s, ASTNode *mod) {
    for (size_t i = 0; i < mod->module.count; i++) {
        ASTNode *d = mod->module.decls[i];
        switch (d->kind) {
        case NODE_FN_DECL: {
            /* Build fn type */
            Type *ft = type_new(s->arena, TY_FN);
            ft->fn.params      = arena_alloc(s->arena, d->fn_decl.param_count * sizeof(Type*));
            ft->fn.param_count = d->fn_decl.param_count;
            for (size_t j = 0; j < d->fn_decl.param_count; j++)
                ft->fn.params[j] = d->fn_decl.params[j]->param.type;
            ft->fn.ret = d->fn_decl.ret_type ? d->fn_decl.ret_type
                                              : type_primitive(s->arena, TY_VOID);
            scope_define(s, d->fn_decl.name,
                         d->fn_decl.is_extern ? SYM_EXTERN_FN : SYM_FN,
                         ft, d, 0);
            break;
        }
        case NODE_STRUCT_DECL: {
            Type *st = type_new(s->arena, TY_STRUCT);
            st->name = d->struct_decl.name;
            scope_define(s, d->struct_decl.name, SYM_STRUCT, st, d, 0);
            break;
        }
        case NODE_ENUM_DECL: {
            Type *et = type_new(s->arena, TY_ENUM);
            et->name = d->enum_decl.name;
            scope_define(s, d->enum_decl.name, SYM_ENUM, et, d, 0);
            /* Register each variant as a constant i64 */
            for (size_t j = 0; j < d->enum_decl.variant_count; j++) {
                char vname[256];
                snprintf(vname, sizeof(vname), "%s_%s",
                         d->enum_decl.name, d->enum_decl.variants[j]);
                scope_define(s, arena_strdup(s->arena, vname), SYM_VAR,
                             type_primitive(s->arena, TY_I64), NULL, 0);
            }
            break;
        }
        default: break;
        }
    }
}

/* ── Second pass: analyse function bodies ─────────────────────────────────── */
static void sema_second_pass(Sema *s, ASTNode *mod) {
    for (size_t i = 0; i < mod->module.count; i++) {
        ASTNode *d = mod->module.decls[i];
        if (d->kind == NODE_FN_DECL && d->fn_decl.body) {
            push_scope(s);
            Type *saved_ret  = s->current_fn_ret;
            s->current_fn_ret = d->fn_decl.ret_type
                                 ? d->fn_decl.ret_type
                                 : type_primitive(s->arena, TY_VOID);
            /* Register params in function scope */
            for (size_t j = 0; j < d->fn_decl.param_count; j++) {
                ASTNode *pr = d->fn_decl.params[j];
                scope_define(s, pr->param.name, SYM_PARAM,
                             resolve_type(s, pr->param.type), pr, pr->param.is_mut);
            }
            sema_block(s, d->fn_decl.body);
            s->current_fn_ret = saved_ret;
            pop_scope(s);
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */
void sema_init(Sema *s, Arena *arena) {
    memset(s, 0, sizeof(*s));
    s->arena         = arena;
    s->current_scope = scope_new(arena, NULL);
}

int sema_analyse(Sema *s, ASTNode *module) {
    sema_first_pass(s, module);
    sema_second_pass(s, module);
    return !s->had_error;
}

void sema_free(Sema *s) {
    (void)s; /* arena owns everything */
}
