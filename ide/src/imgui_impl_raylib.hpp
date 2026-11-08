#pragma once

#include "imgui.h"

bool ImGui_ImplRaylib_Init();
void ImGui_ImplRaylib_Shutdown();
void ImGui_ImplRaylib_NewFrame();
void ImGui_ImplRaylib_RenderDrawData(ImDrawData* draw_data);

ImFont* ImGui_ImplRaylib_GetMonoFont();
ImFont* ImGui_ImplRaylib_GetUiFont();
bool ImGui_ImplRaylib_LoadFonts(float fontSize = 16.0f);
