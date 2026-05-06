/*
 * Nova Language Compiler - Parser Implementation
 * Recursive-descent parser for the Nova DSL.
 */
#include "parser.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Internal helpers ─────────────────────────────────────────────────────── */
static Token advance_tok(Parser *p) {
    Token prev   = p->current;
    p->current   = p->lookahead;
    p->lookahead = lexer_next(p->lex);
    return prev;
}

static Token peek(const Parser *p)  { return p->current; }
static Token peek2(const Parser *p) { return p->lookahead; }

static int check(const Parser *p, TokenKind k) {
    return p->current.kind == k;
}

static int match_tok(Parser *p, TokenKind k) {
    if (check(p, k)) { advance_tok(p); return 1; }
    return 0;
}

static Token expect(Parser *p, TokenKind k) {
    if (check(p, k)) return advance_tok(p);
    SrcLoc loc = p->current.loc;
    DIAG_ERR(&loc, "expected '%s' but got '%s'",
             token_kind_name(k), token_kind_name(p->current.kind));
    p->had_error = 1;
    p->error_count++;
    /* return a dummy token to allow continued parsing */
    return p->current;
}

static void synchronize(Parser *p) {
    p->panic_mode = 0;
    while (!check(p, TOK_EOF)) {
        if (p->current.kind == TOK_SEMICOLON) { advance_tok(p); return; }
        switch (p->current.kind) {
            case TOK_FN: case TOK_LET: case TOK_RETURN:
            case TOK_IF: case TOK_WHILE: case TOK_FOR:
            case TOK_STRUCT: case TOK_ENUM: case TOK_EXTERN:
                return;
            default: advance_tok(p);
        }
    }
}

/* ── Arena-backed dynamic array helpers ───────────────────────────────────── */
#define DA_PUSH(arena, arr, cnt, cap, val) do { \
    if ((cnt) >= (cap)) { \
        size_t newcap = (cap) ? (cap) * 2 : 4; \
        void **tmp = arena_alloc((arena), newcap * sizeof(void*)); \
        if (cnt) memcpy(tmp, (arr), (cnt) * sizeof(void*)); \
        (arr) = (void*)tmp; \
        (cap) = newcap; \
    } \
    ((void**)(arr))[(cnt)++] = (void*)(val); \
} while(0)

/* ── Type parsing ─────────────────────────────────────────────────────────── */
static Type *parse_type(Parser *p) {
    Token t = p->current;
    switch (t.kind) {
        case TOK_TYPE_I8:   advance_tok(p); return type_primitive(p->arena, TY_I8);
        case TOK_TYPE_I16:  advance_tok(p); return type_primitive(p->arena, TY_I16);
        case TOK_TYPE_I32:  advance_tok(p); return type_primitive(p->arena, TY_I32);
        case TOK_TYPE_I64:  advance_tok(p); return type_primitive(p->arena, TY_I64);
        case TOK_TYPE_U8:   advance_tok(p); return type_primitive(p->arena, TY_U8);
        case TOK_TYPE_U16:  advance_tok(p); return type_primitive(p->arena, TY_U16);
        case TOK_TYPE_U32:  advance_tok(p); return type_primitive(p->arena, TY_U32);
        case TOK_TYPE_U64:  advance_tok(p); return type_primitive(p->arena, TY_U64);
        case TOK_TYPE_F32:  advance_tok(p); return type_primitive(p->arena, TY_F32);
        case TOK_TYPE_F64:  advance_tok(p); return type_primitive(p->arena, TY_F64);
        case TOK_TYPE_BOOL: advance_tok(p); return type_primitive(p->arena, TY_BOOL);
        case TOK_TYPE_STR:  advance_tok(p); return type_primitive(p->arena, TY_STR);
        case TOK_TYPE_VOID: advance_tok(p); return type_primitive(p->arena, TY_VOID);
        case TOK_STAR: {
            advance_tok(p);
            Type *inner = parse_type(p);
            return type_ptr(p->arena, inner);
        }
        case TOK_LBRACKET: {
            advance_tok(p);
            int64_t sz = -1;
            if (!check(p, TOK_RBRACKET)) {
                if (check(p, TOK_INT_LIT)) {
                    sz = p->current.val.ival;
                    advance_tok(p);
                }
            }
            expect(p, TOK_RBRACKET);
            Type *elem = parse_type(p);
            return type_array(p->arena, elem, sz);
        }
        case TOK_IDENT: {
            char name[256];
            snprintf(name, sizeof(name), "%.*s", (int)t.len, t.start);
            advance_tok(p);
            return type_unresolved(p->arena, name);
        }
        default: {
            DIAG_ERR(&t.loc, "expected type, got '%s'", token_kind_name(t.kind));
            p->had_error = 1;
            return type_primitive(p->arena, TY_ERROR);
        }
    }
}

