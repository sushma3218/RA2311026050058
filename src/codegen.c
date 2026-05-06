/*
 * Nova Language Compiler - LLVM IR Code Generator
 * Emits textual LLVM IR (.ll) from the annotated AST.
 */
#define _POSIX_C_SOURCE 200112L
#include "codegen.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Utilities ────────────────────────────────────────────────────────────── */
static int new_reg(CodeGen *cg)   { return cg->reg_counter++;   }
static int new_label(CodeGen *cg) { return cg->label_counter++; }

static const char *llvm_type(const Type *t) {
    if (!t) return "void";
    static char buf[256];
    switch (t->kind) {
        case TY_VOID:  return "void";
        case TY_BOOL:  return "i1";
        case TY_I8:    return "i8";
        case TY_I16:   return "i16";
        case TY_I32:   return "i32";
        case TY_I64:   return "i64";
        case TY_U8:    return "i8";
        case TY_U16:   return "i16";
        case TY_U32:   return "i32";
        case TY_U64:   return "i64";
        case TY_F32:   return "float";
        case TY_F64:   return "double";
        case TY_STR:   return "i8*";
        case TY_PTR:
            snprintf(buf, sizeof(buf), "%s*", llvm_type(t->ptr_to));
            return buf;
        case TY_ARRAY:
            if (t->array.size >= 0)
                snprintf(buf, sizeof(buf), "[%lld x %s]",
                         (long long)t->array.size, llvm_type(t->array.elem));
            else
                snprintf(buf, sizeof(buf), "%s*", llvm_type(t->array.elem));
            return buf;
        case TY_STRUCT:
            snprintf(buf, sizeof(buf), "%%struct.%s", t->name ? t->name : "anon");
            return buf;
        default: return "i64";
    }
}

static int type_is_void(const Type *t) { return !t || t->kind == TY_VOID; }

static void emit_label(CodeGen *cg, int id) {
    fprintf(cg->out, "bb%d:\n", id);
}

/* ── Variable table ───────────────────────────────────────────────────────── */
#define VAR_TABLE_SIZE 512

typedef struct VarEntry VarEntry;
struct VarEntry {
    char      *name;
    char       alloca_reg[64];
    Type      *type;
    VarEntry  *next;
};

typedef struct { VarEntry *buckets[VAR_TABLE_SIZE]; } VarTable;

static uint32_t var_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h % VAR_TABLE_SIZE;
}

static void var_set(CodeGen *cg, VarTable *vt, const char *name,
                    const char *reg, Type *type) {
    uint32_t  idx = var_hash(name);
    VarEntry *e   = arena_alloc(cg->arena, sizeof(VarEntry));
    e->name       = arena_strdup(cg->arena, name);
    e->type       = type;
    strncpy(e->alloca_reg, reg, sizeof(e->alloca_reg)-1);
    e->alloca_reg[sizeof(e->alloca_reg)-1] = '\0';
    e->next            = vt->buckets[idx];
    vt->buckets[idx]   = e;
}

