#ifndef PIXEL_WINDOW_H
#define PIXEL_WINDOW_H

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif

    // ----------------------------------------------------------------------------
    // INPUT SYSTEM
    // ----------------------------------------------------------------------------

    // Unified Keycodes
typedef enum {
    // Standard ASCII Control Characters
    PW_KEY_BACKSPACE = 8,
    PW_KEY_TAB       = 9,
    PW_KEY_ENTER     = 10,
    PW_KEY_ESCAPE    = 27,
    PW_KEY_SPACE     = 32,

    // Letters A-Z (65-90), Numbers 0-9 (48-57), and punctuation map
    // directly to their standard uppercase ASCII values (0-127).

    // Extended Navigation Keys
    PW_KEY_LEFT      = 256,
    PW_KEY_RIGHT     = 257,
    PW_KEY_UP        = 258,
    PW_KEY_DOWN      = 259,

    // Modifier Keys
    PW_KEY_LSHIFT    = 260,
    PW_KEY_RSHIFT    = 261,
    PW_KEY_LCTRL     = 262,
    PW_KEY_RCTRL     = 263,
    PW_KEY_LALT      = 264,
    PW_KEY_RALT      = 265,
    PW_KEY_LGUI      = 266, // Mac Command / Windows Key (Left)
    PW_KEY_RGUI      = 267, // Mac Command / Windows Key (Right)

    // Editing & Pagination
    PW_KEY_INSERT    = 268,
    PW_KEY_DELETE    = 269,
    PW_KEY_HOME      = 270,
    PW_KEY_END       = 271,
    PW_KEY_PAGEUP    = 272,
    PW_KEY_PAGEDOWN  = 273,

    // Function Keys
    PW_KEY_F1        = 274,
    PW_KEY_F2        = 275,
    PW_KEY_F3        = 276,
    PW_KEY_F4        = 277,
    PW_KEY_F5        = 278,
    PW_KEY_F6        = 279,
    PW_KEY_F7        = 280,
    PW_KEY_F8        = 281,
    PW_KEY_F9        = 282,
    PW_KEY_F10       = 283,
    PW_KEY_F11       = 284,
    PW_KEY_F12       = 285,

    // System & Locks
    PW_KEY_CAPSLOCK    = 286,
    PW_KEY_SCROLLLOCK  = 287,
    PW_KEY_NUMLOCK     = 288,
    PW_KEY_PRINTSCREEN = 289,
    PW_KEY_PAUSE       = 290,

    // Numpad
    PW_KEY_KP_0        = 291,
    PW_KEY_KP_1        = 292,
    PW_KEY_KP_2        = 293,
    PW_KEY_KP_3        = 294,
    PW_KEY_KP_4        = 295,
    PW_KEY_KP_5        = 296,
    PW_KEY_KP_6        = 297,
    PW_KEY_KP_7        = 298,
    PW_KEY_KP_8        = 299,
    PW_KEY_KP_9        = 300,
    PW_KEY_KP_DECIMAL  = 301,
    PW_KEY_KP_DIVIDE   = 302,
    PW_KEY_KP_MULTIPLY = 303,
    PW_KEY_KP_SUBTRACT = 304,
    PW_KEY_KP_ADD      = 305,
    PW_KEY_KP_ENTER    = 306,

    PW_KEY_MAX         = 512
    } PwKey;

    // Input State Representation
    typedef struct {
        float mouse_x;          // Normalized 0.0 (left) to 1.0 (right)
        float mouse_y;          // Normalized 0.0 (top) to 1.0 (bottom)

        bool mouse_down[3];     // [0] Left, [1] Right, [2] Middle
        bool mouse_pressed[3];  // True ONLY on the frame it was pressed
        bool mouse_released[3]; // True ONLY on the frame it was released

        bool keys_down[512];    // Is the key currently held down?
        bool keys_pressed[512]; // Was the key pressed THIS frame?
        bool keys_released[512];// Was the key released THIS frame?
    } PwInputState;

    typedef struct PixelWindow PixelWindow;

    // Creates a window with the specified dimensions. 'resizable' allows window scaling.
    PixelWindow* pw_create_window(int width, int height, const char* title, bool resizable);

    // Processes OS events (like closing the window). Returns false if the window is closed.
    bool pw_process_events(PixelWindow* win);

    // Takes a tightly packed 8-bit RGB array (size: width * height * 3) and draws it.
    void pw_update_window(PixelWindow* win, const uint8_t* rgb_buffer);

    // Cleans up resources.
    void pw_destroy_window(PixelWindow* win);

    // Get the current input state for the given window
    const PwInputState* pw_get_input(PixelWindow* win);

    static void pw__clear_single_frame_input(PixelWindow* win);

