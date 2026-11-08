#include "imgui_impl_raylib.hpp"
#include "raylib.h"
#include "rlgl.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

struct ImGui_ImplRaylib_KeyMap {
    KeyboardKey raylib_key;
    ImGuiKey imgui_key;
};

static const ImGui_ImplRaylib_KeyMap g_key_map[] = {
    { KEY_TAB, ImGuiKey_Tab },
    { KEY_LEFT, ImGuiKey_LeftArrow },
    { KEY_RIGHT, ImGuiKey_RightArrow },
    { KEY_UP, ImGuiKey_UpArrow },
    { KEY_DOWN, ImGuiKey_DownArrow },
    { KEY_PAGE_UP, ImGuiKey_PageUp },
    { KEY_PAGE_DOWN, ImGuiKey_PageDown },
    { KEY_HOME, ImGuiKey_Home },
    { KEY_END, ImGuiKey_End },
    { KEY_INSERT, ImGuiKey_Insert },
    { KEY_DELETE, ImGuiKey_Delete },
    { KEY_BACKSPACE, ImGuiKey_Backspace },
    { KEY_SPACE, ImGuiKey_Space },
    { KEY_ENTER, ImGuiKey_Enter },
    { KEY_ESCAPE, ImGuiKey_Escape },
    { KEY_APOSTROPHE, ImGuiKey_Apostrophe },
    { KEY_COMMA, ImGuiKey_Comma },
    { KEY_MINUS, ImGuiKey_Minus },
    { KEY_PERIOD, ImGuiKey_Period },
    { KEY_SLASH, ImGuiKey_Slash },
    { KEY_SEMICOLON, ImGuiKey_Semicolon },
    { KEY_EQUAL, ImGuiKey_Equal },
    { KEY_LEFT_BRACKET, ImGuiKey_LeftBracket },
    { KEY_BACKSLASH, ImGuiKey_Backslash },
    { KEY_RIGHT_BRACKET, ImGuiKey_RightBracket },
    { KEY_GRAVE, ImGuiKey_GraveAccent },
    { KEY_CAPS_LOCK, ImGuiKey_CapsLock },
    { KEY_SCROLL_LOCK, ImGuiKey_ScrollLock },
    { KEY_NUM_LOCK, ImGuiKey_NumLock },
    { KEY_PRINT_SCREEN, ImGuiKey_PrintScreen },
    { KEY_PAUSE, ImGuiKey_Pause },
    { KEY_KP_0, ImGuiKey_Keypad0 },
    { KEY_KP_1, ImGuiKey_Keypad1 },
    { KEY_KP_2, ImGuiKey_Keypad2 },
    { KEY_KP_3, ImGuiKey_Keypad3 },
    { KEY_KP_4, ImGuiKey_Keypad4 },
    { KEY_KP_5, ImGuiKey_Keypad5 },
    { KEY_KP_6, ImGuiKey_Keypad6 },
    { KEY_KP_7, ImGuiKey_Keypad7 },
    { KEY_KP_8, ImGuiKey_Keypad8 },
    { KEY_KP_9, ImGuiKey_Keypad9 },
    { KEY_KP_DECIMAL, ImGuiKey_KeypadDecimal },
    { KEY_KP_DIVIDE, ImGuiKey_KeypadDivide },
    { KEY_KP_MULTIPLY, ImGuiKey_KeypadMultiply },
    { KEY_KP_SUBTRACT, ImGuiKey_KeypadSubtract },
    { KEY_KP_ADD, ImGuiKey_KeypadAdd },
    { KEY_KP_ENTER, ImGuiKey_KeypadEnter },
    { KEY_KP_EQUAL, ImGuiKey_KeypadEqual },
    { KEY_LEFT_SHIFT, ImGuiKey_LeftShift },
    { KEY_LEFT_CONTROL, ImGuiKey_LeftCtrl },
    { KEY_LEFT_ALT, ImGuiKey_LeftAlt },
    { KEY_LEFT_SUPER, ImGuiKey_LeftSuper },
    { KEY_RIGHT_SHIFT, ImGuiKey_RightShift },
    { KEY_RIGHT_CONTROL, ImGuiKey_RightCtrl },
    { KEY_RIGHT_ALT, ImGuiKey_RightAlt },
    { KEY_RIGHT_SUPER, ImGuiKey_RightSuper },
    { KEY_KB_MENU, ImGuiKey_Menu },
    { KEY_ZERO, ImGuiKey_0 },
    { KEY_ONE, ImGuiKey_1 },
    { KEY_TWO, ImGuiKey_2 },
    { KEY_THREE, ImGuiKey_3 },
    { KEY_FOUR, ImGuiKey_4 },
    { KEY_FIVE, ImGuiKey_5 },
    { KEY_SIX, ImGuiKey_6 },
    { KEY_SEVEN, ImGuiKey_7 },
    { KEY_EIGHT, ImGuiKey_8 },
    { KEY_NINE, ImGuiKey_9 },
    { KEY_A, ImGuiKey_A },
    { KEY_B, ImGuiKey_B },
    { KEY_C, ImGuiKey_C },
    { KEY_D, ImGuiKey_D },
    { KEY_E, ImGuiKey_E },
    { KEY_F, ImGuiKey_F },
    { KEY_G, ImGuiKey_G },
    { KEY_H, ImGuiKey_H },
    { KEY_I, ImGuiKey_I },
    { KEY_J, ImGuiKey_J },
    { KEY_K, ImGuiKey_K },
    { KEY_L, ImGuiKey_L },
    { KEY_M, ImGuiKey_M },
    { KEY_N, ImGuiKey_N },
    { KEY_O, ImGuiKey_O },
    { KEY_P, ImGuiKey_P },
    { KEY_Q, ImGuiKey_Q },
    { KEY_R, ImGuiKey_R },
    { KEY_S, ImGuiKey_S },
    { KEY_T, ImGuiKey_T },
    { KEY_U, ImGuiKey_U },
    { KEY_V, ImGuiKey_V },
    { KEY_W, ImGuiKey_W },
    { KEY_X, ImGuiKey_X },
    { KEY_Y, ImGuiKey_Y },
    { KEY_Z, ImGuiKey_Z },
    { KEY_F1, ImGuiKey_F1 },
    { KEY_F2, ImGuiKey_F2 },
    { KEY_F3, ImGuiKey_F3 },
    { KEY_F4, ImGuiKey_F4 },
    { KEY_F5, ImGuiKey_F5 },
    { KEY_F6, ImGuiKey_F6 },
    { KEY_F7, ImGuiKey_F7 },
    { KEY_F8, ImGuiKey_F8 },
    { KEY_F9, ImGuiKey_F9 },
    { KEY_F10, ImGuiKey_F10 },
    { KEY_F11, ImGuiKey_F11 },
    { KEY_F12, ImGuiKey_F12 },
};