static VarEntry *var_get(VarTable *vt, const char *name) {
    uint32_t idx = var_hash(name);
    for (VarEntry *e = vt->buckets[idx]; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

/* ── String literal table ─────────────────────────────────────────────────── */
#define MAX_STR_GLOBALS 256
typedef struct { char *value; size_t len; int idx; } StrGlobal;
static StrGlobal g_strings[MAX_STR_GLOBALS];
static int       g_str_count = 0;

static int register_string(const char *val) {
    for (int i = 0; i < g_str_count; i++)
        if (strcmp(g_strings[i].value, val) == 0) return i;
    int idx = g_str_count++;
    /* safe copy without strdup portability issues */
    size_t len = strlen(val);
    g_strings[idx].value = malloc(len + 1);
    if (!g_strings[idx].value) { perror("malloc"); exit(1); }
    memcpy(g_strings[idx].value, val, len + 1);
    g_strings[idx].len = len;
    g_strings[idx].idx = idx;
    return idx;
}

/* ── SSA register helpers ─────────────────────────────────────────────────── */
static char *new_reg_str(CodeGen *cg) {
    char *buf = arena_alloc(cg->arena, 16);
    snprintf(buf, 16, "%%%d", new_reg(cg));
    return buf;
}

/* ── Forward declarations ─────────────────────────────────────────────────── */
static char *gen_expr(CodeGen *cg, ASTNode *n, VarTable *vt);
static void  gen_stmt(CodeGen *cg, ASTNode *n, VarTable *vt);
static void  gen_block(CodeGen *cg, ASTNode *n, VarTable *vt);

/* ── Binary operator codegen ──────────────────────────────────────────────── */
static char *gen_binop(CodeGen *cg, ASTNode *n, VarTable *vt) {
    char *lv = gen_expr(cg, n->binop.left,  vt);
    char *rv = gen_expr(cg, n->binop.right, vt);
    Type *ty = n->binop.left->type;
    int   is_float    = type_is_float(ty);
    int   is_unsigned = ty && (ty->kind==TY_U8||ty->kind==TY_U16||
                               ty->kind==TY_U32||ty->kind==TY_U64);
    const char *llt = llvm_type(ty ? ty : n->type);
    char *res = new_reg_str(cg);

    switch (n->binop.op) {
    case TOK_PLUS:    fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fadd":"add",  llt,lv,rv); break;
    case TOK_MINUS:   fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fsub":"sub",  llt,lv,rv); break;
    case TOK_STAR:    fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fmul":"mul",  llt,lv,rv); break;
    case TOK_SLASH:   fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fdiv":(is_unsigned?"udiv":"sdiv"),llt,lv,rv); break;
    case TOK_PERCENT: fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_unsigned?"urem":"srem",llt,lv,rv); break;
    case TOK_AMP:     fprintf(cg->out,"  %s = and %s %s, %s\n",res,llt,lv,rv); break;
    case TOK_PIPE:    fprintf(cg->out,"  %s = or  %s %s, %s\n",res,llt,lv,rv); break;
    case TOK_CARET:   fprintf(cg->out,"  %s = xor %s %s, %s\n",res,llt,lv,rv); break;
    case TOK_LSHIFT:  fprintf(cg->out,"  %s = shl %s %s, %s\n",res,llt,lv,rv); break;
    case TOK_RSHIFT:  fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_unsigned?"lshr":"ashr",llt,lv,rv); break;
    case TOK_EQ:      fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fcmp oeq":"icmp eq", llt,lv,rv); break;
    case TOK_NEQ:     fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fcmp one":"icmp ne", llt,lv,rv); break;
    case TOK_LT:      fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fcmp olt":(is_unsigned?"icmp ult":"icmp slt"),llt,lv,rv); break;
    case TOK_GT:      fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fcmp ogt":(is_unsigned?"icmp ugt":"icmp sgt"),llt,lv,rv); break;
    case TOK_LEQ:     fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fcmp ole":(is_unsigned?"icmp ule":"icmp sle"),llt,lv,rv); break;
    case TOK_GEQ:     fprintf(cg->out,"  %s = %s %s %s, %s\n",res,is_float?"fcmp oge":(is_unsigned?"icmp uge":"icmp sge"),llt,lv,rv); break;
    case TOK_AND:     fprintf(cg->out,"  %s = and i1 %s, %s\n",res,lv,rv); break;
    case TOK_OR:      fprintf(cg->out,"  %s = or  i1 %s, %s\n",res,lv,rv); break;
    default:
        DIAG_ERR(&n->loc,"unhandled binary op in codegen");
        cg->had_error = 1;
    }
    return res;
}

