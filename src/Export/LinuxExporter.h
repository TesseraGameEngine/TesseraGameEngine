#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "Scene/Scene.h"

namespace Export {

struct LinuxExportOptions {
    std::string destinationDir{"exports/MyGame_Linux"};
    std::string gameTitle{"MyGame"};
    std::string startScene{"scenes/main.scene"};
    std::string projectRoot{""};
    bool fullscreen{false};
    int windowWidth{1600};
    int windowHeight{900};
    bool includeSteamDeckLauncher{true};
    bool createTarGzArchive{true};
    bool includeDualProtonRunner{true};
};

class LinuxExporter {
public:
    // Exports the active scene and project dependencies into a complete standalone Linux game distribution
    static bool Export(const Scene& scene, const LinuxExportOptions& options, std::string& outStatusMessage);

    // Generators for Linux platform assets
    static std::string GenerateLaunchScript(const LinuxExportOptions& options);
    static std::string GenerateDesktopEntry(const LinuxExportOptions& options);
    static std::string GenerateReadme(const LinuxExportOptions& options);
};

} // namespace Export