/* Forward declarations for mutually recursive functions */
static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_block(Parser *p);

/* ── Expression parsing (Pratt/precedence climbing) ──────────────────────── */
typedef enum {
    PREC_NONE = 0,
    PREC_ASSIGN,    /* = += -= *= /=       */
    PREC_OR,        /* ||                  */
    PREC_AND,       /* &&                  */
    PREC_BIT_OR,    /* |                   */
    PREC_BIT_XOR,   /* ^                   */
    PREC_BIT_AND,   /* &                   */
    PREC_EQ,        /* == !=               */
    PREC_COMPARE,   /* < > <= >=           */
    PREC_SHIFT,     /* << >>               */
    PREC_ADD,       /* + -                 */
    PREC_MUL,       /* * / %               */
    PREC_UNARY,     /* - ! ~               */
    PREC_CALL,      /* () [] . as          */
} Prec;

static Prec token_prec(TokenKind k) {
    switch (k) {
        case TOK_ASSIGN:
        case TOK_PLUS_ASSIGN: case TOK_MINUS_ASSIGN:
        case TOK_STAR_ASSIGN: case TOK_SLASH_ASSIGN: return PREC_ASSIGN;
        case TOK_OR:     return PREC_OR;
        case TOK_AND:    return PREC_AND;
        case TOK_PIPE:   return PREC_BIT_OR;
        case TOK_CARET:  return PREC_BIT_XOR;
        case TOK_AMP:    return PREC_BIT_AND;
        case TOK_EQ: case TOK_NEQ: return PREC_EQ;
        case TOK_LT: case TOK_GT:
        case TOK_LEQ: case TOK_GEQ: return PREC_COMPARE;
        case TOK_LSHIFT: case TOK_RSHIFT: return PREC_SHIFT;
        case TOK_PLUS: case TOK_MINUS: return PREC_ADD;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return PREC_MUL;
        case TOK_LPAREN: case TOK_LBRACKET: case TOK_DOT:
        case TOK_AS: return PREC_CALL;
        default: return PREC_NONE;
    }
}

