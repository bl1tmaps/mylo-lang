// Mylo IDE: edit, run and debug Mylo scripts.
//
//   mylo-ide [script...]

#include "ide_app.hpp"
#include "ide_style.h"
#include "imgui.h"
#include "imgui_impl_raylib.hpp"
#include "raylib.h"
#include "rlgl.h"

extern "C" {
#include "version.h"
}

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#endif

namespace {

void printUsage() {
    std::cout << "Mylo IDE " << MYLO_VERSION_FULL << " - edit, run and debug Mylo scripts\n\n"
              << "Usage:\n"
              << "  mylo-ide [file...]    Open scripts, creating any that don't exist when saved\n"
              << "  mylo-ide --version    Show the version\n"
              << "  mylo-ide --help       Show this help message\n"
              << "  mylo-ide --automate FILE script.mylo\n"
              << "                        Run IDE commands from FILE, one per line (for tests)\n\n"
              << "The IDE runs scripts with the mylo executable beside it (or on PATH), which can be\n"
              << "changed in File > Settings.\n";
}

// Where settings and the window layout are kept. MYLO_IDE_CONFIG overrides it, for tests.
std::string configDirectory() {
    namespace fs = std::filesystem;
    if (const char* custom = std::getenv("MYLO_IDE_CONFIG"); custom && *custom) return custom;
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA")) return (fs::path(appData) / "MyloIDE").string();
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) return (fs::path(home) / "Library" / "Application Support" / "MyloIDE").string();
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return (fs::path(xdg) / "mylo-ide").string();
    if (const char* home = std::getenv("HOME")) return (fs::path(home) / ".config" / "mylo-ide").string();
#endif
    return ".mylo-ide";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    std::vector<std::string> automation;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--automate" && i + 1 < argc) {
            std::ifstream in(argv[++i]);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty() && line[0] != '#') automation.push_back(line);
            }
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << "Mylo IDE " << MYLO_VERSION_FULL << "\n";
            return 0;
        }
        if (arg.rfind("-", 0) == 0) {
            std::cerr << "Unknown option: " << arg << "\n\n";
            printUsage();
            return 1;
        }
        paths.push_back(arg);
    }

#ifndef _WIN32
    // Writing to a program that has just exited must not end the IDE.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1400, 860, "Mylo IDE");
    if (!IsWindowReady()) {
        std::cerr << "Error: could not open a window." << std::endl;
        return 1;
    }
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui_ImplRaylib_Init();
    SetupImGuiStyle();
    // The shared style's light table headers make their text hard to read here.
    ImGui::GetStyle().Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.20f, 0.24f, 0.33f, 1.0f);

    bool automationFailed = false;
    {
        IdeApp app(configDirectory());
        io.IniFilename = app.layoutPath();   // remembers where panels are docked

        for (const auto& path : paths) app.openDocument(path);
        if (app.isEmpty()) app.newDocument();
        app.setAutomation(automation);

        while (!app.isDone()) {
            if (WindowShouldClose()) app.requestQuit();

            if (IsFileDropped()) {
                FilePathList dropped = LoadDroppedFiles();
                for (unsigned int i = 0; i < dropped.count; ++i) app.openDocument(dropped.paths[i]);
                UnloadDroppedFiles(dropped);
            }

            ImGui_ImplRaylib_NewFrame();
            ImGui::NewFrame();
            app.update();
            ImGui::Render();

            BeginDrawing();
            ClearBackground(BACKGROUND_COLOR);
            ImGui_ImplRaylib_RenderDrawData(ImGui::GetDrawData());
            std::string shot = app.takeScreenshotRequest();
            if (!shot.empty()) {
                // The framebuffer's real size; LoadImageFromScreen assumes display scaling applies.
                int w = GetRenderWidth(), h = GetRenderHeight();
                Image image = {rlReadScreenPixels(w, h), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                ExportImage(image, shot.c_str());
                UnloadImage(image);
            }
            EndDrawing();
        }
        automationFailed = app.automationFailed();
        ImGui::SaveIniSettingsToDisk(io.IniFilename);
        io.IniFilename = nullptr;   // the path belongs to app, which is about to go
    }

    ImGui_ImplRaylib_Shutdown();
    ImGui::DestroyContext();
    CloseWindow();
    return automationFailed ? 2 : 0;
}
