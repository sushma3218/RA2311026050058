/*
 * Nova Language Compiler — Main Driver  (novac)
 *
 * Usage:  novac [options] <source.nova>
 *
 * Options:
 *   -o <file>      Output file            (default: a.out)
 *   --emit-ir      Stop at LLVM IR stage  (.ll)
 *   --emit-asm     Stop at assembly stage (.s)
 *   --ast          Dump AST and exit
 *   --tokens       Dump token stream and exit
 *   --no-color     Disable colour in diagnostics
 *   -O0 / -O1 / -O2   Optimisation level (passed to llc)
 *   -v             Verbose pipeline output
 *   --help         Show this message
 */
#define _POSIX_C_SOURCE 200112L
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "sema.h"
#include "codegen.h"
#include "diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define PATH_SEP '\\'
#else
#  define PATH_SEP '/'
#endif

/* ── Helpers ──────────────────────────────────────────────────────────────── */
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr,"error: cannot open '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f); rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { perror("malloc"); exit(1); }
    *out_len = fread(buf, 1, (size_t)sz, f);
    buf[*out_len] = '\0';
    fclose(f);
    return buf;
}

static const char *file_basename(const char *path) {
    const char *p = strrchr(path, PATH_SEP);
    return p ? p+1 : path;
}

static void strip_ext(char *s) {
    char *dot = strrchr(s, '.');
    if (dot) *dot = '\0';
}

static int run_cmd(const char *cmd, int verbose) {
    if (verbose) fprintf(stderr,"  [cmd] %s\n", cmd);
    return system(cmd);
}

static void dump_tokens(const char *src, size_t len, const char *fname) {
    Lexer lex; lexer_init(&lex, src, len, fname);
    Token t;
    do { t = lexer_next(&lex); token_print(&t); putchar('\n'); }
    while (t.kind != TOK_EOF);
    lexer_free(&lex);
}