static ASTNode *parse_primary(Parser *p) {
    Token t = p->current;
    SrcLoc loc = t.loc;

    /* Integer literal */
    if (t.kind == TOK_INT_LIT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(p->arena, NODE_INT_LIT, loc);
        n->int_lit.ival = t.val.ival;
        return n;
    }
    /* Float literal */
    if (t.kind == TOK_FLOAT_LIT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(p->arena, NODE_FLOAT_LIT, loc);
        n->float_lit.fval = t.val.fval;
        return n;
    }
    /* String literal */
    if (t.kind == TOK_STRING_LIT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(p->arena, NODE_STRING_LIT, loc);
        n->string_lit.sval = arena_strdup(p->arena, t.val.sval);
        return n;
    }
    /* Bool literal */
    if (t.kind == TOK_BOOL_LIT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(p->arena, NODE_BOOL_LIT, loc);
        n->bool_lit.bval = t.val.bval;
        return n;
    }
    /* Null literal */
    if (t.kind == TOK_NULL) {
        advance_tok(p);
        return ast_node_new(p->arena, NODE_NULL_LIT, loc);
    }
    /* Identifier */
    if (t.kind == TOK_IDENT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(p->arena, NODE_IDENT_REF, loc);
        n->ident.name = arena_alloc(p->arena, t.len + 1);
        memcpy(n->ident.name, t.start, t.len);
        n->ident.name[t.len] = '\0';
        return n;
    }
    /* Grouped expression */
    if (t.kind == TOK_LPAREN) {
        advance_tok(p);
        ASTNode *inner = parse_expr(p);
        expect(p, TOK_RPAREN);
        return inner;
    }
    /* Array literal  [a, b, c] */
    if (t.kind == TOK_LBRACKET) {
        advance_tok(p);
        ASTNode *n = ast_node_new(p->arena, NODE_ARRAY_LIT, loc);
        ASTNode **elems = NULL;
        size_t cnt = 0, cap = 0;
        if (!check(p, TOK_RBRACKET)) {
            do {
                ASTNode *e = parse_expr(p);
                size_t newcap = cap ? cap * 2 : 4;
                if (cnt >= cap) {
                    ASTNode **tmp = arena_alloc(p->arena, newcap * sizeof(ASTNode*));
                    if (cnt) memcpy(tmp, elems, cnt * sizeof(ASTNode*));
                    elems = tmp; cap = newcap;
                }
                elems[cnt++] = e;
            } while (match_tok(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACKET);
        n->array_lit.elems = elems;
        n->array_lit.count = cnt;
        return n;
    }
    /* Unary: - ! ~ */
    if (t.kind == TOK_MINUS || t.kind == TOK_BANG || t.kind == TOK_TILDE) {
        advance_tok(p);
        ASTNode *n = ast_node_new(p->arena, NODE_UNOP, loc);
        n->unop.op      = t.kind;
        n->unop.operand = parse_expr(p); /* will grab right sub-expr */
        return n;
    }

    DIAG_ERR(&loc, "unexpected token '%s' in expression",
             token_kind_name(t.kind));
    p->had_error = 1;
    advance_tok(p);
    return ast_node_new(p->arena, NODE_INT_LIT, loc); /* error recovery */
}

static ASTNode *parse_expr_prec(Parser *p, Prec min_prec) {
    ASTNode *left = parse_primary(p);

    for (;;) {
        TokenKind op = p->current.kind;
        Prec prec = token_prec(op);
        if (prec < min_prec || prec == PREC_NONE) break;

        SrcLoc loc = p->current.loc;
        advance_tok(p);

        /* Function call */
        if (op == TOK_LPAREN) {
            ASTNode *call = ast_node_new(p->arena, NODE_CALL, loc);
            call->call.callee = left;
            ASTNode **args = NULL;
            size_t cnt = 0, cap = 0;
            if (!check(p, TOK_RPAREN)) {
                do {
                    ASTNode *a = parse_expr(p);
                    size_t newcap = cap ? cap * 2 : 4;
                    if (cnt >= cap) {
                        ASTNode **tmp = arena_alloc(p->arena, newcap * sizeof(ASTNode*));
                        if (cnt) memcpy(tmp, args, cnt * sizeof(ASTNode*));
                        args = tmp; cap = newcap;
                    }
                    args[cnt++] = a;
                } while (match_tok(p, TOK_COMMA) && !check(p, TOK_RPAREN));
            }
            expect(p, TOK_RPAREN);
            call->call.args      = args;
            call->call.arg_count = cnt;
            left = call;
            continue;
        }
        /* Array index */
        if (op == TOK_LBRACKET) {
            ASTNode *idx = ast_node_new(p->arena, NODE_INDEX, loc);
            idx->index.array = left;
            idx->index.index = parse_expr(p);
            expect(p, TOK_RBRACKET);
            left = idx;
            continue;
        }
        /* Field access */
        if (op == TOK_DOT) {
            Token field_tok = expect(p, TOK_IDENT);
            ASTNode *fld = ast_node_new(p->arena, NODE_FIELD, loc);
            fld->field.object = left;
            char *fname = arena_alloc(p->arena, field_tok.len + 1);
            memcpy(fname, field_tok.start, field_tok.len);
            fname[field_tok.len] = '\0';
            fld->field.field = fname;
            left = fld;
            continue;
        }
        /* Cast: expr as Type */
        if (op == TOK_AS) {
            ASTNode *cast = ast_node_new(p->arena, NODE_CAST, loc);
            cast->cast.expr = left;
            cast->cast.to   = parse_type(p);
            left = cast;
            continue;
        }
        /* Assignment operators (right-associative) */
        if (op == TOK_ASSIGN || op == TOK_PLUS_ASSIGN || op == TOK_MINUS_ASSIGN ||
            op == TOK_STAR_ASSIGN || op == TOK_SLASH_ASSIGN) {
            ASTNode *rhs = parse_expr_prec(p, PREC_ASSIGN);
            ASTNode *assign = ast_node_new(p->arena, NODE_ASSIGN, loc);
            assign->assign.target = left;
            assign->assign.value  = rhs;
            assign->assign.op     = op;
            left = assign;
            continue;
        }
        /* Binary operators (left-associative) */
        ASTNode *right = parse_expr_prec(p, (Prec)(prec + 1));
        ASTNode *bin   = ast_node_new(p->arena, NODE_BINOP, loc);
        bin->binop.op    = op;
        bin->binop.left  = left;
        bin->binop.right = right;
        left = bin;
    }
    return left;
}

static ASTNode *parse_expr(Parser *p) {
    return parse_expr_prec(p, PREC_ASSIGN);
}

/* ── Statement parsing ────────────────────────────────────────────────────── */
static ASTNode *parse_let(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_LET);
    int is_mut = match_tok(p, TOK_MUT);

    Token name_tok = expect(p, TOK_IDENT);
    char *name = arena_alloc(p->arena, name_tok.len + 1);
    memcpy(name, name_tok.start, name_tok.len);
    name[name_tok.len] = '\0';

    Type *annot = NULL;
    if (match_tok(p, TOK_COLON)) annot = parse_type(p);

    ASTNode *init = NULL;
    if (match_tok(p, TOK_ASSIGN)) init = parse_expr(p);

    expect(p, TOK_SEMICOLON);

    ASTNode *n = ast_node_new(p->arena, NODE_LET, loc);
    n->let.name   = name;
    n->let.annot  = annot;
    n->let.init   = init;
    n->let.is_mut = is_mut;
    return n;
}

static ASTNode *parse_return(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_RETURN);
    ASTNode *val = NULL;
    if (!check(p, TOK_SEMICOLON)) val = parse_expr(p);
    expect(p, TOK_SEMICOLON);
    ASTNode *n = ast_node_new(p->arena, NODE_RETURN, loc);
    n->ret.value = val;
    return n;
}