#ifdef __cplusplus
}
#endif

#endif // PIXEL_WINDOW_H

// ============================================================================
// IMPLEMENTATION
// Define PIXEL_WINDOW_IMPLEMENTATION in exactly ONE source file.
// ============================================================================

#ifdef PIXEL_WINDOW_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

// Helper Macros for updating input state
// Helper Macros for updating input state safely
#define PW__UPDATE_KEY(win, code, is_down_val) \
    do { \
        int _c = (code); \
        bool _d = (is_down_val); \
        if (_c > 0 && _c < 512) { \
            if (_d && !(win)->input.keys_down[_c]) (win)->input.keys_pressed[_c] = true; \
            if (!_d && (win)->input.keys_down[_c]) (win)->input.keys_released[_c] = true; \
            (win)->input.keys_down[_c] = _d; \
        } \
    } while(0)

#define PW__UPDATE_MOUSE(win, btn, is_down_val) \
    do { \
        int _b = (btn); \
        bool _d = (is_down_val); \
        if (_b >= 0 && _b < 3) { \
            if (_d && !(win)->input.mouse_down[_b]) (win)->input.mouse_pressed[_b] = true; \
            if (!_d && (win)->input.mouse_down[_b]) (win)->input.mouse_released[_b] = true; \
            (win)->input.mouse_down[_b] = _d; \
        } \
    } while(0)

// ----------------------------------------------------------------------------
// WINDOWS (Win32)
// ----------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>

struct PixelWindow {
    int width;
    int height;
    bool is_running;
    PwInputState input;
    HWND hwnd;
    uint8_t* bgra_buffer; // Win32 standard is BGRA
    BITMAPINFO bmi;
};

static int pw__map_win_key(WPARAM k) {
    if (k >= 'A' && k <= 'Z') return (int)k;
    if (k >= '0' && k <= '9') return (int)k;
    switch (k) {
    case VK_SPACE: return PW_KEY_SPACE; case VK_ESCAPE: return PW_KEY_ESCAPE;
    case VK_RETURN: return PW_KEY_ENTER; case VK_LEFT: return PW_KEY_LEFT;
    case VK_RIGHT: return PW_KEY_RIGHT; case VK_UP: return PW_KEY_UP;
    case VK_DOWN: return PW_KEY_DOWN; case VK_BACK: return PW_KEY_BACKSPACE;
    case VK_TAB: return PW_KEY_TAB;
    }
    return 0;
}