static void print_banner(void) {
    fprintf(stderr,
        "\033[36m"
        "  _   _  _____     _   \n"
        " | \\ | |/ ___ \\   / \\  \n"
        " |  \\| | |   | | / _ \\ \n"
        " | |\\  | |___| |/ ___ \\\n"
        " |_| \\_|\\_____//_/   \\_\\\n"
        "\033[0m"
        "\033[1m  Nova DSL Compiler  v1.0.0\033[0m\n"
        "  LLVM-based · Written in C\n\n");
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <source.nova>\n\n"
        "Options:\n"
        "  -o <file>      Output file (default: a.out)\n"
        "  --emit-ir      Emit LLVM IR and stop\n"
        "  --emit-asm     Emit assembly and stop\n"
        "  --ast          Dump AST to stdout\n"
        "  --tokens       Dump token stream\n"
        "  --no-color     Disable coloured output\n"
        "  -O0 / -O1 / -O2   Optimisation level\n"
        "  -v             Verbose output\n"
        "  --help         Show this help\n\n"
        "Pipeline:\n"
        "  .nova -> Lex -> Parse -> Sema -> LLVM IR -> llc -> native binary\n\n",
        prog);
    exit(1);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    const char *input_file  = NULL;
    const char *output_file = "a.out";
    int emit_ir   = 0, emit_asm  = 0;
    int dump_ast  = 0, dump_toks = 0;
    int verbose   = 0, no_color  = 0;
    int opt_level = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")) { print_banner(); usage(argv[0]); }
        else if (!strcmp(argv[i],"--emit-ir"))  emit_ir   = 1;
        else if (!strcmp(argv[i],"--emit-asm")) emit_asm  = 1;
        else if (!strcmp(argv[i],"--ast"))      dump_ast  = 1;
        else if (!strcmp(argv[i],"--tokens"))   dump_toks = 1;
        else if (!strcmp(argv[i],"-v"))         verbose   = 1;
        else if (!strcmp(argv[i],"--no-color")) no_color  = 1;
        else if (!strcmp(argv[i],"-O0"))        opt_level = 0;
        else if (!strcmp(argv[i],"-O1"))        opt_level = 1;
        else if (!strcmp(argv[i],"-O2"))        opt_level = 2;
        else if (!strcmp(argv[i],"-o")) {
            if (++i >= argc) { fprintf(stderr,"error: -o needs an argument\n"); exit(1); }
            output_file = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr,"error: unknown option '%s'\n", argv[i]); usage(argv[0]);
        } else {
            if (input_file) { fprintf(stderr,"error: multiple input files\n"); exit(1); }
            input_file = argv[i];
        }
    }

    if (!input_file) { print_banner(); usage(argv[0]); }
    diag_use_color(!no_color);

    /* ── Read source ──────────────────────────────────────────────────────── */
    size_t src_len;
    char  *src = read_file(input_file, &src_len);
    if (verbose) { print_banner(); fprintf(stderr,"  Source: %s (%zu bytes)\n\n", input_file, src_len); }

    if (dump_toks) { dump_tokens(src, src_len, input_file); free(src); return 0; }

    /* module name = filename without extension */
    char module_name[256];
    snprintf(module_name, sizeof(module_name), "%s", file_basename(input_file));
    strip_ext(module_name);

    /* ── Lex ──────────────────────────────────────────────────────────────── */
    if (verbose) fprintf(stderr,"  [1/4] Lexing...\n");
    Lexer lex;
    lexer_init(&lex, src, src_len, input_file);

    /* ── Arena ────────────────────────────────────────────────────────────── */
    Arena *arena = arena_new();

    /* ── Parse ────────────────────────────────────────────────────────────── */
    if (verbose) fprintf(stderr,"  [2/4] Parsing...\n");
    Parser parser;
    parser_init(&parser, &lex, arena);
    ASTNode *module = parser_parse_module(&parser, module_name);

    if (parser.had_error) {
        fprintf(stderr,"\033[31merror:\033[0m %u parse error(s). Aborting.\n", parser.error_count);
        lexer_free(&lex); arena_free(arena); free(src); return 1;
    }

    if (dump_ast) { ast_print(module, 0); lexer_free(&lex); arena_free(arena); free(src); return 0; }

    /* ── Sema ─────────────────────────────────────────────────────────────── */
    if (verbose) fprintf(stderr,"  [3/4] Semantic analysis...\n");
    Sema sema;
    sema_init(&sema, arena);
    if (!sema_analyse(&sema, module)) {
        fprintf(stderr,"\033[31merror:\033[0m %u semantic error(s). Aborting.\n", sema.error_count);
        sema_free(&sema); lexer_free(&lex); arena_free(arena); free(src); return 1;
    }

    /* ── Determine output paths ───────────────────────────────────────────── */
    char ir_path[512], asm_path[512];
    if (emit_ir)
        snprintf(ir_path, sizeof(ir_path), "%s", output_file);
    else
        snprintf(ir_path, sizeof(ir_path), "%s.ll", module_name);

    snprintf(asm_path, sizeof(asm_path), "%s.s", module_name);

    /* ── Code generation ──────────────────────────────────────────────────── */
    if (verbose) fprintf(stderr,"  [4/4] Emitting LLVM IR -> %s\n", ir_path);
    FILE *ir_file = fopen(ir_path, "w");
    if (!ir_file) { fprintf(stderr,"error: cannot open '%s' for writing\n", ir_path); return 1; }

    CodeGen cg;
    codegen_init(&cg, ir_file, arena, input_file);
    int ok = codegen_emit(&cg, module);
    codegen_free(&cg);
    fclose(ir_file);

    if (!ok) { fprintf(stderr,"\033[31merror:\033[0m Code generation failed.\n"); return 1; }
    fprintf(stderr,"\033[32m✓\033[0m LLVM IR  -> \033[1m%s\033[0m\n", ir_path);
    if (emit_ir) goto cleanup;

    /* ── llc: IR -> assembly ──────────────────────────────────────────────── */
    {
        char cmd[2048];
        const char *out_asm = emit_asm ? output_file : asm_path;
        snprintf(cmd, sizeof(cmd), "llc -O%d -filetype=asm -o \"%s\" \"%s\"",
                 opt_level, out_asm, ir_path);
        if (run_cmd(cmd, verbose) != 0) {
            fprintf(stderr,"\033[33mhint:\033[0m Is LLVM installed? Run: sudo apt install llvm\n");
            return 1;
        }
        fprintf(stderr,"\033[32m✓\033[0m Assembly -> \033[1m%s\033[0m\n", out_asm);
        if (emit_asm) goto cleanup;
    }

    /* ── clang/gcc: asm -> native binary ──────────────────────────────────── */
    {
        char cmd[2048];
        /* Try clang first, fall back to gcc */
        snprintf(cmd, sizeof(cmd),
                 "clang -O%d -o \"%s\" \"%s\" 2>/dev/null || gcc -O%d -o \"%s\" \"%s\"",
                 opt_level, output_file, asm_path,
                 opt_level, output_file, asm_path);
        if (run_cmd(cmd, verbose) != 0) {
            fprintf(stderr,"\033[33mhint:\033[0m Install clang or gcc: sudo apt install clang\n");
            return 1;
        }
        fprintf(stderr,"\033[32m✓\033[0m Binary   -> \033[1m%s\033[0m\n", output_file);
    }

cleanup:
    sema_free(&sema);
    lexer_free(&lex);
    arena_free(arena);
    free(src);
    return 0;
}
