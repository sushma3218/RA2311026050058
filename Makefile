# ─────────────────────────────────────────────────────────────────────────────
#  Nova DSL Compiler — Makefile
# ─────────────────────────────────────────────────────────────────────────────
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wno-unused-function -Iinclude -g
RFLAGS  := -std=c11 -O2 -DNDEBUG -Iinclude
SRCS    := src/diag.c src/ast.c src/lexer.c src/parser.c \
           src/sema.c src/codegen.c src/main.c
TARGET  := novac
PREFIX  ?= /usr/local

.PHONY: all clean test release install run

all:
	@echo "  Building Nova compiler..."
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)
	@echo "  ✓  Built: ./$(TARGET)"

release:
	$(CC) $(RFLAGS) $(SRCS) -o $(TARGET)
	@echo "  ✓  Release build: ./$(TARGET)"

clean:
	@rm -f $(TARGET) tests/*.ll tests/*.s *.ll *.s *.o a.out out_*
	@echo "  ✓  Cleaned"

install: all
	@cp $(TARGET) $(PREFIX)/bin/$(TARGET)
	@echo "  ✓  Installed to $(PREFIX)/bin/$(TARGET)"

TESTS := tests/hello.nova tests/arith.nova tests/control.nova \
         tests/fib.nova tests/structs.nova tests/loops.nova tests/strings.nova

test: all
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  Nova Compiler — Test Suite"
	@echo "══════════════════════════════════════════"
	@PASS=0; FAIL=0; \
	for f in $(TESTS); do \
	    name=$$(basename $$f .nova); \
	    printf "  %-20s " "$$name"; \
	    if ./$(TARGET) --emit-ir -o tests/$$name.ll $$f 2>/dev/null; then \
	        echo "\033[32mPASS\033[0m"; PASS=$$((PASS+1)); \
	    else \
	        echo "\033[31mFAIL\033[0m"; FAIL=$$((FAIL+1)); \
	    fi; \
	done; \
	echo ""; \
	echo "  Passed: $$PASS  Failed: $$FAIL"; \
	echo "══════════════════════════════════════════"

# make run FILE=examples/showcase.nova
run: all
	@test -n "$(FILE)" || (echo "Usage: make run FILE=<path.nova>"; exit 1)
	./$(TARGET) -v -o out_prog $(FILE)
	@echo ""; echo "── Running: ./out_prog ──"
	@./out_prog