static LRESULT CALLBACK pw__window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    PixelWindow* win = (PixelWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!win) return DefWindowProc(hwnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_CLOSE: case WM_DESTROY:
        win->is_running = false; PostQuitMessage(0); return 0;

    case WM_MOUSEMOVE:
        win->input.mouse_x = (float)(short)LOWORD(lParam) / win->width;
        win->input.mouse_y = (float)(short)HIWORD(lParam) / win->height;
        break;

    case WM_LBUTTONDOWN: PW__UPDATE_MOUSE(win, 0, true); break;
    case WM_LBUTTONUP:   PW__UPDATE_MOUSE(win, 0, false); break;
    case WM_RBUTTONDOWN: PW__UPDATE_MOUSE(win, 1, true); break;
    case WM_RBUTTONUP:   PW__UPDATE_MOUSE(win, 1, false); break;
    case WM_MBUTTONDOWN: PW__UPDATE_MOUSE(win, 2, true); break;
    case WM_MBUTTONUP:   PW__UPDATE_MOUSE(win, 2, false); break;

    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        PW__UPDATE_KEY(win, pw__map_win_key(wParam), true); break;
    case WM_KEYUP: case WM_SYSKEYUP:
        PW__UPDATE_KEY(win, pw__map_win_key(wParam), false); break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

PixelWindow* pw_create_window(int width, int height, const char* title, bool resizable) {
    PixelWindow* win = (PixelWindow*)calloc(1, sizeof(PixelWindow));
    win->width = width;
    win->height = height;
    win->is_running = true;
    win->bgra_buffer = (uint8_t*)malloc(width * height * 4);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = pw__window_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "PixelWindowClass";
    RegisterClass(&wc);

    DWORD window_style = WS_OVERLAPPEDWINDOW;
    if (!resizable) {
        window_style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, window_style, FALSE);

    win->hwnd = CreateWindowEx(0, "PixelWindowClass", title,
        window_style | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, wc.hInstance, NULL);

    SetWindowLongPtr(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);

    win->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    win->bmi.bmiHeader.biWidth = width;
    win->bmi.bmiHeader.biHeight = -height; // Top-down
    win->bmi.bmiHeader.biPlanes = 1;
    win->bmi.bmiHeader.biBitCount = 32;
    win->bmi.bmiHeader.biCompression = BI_RGB;

    return win;
}

bool pw_process_events(PixelWindow* win) {
    pw__clear_single_frame_input(win);
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return win->is_running;
}

void pw_update_window(PixelWindow* win, const uint8_t* rgb_buffer) {
    int pixels = win->width * win->height;
    for (int i = 0; i < pixels; ++i) {
        win->bgra_buffer[i * 4 + 0] = rgb_buffer[i * 3 + 2]; // B
        win->bgra_buffer[i * 4 + 1] = rgb_buffer[i * 3 + 1]; // G
        win->bgra_buffer[i * 4 + 2] = rgb_buffer[i * 3 + 0]; // R
        win->bgra_buffer[i * 4 + 3] = 255;                 // A
    }

    HDC hdc = GetDC(win->hwnd);
    StretchDIBits(hdc, 0, 0, win->width, win->height,
        0, 0, win->width, win->height,
        win->bgra_buffer, &win->bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(win->hwnd, hdc);
}

void pw_destroy_window(PixelWindow* win) {
    if (!win) return;
    DestroyWindow(win->hwnd);
    free(win->bgra_buffer);
    free(win);
}

// ----------------------------------------------------------------------------
// LINUX (X11)
// ----------------------------------------------------------------------------
#elif defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

struct PixelWindow {
    int width;
    int height;
    bool is_running;
    PwInputState input;
    Display* display;
    Window window;
    GC gc;
    XImage* ximage;
    uint8_t* bgra_buffer;
    Atom wm_delete_window;
};

static int pw__map_x11_key(KeySym k) {
    if (k >= XK_a && k <= XK_z) return k - 32; // Convert to uppercase ASCII
    if (k >= XK_A && k <= XK_Z) return k;
    if (k >= XK_0 && k <= XK_9) return k;
    switch (k) {
    case XK_space: return PW_KEY_SPACE; case XK_Escape: return PW_KEY_ESCAPE;
    case XK_Return: return PW_KEY_ENTER; case XK_Left: return PW_KEY_LEFT;
    case XK_Right: return PW_KEY_RIGHT; case XK_Up: return PW_KEY_UP;
    case XK_Down: return PW_KEY_DOWN; case XK_BackSpace: return PW_KEY_BACKSPACE;
    case XK_Tab: return PW_KEY_TAB;
    }
    return 0;
}

PixelWindow* pw_create_window(int width, int height, const char* title, bool resizable) {
    PixelWindow* win = (PixelWindow*)calloc(1, sizeof(PixelWindow));
    win->width = width;
    win->height = height;
    win->is_running = true;
    win->bgra_buffer = (uint8_t*)malloc(width * height * 4);

    win->display = XOpenDisplay(NULL);
    int screen = DefaultScreen(win->display);

    win->window = XCreateSimpleWindow(win->display, RootWindow(win->display, screen),
        0, 0, width, height, 0,
        BlackPixel(win->display, screen),
        BlackPixel(win->display, screen));

    XStoreName(win->display, win->window, title);

    if (!resizable) {
        XSizeHints* hints = XAllocSizeHints();
        if (hints) {
            hints->flags = PMinSize | PMaxSize;
            hints->min_width = hints->max_width = width;
            hints->min_height = hints->max_height = height;
            XSetWMNormalHints(win->display, win->window, hints);
            XFree(hints);
        }
    }

    win->wm_delete_window = XInternAtom(win->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(win->display, win->window, &win->wm_delete_window, 1);

    XSelectInput(win->display, win->window, ExposureMask | PointerMotionMask |
        ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask);

    XMapWindow(win->display, win->window);

    win->gc = DefaultGC(win->display, screen);

    win->ximage = XCreateImage(win->display, DefaultVisual(win->display, screen),
        24, ZPixmap, 0, (char*)win->bgra_buffer,
        width, height, 32, 0);

    return win;
}

bool pw_process_events(PixelWindow* win) {
    pw__clear_single_frame_input(win);
    XEvent event;
    while (XPending(win->display) > 0) {
        XNextEvent(win->display, &event);
        if (event.type == ClientMessage) {
            if ((Atom)event.xclient.data.l[0] == win->wm_delete_window) win->is_running = false;
        }
        else if (event.type == MotionNotify) {
            win->input.mouse_x = (float)event.xmotion.x / win->width;
            win->input.mouse_y = (float)event.xmotion.y / win->height;
        }
        else if (event.type == ButtonPress || event.type == ButtonRelease) {
            bool is_down = (event.type == ButtonPress);
            if (event.xbutton.button == 1) PW__UPDATE_MOUSE(win, 0, is_down);      // Left
            else if (event.xbutton.button == 3) PW__UPDATE_MOUSE(win, 1, is_down); // Right
            else if (event.xbutton.button == 2) PW__UPDATE_MOUSE(win, 2, is_down); // Middle
        }
        else if (event.type == KeyPress || event.type == KeyRelease) {
            KeySym keysym = XLookupKeysym(&event.xkey, 0);
            PW__UPDATE_KEY(win, pw__map_x11_key(keysym), (event.type == KeyPress));
        }
    }
    return win->is_running;
}

void pw_update_window(PixelWindow* win, const uint8_t* rgb_buffer) {
    int pixels = win->width * win->height;
    for (int i = 0; i < pixels; ++i) {
        win->bgra_buffer[i * 4 + 0] = rgb_buffer[i * 3 + 2]; // B
        win->bgra_buffer[i * 4 + 1] = rgb_buffer[i * 3 + 1]; // G
        win->bgra_buffer[i * 4 + 2] = rgb_buffer[i * 3 + 0]; // R
        win->bgra_buffer[i * 4 + 3] = 255;
    }

    XPutImage(win->display, win->window, win->gc, win->ximage, 0, 0, 0, 0, win->width, win->height);
    XFlush(win->display);
}

void pw_destroy_window(PixelWindow* win) {
    if (!win) return;
    win->ximage->data = NULL;
    XDestroyImage(win->ximage);
    XDestroyWindow(win->display, win->window);
    XCloseDisplay(win->display);
    free(win->bgra_buffer);
    free(win);
}

// ----------------------------------------------------------------------------
// macOS (Metal)
// ----------------------------------------------------------------------------
#elif defined(__APPLE__)

#ifdef __OBJC__

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

struct PixelWindow {
    int width;
    int height;
    bool is_running;
    PwInputState input;
    uint8_t* rgba_buffer;

    NSWindow* window;
    id delegate;
    CAMetalLayer* metal_layer;
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> pipeline_state;
    id<MTLTexture> texture;
};

static int pw__map_mac_key(unsigned short k) {
    switch (k) {
        // Letters
        case 0: return 'A'; case 1: return 'S'; case 2: return 'D'; case 3: return 'F';
        case 4: return 'H'; case 5: return 'G'; case 6: return 'Z'; case 7: return 'X';
        case 8: return 'C'; case 9: return 'V'; case 11: return 'B'; case 12: return 'Q';
        case 13: return 'W'; case 14: return 'E'; case 15: return 'R'; case 16: return 'Y';
        case 17: return 'T'; case 31: return 'O'; case 32: return 'U'; case 34: return 'I';
        case 35: return 'P'; case 37: return 'L'; case 38: return 'J'; case 40: return 'K';
        case 45: return 'N'; case 46: return 'M';
            // Numbers
        case 18: return '1'; case 19: return '2'; case 20: return '3'; case 21: return '4';
        case 23: return '5'; case 22: return '6'; case 26: return '7'; case 28: return '8';
        case 25: return '9'; case 29: return '0';
            // Punctuation & Symbols (NEW)
        case 27: return '-'; case 24: return '='; case 33: return '['; case 30: return ']';
        case 42: return '\\'; case 41: return ';'; case 39: return '\''; case 43: return ',';
        case 47: return '.'; case 44: return '/'; case 50: return '`';
            // System Keys
        case 123: return PW_KEY_LEFT; case 124: return PW_KEY_RIGHT;
        case 125: return PW_KEY_DOWN; case 126: return PW_KEY_UP;
        case 49: return PW_KEY_SPACE; case 53: return PW_KEY_ESCAPE;
        case 36: return PW_KEY_ENTER; case 51: return PW_KEY_BACKSPACE;
        case 48: return PW_KEY_TAB;
        case 56: return PW_KEY_LSHIFT; case 60: return PW_KEY_RSHIFT;
    }
    return 0;
}

static const char* msl_source = R"(
#include <metal_stdlib>
using namespace metal;
struct VertexOut { float4 pos [[position]]; float2 uv; };
vertex VertexOut vs(uint vid [[vertex_id]]) {
    float2 pos[] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
    float2 uv[]  = { float2(0, 1),   float2(1, 1),  float2(0, 0),  float2(1, 0) };
    return { float4(pos[vid], 0.0, 1.0), uv[vid] };
}
fragment float4 fs(VertexOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(filter::nearest);
    return tex.sample(s, in.uv);
}
)";

@interface PWWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) bool* is_running_ptr;
@end
@implementation PWWindowDelegate
- (BOOL)windowShouldClose:(id)sender {
    if (_is_running_ptr) *_is_running_ptr = false;
    return YES;
}
@end

PixelWindow* pw_create_window(int width, int height, const char* title, bool resizable) {
    [NSApplication sharedApplication] ;
    [NSApp setActivationPolicy : NSApplicationActivationPolicyRegular] ;

    PixelWindow* win = (PixelWindow*)calloc(1, sizeof(PixelWindow));
    win->width = width;
    win->height = height;
    win->is_running = true;
    win->rgba_buffer = (uint8_t*)malloc(width * height * 4);

    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    if (resizable) style |= NSWindowStyleMaskResizable;

    NSRect rect = NSMakeRect(0, 0, width, height);
    win->window = [[NSWindow alloc]initWithContentRect:rect
        styleMask : style
        backing : NSBackingStoreBuffered
        defer : NO];
    [win->window setTitle : [NSString stringWithUTF8String : title] ] ;
    [win->window center] ;

    PWWindowDelegate* delegate = [[PWWindowDelegate alloc]init];
    delegate.is_running_ptr = &win->is_running;
    win->delegate = delegate; // <-- Keep alive under ARC
    [win->window setDelegate : delegate] ;

    win->device = MTLCreateSystemDefaultDevice();
    win->command_queue = [win->device newCommandQueue];

    NSView* view = [win->window contentView];
    [view setWantsLayer : YES] ;
    win->metal_layer = [CAMetalLayer layer];
    win->metal_layer.device = win->device;
    win->metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    win->metal_layer.framebufferOnly = YES;
    win->metal_layer.drawableSize = CGSizeMake(width, height);
    [view setLayer : win->metal_layer] ;

    MTLTextureDescriptor* texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat : MTLPixelFormatRGBA8Unorm
        width : width
        height : height
        mipmapped : NO];
    win->texture = [win->device newTextureWithDescriptor : texDesc];

    NSError* error = nil;
    NSString* sourceString = [NSString stringWithUTF8String : msl_source];
    id<MTLLibrary> library = [win->device newLibraryWithSource : sourceString options : nil error : &error];

    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc]init];
    pipelineDesc.vertexFunction = [library newFunctionWithName : @"vs"];
    pipelineDesc.fragmentFunction = [library newFunctionWithName : @"fs"];
    pipelineDesc.colorAttachments[0].pixelFormat = win->metal_layer.pixelFormat;

    win->pipeline_state = [win->device newRenderPipelineStateWithDescriptor : pipelineDesc error : &error];

    [win->window makeKeyAndOrderFront : nil] ;
    [NSApp activateIgnoringOtherApps : YES] ;

    return win;
}