/* ── Expression codegen ───────────────────────────────────────────────────── */
static char *gen_expr(CodeGen *cg, ASTNode *n, VarTable *vt) {
    if (!n) return "undef";

    switch (n->kind) {

    case NODE_INT_LIT: {
        char *r = new_reg_str(cg);
        const char *lt = (n->type && n->type->kind != TY_VOID) ? llvm_type(n->type) : "i64";
        fprintf(cg->out,"  %s = add %s 0, %lld\n", r, lt, (long long)n->int_lit.ival);
        return r;
    }
    case NODE_FLOAT_LIT: {
        char *r = new_reg_str(cg);
        const char *lt = (n->type && n->type->kind==TY_F32) ? "float" : "double";
        fprintf(cg->out,"  %s = fadd %s 0.0, %g\n", r, lt, n->float_lit.fval);
        return r;
    }
    case NODE_BOOL_LIT: {
        char *r = new_reg_str(cg);
        fprintf(cg->out,"  %s = add i1 0, %d\n", r, n->bool_lit.bval);
        return r;
    }
    case NODE_NULL_LIT: {
        char *r = new_reg_str(cg);
        fprintf(cg->out,"  %s = inttoptr i64 0 to i8*\n", r);
        return r;
    }
    case NODE_STRING_LIT: {
        int    idx = register_string(n->string_lit.sval);
        size_t len = strlen(n->string_lit.sval) + 1;
        char  *r   = new_reg_str(cg);
        fprintf(cg->out,
            "  %s = getelementptr inbounds [%zu x i8], [%zu x i8]* @.str%d, i64 0, i64 0\n",
            r, len, len, idx);
        return r;
    }
    case NODE_IDENT_REF: {
        VarEntry *ve = var_get(vt, n->ident.name);
        if (!ve) {
            /* Global / function reference */
            char *r = arena_alloc(cg->arena, strlen(n->ident.name)+2);
            snprintf(r, strlen(n->ident.name)+2, "@%s", n->ident.name);
            return r;
        }
        char *r = new_reg_str(cg);
        fprintf(cg->out,"  %s = load %s, %s* %s\n",
                r, llvm_type(ve->type), llvm_type(ve->type), ve->alloca_reg);
        return r;
    }
    case NODE_BINOP:
        return gen_binop(cg, n, vt);

    case NODE_UNOP: {
        char       *val = gen_expr(cg, n->unop.operand, vt);
        char       *r   = new_reg_str(cg);
        const char *lt  = llvm_type(n->type ? n->type : n->unop.operand->type);
        switch (n->unop.op) {
            case TOK_MINUS:
                if (type_is_float(n->unop.operand ? n->unop.operand->type : NULL))
                    fprintf(cg->out,"  %s = fneg %s %s\n", r, lt, val);
                else
                    fprintf(cg->out,"  %s = sub %s 0, %s\n", r, lt, val);
                break;
            case TOK_BANG:
                fprintf(cg->out,"  %s = icmp eq i1 %s, 0\n", r, val);
                break;
            case TOK_TILDE:
                fprintf(cg->out,"  %s = xor %s %s, -1\n", r, lt, val);
                break;
            default:
                fprintf(cg->out,"  %s = add %s 0, %s\n", r, lt, val);
        }
        return r;
    }

    case NODE_CALL: {
        const char *fn_name = (n->call.callee->kind == NODE_IDENT_REF)
                              ? n->call.callee->ident.name : "unknown";

        /* Evaluate args first */
        char **arg_regs  = arena_alloc(cg->arena, (n->call.arg_count+1)*sizeof(char*));
        Type **arg_types = arena_alloc(cg->arena, (n->call.arg_count+1)*sizeof(Type*));
        for (size_t i = 0; i < n->call.arg_count; i++) {
            arg_regs[i]  = gen_expr(cg, n->call.args[i], vt);
            arg_types[i] = n->call.args[i]->type;
        }

        Type *ret_t       = n->type;
        int   returns_val = !type_is_void(ret_t);
        char *r = returns_val ? new_reg_str(cg) : NULL;

        if (returns_val)
            fprintf(cg->out,"  %s = call %s @%s(", r, llvm_type(ret_t), fn_name);
        else
            fprintf(cg->out,"  call %s @%s(", llvm_type(ret_t), fn_name);

        for (size_t i = 0; i < n->call.arg_count; i++) {
            if (i) fprintf(cg->out,", ");
            const char *at = arg_types[i] ? llvm_type(arg_types[i]) : "i64";
            fprintf(cg->out,"%s %s", at, arg_regs[i]);
        }
        fprintf(cg->out,")\n");
        return r ? r : "void_val";
    }

    case NODE_INDEX: {
        char *arr_r  = gen_expr(cg, n->index.array, vt);
        char *idx_r  = gen_expr(cg, n->index.index, vt);
        Type *elem_t = n->type ? n->type : type_primitive(cg->arena, TY_I64);
        char *ptr_r  = new_reg_str(cg);
        char *val_r  = new_reg_str(cg);
        const char *ell = llvm_type(elem_t);
        fprintf(cg->out,"  %s = getelementptr inbounds %s, %s* %s, i64 %s\n",
                ptr_r, ell, ell, arr_r, idx_r);
        fprintf(cg->out,"  %s = load %s, %s* %s\n", val_r, ell, ell, ptr_r);
        return val_r;
    }

    case NODE_CAST: {
        char       *src     = gen_expr(cg, n->cast.expr, vt);
        Type       *from    = n->cast.expr->type;
        Type       *to      = n->cast.to;
        char       *r       = new_reg_str(cg);
        const char *from_ll = llvm_type(from);
        const char *to_ll   = llvm_type(to);

        if (type_is_integer(from) && type_is_integer(to)) {
            int fb=64, tb=64;
            if (from) switch(from->kind){case TY_I8:case TY_U8:fb=8;break;case TY_I16:case TY_U16:fb=16;break;case TY_I32:case TY_U32:fb=32;break;default:fb=64;}
            if (to)   switch(to->kind)  {case TY_I8:case TY_U8:tb=8;break;case TY_I16:case TY_U16:tb=16;break;case TY_I32:case TY_U32:tb=32;break;default:tb=64;}
            if      (tb > fb) fprintf(cg->out,"  %s = sext  %s %s to %s\n",r,from_ll,src,to_ll);
            else if (tb < fb) fprintf(cg->out,"  %s = trunc %s %s to %s\n",r,from_ll,src,to_ll);
            else              fprintf(cg->out,"  %s = bitcast %s %s to %s\n",r,from_ll,src,to_ll);
        } else if (type_is_integer(from) && type_is_float(to))
            fprintf(cg->out,"  %s = sitofp %s %s to %s\n",r,from_ll,src,to_ll);
        else if (type_is_float(from) && type_is_integer(to))
            fprintf(cg->out,"  %s = fptosi %s %s to %s\n",r,from_ll,src,to_ll);
        else
            fprintf(cg->out,"  %s = bitcast %s %s to %s\n",r,from_ll,src,to_ll);
        return r;
    }

    case NODE_ASSIGN: {
        char *rhs = gen_expr(cg, n->assign.value, vt);
        if (n->assign.target->kind == NODE_IDENT_REF) {
            VarEntry *ve = var_get(vt, n->assign.target->ident.name);
            if (ve) {
                const char *lt = llvm_type(ve->type);
                /* coerce integer width if needed */
                Type *rhs_t = n->assign.value->type;
                char *coerced = rhs;
                if (rhs_t && ve->type &&
                    type_is_integer(rhs_t) && type_is_integer(ve->type) &&
                    rhs_t->kind != ve->type->kind) {
                    char *c = new_reg_str(cg);
                    fprintf(cg->out,"  %s = trunc %s %s to %s\n",
                            c, llvm_type(rhs_t), rhs, lt);
                    coerced = c;
                }
                fprintf(cg->out,"  store %s %s, %s* %s\n",lt,coerced,lt,ve->alloca_reg);
            }
        }
        return rhs;
    }

    case NODE_ARRAY_LIT: {
        /* We don't support full runtime array literals yet; return undef */
        return "undef";
    }

    case NODE_FIELD: {
        /* Simplified: just evaluate the object and return undef for now */
        gen_expr(cg, n->field.object, vt);
        return "undef";
    }

    default:
        return "undef";
    }
}

