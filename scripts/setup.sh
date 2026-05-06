#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  Nova DSL Compiler — Full Setup & Test Script
#  Run this once after cloning to verify everything works.
# ─────────────────────────────────────────────────────────────────────────────

set -e

RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[0;33m'
CYN='\033[0;36m'
BLD='\033[1m'
RST='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

echo -e "${CYN}${BLD}"
echo "  ╔══════════════════════════════════════╗"
echo "  ║   Nova DSL Compiler — Setup Script   ║"
echo "  ╚══════════════════════════════════════╝"
echo -e "${RST}"

# ── Step 1: Check prerequisites ───────────────────────────────────────────────
echo -e "${BLD}[1/5] Checking prerequisites...${RST}"

check_tool() {
    if command -v "$1" &>/dev/null; then
        echo -e "  ${GRN}✓${RST} $1 found ($(command -v $1))"
    else
        echo -e "  ${RED}✗${RST} $1 NOT found"
        echo -e "  ${YLW}hint:${RST} $2"
        MISSING=1
    fi
}

MISSING=0
check_tool gcc   "sudo apt install gcc"
check_tool make  "sudo apt install make"
check_tool llc   "sudo apt install llvm"
check_tool clang "sudo apt install clang"

if [ "$MISSING" = "1" ]; then
    echo ""
    echo -e "${RED}Some tools are missing. Install them and re-run this script.${RST}"
    echo -e "On Ubuntu/Debian: ${YLW}sudo apt install gcc make llvm clang${RST}"
    exit 1
fi
echo ""

# ── Step 2: Build ─────────────────────────────────────────────────────────────
echo -e "${BLD}[2/5] Building Nova compiler...${RST}"
make clean >/dev/null 2>&1 || true
make 2>&1 | sed 's/^/  /'
echo ""

if [ ! -f "./novac" ]; then
    echo -e "${RED}Build failed! Check errors above.${RST}"
    exit 1
fi
echo -e "  ${GRN}✓${RST} novac binary ready"
echo ""

# ── Step 3: Token dump ────────────────────────────────────────────────────────
echo -e "${BLD}[3/5] Token dump of hello.nova...${RST}"
./novac --tokens tests/hello.nova | head -20 | sed 's/^/  /'
echo ""

# ── Step 4: AST dump ──────────────────────────────────────────────────────────
echo -e "${BLD}[4/5] AST dump of hello.nova...${RST}"
./novac --ast tests/hello.nova | sed 's/^/  /'
echo ""

# ── Step 5: IR generation test ────────────────────────────────────────────────
echo -e "${BLD}[5/5] Generating LLVM IR for all test files...${RST}"
PASS=0
FAIL=0
for f in tests/*.nova; do
    name=$(basename "$f" .nova)
    printf "  %-20s " "$name"
    if ./novac --emit-ir -o "tests/${name}.ll" "$f" 2>/dev/null; then
        echo -e "${GRN}✓ PASS${RST}"
        PASS=$((PASS+1))
    else
        echo -e "${RED}✗ FAIL${RST}"
        FAIL=$((FAIL+1))
    fi
done
echo ""
echo -e "  Tests: ${GRN}${PASS} passed${RST}  ${RED}${FAIL} failed${RST}"
echo ""

# ── End-to-end compile if llc is available ────────────────────────────────────
echo -e "${BLD}[BONUS] End-to-end compilation of showcase.nova...${RST}"
if ./novac -v examples/showcase.nova -o out_showcase 2>&1 | sed 's/^/  /'; then
    echo ""
    echo -e "  ${GRN}✓${RST} Binary compiled! Running it:"
    echo -e "  ${CYN}─────────────────────────────────────────${RST}"
    ./out_showcase | sed 's/^/  /'
    echo -e "  ${CYN}─────────────────────────────────────────${RST}"
    rm -f out_showcase
else
    echo -e "  ${YLW}(end-to-end skipped — llc or linker unavailable)${RST}"
fi

echo ""
echo -e "${GRN}${BLD}✓ Nova compiler setup complete!${RST}"
echo ""
echo "Usage examples:"
echo "  ./novac examples/showcase.nova -o prog && ./prog"
echo "  ./novac --emit-ir examples/showcase.nova -o out.ll"
echo "  ./novac --ast tests/fib.nova"
echo "  ./novac --tokens tests/arith.nova"
echo "  make test"
echo ""