static ASTNode *parse_if(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_IF);
    ASTNode *cond = parse_expr(p);
    ASTNode *then = parse_block(p);
    ASTNode *els  = NULL;
    if (match_tok(p, TOK_ELSE)) {
        if (check(p, TOK_IF)) els = parse_if(p);
        else                   els = parse_block(p);
    }
    ASTNode *n = ast_node_new(p->arena, NODE_IF, loc);
    n->if_stmt.cond       = cond;
    n->if_stmt.then_block = then;
    n->if_stmt.else_block = els;
    return n;
}

static ASTNode *parse_while(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_WHILE);
    ASTNode *cond = parse_expr(p);
    ASTNode *body = parse_block(p);
    ASTNode *n = ast_node_new(p->arena, NODE_WHILE, loc);
    n->while_stmt.cond = cond;
    n->while_stmt.body = body;
    return n;
}

static ASTNode *parse_for(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_FOR);
    Token var_tok = expect(p, TOK_IDENT);
    char *var = arena_alloc(p->arena, var_tok.len + 1);
    memcpy(var, var_tok.start, var_tok.len);
    var[var_tok.len] = '\0';
    expect(p, TOK_IN);
    ASTNode *from = parse_expr(p);
    expect(p, TOK_DOTDOT);
    ASTNode *to   = parse_expr(p);
    ASTNode *body = parse_block(p);
    ASTNode *n = ast_node_new(p->arena, NODE_FOR_RANGE, loc);
    n->for_range.var  = var;
    n->for_range.from = from;
    n->for_range.to   = to;
    n->for_range.body = body;
    return n;
}

static ASTNode *parse_block(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_LBRACE);
    ASTNode **stmts = NULL;
    size_t cnt = 0, cap = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ASTNode *s = parse_stmt(p);
        if (!s) continue;
        size_t newcap = cap ? cap * 2 : 8;
        if (cnt >= cap) {
            ASTNode **tmp = arena_alloc(p->arena, newcap * sizeof(ASTNode*));
            if (cnt) memcpy(tmp, stmts, cnt * sizeof(ASTNode*));
            stmts = tmp; cap = newcap;
        }
        stmts[cnt++] = s;
    }
    expect(p, TOK_RBRACE);
    ASTNode *n = ast_node_new(p->arena, NODE_BLOCK, loc);
    n->block.stmts = stmts;
    n->block.count = cnt;
    return n;
}