bool pw_process_events(PixelWindow* win) {
    @autoreleasepool {
        pw__clear_single_frame_input(win);
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask: NSEventMaskAny
                                           untilDate: nil
                                              inMode: NSDefaultRunLoopMode
                                             dequeue: YES])) {
            NSEventType type = [event type];

            if (type == NSEventTypeMouseMoved || type == NSEventTypeLeftMouseDragged ||
                type == NSEventTypeRightMouseDragged || type == NSEventTypeOtherMouseDragged) {

            NSPoint p = [event locationInWindow];
            NSRect contentRect = [[win->window contentView]frame];
            win->input.mouse_x = p.x / contentRect.size.width;
            win->input.mouse_y = 1.0f - (p.y / contentRect.size.height); // Flip Y 

            }
            else if (type == NSEventTypeLeftMouseDown) { PW__UPDATE_MOUSE(win, 0, true); }
            else if (type == NSEventTypeLeftMouseUp) { PW__UPDATE_MOUSE(win, 0, false); }
            else if (type == NSEventTypeRightMouseDown) { PW__UPDATE_MOUSE(win, 1, true); }
            else if (type == NSEventTypeRightMouseUp) { PW__UPDATE_MOUSE(win, 1, false); }
            else if (type == NSEventTypeKeyDown && ![event isARepeat]) {
                PW__UPDATE_KEY(win, pw__map_mac_key([event keyCode]), true);

                if (!([event modifierFlags] & NSEventModifierFlagCommand)) {
                    continue;
                }
            }
            else if (type == NSEventTypeKeyUp) {
                PW__UPDATE_KEY(win, pw__map_mac_key([event keyCode]), false);
            }
            else if (type == NSEventTypeFlagsChanged) {
                unsigned short raw_key = [event keyCode];
                int mapped_key = pw__map_mac_key(raw_key);

                if (mapped_key > 0 && mapped_key < 512) {
                    bool is_down = false;
                    NSEventModifierFlags flags = [event modifierFlags];

                    if (mapped_key == PW_KEY_LSHIFT || mapped_key == PW_KEY_RSHIFT) {
                        is_down = (flags & NSEventModifierFlagShift) != 0;
                    } else if (mapped_key == PW_KEY_LCTRL || mapped_key == PW_KEY_RCTRL) {
                        // Check both Command and Control since we map them both to LCTRL/RCTRL
                        is_down = (flags & NSEventModifierFlagCommand) != 0 ||
                                  (flags & NSEventModifierFlagControl) != 0;
                    }

                    win->input.keys_down[mapped_key] = is_down;
                    if (is_down) {
                        win->input.keys_pressed[mapped_key] = true;
                    }
                }
            }

            [NSApp sendEvent: event];
        }
        return win->is_running;
    }
}
void pw_update_window(PixelWindow* win, const uint8_t* rgb_buffer) {
    @autoreleasepool {
    int pixels = win->width * win->height;
    for (int i = 0; i < pixels; ++i) {
        win->rgba_buffer[i * 4 + 0] = rgb_buffer[i * 3 + 0]; // R
        win->rgba_buffer[i * 4 + 1] = rgb_buffer[i * 3 + 1]; // G
        win->rgba_buffer[i * 4 + 2] = rgb_buffer[i * 3 + 2]; // B
        win->rgba_buffer[i * 4 + 3] = 255;
    }

    MTLRegion region = MTLRegionMake2D(0, 0, win->width, win->height);
    [win->texture replaceRegion : region mipmapLevel : 0 withBytes : win->rgba_buffer bytesPerRow : win->width * 4] ;

    id<CAMetalDrawable> drawable = [win->metal_layer nextDrawable];
    if (drawable) {
        id<MTLCommandBuffer> commandBuffer = [win->command_queue commandBuffer];

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor : passDesc];
        [encoder setRenderPipelineState : win->pipeline_state] ;
        [encoder setFragmentTexture : win->texture atIndex : 0] ;
        [encoder drawPrimitives : MTLPrimitiveTypeTriangleStrip vertexStart : 0 vertexCount : 4] ;
        [encoder endEncoding] ;

        [commandBuffer presentDrawable : drawable] ;
        [commandBuffer commit] ;
    }
    }
}

