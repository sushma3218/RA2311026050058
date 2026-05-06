#ifndef NOVA_CODEGEN_H
#define NOVA_CODEGEN_H

/*
 * Nova Language Compiler - LLVM IR Code Generator
 * Walks the annotated AST and emits textual LLVM IR (.ll)
 * ready for `llc` or `clang` to compile to native code.
 */

#include "ast.h"
#include <stdio.h>

/* ── Code generator state ─────────────────────────────────────────────────── */
#define CG_MAX_REGS  4096
#define CG_MAX_BBLKS 1024

typedef struct {
    FILE   *out;           /* output stream for the .ll file */
    Arena  *arena;
    int     reg_counter;   /* next SSA register number      */
    int     label_counter; /* next basic-block label number  */
    int     had_error;
    char    module_name[256];

    /* Current function context */
    Type   *cur_ret_type;
    char    ret_label[64];   /* label for function exit block  */
    char    ret_val_reg[32]; /* alloca holding return value    */
} CodeGen;

/* ── Public API ───────────────────────────────────────────────────────────── */
void codegen_init(CodeGen *cg, FILE *out, Arena *arena, const char *module_name);
int  codegen_emit(CodeGen *cg, ASTNode *module);
void codegen_free(CodeGen *cg);

#endif /* NOVA_CODEGEN_H */
