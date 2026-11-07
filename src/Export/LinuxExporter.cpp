#include "LinuxExporter.h"
#include "Core/InputManager.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace Export {

std::string LinuxExporter::GenerateLaunchScript(const LinuxExportOptions& options) {
    std::ostringstream ss;
    ss << "#!/usr/bin/env bash\n";
    ss << "# =============================================================================\n";
    ss << "# " << options.gameTitle << " - Linux / SteamOS Game Launcher\n";
    ss << "# Tessera Game Engine - Native Vulkan 1.2 Execution\n";
    ss << "# Compatible with Ubuntu, Debian, Fedora, Arch Linux, and Valve Steam Deck\n";
    ss << "# =============================================================================\n\n";
    ss << "set -e\n";
    ss << "SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
    ss << "cd \"$SCRIPT_DIR\"\n\n";
    ss << "# Set library search path for bundled dynamic libraries\n";
    ss << "export LD_LIBRARY_PATH=\".:$SCRIPT_DIR/lib:$LD_LIBRARY_PATH\"\n\n";
    ss << "# SteamOS / Steam Deck MangoHud & Gamescope flags compatibility\n";
    ss << "export VK_ICD_FILENAMES=\"${VK_ICD_FILENAMES:-}\"\n";
    ss << "export MANGOHUD=\"${MANGOHUD:-0}\"\n\n";
    ss << "# 1. Check for native Linux ELF game runner\n";
    ss << "if [ -f \"./" << options.gameTitle << ".x86_64\" ]; then\n";
    ss << "    chmod +x \"./" << options.gameTitle << ".x86_64\"\n";
    ss << "    exec ./" << options.gameTitle << ".x86_64 \"$@\"\n";
    ss << "elif [ -f \"./TesseraGameRunner.x86_64\" ]; then\n";
    ss << "    chmod +x ./TesseraGameRunner.x86_64\n";
    ss << "    exec ./TesseraGameRunner.x86_64 \"$@\"\n";
    ss << "fi\n\n";
    ss << "# 2. Valve Proton / Wine compatibility runner\n";
    ss << "if [ -f \"./GameRunner.exe\" ] || [ -f \"./" << options.gameTitle << ".exe\" ]; then\n";
    ss << "    TARGET_EXE=\"./" << options.gameTitle << ".exe\"\n";
    ss << "    [ ! -f \"$TARGET_EXE\" ] && TARGET_EXE=\"./GameRunner.exe\"\n\n";
    ss << "    if command -v proton &> /dev/null; then\n";
    ss << "        echo \"[Tessera] Launching via Valve Proton...\"\n";
    ss << "        exec proton run \"$TARGET_EXE\" \"$@\"\n";
    ss << "    elif command -v wine &> /dev/null; then\n";
    ss << "        echo \"[Tessera] Launching via Wine (Vulkan 1.2 Pass-Through)...\"\n";
    ss << "        exec wine \"$TARGET_EXE\" \"$@\"\n";
    ss << "    else\n";
    ss << "        echo \"[Tessera Error] Native binary not found, and Wine/Proton is not in PATH.\"\n";
    ss << "        echo \"To run on Linux, install Wine/Proton or build natively via CMake:\"\n";
    ss << "        echo \"  cmake -B build && cmake --build build\"\n";
    ss << "        exit 1\n";
    ss << "    fi\n";
    ss << "fi\n\n";
    ss << "echo \"[Tessera Error] No game executable found in $SCRIPT_DIR\"\n";
    ss << "exit 1\n";
    return ss.str();
}

std::string LinuxExporter::GenerateDesktopEntry(const LinuxExportOptions& options) {
    std::ostringstream ss;
    ss << "[Desktop Entry]\n";
    ss << "Type=Application\n";
    ss << "Name=" << options.gameTitle << "\n";
    ss << "Comment=" << options.gameTitle << " created with Tessera Engine\n";
    ss << "Exec=bash -c \"$(dirname %k)/launch.sh\"\n";
    ss << "Icon=icon\n";
    ss << "Terminal=false\n";
    ss << "Categories=Game;\n";
    ss << "StartupNotify=true\n";
    return ss.str();
}