static ASTNode *parse_stmt(Parser *p) {
    switch (p->current.kind) {
        case TOK_LET:    return parse_let(p);
        case TOK_RETURN: return parse_return(p);
        case TOK_IF:     return parse_if(p);
        case TOK_WHILE:  return parse_while(p);
        case TOK_FOR:    return parse_for(p);
        case TOK_LBRACE: return parse_block(p);
        case TOK_SEMICOLON: advance_tok(p); return NULL;
        default: {
            SrcLoc loc = p->current.loc;
            ASTNode *e = parse_expr(p);
            expect(p, TOK_SEMICOLON);
            ASTNode *n = ast_node_new(p->arena, NODE_EXPR_STMT, loc);
            n->expr_stmt.expr = e;
            return n;
        }
    }
}

/* ── Top-level declaration parsing ───────────────────────────────────────── */
static ASTNode *parse_fn_decl(Parser *p, int is_extern) {
    SrcLoc loc = p->current.loc;
    if (!is_extern) expect(p, TOK_FN);

    Token name_tok = expect(p, TOK_IDENT);
    char *name = arena_alloc(p->arena, name_tok.len + 1);
    memcpy(name, name_tok.start, name_tok.len);
    name[name_tok.len] = '\0';

    expect(p, TOK_LPAREN);
    ASTNode **params = NULL;
    size_t pcnt = 0, pcap = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        SrcLoc ploc = p->current.loc;
        int is_mut = match_tok(p, TOK_MUT);
        Token pname_tok = expect(p, TOK_IDENT);
        char *pname = arena_alloc(p->arena, pname_tok.len + 1);
        memcpy(pname, pname_tok.start, pname_tok.len);
        pname[pname_tok.len] = '\0';
        expect(p, TOK_COLON);
        Type *ptype = parse_type(p);

        ASTNode *param = ast_node_new(p->arena, NODE_PARAM, ploc);
        param->param.name   = pname;
        param->param.type   = ptype;
        param->param.is_mut = is_mut;

        size_t newcap = pcap ? pcap * 2 : 4;
        if (pcnt >= pcap) {
            ASTNode **tmp = arena_alloc(p->arena, newcap * sizeof(ASTNode*));
            if (pcnt) memcpy(tmp, params, pcnt * sizeof(ASTNode*));
            params = tmp; pcap = newcap;
        }
        params[pcnt++] = param;
        if (!match_tok(p, TOK_COMMA)) break;
    }
    expect(p, TOK_RPAREN);

    Type *ret = type_primitive(p->arena, TY_VOID);
    if (match_tok(p, TOK_ARROW)) ret = parse_type(p);

    ASTNode *n = ast_node_new(p->arena, NODE_FN_DECL, loc);
    n->fn_decl.name        = name;
    n->fn_decl.params      = params;
    n->fn_decl.param_count = pcnt;
    n->fn_decl.ret_type    = ret;
    n->fn_decl.is_extern   = is_extern;

    if (is_extern) {
        expect(p, TOK_SEMICOLON);
        n->fn_decl.body = NULL;
    } else {
        n->fn_decl.body = parse_block(p);
    }
    return n;
}

static ASTNode *parse_struct_decl(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_STRUCT);
    Token name_tok = expect(p, TOK_IDENT);
    char *name = arena_alloc(p->arena, name_tok.len + 1);
    memcpy(name, name_tok.start, name_tok.len);
    name[name_tok.len] = '\0';
    expect(p, TOK_LBRACE);

    char  **field_names = NULL;
    Type  **field_types = NULL;
    size_t  cnt = 0, cap = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Token fn_tok = expect(p, TOK_IDENT);
        expect(p, TOK_COLON);
        Type *ft = parse_type(p);
        match_tok(p, TOK_COMMA);

        if (cnt >= cap) {
            size_t nc = cap ? cap * 2 : 4;
            char **nt  = arena_alloc(p->arena, nc * sizeof(char*));
            Type **ntt = arena_alloc(p->arena, nc * sizeof(Type*));
            if (cnt) {
                memcpy(nt, field_names, cnt * sizeof(char*));
                memcpy(ntt, field_types, cnt * sizeof(Type*));
            }
            field_names = nt; field_types = ntt; cap = nc;
        }
        char *fn2 = arena_alloc(p->arena, fn_tok.len + 1);
        memcpy(fn2, fn_tok.start, fn_tok.len); fn2[fn_tok.len] = '\0';
        field_names[cnt] = fn2;
        field_types[cnt] = ft;
        cnt++;
    }
    expect(p, TOK_RBRACE);

    ASTNode *n = ast_node_new(p->arena, NODE_STRUCT_DECL, loc);
    n->struct_decl.name        = name;
    n->struct_decl.field_names = field_names;
    n->struct_decl.field_types = field_types;
    n->struct_decl.field_count = cnt;
    return n;
}

