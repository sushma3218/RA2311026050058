# Nova DSL Compiler — Setup & Run Instructions

## Prerequisites (install once)

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install gcc make llvm clang
```

### macOS
```bash
brew install llvm gcc
# Add llvm to PATH:
echo 'export PATH="/opt/homebrew/opt/llvm/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### Windows (WSL recommended)
Use WSL2 with Ubuntu, then follow Linux instructions above.

---

## Step 1 — Extract the project

```bash
unzip nova-lang-compiler.zip
cd nova-lang-clean
```

---

## Step 2 — Open in VS Code

```bash
code .
```

Install the **C/C++** extension (Microsoft) for IntelliSense.

---

## Step 3 — Build the compiler

```bash
make
```

Expected output:
```
  Building Nova compiler...
  ✓  Built: ./novac
```

---

## Step 4 — Run the full test suite

```bash
make test
```

Expected output:
```
══════════════════════════════════════════
  Nova Compiler — Test Suite
══════════════════════════════════════════
  hello                PASS
  arith                PASS
  control              PASS
  fib                  PASS
  structs              PASS
  loops                PASS
  strings              PASS

  Passed: 7  Failed: 0
══════════════════════════════════════════
```

---

## Step 5 — Compile a Nova program end-to-end

```bash
# Full pipeline: .nova → LLVM IR → Assembly → Native binary
./novac examples/showcase.nova -o showcase
./showcase
```

---

## All Compiler Commands

```bash
# Compile to native binary (full pipeline)
./novac examples/showcase.nova -o myprogram
./myprogram

# Emit LLVM IR only (most useful for learning/assignment)
./novac --emit-ir examples/showcase.nova -o output.ll
cat output.ll

# Emit assembly only
./novac --emit-asm examples/showcase.nova -o output.s
cat output.s

# Dump the full AST
./novac --ast tests/fib.nova

# Dump the token stream
./novac --tokens tests/hello.nova

# Verbose pipeline (shows each stage)
./novac -v examples/showcase.nova -o prog

# With optimisation
./novac -O2 examples/showcase.nova -o prog_fast

# No colour (for CI/logs)
./novac --no-color examples/showcase.nova
```

---

## VS Code Tasks (Ctrl+Shift+B)

The `.vscode/tasks.json` provides these ready-to-run tasks:

| Task | What it does |
|------|-------------|
| **Build Nova Compiler** | Runs `make` |
| **Run Tests** | Runs `make test` |
| **Emit IR (active file)** | Generates .ll for the open .nova file |
| **Compile and Run (active file)** | Full pipeline + executes binary |
| **Dump AST (active file)** | Prints AST tree |
| **Dump Tokens (active file)** | Prints token stream |

Press **Ctrl+Shift+B** to build, **Ctrl+Shift+P → Tasks: Run Task** for the rest.

---

## Debugging in VS Code

1. Open `src/main.c`
2. Set a breakpoint anywhere
3. Press **F5** (uses `.vscode/launch.json`)
4. The debugger will stop at your breakpoint

---

## Write your own Nova programs

Create `myprogram.nova`:

```nova
extern fn printf(fmt: str, val: i64) -> i32;
extern fn puts(s: str) -> i32;

fn factorial(n: i64) -> i64 {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

fn main() -> i32 {
    for i in 1..11 {
        let f: i64 = factorial(i);
        printf("%lld! = %lld\n", i, f);
    }
    return 0;
}
```

Compile and run:
```bash
./novac myprogram.nova -o myprogram
./myprogram
```

---

## Project Structure

```
nova-lang-clean/
├── include/          ← All header files (read these to understand the design)
│   ├── lexer.h       ← Token types, Lexer API
│   ├── ast.h         ← Every AST node type + Arena allocator
│   ├── parser.h      ← Parser state
│   ├── sema.h        ← Semantic analyser, Scope, Symbol table
│   ├── codegen.h     ← LLVM IR code generator
│   └── diag.h        ← Error reporting
├── src/              ← Implementation files
│   ├── diag.c        ← Coloured diagnostics
│   ├── ast.c         ← Arena allocator + AST printer
│   ├── lexer.c       ← Tokeniser (handles hex, binary, strings, escapes)
│   ├── parser.c      ← Recursive-descent parser
│   ├── sema.c        ← Two-pass type checker + name resolver
│   ├── codegen.c     ← LLVM IR emitter
│   └── main.c        ← Compiler driver (argument parsing, pipeline)
├── tests/            ← 7 .nova test programs
├── examples/         ← showcase.nova (comprehensive demo)
├── scripts/          ← setup.sh automated installer
├── .vscode/          ← VS Code tasks, launch, IntelliSense config
├── Makefile
└── README.md
```

---

## Compiler Pipeline (what happens when you run novac)

```
source.nova
    │
    ▼ Lexer (lexer.c)
    │  Converts source text → stream of Token structs
    │  Handles: identifiers, keywords, integer/float/hex/binary literals,
    │           string escapes, line/block comments, all operators
    │
    ▼ Parser (parser.c)
    │  Recursive-descent + Pratt precedence climbing
    │  Produces: ASTNode tree (arena-allocated, zero heap fragmentation)
    │
    ▼ Semantic Analyser (sema.c)
    │  Pass 1: Register all top-level functions/structs/enums
    │  Pass 2: Type-check all function bodies
    │  Resolves: names, types, checks return types, arg counts
    │
    ▼ Code Generator (codegen.c)
    │  Walks annotated AST, emits textual LLVM IR (.ll)
    │  SSA form with alloca/load/store pattern
    │  Handles: arithmetic, comparisons, calls, loops, if/else,
    │           for-range, casts, string literals
    │
    ▼ llc (LLVM backend)
    │  Converts LLVM IR → native assembly (.s)
    │
    ▼ clang/gcc (linker)
       Converts assembly → native ELF binary
```