std::string LinuxExporter::GenerateReadme(const LinuxExportOptions& options) {
    std::ostringstream ss;
    ss << "===================================================================\n";
    ss << "  " << options.gameTitle << " - Linux Standalone Game Package\n";
    ss << "  Engine: Tessera 3D Game Engine (Vulkan 1.2 / Jolt Physics)\n";
    ss << "===================================================================\n\n";
    ss << "HOW TO RUN ON LINUX / STEAM DECK:\n\n";
    ss << "1. Open a terminal in this game folder:\n";
    ss << "     chmod +x launch.sh\n";
    ss << "     ./launch.sh\n\n";
    ss << "2. ON STEAM DECK / STEAM CLIENT:\n";
    ss << "   - Open Steam on your desktop or Steam Deck.\n";
    ss << "   - Click Games -> 'Add a Non-Steam Game to My Library...'\n";
    ss << "   - Browse to 'launch.sh' (or the game executable) and add it.\n";
    ss << "   - Enjoy with full gamepad/controller and custom resolution support!\n\n";
    ss << "SYSTEM REQUIREMENTS:\n";
    ss << " - 64-bit Linux distribution (Ubuntu 20.04+, Fedora 36+, Arch, SteamOS 3+)\n";
    ss << " - Vulkan 1.2 capable GPU (NVIDIA 440+, AMD RADV Mesa 20+, Intel Mesa 20+)\n";
    ss << " - X11 or Wayland display server\n";
    return ss.str();
}

