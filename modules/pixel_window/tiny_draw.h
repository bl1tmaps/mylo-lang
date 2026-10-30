//
// Created by Brad on 18/10/2026.
//

#ifndef PIXELWINDOWEXAMPLE_TINY_DRAW_H
#define PIXELWINDOWEXAMPLE_TINY_DRAW_H

/* TinyDraw
 * Minimalist, single-header C graphics library for microcontrollers and pixel buffers.
 *
 * USAGE:
 * In EXACTLY ONE source file, define the implementation macro before including:
 *   #define TINY_DRAW_IMPLEMENTATION
 *   #include "tiny_draw.h"
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Safely plots a single pixel to the RGB buffer.
void td_draw_pixel(uint8_t* buffer, int buf_width, int buf_height, int x, int y, uint8_t r, uint8_t g, uint8_t b);

// Draws a line using Bresenham's algorithm. Thickness > 1 uses a square-brush technique.
void td_draw_line(uint8_t* buffer, int buf_width, int buf_height, int x0, int y0, int x1, int y1, int thickness, uint8_t r, uint8_t g, uint8_t b);

// Draws a rectangle. If not filled, uses lines for the border based on thickness.
void td_draw_rect(uint8_t* buffer, int buf_width, int buf_height, int x, int y, int w, int h, int thickness, bool filled, uint8_t r, uint8_t g, uint8_t b);

// Draws a circle. Filled and thick circles use a distance-squared bounding box (fast on MCUs).
void td_draw_circle(uint8_t* buffer, int buf_width, int buf_height, int cx, int cy, int radius, int thickness, bool filled, uint8_t r, uint8_t g, uint8_t b);

// Draws a triangle. Filled triangles use integer barycentric coordinates.
void td_draw_triangle(uint8_t* buffer, int buf_width, int buf_height, int x1, int y1, int x2, int y2, int x3, int y3, int thickness, bool filled, uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif // TINY_DRAW_H

// ============================================================================
// IMPLEMENTATION
// ============================================================================

#ifdef TINY_DRAW_IMPLEMENTATION

#include <stdlib.h> // for abs()

// Internal macros to avoid namespace pollution
#define TD__MIN(a, b) ((a) < (b) ? (a) : (b))
#define TD__MAX(a, b) ((a) > (b) ? (a) : (b))

void td_draw_pixel(uint8_t* buffer, int buf_width, int buf_height, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= buf_width || y < 0 || y >= buf_height) return;
    int idx = (y * buf_width + x) * 3;
    buffer[idx] = r;
    buffer[idx + 1] = g;
    buffer[idx + 2] = b;
}

void td_draw_line(uint8_t* buffer, int buf_width, int buf_height, int x0, int y0, int x1, int y1, int thickness, uint8_t r, uint8_t g, uint8_t b) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    int half_t = thickness / 2;

    while (1) {
        if (thickness <= 1) {
            td_draw_pixel(buffer, buf_width, buf_height, x0, y0, r, g, b);
        } else {
            // Square brush for thickness
            for (int ty = -half_t; ty <= half_t; ty++) {
                for (int tx = -half_t; tx <= half_t; tx++) {
                    td_draw_pixel(buffer, buf_width, buf_height, x0 + tx, y0 + ty, r, g, b);
                }
            }
        }

        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void td_draw_rect(uint8_t* buffer, int buf_width, int buf_height, int x, int y, int w, int h, int thickness, bool filled, uint8_t r, uint8_t g, uint8_t b) {
    if (filled) {
        for (int py = y; py < y + h; py++) {
            for (int px = x; px < x + w; px++) {
                td_draw_pixel(buffer, buf_width, buf_height, px, py, r, g, b);
            }
        }
    } else {
        int x2 = x + w - 1;
        int y2 = y + h - 1;
        td_draw_line(buffer, buf_width, buf_height, x, y, x2, y, thickness, r, g, b);
        td_draw_line(buffer, buf_width, buf_height, x2, y, x2, y2, thickness, r, g, b);
        td_draw_line(buffer, buf_width, buf_height, x2, y2, x, y2, thickness, r, g, b);
        td_draw_line(buffer, buf_width, buf_height, x, y2, x, y, thickness, r, g, b);
    }
}

void td_draw_circle(uint8_t* buffer, int buf_width, int buf_height, int cx, int cy, int radius, int thickness, bool filled, uint8_t r, uint8_t g, uint8_t b) {
    if (filled || thickness > 1) {
        // Distance-squared bounding box approach (Very compact and integer-only)
        int r_out = radius + thickness / 2;
        int r_in = filled ? 0 : TD__MAX(0, radius - thickness / 2);

        int r_out_sq = r_out * r_out;
        int r_in_sq = r_in * r_in;

        for (int y = cy - r_out; y <= cy + r_out; y++) {
            for (int x = cx - r_out; x <= cx + r_out; x++) {
                int dx = x - cx;
                int dy = y - cy;
                int dist_sq = dx * dx + dy * dy;
                if (dist_sq <= r_out_sq && dist_sq >= r_in_sq) {
                    td_draw_pixel(buffer, buf_width, buf_height, x, y, r, g, b);
                }
            }
        }
    } else {
        // Standard Midpoint Circle Algorithm for pure 1px outlines
        int x = radius;
        int y = 0;
        int err = 0;

        while (x >= y) {
            td_draw_pixel(buffer, buf_width, buf_height, cx + x, cy + y, r, g, b);
            td_draw_pixel(buffer, buf_width, buf_height, cx + y, cy + x, r, g, b);
            td_draw_pixel(buffer, buf_width, buf_height, cx - y, cy + x, r, g, b);
            td_draw_pixel(buffer, buf_width, buf_height, cx - x, cy + y, r, g, b);
            td_draw_pixel(buffer, buf_width, buf_height, cx - x, cy - y, r, g, b);
            td_draw_pixel(buffer, buf_width, buf_height, cx - y, cy - x, r, g, b);
            td_draw_pixel(buffer, buf_width, buf_height, cx + y, cy - x, r, g, b);
            td_draw_pixel(buffer, buf_width, buf_height, cx + x, cy - y, r, g, b);

            if (err <= 0) {
                y += 1;
                err += 2 * y + 1;
            }
            if (err > 0) {
                x -= 1;
                err -= 2 * x + 1;
            }
        }
    }
}

void td_draw_triangle(uint8_t* buffer, int buf_width, int buf_height, int x1, int y1, int x2, int y2, int x3, int y3, int thickness, bool filled, uint8_t r, uint8_t g, uint8_t b) {
    if (filled) {
        // Integer Barycentric bounding box
        int minX = TD__MIN(x1, TD__MIN(x2, x3));
        int maxX = TD__MAX(x1, TD__MAX(x2, x3));
        int minY = TD__MIN(y1, TD__MIN(y2, y3));
        int maxY = TD__MAX(y1, TD__MAX(y2, y3));

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                int w0 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
                int w1 = (x3 - x2) * (y - y2) - (y3 - y2) * (x - x2);
                int w2 = (x1 - x3) * (y - y3) - (y1 - y3) * (x - x3);

                // Point is inside if all weights share the same sign
                if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                    td_draw_pixel(buffer, buf_width, buf_height, x, y, r, g, b);
                }
            }
        }
    } else {
        td_draw_line(buffer, buf_width, buf_height, x1, y1, x2, y2, thickness, r, g, b);
        td_draw_line(buffer, buf_width, buf_height, x2, y2, x3, y3, thickness, r, g, b);
        td_draw_line(buffer, buf_width, buf_height, x3, y3, x1, y1, thickness, r, g, b);
    }
}

#endif //TINY_DRAW_IMPLEMENTATION