void pw_destroy_window(PixelWindow* win) {
    if (!win) return;
    free(win->rgba_buffer);
    free(win);
}

#endif // __OBJC__

// ----------------------------------------------------------------------------
// AmigaOS Classic (CyberGraphics / P96 RTG)
// ----------------------------------------------------------------------------
#elif defined(__amigaos__)

#include <proto/exec.h>
#include <proto/intuition.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>
#include <libraries/Picasso96.h>
#include <proto/Picasso96.h>
#include <stdio.h>

// Automatically allocate 128 KB of stack space when launched on AmigaOS
static const char __attribute__((used)) min_stack[] = "$STACK:131072";

struct IntuitionBase *IntuitionBase = NULL;
struct Library *CyberGfxBase = NULL;
struct Library *P96Base = NULL;

struct PixelWindow {
    int width;
    int height;
    bool is_running;
    PwInputState input;
    struct Window* amg_win;
    uint32_t* rtg_buffer; 
};

// Map Amiga hardware rawkeys to standard uppercase ASCII and PW_KEYs
static int pw__map_amiga_key(uint16_t code) {
    uint16_t base_code = code & 0x7F; // Strip the KeyUp bit (0x80)

    static const int amiga_key_map[0x40] = {
        '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\\', 0, 0,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', 0, 0, 0, 0,
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '#', 0, 0, 0, 0,
        '\\', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '/', 0, 0, 0, 0, 0
    };

    if (base_code < 0x40) return amiga_key_map[base_code];

    switch (base_code) {
        case 0x40: return PW_KEY_SPACE;
        case 0x41: return PW_KEY_BACKSPACE;
        case 0x42: return PW_KEY_TAB;
        case 0x44: return PW_KEY_ENTER;
        case 0x45: return PW_KEY_ESCAPE;
        case 0x4C: return PW_KEY_UP;
        case 0x4D: return PW_KEY_DOWN;
        case 0x4E: return PW_KEY_RIGHT;
        case 0x4F: return PW_KEY_LEFT;
        case 0x60: return PW_KEY_LSHIFT;
        case 0x61: return PW_KEY_RSHIFT;
    }
    return 0; 
}