static ASTNode *parse_enum_decl(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_ENUM);
    Token name_tok = expect(p, TOK_IDENT);
    char *name = arena_alloc(p->arena, name_tok.len + 1);
    memcpy(name, name_tok.start, name_tok.len);
    name[name_tok.len] = '\0';
    expect(p, TOK_LBRACE);

    char  **variants = NULL;
    size_t  cnt = 0, cap = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Token vt = expect(p, TOK_IDENT);
        match_tok(p, TOK_COMMA);
        if (cnt >= cap) {
            size_t nc = cap ? cap * 2 : 4;
            char **nv = arena_alloc(p->arena, nc * sizeof(char*));
            if (cnt) memcpy(nv, variants, cnt * sizeof(char*));
            variants = nv; cap = nc;
        }
        char *vn = arena_alloc(p->arena, vt.len + 1);
        memcpy(vn, vt.start, vt.len); vn[vt.len] = '\0';
        variants[cnt++] = vn;
    }
    expect(p, TOK_RBRACE);

    ASTNode *n = ast_node_new(p->arena, NODE_ENUM_DECL, loc);
    n->enum_decl.name          = name;
    n->enum_decl.variants      = variants;
    n->enum_decl.variant_count = cnt;
    return n;
}

static ASTNode *parse_import(Parser *p) {
    SrcLoc loc = p->current.loc;
    expect(p, TOK_IMPORT);
    Token path_tok = expect(p, TOK_STRING_LIT);
    char *path = arena_alloc(p->arena, path_tok.len + 1);
    memcpy(path, path_tok.start, path_tok.len);
    path[path_tok.len] = '\0';
    char *alias = NULL;
    if (match_tok(p, TOK_AS)) {
        Token at = expect(p, TOK_IDENT);
        alias = arena_alloc(p->arena, at.len + 1);
        memcpy(alias, at.start, at.len); alias[at.len] = '\0';
    }
    expect(p, TOK_SEMICOLON);
    ASTNode *n = ast_node_new(p->arena, NODE_IMPORT, loc);
    n->import.path  = path;
    n->import.alias = alias;
    return n;
}

/* ── Module-level parse ───────────────────────────────────────────────────── */
ASTNode *parser_parse_module(Parser *p, const char *module_name) {
    SrcLoc loc = p->current.loc;
    ASTNode **decls = NULL;
    size_t cnt = 0, cap = 0;

    while (!check(p, TOK_EOF)) {
        ASTNode *d = NULL;
        switch (p->current.kind) {
            case TOK_FN:     d = parse_fn_decl(p, 0); break;
            case TOK_STRUCT: d = parse_struct_decl(p); break;
            case TOK_ENUM:   d = parse_enum_decl(p);  break;
            case TOK_IMPORT: d = parse_import(p);      break;
            case TOK_EXTERN: {
                advance_tok(p);
                expect(p, TOK_FN);
                d = parse_fn_decl(p, 1);
                break;
            }
            default: {
                DIAG_ERR(&p->current.loc,
                         "expected top-level declaration, got '%s'",
                         token_kind_name(p->current.kind));
                p->had_error = 1;
                if (p->panic_mode) synchronize(p);
                else { p->panic_mode = 1; advance_tok(p); }
                continue;
            }
        }
        if (!d) continue;
        size_t newcap = cap ? cap * 2 : 8;
        if (cnt >= cap) {
            ASTNode **tmp = arena_alloc(p->arena, newcap * sizeof(ASTNode*));
            if (cnt) memcpy(tmp, decls, cnt * sizeof(ASTNode*));
            decls = tmp; cap = newcap;
        }
        decls[cnt++] = d;
    }

    ASTNode *mod = ast_node_new(p->arena, NODE_MODULE, loc);
    mod->module.name  = arena_strdup(p->arena, module_name);
    mod->module.decls = decls;
    mod->module.count = cnt;
    return mod;
}

void parser_init(Parser *p, Lexer *lex, Arena *arena) {
    p->lex         = lex;
    p->arena       = arena;
    p->had_error   = 0;
    p->panic_mode  = 0;
    p->error_count = 0;
    p->current     = lexer_next(lex);
    p->lookahead   = lexer_next(lex);
}
