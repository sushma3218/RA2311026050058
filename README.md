# 🔷 Nova DSL Compiler

> **A complete, production-quality compiler for a custom Domain-Specific Language — written entirely in C, targeting native binaries via LLVM.**

[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![Language](https://img.shields.io/badge/language-C11-blue)](#)
[![Backend](https://img.shields.io/badge/backend-LLVM-orange)](#)

---

## Architecture Overview

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   Source     │    │    Lexer    │    │   Parser    │    │   Sema      │    │  CodeGen    │
│  .nova file  │───▶│  lexer.c   │───▶│  parser.c  │───▶│  sema.c    │───▶│ codegen.c   │
│             │    │  Token stream│    │  AST nodes  │    │  Type check │    │  LLVM IR    │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
                                                                                     │
                                                                              ┌──────▼──────┐
                                                                              │    llc      │
                                                                              │  Assembly   │
                                                                              └──────┬──────┘
                                                                                     │
                                                                              ┌──────▼──────┐
                                                                              │ clang/gcc   │
                                                                              │ Native EXE  │
                                                                              └─────────────┘
```

## Pipeline Stages

| Stage | File | Description |
|-------|------|-------------|
| **Lexer** | `src/lexer.c` | Tokenises source into a stream of `Token` structs. Handles hex/binary literals, escape sequences, block comments, keyword recognition. |
| **Parser** | `src/parser.c` | Recursive-descent parser building a typed AST. Implements Pratt/precedence-climbing for expressions. |
| **AST** | `src/ast.c` | All node types + arena allocator. Zero heap fragmentation — entire compilation uses a single arena. |
| **Sema** | `src/sema.c` | Two-pass semantic analyser: first pass registers all top-level symbols; second pass type-checks all function bodies. Hash-table scopes. |
| **CodeGen** | `src/codegen.c` | Walks annotated AST and emits textual LLVM IR ready for `llc`. Handles SSA registers, stack allocation, control flow, type coercion. |
| **Diag** | `src/diag.c` | Colour-coded, location-aware error/warning messages. Auto-disables colour when not a TTY. |

---

## Nova Language Reference

### Types

| Nova   | LLVM IR  | Description             |
|--------|----------|-------------------------|
| `i8`   | `i8`     | 8-bit signed integer    |
| `i16`  | `i16`    | 16-bit signed integer   |
| `i32`  | `i32`    | 32-bit signed integer   |
| `i64`  | `i64`    | 64-bit signed integer   |
| `u8`   | `i8`     | 8-bit unsigned integer  |
| `u32`  | `i32`    | 32-bit unsigned integer |
| `u64`  | `i64`    | 64-bit unsigned integer |
| `f32`  | `float`  | 32-bit float            |
| `f64`  | `double` | 64-bit float            |
| `bool` | `i1`     | Boolean                 |
| `str`  | `i8*`    | String literal pointer  |
| `void` | `void`   | No value                |
| `*T`   | `T*`     | Pointer to T            |
| `[N]T` | `[N x T]`| Fixed-size array        |

### Syntax at a Glance

```nova
// Functions
fn add(a: i64, b: i64) -> i64 {
    return a + b;
}

// Variables
let x: i64 = 42;
let mut counter: i64 = 0;

// Control flow
if x > 0 {
    puts("positive");
} else if x == 0 {
    puts("zero");
} else {
    puts("negative");
}

// Loops
while counter < 10 {
    counter = counter + 1;
}

for i in 0..100 {
    // i goes from 0 to 99
}

// Extern functions (call into libc)
extern fn printf(fmt: str, val: i64) -> i32;

// Structs
struct Point {
    x: i64,
    y: i64,
}

// Enums
enum Color {
    Red,
    Green,
    Blue,
}

// Type casting
let f: f64 = 3;
let n: i64 = f as i64;
```

### Operators

| Category   | Operators                          |
|------------|------------------------------------|
| Arithmetic | `+ - * / %`                        |
| Bitwise    | `& \| ^ ~ << >>`                   |
| Logical    | `&& \|\| !`                        |
| Comparison | `== != < > <= >=`                  |
| Assignment | `= += -= *= /=`                    |
| Other      | `as` (cast), `..` (range), `->` (return type) |

---

 

```powershell
# Dump AST (Abstract Syntax Tree)
.\nova_compiler.exe --ast examples\showcase.nova

# Dump Token Stream
.\nova_compiler.exe --tokens examples\showcase.nova

# With optimization (via Clang)
clang -O2 showcase.ll -o showcase_fast.exe
```

## VS Code Setup

Install these extensions:
- **C/C++** (Microsoft) — IntelliSense, debugging
- **clangd** — Code completion
- **CodeLLDB** — LLDB debugger integration

Build & debug config is in `.vscode/` (included in the repo).

---

## Project Structure

```
nova-lang/
├── include/          # Public headers
│   ├── lexer.h       # Token types, Lexer state
│   ├── ast.h         # ASTNode, Type, Arena
│   ├── parser.h      # Parser state
│   ├── sema.h        # Sema, Scope, Symbol
│   ├── codegen.h     # CodeGen state
│   └── diag.h        # Diagnostic helpers
├── src/              # Implementation
│   ├── lexer.c
│   ├── ast.c
│   ├── parser.c
│   ├── sema.c
│   ├── codegen.c
│   ├── diag.c
│   └── main.c        # Compiler driver
├── tests/            # .nova test programs
├── examples/         # Showcase programs
├── docs/             # Extended documentation
├── scripts/          # Helper scripts
├── Makefile
└── README.md
```
## Steps to Test

### Using VS Code (Recommended)
1. Open any `.nova` file (e.g., `sort.nova` or `simple_math.nova`).
2. Press **`Ctrl + Shift + B`** to open the Build Tasks menu.
3. Select **"Compile and Run (active file)"**.
4. The terminal will automatically compile the code to LLVM IR, link it with any necessary helpers, and execute the resulting binary.

*Alternatively:*
- Press **`Ctrl + Shift + P`** to open the Command Palette.
- Type **"Run Task"** and select it.
- Choose **"Compile and Run (active file)"**.

### Using the Terminal (PowerShell)
If you prefer running commands manually in the terminal:

1. **Compile the Nova file to LLVM IR:**
   ```powershell
   .\nova_compiler.exe --emit-ir your_file.nova -o your_file.ll
   ```
2. **Compile the IR to an EXE:**
   ```powershell
   # Standard:
   clang your_file.ll -o your_file.exe
   
   # With helpers (required for sort.nova):
   clang your_file.ll helpers.c -o your_file.exe
   ```
3. **Run the EXE:**
   ```powershell
   .\your_file.exe
   ```

---