PixelWindow* pw_create_window(int width, int height, const char* title, bool resizable) {
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!IntuitionBase) {
        printf("[ERROR] Failed to open intuition.library v39+\n");
        return NULL;
    }

    if (!CyberGfxBase && !P96Base) {
        // 1. Try standard cybergraphics.library
        CyberGfxBase = OpenLibrary("cybergraphics.library", 41);
        if (!CyberGfxBase) CyberGfxBase = OpenLibrary("cybergraphics.library", 0);
        // 2. Try Picasso96's CGX wrapper in LIBS:Picasso96/
        if (!CyberGfxBase) CyberGfxBase = OpenLibrary("Picasso96/cybergraphics.library", 0);
        if (!CyberGfxBase) CyberGfxBase = OpenLibrary("LIBS:Picasso96/cybergraphics.library", 0);
        if (!CyberGfxBase) CyberGfxBase = OpenLibrary("LIBS:cybergraphics.library", 0);

        // 3. Try native Picasso96API.library
        if (!CyberGfxBase) {
            P96Base = OpenLibrary("Picasso96API.library", 2);
            if (!P96Base) P96Base = OpenLibrary("Picasso96API.library", 0);
            if (!P96Base) P96Base = OpenLibrary("LIBS:Picasso96API.library", 0);
        }
    }

    if (!CyberGfxBase && !P96Base) {
        printf("[ERROR] Failed to open cybergraphics.library or Picasso96API.library\n");
        printf("        Please ensure an RTG system (Picasso96 or CyberGraphX) is installed in LIBS:\n");
        return NULL;
    }

    PixelWindow* win = (PixelWindow*)calloc(1, sizeof(PixelWindow));
    if (!win) return NULL;

    win->width = width;
    win->height = height;
    win->is_running = true;
    win->rtg_buffer = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    
    if (!win->rtg_buffer) {
        free(win);
        return NULL;
    }

    struct TagItem tags[] = {
        { WA_Left, 0 },
        { WA_Top, 0 },
        { WA_InnerWidth, width },
        { WA_InnerHeight, height },
        { WA_Title, (ULONG)title },
        { WA_CloseGadget, TRUE },
        { WA_DepthGadget, TRUE },
        { WA_DragBar, TRUE },
        { WA_Activate, TRUE },
        { WA_AutoAdjust, TRUE },
        { WA_ReportMouse, TRUE },
        { WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY },
        { resizable ? WA_SizeGadget : TAG_IGNORE, TRUE },
        { TAG_DONE, 0 }
    };

    win->amg_win = OpenWindowTagList(NULL, tags);

    if (!win->amg_win) {
        printf("[ERROR] OpenWindowTagList failed! Check requested resolution or screen mode.\n");
        free(win->rtg_buffer);
        free(win);
        return NULL; 
    }
    return win;
}