/* ── Statement codegen ────────────────────────────────────────────────────── */
static void gen_block(CodeGen *cg, ASTNode *n, VarTable *vt) {
    if (!n) return;
    if (n->kind != NODE_BLOCK) { gen_stmt(cg, n, vt); return; }
    for (size_t i = 0; i < n->block.count; i++)
        gen_stmt(cg, n->block.stmts[i], vt);
}

static void gen_stmt(CodeGen *cg, ASTNode *n, VarTable *vt) {
    if (!n) return;
    switch (n->kind) {

    case NODE_LET: {
        Type       *ty  = n->type ? n->type : type_primitive(cg->arena, TY_I64);
        const char *llt = llvm_type(ty);
        char alloca_name[128];
        snprintf(alloca_name, sizeof(alloca_name), "%%v_%s_%d", n->let.name, new_reg(cg));
        fprintf(cg->out,"  %s = alloca %s\n", alloca_name, llt);
        var_set(cg, vt, n->let.name, alloca_name, ty);
        if (n->let.init) {
            char *val    = gen_expr(cg, n->let.init, vt);
            Type *init_t = n->let.init->type;
            char *coerced = val;
            if (init_t && ty && type_is_integer(init_t) && type_is_integer(ty) &&
                init_t->kind != ty->kind) {
                char *c = new_reg_str(cg);
                fprintf(cg->out,"  %s = trunc %s %s to %s\n",
                        c, llvm_type(init_t), val, llt);
                coerced = c;
            }
            fprintf(cg->out,"  store %s %s, %s* %s\n",llt,coerced,llt,alloca_name);
        }
        break;
    }

    case NODE_RETURN: {
        if (!n->ret.value || type_is_void(cg->cur_ret_type)) {
            fprintf(cg->out,"  ret void\n");
        } else {
            char *val = gen_expr(cg, n->ret.value, vt);
            /* coerce return value if needed */
            Type *vt_type = n->ret.value->type;
            char *coerced = val;
            if (vt_type && cg->cur_ret_type &&
                type_is_integer(vt_type) && type_is_integer(cg->cur_ret_type) &&
                vt_type->kind != cg->cur_ret_type->kind) {
                char *c = new_reg_str(cg);
                fprintf(cg->out,"  %s = trunc %s %s to %s\n",
                        c, llvm_type(vt_type), val, llvm_type(cg->cur_ret_type));
                coerced = c;
            }
            fprintf(cg->out,"  ret %s %s\n", llvm_type(cg->cur_ret_type), coerced);
        }
        /* Emit dead block so LLVM is happy after a ret */
        int dead = new_label(cg);
        fprintf(cg->out,"\nbb%d:\n", dead);
        break;
    }

    case NODE_IF: {
        char *cond_val = gen_expr(cg, n->if_stmt.cond, vt);
        int then_lbl  = new_label(cg);
        int else_lbl  = new_label(cg);
        int merge_lbl = new_label(cg);

        fprintf(cg->out,"  br i1 %s, label %%bb%d, label %%bb%d\n",
                cond_val, then_lbl, else_lbl);

        emit_label(cg, then_lbl);
        gen_block(cg, n->if_stmt.then_block, vt);
        fprintf(cg->out,"  br label %%bb%d\n", merge_lbl);

        emit_label(cg, else_lbl);
        if (n->if_stmt.else_block) {
            if (n->if_stmt.else_block->kind == NODE_IF)
                gen_stmt(cg, n->if_stmt.else_block, vt);
            else
                gen_block(cg, n->if_stmt.else_block, vt);
        }
        fprintf(cg->out,"  br label %%bb%d\n", merge_lbl);
        emit_label(cg, merge_lbl);
        break;
    }

    case NODE_WHILE: {
        int cond_lbl  = new_label(cg);
        int body_lbl  = new_label(cg);
        int merge_lbl = new_label(cg);

        fprintf(cg->out,"  br label %%bb%d\n", cond_lbl);
        emit_label(cg, cond_lbl);
        char *cv = gen_expr(cg, n->while_stmt.cond, vt);
        fprintf(cg->out,"  br i1 %s, label %%bb%d, label %%bb%d\n",
                cv, body_lbl, merge_lbl);

        emit_label(cg, body_lbl);
        gen_block(cg, n->while_stmt.body, vt);
        fprintf(cg->out,"  br label %%bb%d\n", cond_lbl);
        emit_label(cg, merge_lbl);
        break;
    }

    case NODE_FOR_RANGE: {
        char iter_alloca[128];
        snprintf(iter_alloca, sizeof(iter_alloca), "%%v_%s_%d", n->for_range.var, new_reg(cg));
        fprintf(cg->out,"  %s = alloca i64\n", iter_alloca);
        Type *i64t = type_primitive(cg->arena, TY_I64);
        /* dummy ASTNode for symbol */
        ASTNode *dummy = ast_node_new(cg->arena, NODE_LET, n->loc);
        (void)dummy;
        var_set(cg, vt, n->for_range.var, iter_alloca, i64t);

        char *from_val = gen_expr(cg, n->for_range.from, vt);
        fprintf(cg->out,"  store i64 %s, i64* %s\n", from_val, iter_alloca);

        int cond_lbl  = new_label(cg);
        int body_lbl  = new_label(cg);
        int merge_lbl = new_label(cg);

        fprintf(cg->out,"  br label %%bb%d\n", cond_lbl);
        emit_label(cg, cond_lbl);

        char *to_val  = gen_expr(cg, n->for_range.to, vt);
        char *cur_val = new_reg_str(cg);
        char *cmp_r   = new_reg_str(cg);
        fprintf(cg->out,"  %s = load i64, i64* %s\n", cur_val, iter_alloca);
        fprintf(cg->out,"  %s = icmp slt i64 %s, %s\n", cmp_r, cur_val, to_val);
        fprintf(cg->out,"  br i1 %s, label %%bb%d, label %%bb%d\n",
                cmp_r, body_lbl, merge_lbl);

        emit_label(cg, body_lbl);
        gen_block(cg, n->for_range.body, vt);

        char *cur2 = new_reg_str(cg);
        char *inc  = new_reg_str(cg);
        fprintf(cg->out,"  %s = load i64, i64* %s\n", cur2, iter_alloca);
        fprintf(cg->out,"  %s = add i64 %s, 1\n", inc, cur2);
        fprintf(cg->out,"  store i64 %s, i64* %s\n", inc, iter_alloca);
        fprintf(cg->out,"  br label %%bb%d\n", cond_lbl);
        emit_label(cg, merge_lbl);
        break;
    }

    case NODE_EXPR_STMT:
        gen_expr(cg, n->expr_stmt.expr, vt);
        break;

    case NODE_BLOCK:
        gen_block(cg, n, vt);
        break;

    default:
        gen_expr(cg, n, vt);
        break;
    }
}

