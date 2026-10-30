#!/bin/bash
set -e

# ==============================================================================
# Configuration
# ==============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MYLO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Path to the Amiga m68k cross-compiler
CC="${AMIGA_CC:-$HOME/Downloads/m68k-amigaos-gcc/bin/m68k-amigaos-gcc}"

echo "======================================================================"
echo " Building Mylo with Pixel-Window for AmigaOS (68040 / PiStorm)"
echo "======================================================================"

# Highly optimized PiStorm RTG compilation flags
# -m68040: Target 68040 instruction set (optimal for PiStorm JIT / Emu68)
# -mcrt=clib2: Modern C standard library for AmigaOS
# -fomit-frame-pointer: Free up address registers for speed
# -O3: Maximum optimization for inner pixel rendering loops
CFLAGS="-O3 -m68040 -fomit-frame-pointer -mcrt=clib2"
INCLUDES="-I$MYLO_ROOT/src -I$SCRIPT_DIR"
LIBS="-lm"

OUTPUT_BINARY="$MYLO_ROOT/mylo_amiga"

# Core Mylo source files + Pixel-Window implementation
SOURCES=(
    "$MYLO_ROOT/src/main.c"
    "$MYLO_ROOT/src/compiler.c"
    "$MYLO_ROOT/src/vm.c"
    "$MYLO_ROOT/src/utils.c"
    "$MYLO_ROOT/src/mylolib.c"
    "$MYLO_ROOT/src/debug_adapter.c"
    "$MYLO_ROOT/src/amiga_setjmp.s"
    "$SCRIPT_DIR/pixel_window_impl.c"
)

echo "[INFO] Compiling Mylo executable..."
echo "       Compiler : $CC"
echo "       Flags    : $CFLAGS"
echo "       Output   : $OUTPUT_BINARY"

if [ ! -f "$CC" ]; then
    echo "[WARNING] Compiler '$CC' not found."
    echo "          Set AMIGA_CC environment variable to your m68k-amigaos-gcc path."
    echo "          Example: export AMIGA_CC=/opt/amiga/bin/m68k-amigaos-gcc"
    exit 1
fi

"$CC" $CFLAGS $INCLUDES "${SOURCES[@]}" -o "$OUTPUT_BINARY" $LIBS

echo "======================================================================"
echo "[SUCCESS] Build completed: $OUTPUT_BINARY"
echo "======================================================================"