bool pw_process_events(PixelWindow* win) {
    if (!win || !win->amg_win) return false; // Null safety guard

    pw__clear_single_frame_input(win);
    struct IntuiMessage *msg;
    
    while ((msg = (struct IntuiMessage *)GetMsg(win->amg_win->UserPort))) {
        ULONG msgClass = msg->Class;
        UWORD msgCode = msg->Code;
        int16_t mx = msg->MouseX;
        int16_t my = msg->MouseY;
        
        ReplyMsg((struct Message *)msg);

        if (msgClass == IDCMP_CLOSEWINDOW) {
            win->is_running = false;
        } 
        else if (msgClass == IDCMP_MOUSEMOVE) {
            float nx = (float)(mx - win->amg_win->BorderLeft) / win->width;
            float ny = (float)(my - win->amg_win->BorderTop)  / win->height;
            
            if (nx < 0.0f) nx = 0.0f; if (nx > 1.0f) nx = 1.0f;
            if (ny < 0.0f) ny = 0.0f; if (ny > 1.0f) ny = 1.0f;
            
            win->input.mouse_x = nx;
            win->input.mouse_y = ny;
        } 
        else if (msgClass == IDCMP_MOUSEBUTTONS) {
            if (msgCode == SELECTDOWN) PW__UPDATE_MOUSE(win, 0, true);
            else if (msgCode == SELECTUP) PW__UPDATE_MOUSE(win, 0, false);
            else if (msgCode == MENUDOWN) PW__UPDATE_MOUSE(win, 1, true);
            else if (msgCode == MENUUP) PW__UPDATE_MOUSE(win, 1, false);
            else if (msgCode == MIDDLEDOWN) PW__UPDATE_MOUSE(win, 2, true);
            else if (msgCode == MIDDLEUP) PW__UPDATE_MOUSE(win, 2, false);
        }
        else if (msgClass == IDCMP_RAWKEY) {
            bool is_down = !(msgCode & 0x80); 
            PW__UPDATE_KEY(win, pw__map_amiga_key(msgCode), is_down);
        }
    }
    return win->is_running;
}