/* ── Struct type declarations ─────────────────────────────────────────────── */
static void gen_struct_decl(CodeGen *cg, ASTNode *n) {
    fprintf(cg->out,"%%struct.%s = type { ", n->struct_decl.name);
    for (size_t i = 0; i < n->struct_decl.field_count; i++) {
        if (i) fprintf(cg->out,", ");
        fprintf(cg->out,"%s", llvm_type(n->struct_decl.field_types[i]));
    }
    fprintf(cg->out," }\n");
}

/* ── Function codegen ─────────────────────────────────────────────────────── */
static void gen_fn(CodeGen *cg, ASTNode *fn) {
    if (!fn || fn->kind != NODE_FN_DECL || fn->fn_decl.is_extern || !fn->fn_decl.body)
        return;

    cg->reg_counter   = 0;
    cg->label_counter = 0;

    Type *ret_t = fn->fn_decl.ret_type ? fn->fn_decl.ret_type
                                       : type_primitive(cg->arena, TY_VOID);
    cg->cur_ret_type = ret_t;

    fprintf(cg->out,"\ndefine %s @%s(", llvm_type(ret_t), fn->fn_decl.name);
    for (size_t i = 0; i < fn->fn_decl.param_count; i++) {
        ASTNode *pr = fn->fn_decl.params[i];
        if (i) fprintf(cg->out,", ");
        fprintf(cg->out,"%s %%arg_%s", llvm_type(pr->param.type), pr->param.name);
    }
    fprintf(cg->out,") {\nentry:\n");

    VarTable *vt = arena_alloc(cg->arena, sizeof(VarTable));
    memset(vt, 0, sizeof(*vt));

    for (size_t i = 0; i < fn->fn_decl.param_count; i++) {
        ASTNode *pr = fn->fn_decl.params[i];
        Type    *pt = pr->param.type;
        char alloca_name[128];
        snprintf(alloca_name, sizeof(alloca_name), "%%v_%s", pr->param.name);
        fprintf(cg->out,"  %s = alloca %s\n", alloca_name, llvm_type(pt));
        fprintf(cg->out,"  store %s %%arg_%s, %s* %s\n",
                llvm_type(pt), pr->param.name, llvm_type(pt), alloca_name);
        var_set(cg, vt, pr->param.name, alloca_name, pt);
    }

    gen_block(cg, fn->fn_decl.body, vt);

    if (type_is_void(ret_t))
        fprintf(cg->out,"  ret void\n");
    else
        fprintf(cg->out,"  unreachable\n");

    fprintf(cg->out,"}\n");
}

