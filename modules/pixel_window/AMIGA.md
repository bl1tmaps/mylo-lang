# Building and Running Mylo on AmigaOS (m68k / PiStorm) with Pixel-Window

This guide details how to build the core **Mylo** executable with integrated **Pixel-Window**, **TinyDraw**, and **TinyGlyph** graphics support for AmigaOS 3.x using the `m68k-amigaos-gcc` cross-compiler.

---

## 1. Overview & Architecture

### Target System
* **Architecture**: Motorola 68000 family (`-m68040` recommended for PiStorm / Emu68).
* **Operating System**: AmigaOS 3.1 / 3.2 / 3.9 with RTG (CyberGraphics v39+ or Picasso96).
* **C Runtime**: `clib2` (recommended for full modern C99/C11 standard library support).

### Why Pixel-Window on AmigaOS?
Pixel-Window is a minimal, header-only C library with native AmigaOS support. On AmigaOS, it:
1. Automatically opens `intuition.library` (v39+) and either `cybergraphics.library` or `Picasso96API.library` (Picasso96 / P96).
2. Creates an Intuition window on the Workbench or RTG screen.
3. Maps raw Amiga keyboard and mouse events to unified keycodes and normalized coordinates.
4. Blits a standard 24-bit RGB packed pixel buffer directly to the screen via hardware-accelerated RTG `WritePixelArray`.
5. Pairs with **TinyDraw** (fast integer Bresenham lines, bounding-box circles, barycentric triangles, rectangles) and **TinyGlyph** (blazing 8x8 bitmap font sprite rendering) with zero external dependencies.

By compiling Pixel-Window directly into the core `mylo` binary, all graphics symbols (`pw_*`, `td_*`, `draw_string_rgb`) become resident. When Mylo scripts run in interpreted mode, Mylo's dynamic `cfn` loader resolves all functions instantly from the host process!

---

## 2. Cross-Compiler Setup

You can use the `m68k-amigaos-gcc` cross-compilation suite (e.g. from `amiga-gcc` / `bebbo` toolchain or prebuilt downloads).

Ensure your cross-compiler binaries are located in your environment or set in the script:
```bash
# Path to your m68k-amigaos-gcc binaries
export AMIGA_GCC="$HOME/Downloads/m68k-amigaos-gcc/bin/m68k-amigaos-gcc"
```

Verify your compiler:
```bash
$AMIGA_GCC -v
```

---

## 3. Build Script: `build_amiga.sh`

Here is the complete script to compile the core Mylo interpreter with embedded Pixel-Window, TinyDraw, and TinyGlyph:

```bash
#!/bin/bash
set -e

# ==============================================================================
# Configuration
# ==============================================================================
# Path to the cross-compiler
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
INCLUDES="-Isrc -Imodules/pixel_window"
LIBS="-lm"

OUTPUT_BINARY="mylo_amiga"

# Core Mylo source files + Pixel-Window implementation
SOURCES=(
    src/main.c
    src/compiler.c
    src/vm.c
    src/utils.c
    src/mylolib.c
    src/debug_adapter.c
    modules/pixel_window/pixel_window_impl.c
)

echo "[INFO] Compiling Mylo executable..."
echo "       Compiler : $CC"
echo "       Flags    : $CFLAGS"
echo "       Output   : $OUTPUT_BINARY"

"$CC" $CFLAGS $INCLUDES "${SOURCES[@]}" -o "$OUTPUT_BINARY" $LIBS

echo "======================================================================"
echo "[SUCCESS] Build complete: $OUTPUT_BINARY"
echo "======================================================================"
```

Make the script executable and build:
```bash
chmod +x build_amiga.sh
./build_amiga.sh
```

---

## 4. Stack Space Considerations on AmigaOS

AmigaOS default shell stack (typically 4 KB) is too small for compiler and interpreter execution.
`pixel_window.h` automatically embeds a 128 KB cookie:
```c
static const char __attribute__((used)) min_stack[] = "$STACK:131072";
```
When launching from the Amiga Shell, it is still recommended to run:
```amiga
stack 200000
```
before launching intensive Mylo scripts.

---

## 5. Pixel-Window, TinyDraw & TinyGlyph API in Mylo

