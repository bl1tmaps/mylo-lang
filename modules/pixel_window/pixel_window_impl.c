#define PIXEL_WINDOW_IMPLEMENTATION
#include "pixel_window.h"

#define TINY_DRAW_IMPLEMENTATION
#include "tiny_draw.h"

#include "tiny_glyph.h"

#ifdef __cplusplus
extern "C" {
#endif

float pw_get_mouse_x(PixelWindow* win) {
    if (!win) return 0.0f;
    const PwInputState* in = pw_get_input(win);
    return in ? in->mouse_x : 0.0f;
}

float pw_get_mouse_y(PixelWindow* win) {
    if (!win) return 0.0f;
    const PwInputState* in = pw_get_input(win);
    return in ? in->mouse_y : 0.0f;
}

bool pw_is_mouse_down(PixelWindow* win, int button) {
    if (!win || button < 0 || button >= 3) return false;
    const PwInputState* in = pw_get_input(win);
    return in ? in->mouse_down[button] : false;
}

bool pw_is_mouse_pressed(PixelWindow* win, int button) {
    if (!win || button < 0 || button >= 3) return false;
    const PwInputState* in = pw_get_input(win);
    return in ? in->mouse_pressed[button] : false;
}

bool pw_is_key_down(PixelWindow* win, int key) {
    if (!win || key < 0 || key >= 512) return false;
    const PwInputState* in = pw_get_input(win);
    return in ? in->keys_down[key] : false;
}

bool pw_is_key_pressed(PixelWindow* win, int key) {
    if (!win || key < 0 || key >= 512) return false;
    const PwInputState* in = pw_get_input(win);
    return in ? in->keys_pressed[key] : false;
}

static PixelWindow* s_pw_window_table[64] = {0};

static PixelWindow* pw_resolve_handle(int handle) {
    if (handle > 0 && handle < 64 && s_pw_window_table[handle]) {
        return s_pw_window_table[handle];
    }
    return (PixelWindow*)(intptr_t)handle;
}

int pw_create_window_id(int width, int height, const char* title, bool resizable) {
    PixelWindow* win = pw_create_window(width, height, title, resizable);
    if (!win) return 0;
    for (int i = 1; i < 64; i++) {
        if (!s_pw_window_table[i]) {
            s_pw_window_table[i] = win;
            return i;
        }
    }
    return 1;
}

bool pw_process_events_id(int handle) {
    PixelWindow* win = pw_resolve_handle(handle);
    return win ? pw_process_events(win) : false;
}

void pw_update_window_id(int handle, const uint8_t* rgb_buffer) {
    PixelWindow* win = pw_resolve_handle(handle);
    if (win) pw_update_window(win, rgb_buffer);
}

void pw_destroy_window_id(int handle) {
    if (handle > 0 && handle < 64 && s_pw_window_table[handle]) {
        pw_destroy_window(s_pw_window_table[handle]);
        s_pw_window_table[handle] = NULL;
        return;
    }
    if (handle) pw_destroy_window((PixelWindow*)(intptr_t)handle);
}

float pw_get_mouse_x_id(int handle) {
    PixelWindow* win = pw_resolve_handle(handle);
    return pw_get_mouse_x(win);
}

float pw_get_mouse_y_id(int handle) {
    PixelWindow* win = pw_resolve_handle(handle);
    return pw_get_mouse_y(win);
}

bool pw_is_mouse_down_id(int handle, int button) {
    PixelWindow* win = pw_resolve_handle(handle);
    return pw_is_mouse_down(win, button);
}

bool pw_is_mouse_pressed_id(int handle, int button) {
    PixelWindow* win = pw_resolve_handle(handle);
    return pw_is_mouse_pressed(win, button);
}

bool pw_is_key_down_id(int handle, int key) {
    PixelWindow* win = pw_resolve_handle(handle);
    return pw_is_key_down(win, key);
}

bool pw_is_key_pressed_id(int handle, int key) {
    PixelWindow* win = pw_resolve_handle(handle);
    return pw_is_key_pressed(win, key);
}

void pw_clear_buffer(uint8_t* buffer, intptr_t width, intptr_t height, intptr_t r, intptr_t g, intptr_t b) {
    if (!buffer) return;
    int total = width * height;
    for (int i = 0; i < total; i++) {
        int idx = i * 3;
        buffer[idx + 0] = r;
        buffer[idx + 1] = g;
        buffer[idx + 2] = b;
    }
}

#ifdef __cplusplus
}
#endif