bool LinuxExporter::Export(const Scene& scene, const LinuxExportOptions& options, std::string& outStatusMessage) {
    try {
        fs::path dest(options.destinationDir);
        fs::create_directories(dest);

        // 1. Write Linux launch script
        std::string launchScript = GenerateLaunchScript(options);
        std::ofstream launchFile(dest / "launch.sh", std::ios::binary);
        if (!launchFile.is_open()) {
            outStatusMessage = "Failed to write launch.sh";
            return false;
        }
        // Write UNIX line endings (LF only)
        for (char c : launchScript) {
            if (c != '\r') launchFile.put(c);
        }
        launchFile.close();

        // 2. Write FreeDesktop .desktop launcher
        std::string desktopEntry = GenerateDesktopEntry(options);
        std::ofstream desktopFile(dest / (options.gameTitle + ".desktop"), std::ios::binary);
        if (desktopFile.is_open()) {
            for (char c : desktopEntry) {
                if (c != '\r') desktopFile.put(c);
            }
            desktopFile.close();
        }

        // 3. Write README_LINUX.txt
        std::string readme = GenerateReadme(options);
        std::ofstream readmeFile(dest / "README_LINUX.txt");
        if (readmeFile.is_open()) {
            readmeFile << readme;
            readmeFile.close();
        }

        // 4. Staging Shaders directory
        fs::create_directories(dest / "shaders");
        fs::path shadersSrc = "shaders";
        if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = "build/shaders";
        if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = "build/Release/shaders";
        if (fs::exists(shadersSrc)) {
            for (const auto& entry : fs::directory_iterator(shadersSrc)) {
                if (entry.is_regular_file()) {
                    fs::copy_file(entry.path(), dest / "shaders" / entry.path().filename(), fs::copy_options::overwrite_existing);
                }
            }
        }

        // 5. Staging Scenes directory
        fs::create_directories(dest / "scenes");
        fs::path scenesSrc = "scenes";
        if (!options.projectRoot.empty() && fs::exists(fs::path(options.projectRoot) / "scenes")) {
            scenesSrc = fs::path(options.projectRoot) / "scenes";
        }
        if (fs::exists(scenesSrc)) {
            for (const auto& entry : fs::directory_iterator(scenesSrc)) {
                if (entry.is_regular_file() && entry.path().extension() == ".scene") {
                    fs::copy_file(entry.path(), dest / "scenes" / entry.path().filename(), fs::copy_options::overwrite_existing);
                }
            }
        }

        std::string targetSceneName = options.startScene.empty() ? "main.scene" : options.startScene;
        targetSceneName = fs::path(targetSceneName).filename().string();
        if (targetSceneName.length() < 6 || targetSceneName.substr(targetSceneName.length() - 6) != ".scene") {
            targetSceneName += ".scene";
        }
        fs::path outSceneFile = dest / "scenes" / targetSceneName;
        scene.SaveToFile(outSceneFile.string());

        // 6. Staging Assets directory
        fs::path assetsSrc = "assets";
        if (!options.projectRoot.empty() && fs::exists(fs::path(options.projectRoot) / "assets")) {
            assetsSrc = fs::path(options.projectRoot) / "assets";
        }
        if (fs::exists(assetsSrc)) {
            fs::create_directories(dest / "assets");
            fs::copy(assetsSrc, dest / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }

        // 7. Staging Scripts (.lua) directory
        fs::path scriptsSrc = "scripts";
        if (!options.projectRoot.empty() && fs::exists(fs::path(options.projectRoot) / "scripts")) {
            scriptsSrc = fs::path(options.projectRoot) / "scripts";
        }
        if (fs::exists(scriptsSrc)) {
            fs::create_directories(dest / "scripts");
            fs::copy(scriptsSrc, dest / "scripts", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }

        // 8. Export input_bindings.json
        Core::InputManager::Get().SaveToFile((dest / "input_bindings.json").string());

        // 9. Write game.agconfig manifest
        JsonValue config = JsonValue::Object();
        config["gameTitle"] = options.gameTitle;
        config["startingScene"] = options.startScene.empty() ? "scenes/main.scene" : (options.startScene.rfind("scenes/", 0) == 0 ? options.startScene : ("scenes/" + options.startScene));
        config["fullscreen"] = options.fullscreen;
        config["windowWidth"] = options.windowWidth;
        config["windowHeight"] = options.windowHeight;
        config["targetPlatform"] = "Linux";

        std::ofstream cfgFile(dest / "game.agconfig");
        if (cfgFile.is_open()) {
            cfgFile << config.Serialize(2);
            cfgFile.close();
        }

                // 10. Copy native Linux ELF template if available (Godot-style export template)
        fs::path nativeTemplate = "templates/linux/TesseraGameRunner.x86_64";
        if (!fs::exists(nativeTemplate)) nativeTemplate = "templates/linux/" + options.gameTitle + ".x86_64";
        if (!fs::exists(nativeTemplate)) nativeTemplate = "bin/linux/TesseraGameRunner.x86_64";
        if (!fs::exists(nativeTemplate)) nativeTemplate = "build-linux/Tessera";
        if (!fs::exists(nativeTemplate)) nativeTemplate = "build/Tessera";
        if (!fs::exists(nativeTemplate)) nativeTemplate = "build-linux/TesseraGameEngine";
        if (!fs::exists(nativeTemplate)) nativeTemplate = "build/TesseraGameEngine";
        if (fs::exists(nativeTemplate)) {
            fs::copy_file(nativeTemplate, dest / (options.gameTitle + ".x86_64"), fs::copy_options::overwrite_existing);
            std::cout << "[LinuxExporter] Bundled native Linux binary: " << options.gameTitle << ".x86_64" << std::endl;
        }

        // 10b. Copy GameRunner / Binary for Linux execution
        if (options.includeDualProtonRunner) {
            fs::path curExe = "build/Release/Tessera.exe";
            if (!fs::exists(curExe)) curExe = "build/Debug/Tessera.exe";
            if (!fs::exists(curExe)) curExe = "build/Release/TesseraGameEngine.exe";
            if (!fs::exists(curExe)) curExe = "build/Debug/TesseraGameEngine.exe";
            if (fs::exists(curExe)) {
                fs::copy_file(curExe, dest / "GameRunner.exe", fs::copy_options::overwrite_existing);
            }

            // Copy runtime DLLs for Proton/Wine pass-through
            fs::path binDir = curExe.parent_path();
            if (fs::exists(binDir)) {
                for (const auto& entry : fs::directory_iterator(binDir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                        fs::copy_file(entry.path(), dest / entry.path().filename(), fs::copy_options::overwrite_existing);
                    }
                }
            }
        }

        outStatusMessage = "Linux game distribution packaged successfully to: " + dest.string();
        std::cout << "[LinuxExporter] " << outStatusMessage << std::endl;
        return true;
    } catch (const std::exception& e) {
        outStatusMessage = std::string("Linux export error: ") + e.what();
        std::cerr << "[LinuxExporter] " << outStatusMessage << std::endl;
        return false;
    }
}

} // namespace Export
