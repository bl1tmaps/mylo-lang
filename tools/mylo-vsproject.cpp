#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    fs::path targetDir = fs::current_path();

    if (argc > 1) {
        targetDir /= argv[1];
        if (!fs::exists(targetDir)) {
            if (!fs::create_directories(targetDir)) {
                std::cerr << "Failed to create directory: " << targetDir << std::endl;
                return 1;
            }
        }
    }

    // 1. Create main.mylo
    fs::path mainMyloPath = targetDir / "main.mylo";
    if (!fs::exists(mainMyloPath)) {
        std::ofstream mainFile(mainMyloPath);
        if (mainFile.is_open()) {
            mainFile << "print(\"Hello World!\")\n";
            mainFile.close();
            std::cout << "Created main.mylo\n";
        } else {
            std::cerr << "Failed to create main.mylo\n";
        }
    } else {
        std::cout << "main.mylo already exists, skipping.\n";
    }

    // 2. Create .vscode directory
    fs::path vscodeDir = targetDir / ".vscode";
    if (!fs::exists(vscodeDir)) {
        fs::create_directories(vscodeDir);
    }

    // 3. Create launch.json
    fs::path launchJsonPath = vscodeDir / "launch.json";
    if (!fs::exists(launchJsonPath)) {
        std::ofstream launchFile(launchJsonPath);
        if (launchFile.is_open()) {
            launchFile << "{\n";
            launchFile << "  \"version\": \"0.2.0\",\n";
            launchFile << "  \"configurations\": [\n";
            launchFile << "    {\n";
            launchFile << "      \"type\": \"mylo\",\n";
            launchFile << "      \"name\": \"Debug Mylo File\",\n";
            launchFile << "      \"request\": \"launch\",\n";
            launchFile << "      \"program\": \"${file}\"\n";
            launchFile << "    }\n";
            launchFile << "  ]\n";
            launchFile << "}\n";
            launchFile.close();
            std::cout << "Created .vscode/launch.json\n";
        } else {
            std::cerr << "Failed to create .vscode/launch.json\n";
        }
    } else {
        std::cout << ".vscode/launch.json already exists, skipping.\n";
    }

    std::cout << "Mylo project initialized successfully in " << targetDir << "!\n";
    return 0;
}