/* ── Module emission ──────────────────────────────────────────────────────── */
int codegen_emit(CodeGen *cg, ASTNode *module) {
    if (!module || module->kind != NODE_MODULE) return 0;

    for (int i = 0; i < g_str_count; i++) { free(g_strings[i].value); g_strings[i].value = NULL; }
    g_str_count = 0;

    fprintf(cg->out,"; Nova DSL Compiler — Generated LLVM IR\n");
    fprintf(cg->out,"; Module: %s\n\n", module->module.name);
    fprintf(cg->out,"source_filename = \"%s\"\n", cg->module_name);
    fprintf(cg->out,"target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128\"\n");
    fprintf(cg->out,"target triple = \"x86_64-pc-linux-gnu\"\n\n");

    /* Struct types */
    for (size_t i = 0; i < module->module.count; i++) {
        ASTNode *d = module->module.decls[i];
        if (d->kind == NODE_STRUCT_DECL) gen_struct_decl(cg, d);
    }

    /* Extern declarations */
    for (size_t i = 0; i < module->module.count; i++) {
        ASTNode *d = module->module.decls[i];
        if (d->kind == NODE_FN_DECL && d->fn_decl.is_extern) {
            Type *ret_t = d->fn_decl.ret_type ? d->fn_decl.ret_type
                                              : type_primitive(cg->arena, TY_VOID);
            fprintf(cg->out,"declare %s @%s(", llvm_type(ret_t), d->fn_decl.name);
            for (size_t j = 0; j < d->fn_decl.param_count; j++) {
                if (j) fprintf(cg->out,", ");
                fprintf(cg->out,"%s", llvm_type(d->fn_decl.params[j]->param.type));
            }
            /* variadic externs always get ... */
            fprintf(cg->out,")\n");
        }
    }

    /* Function definitions */
    for (size_t i = 0; i < module->module.count; i++) {
        ASTNode *d = module->module.decls[i];
        if (d->kind == NODE_FN_DECL && !d->fn_decl.is_extern)
            gen_fn(cg, d);
    }

    /* String literal globals */
    if (g_str_count > 0) fprintf(cg->out,"\n; String literals\n");
    for (int i = 0; i < g_str_count; i++) {
        size_t      len = g_strings[i].len + 1;
        const char *sv  = g_strings[i].value;
        fprintf(cg->out,"@.str%d = private unnamed_addr constant [%zu x i8] c\"", i, len);
        for (size_t j = 0; j < g_strings[i].len; j++) {
            unsigned char c = (unsigned char)sv[j];
            if      (c == '\\') fprintf(cg->out,"\\5C");
            else if (c == '"')  fprintf(cg->out,"\\22");
            else if (c < 32 || c > 126) fprintf(cg->out,"\\%02X", c);
            else fputc(c, cg->out);
        }
        fprintf(cg->out,"\\00\"\n");
    }

    return !cg->had_error;
}

void codegen_init(CodeGen *cg, FILE *out, Arena *arena, const char *module_name) {
    memset(cg, 0, sizeof(*cg));
    cg->out   = out;
    cg->arena = arena;
    strncpy(cg->module_name, module_name, sizeof(cg->module_name)-1);
}

void codegen_free(CodeGen *cg) { (void)cg; }
