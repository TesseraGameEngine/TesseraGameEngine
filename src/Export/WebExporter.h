#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "Scene/Scene.h"

namespace Export {

enum class WebCanvasMode {
    Responsive = 0,     // Letterboxed responsive scaling
    FixedResolution = 1, // Fixed width x height
    FillWindow = 2       // 100vw x 100vh full-window fill
};

struct WebExportOptions {
    std::string destinationDir{"exports/MyGame_Web"};
    std::string gameTitle{"Tessera Web Game"};
    std::string startScene{"scenes/main.scene"};
    std::string projectRoot{""};
    WebCanvasMode canvasMode{WebCanvasMode::Responsive};
    int canvasWidth{1280};
    int canvasHeight{720};
    bool enablePBR{true};
    bool enableShadows{true};
    bool enableAudio{true};
    bool enableTouchControls{true};
    bool includeLocalServerHelper{true};
    bool embedSceneInHTML{true}; // Embeds scene payload directly into index.html for direct file:// or http:// loading
};

class WebExporter {
public:
    // Exports the active scene and project dependencies into a complete Web/HTML5 distribution folder
    static bool Export(const Scene& scene, const WebExportOptions& options, std::string& outStatusMessage);

    // Launches the local HTTP server or opens the exported HTML5 game in the user's default browser
    static bool LaunchInBrowser(const std::string& destinationDir, uint16_t port = 8080);

    // Generators
    static std::string GenerateIndexHTML(const Scene& scene, const WebExportOptions& options, const std::string& sceneJson);
    static std::string GenerateEngineRuntimeJS(const WebExportOptions& options);
    static std::string GenerateServerPy(uint16_t port);
    static std::string GenerateServerBat(uint16_t port);
};

} // namespace Export