static const char* ImGui_ImplRaylib_GetClipboardText(ImGuiContext*) {
    return GetClipboardText();
}

static void ImGui_ImplRaylib_SetClipboardText(ImGuiContext*, const char* text) {
    SetClipboardText(text);
}

static void ImGui_ImplRaylib_UpdateTexture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        if (tex->TexID != ImTextureID_Invalid) {
            unsigned int old_id = (unsigned int)(uintptr_t)tex->TexID;
            rlUnloadTexture(old_id);
            tex->SetTexID(ImTextureID_Invalid);
        }
        const void* pixels = tex->GetPixels();
        unsigned int tex_id = rlLoadTexture(pixels, tex->Width, tex->Height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        rlTextureParameters(tex_id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(tex_id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(tex_id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
        rlTextureParameters(tex_id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
        tex->SetTexID((ImTextureID)(uintptr_t)tex_id);
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        unsigned int tex_id = (unsigned int)(uintptr_t)tex->TexID;
        if (tex_id != 0) {
            rlUpdateTexture(tex_id, 0, 0, tex->Width, tex->Height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, tex->GetPixels());
            tex->SetStatus(ImTextureStatus_OK);
        } else {
            const void* pixels = tex->GetPixels();
            tex_id = rlLoadTexture(pixels, tex->Width, tex->Height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
            rlTextureParameters(tex_id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
            rlTextureParameters(tex_id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
            rlTextureParameters(tex_id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
            rlTextureParameters(tex_id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
            tex->SetTexID((ImTextureID)(uintptr_t)tex_id);
            tex->SetStatus(ImTextureStatus_OK);
        }
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        if (tex->TexID != ImTextureID_Invalid) {
            unsigned int tex_id = (unsigned int)(uintptr_t)tex->TexID;
            rlUnloadTexture(tex_id);
        }
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

static ImFont* g_uiFont = nullptr;
static ImFont* g_monoFont = nullptr;

ImFont* ImGui_ImplRaylib_GetMonoFont() { return g_monoFont; }
ImFont* ImGui_ImplRaylib_GetUiFont() { return g_uiFont; }

bool ImGui_ImplRaylib_LoadFonts(float fontSize) {
    ImGuiIO& io = ImGui::GetIO();

    // Beside the executable first, so an installed copy finds its own fonts wherever it's started.
    std::string exeDir = GetApplicationDirectory();
    std::vector<std::string> searchDirs = {
        exeDir + "assets/fonts",
        exeDir + "../assets/fonts",
        exeDir + "../ide/assets/fonts",
        exeDir + "../../ide/assets/fonts",
        exeDir + "../share/mylo/fonts",
        "assets/fonts",
        "ide/assets/fonts",
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/truetype/liberation"
    };

    std::string uiFontPath;
    const char* uiCandidates[] = {
        "Roboto-Medium.ttf", "DroidSans.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf", nullptr
    };
    for (const auto& dir : searchDirs) {
        for (int i = 0; uiCandidates[i] != nullptr; ++i) {
            std::string p = dir + "/" + uiCandidates[i];
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                uiFontPath = p;
                break;
            }
        }
        if (!uiFontPath.empty()) break;
    }

    std::string monoFontPath;
    const char* monoCandidates[] = {
        "Cousine-Regular.ttf", "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf", nullptr
    };
    for (const auto& dir : searchDirs) {
        for (int i = 0; monoCandidates[i] != nullptr; ++i) {
            std::string p = dir + "/" + monoCandidates[i];
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                monoFontPath = p;
                break;
            }
        }
        if (!monoFontPath.empty()) break;
    }

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;

    if (!uiFontPath.empty()) {
        g_uiFont = io.Fonts->AddFontFromFileTTF(uiFontPath.c_str(), fontSize, &cfg);
    }
    if (!monoFontPath.empty()) {
        g_monoFont = io.Fonts->AddFontFromFileTTF(monoFontPath.c_str(), fontSize, &cfg);
    }

    if (!g_uiFont) {
        g_uiFont = io.Fonts->AddFontDefaultVector(&cfg);
    }
    if (!g_monoFont) {
        g_monoFont = g_uiFont;
    }

    return (g_uiFont != nullptr);
}

bool ImGui_ImplRaylib_Init() {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_raylib";
    io.BackendRendererName = "imgui_impl_raylib_rlgl";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_GetClipboardTextFn = ImGui_ImplRaylib_GetClipboardText;
    platform_io.Platform_SetClipboardTextFn = ImGui_ImplRaylib_SetClipboardText;

    ImGui_ImplRaylib_LoadFonts(14.0f);
    return true;
}

void ImGui_ImplRaylib_Shutdown() {
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    for (ImTextureData* tex : platform_io.Textures) {
        if (tex->TexID != ImTextureID_Invalid) {
            unsigned int tex_id = (unsigned int)(uintptr_t)tex->TexID;
            rlUnloadTexture(tex_id);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    platform_io.ClearPlatformHandlers();
    platform_io.ClearRendererHandlers();
}

static void ImGui_ImplRaylib_UpdateMouseCursor() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
        return;

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
        HideCursor();
    } else {
        ShowCursor();
        switch (imgui_cursor) {
            case ImGuiMouseCursor_Arrow:      SetMouseCursor(MOUSE_CURSOR_ARROW); break;
            case ImGuiMouseCursor_TextInput:  SetMouseCursor(MOUSE_CURSOR_IBEAM); break;
            case ImGuiMouseCursor_ResizeAll:  SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL); break;
            case ImGuiMouseCursor_ResizeEW:   SetMouseCursor(MOUSE_CURSOR_RESIZE_EW); break;
            case ImGuiMouseCursor_ResizeNS:   SetMouseCursor(MOUSE_CURSOR_RESIZE_NS); break;
            case ImGuiMouseCursor_ResizeNESW: SetMouseCursor(MOUSE_CURSOR_RESIZE_NESW); break;
            case ImGuiMouseCursor_ResizeNWSE: SetMouseCursor(MOUSE_CURSOR_RESIZE_NWSE); break;
            case ImGuiMouseCursor_Hand:       SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); break;
            case ImGuiMouseCursor_NotAllowed: SetMouseCursor(MOUSE_CURSOR_NOT_ALLOWED); break;
            default:                          SetMouseCursor(MOUSE_CURSOR_DEFAULT); break;
        }
    }
}

void ImGui_ImplRaylib_NewFrame() {
    ImGuiIO& io = ImGui::GetIO();

    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    if (IsWindowFullscreen()) {
        int monitor = GetCurrentMonitor();
        screen_w = GetMonitorWidth(monitor);
        screen_h = GetMonitorHeight(monitor);
    }
    io.DisplaySize = ImVec2((float)screen_w, (float)screen_h);

    int render_w = GetRenderWidth();
    int render_h = GetRenderHeight();
    float scale_x = (screen_w > 0 && render_w > 0) ? ((float)render_w / (float)screen_w) : 1.0f;
    float scale_y = (screen_h > 0 && render_h > 0) ? ((float)render_h / (float)screen_h) : 1.0f;
#if defined(__APPLE__)
    Vector2 dpi = GetWindowScaleDPI();
    if (dpi.x > 0.0f && dpi.y > 0.0f) {
        scale_x = dpi.x;
        scale_y = dpi.y;
    }
#endif
    io.DisplayFramebufferScale = ImVec2(scale_x, scale_y);

    float dt = GetFrameTime();
    io.DeltaTime = (dt > 0.0f) ? dt : (1.0f / 60.0f);

    // Mouse input
    Vector2 mouse_pos = GetMousePosition();
    io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, IsMouseButtonDown(MOUSE_BUTTON_MIDDLE));
    io.AddMouseButtonEvent(3, IsMouseButtonDown(MOUSE_BUTTON_SIDE));
    io.AddMouseButtonEvent(4, IsMouseButtonDown(MOUSE_BUTTON_EXTRA));

    Vector2 wheel = GetMouseWheelMoveV();
    if (wheel.x != 0.0f || wheel.y != 0.0f) {
        io.AddMouseWheelEvent(wheel.x, wheel.y);
    }

    // Modifier keys
    io.AddKeyEvent(ImGuiMod_Ctrl, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT));
    io.AddKeyEvent(ImGuiMod_Super, IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER));

    // Keys
    for (const auto& entry : g_key_map) {
        if (IsKeyPressed(entry.raylib_key) || IsKeyPressedRepeat(entry.raylib_key)) {
            io.AddKeyEvent(entry.imgui_key, true);
        } else if (IsKeyReleased(entry.raylib_key)) {
            io.AddKeyEvent(entry.imgui_key, false);
        }
    }

    // Character input
    int ch = GetCharPressed();
    while (ch > 0) {
        io.AddInputCharacter((unsigned int)ch);
        ch = GetCharPressed();
    }

    ImGui_ImplRaylib_UpdateMouseCursor();
}

void ImGui_ImplRaylib_RenderDrawData(ImDrawData* draw_data) {
    if (!draw_data) return;

    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0) return;

    if (draw_data->Textures != nullptr) {
        for (ImTextureData* tex : *draw_data->Textures) {
            if (tex->Status != ImTextureStatus_OK) {
                ImGui_ImplRaylib_UpdateTexture(tex);
            }
        }
    }

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthTest();
    rlEnableScissorTest();

    rlViewport(0, 0, fb_width, fb_height);
    rlSetFramebufferWidth(fb_width);
    rlSetFramebufferHeight(fb_height);

    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(
        draw_data->DisplayPos.x,
        draw_data->DisplayPos.x + draw_data->DisplaySize.x,
        draw_data->DisplayPos.y + draw_data->DisplaySize.y,
        draw_data->DisplayPos.y,
        0.0f, 1.0f
    );
    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();

    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback != ImDrawCallback_ResetRenderState) {
                    pcmd->UserCallback(cmd_list, pcmd);
                }
            } else {
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                if (clip_min.x < 0.0f) clip_min.x = 0.0f;
                if (clip_min.y < 0.0f) clip_min.y = 0.0f;
                if (clip_max.x > (float)fb_width) clip_max.x = (float)fb_width;
                if (clip_max.y > (float)fb_height) clip_max.y = (float)fb_height;
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                rlScissor(
                    (int)clip_min.x,
                    fb_height - (int)clip_max.y,
                    (int)(clip_max.x - clip_min.x),
                    (int)(clip_max.y - clip_min.y)
                );

                unsigned int tex_id = (unsigned int)(uintptr_t)pcmd->GetTexID();
                if (tex_id == 0) tex_id = rlGetTextureIdDefault();
                rlBegin(RL_TRIANGLES);
                rlSetTexture(tex_id);

                for (unsigned int i = 0; i < pcmd->ElemCount; i += 3) {
                    if (rlCheckRenderBatchLimit(3)) {
                        rlBegin(RL_TRIANGLES);
                        rlSetTexture(tex_id);
                    }
                    for (unsigned int j = 0; j < 3; j++) {
                        ImDrawIdx index = idx_buffer[pcmd->IdxOffset + i + j];
                        const ImDrawVert& v = vtx_buffer[pcmd->VtxOffset + index];
                        rlColor4ub(
                            (unsigned char)(v.col & 0xFF),
                            (unsigned char)((v.col >> 8) & 0xFF),
                            (unsigned char)((v.col >> 16) & 0xFF),
                            (unsigned char)((v.col >> 24) & 0xFF)
                        );
                        rlTexCoord2f(v.uv.x, v.uv.y);
                        rlVertex2f(v.pos.x, v.pos.y);
                    }
                }

                rlEnd();
                rlDrawRenderBatchActive();
            }
        }
    }

    rlSetTexture(0);
    rlDisableScissorTest();
    rlEnableBackfaceCulling();

    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
}