void pw_update_window(PixelWindow* win, const uint8_t* rgb_buffer) {
    if (!win || !win->rtg_buffer || !win->amg_win) return; // Null safety guard

    uint32_t* dst = win->rtg_buffer;
    int pixels = win->width * win->height;
    
    for (int i = 0; i < pixels; ++i) {
        *dst++ = ((uint32_t)rgb_buffer[i * 3] << 24) |      
                 ((uint32_t)rgb_buffer[i * 3 + 1] << 16) |  
                 ((uint32_t)rgb_buffer[i * 3 + 2] << 8) |   
                 0xFF;                            
    }

    if (CyberGfxBase) {
        WritePixelArray(win->rtg_buffer, 0, 0, win->width * 4,
                        win->amg_win->RPort, 
                        win->amg_win->BorderLeft, 
                        win->amg_win->BorderTop, 
                        win->width, win->height, RECTFMT_RGBA);
    } else if (P96Base) {
        struct RenderInfo ri;
        ri.Memory = win->rtg_buffer;
        ri.BytesPerRow = win->width * 4;
        ri.pad = 0;
        ri.RGBFormat = RGBFB_R8G8B8A8;
        p96WritePixelArray(&ri, 0, 0,
                           win->amg_win->RPort,
                           win->amg_win->BorderLeft,
                           win->amg_win->BorderTop,
                           win->width, win->height);
    }
}

void pw_destroy_window(PixelWindow* win) {
    if (!win) return;
    if (win->amg_win) CloseWindow(win->amg_win);
    if (win->rtg_buffer) free(win->rtg_buffer);
    free(win);
    
    if (CyberGfxBase) {
        CloseLibrary(CyberGfxBase);
        CyberGfxBase = NULL;
    }
    if (P96Base) {
        CloseLibrary(P96Base);
        P96Base = NULL;
    }
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}
#endif // __amigaos__

static void pw__clear_single_frame_input(PixelWindow* win) {
    memset(win->input.mouse_pressed, 0, sizeof(win->input.mouse_pressed));
    memset(win->input.mouse_released, 0, sizeof(win->input.mouse_released));
    memset(win->input.keys_pressed, 0, sizeof(win->input.keys_pressed));
    memset(win->input.keys_released, 0, sizeof(win->input.keys_released));
}

const PwInputState* pw_get_input(PixelWindow* win) {
    return &win->input;
}

#endif // PIXEL_WINDOW_IMPLEMENTATION