All functions are available via the `PW` module ([modules/pixel_window/pixel_window.mylo](file:///Users/brad/repos/mylo-lang/modules/pixel_window/pixel_window.mylo)).

### Window Management
* `PW::create_window(width: i32, height: i32, title: str, resizable: bool) -> i32`
* `PW::process_events(win: i32) -> bool`: Pumps Intuition message port, updates keys and mouse. Returns `false` when window is closed.
* `PW::update_window(win: i32, buffer: u8[])`: Blits the 24-bit RGB buffer to the Amiga RTG window.
* `PW::destroy_window(win: i32)`: Closes window and frees resources.

### Framebuffer Management
* `PW::create_buffer(width: i32, height: i32) -> u8[]`: Allocates packed byte buffer (`width * height * 3`).
* `PW::clear_buffer(buffer: u8[], width: i32, height: i32, r: u8, g: u8, b: u8)`: Fast fill.

### Shape Drawing (`tiny_draw`)
* `PW::draw_pixel(buffer, w, h, x, y, r, g, b)`
* `PW::draw_line(buffer, w, h, x0, y0, x1, y1, thickness, r, g, b)`: Fast Bresenham algorithm with square-brush thickness.
* `PW::draw_rect(buffer, w, h, x, y, rw, rh, thickness, filled, r, g, b)`
* `PW::draw_circle(buffer, w, h, cx, cy, radius, thickness, filled, r, g, b)`: MCU/CPU-optimized distance-squared bounding box.
* `PW::draw_triangle(buffer, w, h, x1, y1, x2, y2, x3, y3, thickness, filled, r, g, b)`: Barycentric filled triangles.

### Text Rendering (`tiny_glyph`)
* `PW::draw_text(buffer, w, h, text: str, x: i32, y: i32, size: i32, r: u8, g: u8, b: u8)`:
  * `size = 8`: 1x scale (8x8 pixels per glyph).
  * `size = 16`: 2x scale (16x16 pixels per glyph).
  * `size = 24`: 3x scale (24x24 pixels per glyph).

### Input Handling
* `PW::get_mouse_x(win: i32) -> f32`: Normalized horizontal mouse coordinate (`0.0` to `1.0`).
* `PW::get_mouse_y(win: i32) -> f32`: Normalized vertical mouse coordinate (`0.0` to `1.0`).
* `PW::is_mouse_down(win: i32, button: i32) -> bool`: `0` = Left, `1` = Right, `2` = Middle.
* `PW::is_mouse_pressed(win: i32, button: i32) -> bool`
* `PW::is_key_down(win: i32, key: i32) -> bool`
* `PW::is_key_pressed(win: i32, key: i32) -> bool`: Keycodes like `PW::KEY_ESCAPE`, `PW::KEY_SPACE`, `PW::KEY_LEFT`, etc.

---

## 6. Complete Example: `amiga_demo.mylo`

Create this file on your Amiga or copy it across:

```mylo
import native "pixel_window.mylo"

// 1. Setup window (e.g. 640x480 RTG)
width: i32 = 640
height: i32 = 480

win: i32 = PW::create_window(width, height, "Mylo AmigaOS + PiStorm Demo", false)
buffer: u8[] = PW::create_buffer(width, height)

frame: i32 = 0

print("Starting main loop. Press ESC or close gadget to exit.")

forever {
    // Process Intuition events
    if (PW::process_events(win) == false) {
        break
    }

    // Exit on ESC
    if (PW::is_key_pressed(win, PW::KEY_ESCAPE)) {
        break
    }

    // Clear background
    PW::clear_buffer(buffer, width, height, 15, 20, 30)

    // Decorative Borders
    PW::draw_line(buffer, width, height, 10, 10, width - 10, 10, 2, 60, 120, 200)
    PW::draw_line(buffer, width, height, 10, height - 10, width - 10, height - 10, 2, 60, 120, 200)

    // Animated Circle
    cx: i32 = 120 + ((frame * 2) % (width - 240))
    PW::draw_circle(buffer, width, height, cx, 160, 35, 2, true, 255, 140, 20)
    PW::draw_circle(buffer, width, height, cx, 160, 42, 2, false, 255, 255, 255)

    // Shapes via TinyDraw
    PW::draw_rect(buffer, width, height, 380, 100, 160, 100, 2, true, 40, 160, 220)
    PW::draw_rect(buffer, width, height, 375, 95, 170, 110, 2, false, 255, 255, 255)
    PW::draw_triangle(buffer, width, height, 280, 260, 200, 380, 360, 380, 3, true, 50, 200, 100)

    // Text rendering via TinyGlyph
    PW::draw_text(buffer, width, height, "MYLO AMIGA 68K", 25, 30, 16, 255, 255, 255)
    PW::draw_text(buffer, width, height, "PixelWindow + TinyDraw + TinyGlyph", 25, 60, 8, 160, 190, 220)

    // Mouse tracking
    mx: f32 = PW::get_mouse_x(win)
    my: f32 = PW::get_mouse_y(win)
    m_px: i32 = mx * width
    m_py: i32 = my * height

    // Draw mouse pointer rectangle (Red when pressed, Green when hovering)
    mouse_click: bool = PW::is_mouse_down(win, 0)
    if (mouse_click) {
        PW::draw_rect(buffer, width, height, m_px - 10, m_py - 10, 20, 20, 2, true, 255, 40, 40)
    } else {
        PW::draw_rect(buffer, width, height, m_px - 10, m_py - 10, 20, 20, 2, false, 40, 255, 100)
    }

    PW::draw_text(buffer, width, height, f"Mouse: {m_px}, {m_py} | Frame: {frame}", 25, height - 35, 8, 200, 200, 200)

    // Push buffer to RTG screen
    PW::update_window(win, buffer)
    frame = frame + 1
}

PW::destroy_window(win)
print("Demo finished.")
```

---

## 7. Running on the Amiga

### Running Interpreted:
Transfer the compiled `mylo_amiga` binary and `amiga_demo.mylo` to your Amiga (via PiStorm shared drive, FTP, or SD card):
```amiga
1> stack 200000
1> mylo_amiga amiga_demo.mylo
```
Because `pw_*` and `td_*` symbols are resident in `mylo_amiga`, the interpreter dynamically binds them and starts rendering immediately!

### Compiling to a Standalone Amiga Executable (Ahead-Of-Time):
You can also generate an optimized C source file:
```bash
./mylo --build amiga_demo.mylo
```
This produces `out.c`. Then compile `out.c` directly with `m68k-amigaos-gcc`:
```bash
$AMIGA_GCC -O3 -m68040 -fomit-frame-pointer -mcrt=clib2 out.c \
    src/vm.c src/mylolib.c src/utils.c src/debug_adapter.c \
    modules/pixel_window/pixel_window_impl.c \
    -Isrc -Imodules/pixel_window -o demo_native -lm
```
`demo_native` is a 100% standalone native AmigaOS executable that runs directly on your Amiga without needing any runtime!
