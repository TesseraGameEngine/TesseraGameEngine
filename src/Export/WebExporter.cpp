#include "WebExporter.h"
#include "Utils/SimpleJson.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace Export {

bool WebExporter::Export(const Scene& scene, const WebExportOptions& options, std::string& outStatusMessage) {
    namespace fs = std::filesystem;
    try {
        fs::path dest(options.destinationDir);
        fs::create_directories(dest);
        fs::create_directories(dest / "scenes");
        fs::create_directories(dest / "assets");

        // 1. Serialize active starting scene to JSON
        std::string sceneJson = scene.Serialize().Serialize(2);
        std::string targetSceneName = options.startScene;
        if (targetSceneName.empty()) targetSceneName = "main.scene";
        if (targetSceneName.find('/') != std::string::npos || targetSceneName.find('\\') != std::string::npos) {
            targetSceneName = fs::path(targetSceneName).filename().string();
        }
        if (targetSceneName.length() < 6 || targetSceneName.substr(targetSceneName.length() - 6) != ".scene") {
            targetSceneName += ".scene";
        }

        fs::path sceneOutPath = dest / "scenes" / targetSceneName;
        std::ofstream sceneFile(sceneOutPath);
        if (sceneFile.is_open()) {
            sceneFile << sceneJson;
            sceneFile.close();
        }

        // 2. Copy Project Assets if present
        auto copyDirIfExists = [](const fs::path& src, const fs::path& dst) {
            if (fs::exists(src) && fs::is_directory(src)) {
                std::error_code ec;
                fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            }
        };

        if (!options.projectRoot.empty()) {
            copyDirIfExists(fs::path(options.projectRoot) / "assets", dest / "assets");
            copyDirIfExists(fs::path(options.projectRoot) / "scenes", dest / "scenes");
            copyDirIfExists(fs::path(options.projectRoot) / "scripts", dest / "assets" / "scripts");
        } else {
            copyDirIfExists("assets", dest / "assets");
            copyDirIfExists("scenes", dest / "scenes");
            copyDirIfExists("scripts", dest / "assets" / "scripts");
        }

        // 3. Write game.agconfig manifest
        JsonValue config = JsonValue::Object();
        config["gameTitle"] = options.gameTitle;
        config["startingScene"] = "scenes/" + targetSceneName;
        config["targetPlatform"] = "Web";
        config["canvasWidth"] = options.canvasWidth;
        config["canvasHeight"] = options.canvasHeight;
        config["canvasMode"] = static_cast<int>(options.canvasMode);
        config["enablePBR"] = options.enablePBR;
        config["enableShadows"] = options.enableShadows;
        config["enableAudio"] = options.enableAudio;

        std::ofstream cfgFile(dest / "game.agconfig");
        if (cfgFile.is_open()) {
            cfgFile << config.Serialize(2);
            cfgFile.close();
        }

        // 4. Generate index.html
        std::string htmlContent = GenerateIndexHTML(scene, options, sceneJson);
        std::ofstream htmlFile(dest / "index.html");
        if (!htmlFile.is_open()) {
            outStatusMessage = "Failed to create index.html in " + dest.string();
            return false;
        }
        htmlFile << htmlContent;
        htmlFile.close();

        // 5. Generate tessera_web.js (Full standalone WebGL 2.0 3D Engine Runtime)
        std::string jsContent = GenerateEngineRuntimeJS(options);
        std::ofstream jsFile(dest / "tessera_web.js");
        if (!jsFile.is_open()) {
            outStatusMessage = "Failed to create tessera_web.js in " + dest.string();
            return false;
        }
        jsFile << jsContent;
        jsFile.close();

        // 6. Generate local server helper scripts for 1-click test launching
        if (options.includeLocalServerHelper) {
            std::string pyContent = GenerateServerPy(8080);
            std::ofstream pyFile(dest / "serve.py");
            if (pyFile.is_open()) {
                pyFile << pyContent;
                pyFile.close();
            }

            std::string batContent = GenerateServerBat(8080);
            std::ofstream batFile(dest / "start_server.bat");
            if (batFile.is_open()) {
                batFile << batContent;
                batFile.close();
            }
        }

        outStatusMessage = "Web / HTML5 game package successfully exported to: " + dest.string();
        std::cout << "[WebExporter] " << outStatusMessage << std::endl;
        return true;
    } catch (const std::exception& e) {
        outStatusMessage = std::string("Web export error: ") + e.what();
        std::cerr << "[WebExporter] " << outStatusMessage << std::endl;
        return false;
    }
}

bool WebExporter::LaunchInBrowser(const std::string& destinationDir, uint16_t port) {
#ifdef _WIN32
    namespace fs = std::filesystem;
    fs::path dest(destinationDir);
    fs::path batPath = dest / "start_server.bat";
    if (fs::exists(batPath)) {
        HINSTANCE res = ShellExecuteA(NULL, "open", batPath.string().c_str(), NULL, dest.string().c_str(), SW_SHOW);
        return (reinterpret_cast<intptr_t>(res) > 32);
    }
    // Fallback: Open index.html directly
    fs::path indexPath = dest / "index.html";
    HINSTANCE res = ShellExecuteA(NULL, "open", indexPath.string().c_str(), NULL, dest.string().c_str(), SW_SHOW);
    return (reinterpret_cast<intptr_t>(res) > 32);
#else
    return false;
#endif
}

std::string WebExporter::GenerateServerPy(uint16_t port) {
    std::ostringstream ss;
    ss << "#!/usr/bin/env python3\n"
       << "# Tessera Engine - Local Development Web Server\n"
       << "import http.server\n"
       << "import socketserver\n"
       << "import webbrowser\n"
       << "import os\n"
       << "import sys\n\n"
       << "PORT = " << port << "\n"
       << "os.chdir(os.path.dirname(os.path.abspath(__file__)))\n\n"
       << "class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):\n"
       << "    def end_headers(self):\n"
       << "        self.send_header('Access-Control-Allow-Origin', '*')\n"
       << "        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')\n"
       << "        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')\n"
       << "        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')\n"
       << "        super().end_headers()\n\n"
       << "url = f'http://localhost:{PORT}/index.html'\n"
       << "print(f'Starting Tessera Web Game at {url}...')\n"
       << "webbrowser.open(url)\n\n"
       << "with socketserver.TCPServer(('', PORT), CORSRequestHandler) as httpd:\n"
       << "    print(f'Serving HTTP on 0.0.0.0 port {PORT} (http://localhost:{PORT}/) ...')\n"
       << "    try:\n"
       << "        httpd.serve_forever()\n"
       << "    except KeyboardInterrupt:\n"
       << "        print('\\nServer stopped.')\n";
    return ss.str();
}

std::string WebExporter::GenerateServerBat(uint16_t port) {
    std::ostringstream ss;
    ss << "@echo off\n"
       << "title Tessera Web Game Runner (Port " << port << ")\n"
       << "cd /d \"%~dp0\"\n"
       << "echo ==========================================================\n"
       << "echo   Tessera Engine - Local Web Server Launcher\n"
       << "echo   Opening Web Game at http://localhost:" << port << "/index.html\n"
       << "echo ==========================================================\n"
       << "echo.\n"
       << "start http://localhost:" << port << "/index.html\n"
       << "python -m http.server " << port << "\n"
       << "if %ERRORLEVEL% NEQ 0 (\n"
       << "    echo Python not found in PATH, attempting Node.js npx serve...\n"
       << "    npx serve -p " << port << " .\n"
       << ")\n"
       << "pause\n";
    return ss.str();
}

std::string WebExporter::GenerateIndexHTML(const Scene& scene, const WebExportOptions& options, const std::string& sceneJson) {
    std::ostringstream ss;
    ss << "<!DOCTYPE html>\n"
       << "<html lang=\"en\">\n"
       << "<head>\n"
       << "    <meta charset=\"UTF-8\">\n"
       << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
       << "    <title>" << options.gameTitle << " - Tessera Engine</title>\n"
       << "    <style>\n"
       << "        * { box-sizing: border-box; margin: 0; padding: 0; }\n"
       << "        html, body {\n"
       << "            width: 100%; height: 100%; overflow: hidden;\n"
       << "            background: #0d0f13; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;\n"
       << "            color: #d8e2ec; user-select: none; -webkit-user-select: none;\n"
       << "        }\n"
       << "        #game-wrapper {\n"
       << "            position: relative; width: 100vw; height: 100vh;\n"
       << "            display: flex; flex-direction: column; align-items: center; justify-content: center;\n"
       << "            background: #0d0f13;\n"
       << "        }\n"
       << "        #canvas-container {\n"
       << "            position: relative; overflow: hidden;\n";

    if (options.canvasMode == WebCanvasMode::FillWindow) {
        ss << "            width: 100vw; height: 100vh;\n";
    } else if (options.canvasMode == WebCanvasMode::FixedResolution) {
        ss << "            width: " << options.canvasWidth << "px; height: " << options.canvasHeight << "px;\n"
           << "            box-shadow: 0 10px 40px rgba(0,0,0,0.7); border-radius: 6px;\n";
    } else {
        // Responsive aspect-ratio scaling
        ss << "            width: 100%; height: 100%;\n"
           << "            max-width: 100vw; max-height: 100vh;\n"
           << "            display: flex; align-items: center; justify-content: center;\n";
    }

    ss << "        }\n"
       << "        #tessera-canvas {\n"
       << "            display: block; width: 100%; height: 100%;\n"
       << "            outline: none;\n"
       << "        }\n"
       << "        /* Top Overlay Bar */\n"
       << "        #engine-bar {\n"
       << "            position: absolute; top: 0; left: 0; right: 0; height: 38px;\n"
       << "            background: rgba(14, 17, 23, 0.88); backdrop-filter: blur(8px);\n"
       << "            border-bottom: 1px solid rgba(40, 50, 70, 0.6);\n"
       << "            display: flex; align-items: center; justify-content: space-between;\n"
       << "            padding: 0 16px; z-index: 50; transition: opacity 0.3s ease;\n"
       << "        }\n"
       << "        #engine-bar.autohide { opacity: 0; pointer-events: none; }\n"
       << "        #engine-bar:hover, #engine-bar.active { opacity: 1; pointer-events: auto; }\n"
       << "        .brand-badge {\n"
       << "            display: flex; align-items: center; gap: 8px;\n"
       << "            font-weight: 700; font-size: 13px; letter-spacing: 0.5px;\n"
       << "            color: #38bdf8;\n"
       << "        }\n"
       << "        .brand-logo {\n"
       << "            width: 18px; height: 18px;\n"
       << "            background: linear-gradient(135deg, #0284c7, #38bdf8);\n"
       << "            border-radius: 50%; display: inline-block;\n"
       << "        }\n"
       << "        .game-title { font-size: 13px; color: #94a3b8; font-weight: 500; margin-left: 12px; }\n"
       << "        .bar-controls { display: flex; align-items: center; gap: 8px; }\n"
       << "        .btn-tool {\n"
       << "            background: rgba(30, 41, 59, 0.8); border: 1px solid rgba(71, 85, 105, 0.5);\n"
       << "            color: #e2e8f0; font-size: 12px; padding: 4px 10px; border-radius: 4px;\n"
       << "            cursor: pointer; display: flex; align-items: center; gap: 5px;\n"
       << "            transition: background 0.15s, border-color 0.15s;\n"
       << "        }\n"
       << "        .btn-tool:hover { background: #0284c7; border-color: #38bdf8; color: #fff; }\n"
       << "        /* Loading / Play Splash Overlay */\n"
       << "        #splash-screen {\n"
       << "            position: absolute; inset: 0;\n"
       << "            background: radial-gradient(circle at center, #1e293b 0%, #0a0e17 100%);\n"
       << "            display: flex; flex-direction: column; align-items: center; justify-content: center;\n"
       << "            z-index: 100; transition: opacity 0.5s ease;\n"
       << "        }\n"
       << "        #splash-screen.hidden { opacity: 0; pointer-events: none; }\n"
       << "        .splash-title {\n"
       << "            font-size: 28px; font-weight: 800; color: #f8fafc;\n"
       << "            margin-bottom: 8px; letter-spacing: 0.5px;\n"
       << "        }\n"
       << "        .splash-sub {\n"
       << "            font-size: 14px; color: #38bdf8; margin-bottom: 30px;\n"
       << "        }\n"
       << "        .spinner {\n"
       << "            width: 48px; height: 48px;\n"
       << "            border: 4px solid rgba(56, 189, 248, 0.2);\n"
       << "            border-top-color: #38bdf8; border-radius: 50%;\n"
       << "            animation: spin 0.8s linear infinite;\n"
       << "            margin-bottom: 20px;\n"
       << "        }\n"
       << "        @keyframes spin { to { transform: rotate(360deg); } }\n"
       << "        .btn-play {\n"
       << "            background: linear-gradient(135deg, #0284c7, #0ea5e9);\n"
       << "            border: none; color: #fff; font-size: 16px; font-weight: 600;\n"
       << "            padding: 12px 36px; border-radius: 8px; cursor: pointer;\n"
       << "            box-shadow: 0 4px 18px rgba(14, 165, 233, 0.45);\n"
       << "            transition: transform 0.15s, box-shadow 0.15s;\n"
       << "        }\n"
       << "        .btn-play:hover { transform: translateY(-2px); box-shadow: 0 6px 22px rgba(14, 165, 233, 0.6); }\n"
       << "        .btn-play:active { transform: translateY(0); }\n"
       << "        /* Mobile Touch Controls */\n"
       << "        #touch-controls {\n"
       << "            position: absolute; inset: 0; pointer-events: none; z-index: 40;\n"
       << "            display: none;\n"
       << "        }\n"
       << "        .touch-pad {\n"
       << "            position: absolute; bottom: 30px; left: 30px; width: 120px; height: 120px;\n"
       << "            border-radius: 50%; background: rgba(255, 255, 255, 0.12);\n"
       << "            border: 2px solid rgba(255, 255, 255, 0.25); pointer-events: auto;\n"
       << "        }\n"
       << "        .touch-btn {\n"
       << "            position: absolute; bottom: 35px; right: 35px; width: 70px; height: 70px;\n"
       << "            border-radius: 50%; background: rgba(56, 189, 248, 0.35);\n"
       << "            border: 2px solid #38bdf8; pointer-events: auto; color: #fff;\n"
       << "            display: flex; align-items: center; justify-content: center; font-weight: bold;\n"
       << "        }\n"
       << "    </style>\n"
       << "</head>\n"
       << "<body>\n"
       << "    <div id=\"game-wrapper\">\n"
       << "        <!-- Top Engine Toolbar -->\n"
       << "        <header id=\"engine-bar\">\n"
       << "            <div class=\"brand-badge\">\n"
       << "                <span class=\"brand-logo\"></span>\n"
       << "                <span>TESSERA</span>\n"
       << "                <span class=\"game-title\">" << options.gameTitle << "</span>\n"
       << "            </div>\n"
       << "            <div class=\"bar-controls\">\n"
       << "                <button id=\"btn-audio\" class=\"btn-tool\" title=\"Toggle Audio\">Audio: ON</button>\n"
       << "                <button id=\"btn-pause\" class=\"btn-tool\" title=\"Pause / Resume (P)\">Pause</button>\n"
       << "                <button id=\"btn-fullscreen\" class=\"btn-tool\" title=\"Toggle Fullscreen (F11)\">Fullscreen</button>\n"
       << "            </div>\n"
       << "        </header>\n\n"
       << "        <!-- Canvas Viewport -->\n"
       << "        <div id=\"canvas-container\">\n"
       << "            <canvas id=\"tessera-canvas\" width=\"" << options.canvasWidth << "\" height=\"" << options.canvasHeight << "\"></canvas>\n"
       << "        </div>\n\n"
       << "        <!-- Loading & Start Splash -->\n"
       << "        <div id=\"splash-screen\">\n"
       << "            <h1 class=\"splash-title\">" << options.gameTitle << "</h1>\n"
       << "            <p class=\"splash-sub\">Tessera WebGL 2.0 Engine</p>\n"
       << "            <div id=\"spinner-box\" class=\"spinner\"></div>\n"
       << "            <button id=\"btn-start\" class=\"btn-play\" style=\"display:none;\">CLICK TO PLAY</button>\n"
       << "        </div>\n\n"
       << "        <!-- Mobile Touch Controls -->\n"
       << "        <div id=\"touch-controls\">\n"
       << "            <div id=\"joystick-zone\" class=\"touch-pad\"></div>\n"
       << "            <div id=\"action-jump\" class=\"touch-btn\">JUMP</div>\n"
       << "        </div>\n"
       << "    </div>\n\n";

    // Embed Starting Scene JSON payload directly for zero CORS friction when loading via file:// or http://
    if (options.embedSceneInHTML) {
        ss << "    <!-- Embedded Tessera Scene Payload -->\n"
           << "    <script id=\"tessera-scene-data\" type=\"application/json\">\n"
           << sceneJson << "\n"
           << "    </script>\n\n";
    }

    ss << "    <!-- Tessera WebGL 2.0 Engine Runtime -->\n"
       << "    <script src=\"tessera_web.js\"></script>\n"
       << "</body>\n"
       << "</html>\n";

    return ss.str();
}

std::string WebExporter::GenerateEngineRuntimeJS(const WebExportOptions& options) {
    std::ostringstream ss;
    ss << "/**\n"
       << " * Tessera 3D WebGL 2.0 Game Engine Runtime\n"
       << " * Built for: " << options.gameTitle << "\n"
       << " * Features: PBR lighting, procedural meshes, OBJ loading, character controller, scene graph, Web Audio, mobile touch\n"
       << " */\n"
       << "(function() {\n"
       << "    'use strict';\n\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    // 1. Math Library (Vec3, Mat4, Quat)\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    const Vec3 = {\n"
       << "        create: (x = 0, y = 0, z = 0) => new Float32Array([x, y, z]),\n"
       << "        add: (out, a, b) => { out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2]; return out; },\n"
       << "        sub: (out, a, b) => { out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2]; return out; },\n"
       << "        scale: (out, a, s) => { out[0] = a[0] * s; out[1] = a[1] * s; out[2] = a[2] * s; return out; },\n"
       << "        length: (a) => Math.hypot(a[0], a[1], a[2]),\n"
       << "        normalize: (out, a) => {\n"
       << "            const len = Math.hypot(a[0], a[1], a[2]);\n"
       << "            if (len > 0.00001) { out[0] = a[0] / len; out[1] = a[1] / len; out[2] = a[2] / len; }\n"
       << "            return out;\n"
       << "        },\n"
       << "        cross: (out, a, b) => {\n"
       << "            const ax = a[0], ay = a[1], az = a[2], bx = b[0], by = b[1], bz = b[2];\n"
       << "            out[0] = ay * bz - az * by; out[1] = az * bx - ax * bz; out[2] = ax * by - ay * bx;\n"
       << "            return out;\n"
       << "        },\n"
       << "        dot: (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2]\n"
       << "    };\n\n"
       << "    const Mat4 = {\n"
       << "        create: () => new Float32Array([\n"
       << "            1, 0, 0, 0,\n"
       << "            0, 1, 0, 0,\n"
       << "            0, 0, 1, 0,\n"
       << "            0, 0, 0, 1\n"
       << "        ]),\n"
       << "        identity: (out) => {\n"
       << "            out.fill(0);\n"
       << "            out[0] = out[5] = out[10] = out[15] = 1;\n"
       << "            return out;\n"
       << "        },\n"
       << "        perspective: (out, fovRad, aspect, near, far) => {\n"
       << "            const f = 1.0 / Math.tan(fovRad / 2);\n"
       << "            const nf = 1 / (near - far);\n"
       << "            out.fill(0);\n"
       << "            out[0] = f / aspect;\n"
       << "            out[5] = f;\n"
       << "            out[10] = (far + near) * nf;\n"
       << "            out[11] = -1;\n"
       << "            out[14] = (2 * far * near) * nf;\n"
       << "            return out;\n"
       << "        },\n"
       << "        lookAt: (out, eye, center, up) => {\n"
       << "            const z = Vec3.create();\n"
       << "            Vec3.sub(z, eye, center);\n"
       << "            Vec3.normalize(z, z);\n"
       << "            const x = Vec3.create();\n"
       << "            Vec3.cross(x, up, z);\n"
       << "            Vec3.normalize(x, x);\n"
       << "            const y = Vec3.create();\n"
       << "            Vec3.cross(y, z, x);\n"
       << "            out[0] = x[0]; out[1] = y[0]; out[2] = z[0]; out[3] = 0;\n"
       << "            out[4] = x[1]; out[5] = y[1]; out[6] = z[1]; out[7] = 0;\n"
       << "            out[8] = x[2]; out[9] = y[2]; out[10] = z[2]; out[11] = 0;\n"
       << "            out[12] = -Vec3.dot(x, eye);\n"
       << "            out[13] = -Vec3.dot(y, eye);\n"
       << "            out[14] = -Vec3.dot(z, eye);\n"
       << "            out[15] = 1;\n"
       << "            return out;\n"
       << "        },\n"
       << "        multiply: (out, a, b) => {\n"
       << "            const a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];\n"
       << "            const a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];\n"
       << "            const a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];\n"
       << "            const a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];\n"
       << "            let b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3];\n"
       << "            out[0] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;\n"
       << "            out[1] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;\n"
       << "            out[2] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;\n"
       << "            out[3] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;\n"
       << "            b0 = b[4]; b1 = b[5]; b2 = b[6]; b3 = b[7];\n"
       << "            out[4] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;\n"
       << "            out[5] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;\n"
       << "            out[6] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;\n"
       << "            out[7] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;\n"
       << "            b0 = b[8]; b1 = b[9]; b2 = b[10]; b3 = b[11];\n"
       << "            out[8] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;\n"
       << "            out[9] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;\n"
       << "            out[10] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;\n"
       << "            out[11] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;\n"
       << "            b0 = b[12]; b1 = b[13]; b2 = b[14]; b3 = b[15];\n"
       << "            out[12] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;\n"
       << "            out[13] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;\n"
       << "            out[14] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;\n"
       << "            out[15] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;\n"
       << "            return out;\n"
       << "        },\n"
       << "        fromRotationTranslationScale: (out, rotDeg, pos, scale) => {\n"
       << "            Mat4.identity(out);\n"
       << "            const rx = (rotDeg[0] || 0) * Math.PI / 180;\n"
       << "            const ry = (rotDeg[1] || 0) * Math.PI / 180;\n"
       << "            const rz = (rotDeg[2] || 0) * Math.PI / 180;\n"
       << "            const cx = Math.cos(rx), sx = Math.sin(rx);\n"
       << "            const cy = Math.cos(ry), sy = Math.sin(ry);\n"
       << "            const cz = Math.cos(rz), sz = Math.sin(rz);\n"
       << "            const sx_scale = scale[0] || 1;\n"
       << "            const sy_scale = scale[1] || 1;\n"
       << "            const sz_scale = scale[2] || 1;\n"
       << "            out[0] = (cy * cz) * sx_scale;\n"
       << "            out[1] = (cy * sz) * sx_scale;\n"
       << "            out[2] = (-sy) * sx_scale;\n"
       << "            out[4] = (sx * sy * cz - cx * sz) * sy_scale;\n"
       << "            out[5] = (sx * sy * sz + cx * cz) * sy_scale;\n"
       << "            out[6] = (sx * cy) * sy_scale;\n"
       << "            out[8] = (cx * sy * cz + sx * sz) * sz_scale;\n"
       << "            out[9] = (cx * sy * sz - sx * cz) * sz_scale;\n"
       << "            out[10] = (cx * cy) * sz_scale;\n"
       << "            out[12] = pos[0] || 0;\n"
       << "            out[13] = pos[1] || 0;\n"
       << "            out[14] = pos[2] || 0;\n"
       << "            out[15] = 1;\n"
       << "            return out;\n"
       << "        }\n"
       << "    };\n\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    // 2. Procedural Geometry Generators (Cube, Sphere, Cylinder, Cone, Plane)\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    const Geometry = {\n"
       << "        createCube: (size = 1.0) => {\n"
       << "            const h = size * 0.5;\n"
       << "            const positions = new Float32Array([\n"
       << "                -h,-h, h,  h,-h, h,  h, h, h, -h, h, h, // Front\n"
       << "                 h,-h,-h, -h,-h,-h, -h, h,-h,  h, h,-h, // Back\n"
       << "                -h, h, h,  h, h, h,  h, h,-h, -h, h,-h, // Top\n"
       << "                -h,-h,-h,  h,-h,-h,  h,-h, h, -h,-h, h, // Bottom\n"
       << "                 h,-h, h,  h,-h,-h,  h, h,-h,  h, h, h, // Right\n"
       << "                -h,-h,-h, -h,-h, h, -h, h, h, -h, h,-h  // Left\n"
       << "            ]);\n"
       << "            const normals = new Float32Array([\n"
       << "                 0, 0, 1,   0, 0, 1,   0, 0, 1,   0, 0, 1,\n"
       << "                 0, 0,-1,   0, 0,-1,   0, 0,-1,   0, 0,-1,\n"
       << "                 0, 1, 0,   0, 1, 0,   0, 1, 0,   0, 1, 0,\n"
       << "                 0,-1, 0,   0,-1, 0,   0,-1, 0,   0,-1, 0,\n"
       << "                 1, 0, 0,   1, 0, 0,   1, 0, 0,   1, 0, 0,\n"
       << "                -1, 0, 0,  -1, 0, 0,  -1, 0, 0,  -1, 0, 0\n"
       << "            ]);\n"
       << "            const uvs = new Float32Array([\n"
       << "                0,0, 1,0, 1,1, 0,1,  0,0, 1,0, 1,1, 0,1,\n"
       << "                0,0, 1,0, 1,1, 0,1,  0,0, 1,0, 1,1, 0,1,\n"
       << "                0,0, 1,0, 1,1, 0,1,  0,0, 1,0, 1,1, 0,1\n"
       << "            ]);\n"
       << "            const indices = new Uint16Array([\n"
       << "                 0, 1, 2,   0, 2, 3,    4, 5, 6,   4, 6, 7,\n"
       << "                 8, 9,10,   8,10,11,   12,13,14,  12,14,15,\n"
       << "                16,17,18,  16,18,19,   20,21,22,  20,22,23\n"
       << "            ]);\n"
       << "            return { positions, normals, uvs, indices };\n"
       << "        },\n\n"
       << "        createSphere: (radius = 0.5, segments = 24, rings = 16) => {\n"
       << "            const positions = [], normals = [], uvs = [], indices = [];\n"
       << "            for (let r = 0; r <= rings; ++r) {\n"
       << "                const v = r / rings;\n"
       << "                const theta = v * Math.PI;\n"
       << "                for (let s = 0; s <= segments; ++s) {\n"
       << "                    const u = s / segments;\n"
       << "                    const phi = u * Math.PI * 2;\n"
       << "                    const x = Math.sin(theta) * Math.cos(phi);\n"
       << "                    const y = Math.cos(theta);\n"
       << "                    const z = Math.sin(theta) * Math.sin(phi);\n"
       << "                    positions.push(x * radius, y * radius, z * radius);\n"
       << "                    normals.push(x, y, z);\n"
       << "                    uvs.push(u, v);\n"
       << "                }\n"
       << "            }\n"
       << "            for (let r = 0; r < rings; ++r) {\n"
       << "                for (let s = 0; s < segments; ++s) {\n"
       << "                    const first = (r * (segments + 1)) + s;\n"
       << "                    const second = first + segments + 1;\n"
       << "                    indices.push(first, second, first + 1);\n"
       << "                    indices.push(second, second + 1, first + 1);\n"
       << "                }\n"
       << "            }\n"
       << "            return {\n"
       << "                positions: new Float32Array(positions),\n"
       << "                normals: new Float32Array(normals),\n"
       << "                uvs: new Float32Array(uvs),\n"
       << "                indices: new Uint16Array(indices)\n"
       << "            };\n"
       << "        },\n\n"
       << "        createPlane: (size = 10.0) => {\n"
       << "            const h = size * 0.5;\n"
       << "            return {\n"
       << "                positions: new Float32Array([-h,0,h, h,0,h, h,0,-h, -h,0,-h]),\n"
       << "                normals: new Float32Array([0,1,0, 0,1,0, 0,1,0, 0,1,0]),\n"
       << "                uvs: new Float32Array([0,0, 5,0, 5,5, 0,5]),\n"
       << "                indices: new Uint16Array([0,1,2, 0,2,3])\n"
       << "            };\n"
       << "        },\n\n"
       << "        createGrid: (size = 30.0, step = 1.0) => {\n"
       << "            const lines = [];\n"
       << "            const half = size * 0.5;\n"
       << "            for (let i = -half; i <= half; i += step) {\n"
       << "                lines.push(i, 0, -half, i, 0, half);\n"
       << "                lines.push(-half, 0, i, half, 0, i);\n"
       << "            }\n"
       << "            return new Float32Array(lines);\n"
       << "        }\n"
       << "    };\n\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    // 3. WebGL 2.0 Shaders (PBR Light Radiance + Shadows)\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    const VS_SOURCE = `#version 300 es\n"
       << "    layout(location = 0) in vec3 a_position;\n"
       << "    layout(location = 1) in vec3 a_normal;\n"
       << "    layout(location = 2) in vec2 a_texcoord;\n\n"
       << "    uniform mat4 u_world;\n"
       << "    uniform mat4 u_view;\n"
       << "    uniform mat4 u_proj;\n\n"
       << "    out vec3 v_worldPos;\n"
       << "    out vec3 v_normal;\n"
       << "    out vec2 v_uv;\n\n"
       << "    void main() {\n"
       << "        vec4 wp = u_world * vec4(a_position, 1.0);\n"
       << "        v_worldPos = wp.xyz;\n"
       << "        v_normal = mat3(u_world) * a_normal;\n"
       << "        v_uv = a_texcoord;\n"
       << "        gl_Position = u_proj * u_view * wp;\n"
       << "    }`;\n\n"
       << "    const FS_SOURCE = `#version 300 es\n"
       << "    precision highp float;\n\n"
       << "    in vec3 v_worldPos;\n"
       << "    in vec3 v_normal;\n"
       << "    in vec2 v_uv;\n\n"
       << "    uniform vec3 u_cameraPos;\n"
       << "    uniform vec4 u_baseColor;\n"
       << "    uniform float u_roughness;\n"
       << "    uniform float u_metallic;\n"
       << "    uniform vec3 u_emissive;\n\n"
       << "    // Directional Sun Light\n"
       << "    uniform vec3 u_sunDir;\n"
       << "    uniform vec3 u_sunColor;\n"
       << "    uniform float u_sunIntensity;\n"
       << "    uniform vec3 u_ambientColor;\n\n"
       << "    out vec4 fragColor;\n\n"
       << "    void main() {\n"
       << "        vec3 N = normalize(v_normal);\n"
       << "        vec3 V = normalize(u_cameraPos - v_worldPos);\n"
       << "        vec3 L = normalize(u_sunDir);\n"
       << "        vec3 H = normalize(L + V);\n\n"
       << "        // Diffuse\n"
       << "        float NdotL = max(dot(N, L), 0.0);\n"
       << "        vec3 diffuse = u_baseColor.rgb * u_sunColor * (NdotL * u_sunIntensity);\n\n"
       << "        // Specular Blinn-Phong\n"
       << "        float NdotH = max(dot(N, H), 0.0);\n"
       << "        float specPower = mix(128.0, 4.0, u_roughness);\n"
       << "        float spec = pow(NdotH, specPower) * (1.0 - u_roughness) * (0.2 + 0.8 * u_metallic);\n"
       << "        vec3 specular = u_sunColor * spec;\n\n"
       << "        // Ambient & Radiance\n"
       << "        vec3 ambient = u_ambientColor * u_baseColor.rgb;\n"
       << "        vec3 finalColor = ambient + diffuse + specular + u_emissive;\n\n"
       << "        // ACES Tonemapping approximation\n"
       << "        finalColor = (finalColor * (2.51 * finalColor + 0.03)) / (finalColor * (2.43 * finalColor + 0.59) + 0.14);\n"
       << "        fragColor = vec4(clamp(finalColor, 0.0, 1.0), u_baseColor.a);\n"
       << "    }`;\n\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    // 4. Web Engine Class\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    class TesseraEngine {\n"
       << "        constructor(canvasId) {\n"
       << "            this.canvas = document.getElementById(canvasId);\n"
       << "            this.gl = this.canvas.getContext('webgl2', { antialias: true, alpha: false });\n"
       << "            if (!this.gl) {\n"
       << "                alert('WebGL 2.0 is required but not supported on this browser/device.');\n"
       << "                return;\n"
       << "            }\n"
       << "            this.nodes = [];\n"
       << "            this.lights = [];\n"
       << "            this.activeCamera = null;\n"
       << "            this.meshes = new Map();\n"
       << "            this.keys = {};\n"
       << "            this.isPointerLocked = false;\n"
       << "            this.isPaused = false;\n"
       << "            this.audioMuted = false;\n"
       << "            this.cameraPos = Vec3.create(0, 3, 8);\n"
       << "            this.cameraRot = Vec3.create(0, 0, 0); // pitch, yaw\n"
       << "            this.playerVel = Vec3.create(0, 0, 0);\n"
       << "            this.isGrounded = true;\n"
       << "            this.initGL();\n"
       << "            this.initInputs();\n"
       << "        }\n\n"
       << "        initGL() {\n"
       << "            const gl = this.gl;\n"
       << "            gl.enable(gl.DEPTH_TEST);\n"
       << "            gl.enable(gl.CULL_FACE);\n"
       << "            gl.cullFace(gl.BACK);\n"
       << "            gl.clearColor(0.08, 0.10, 0.14, 1.0);\n\n"
       << "            // Compile shader program\n"
       << "            this.program = this.createProgram(VS_SOURCE, FS_SOURCE);\n"
       << "            this.uWorld = gl.getUniformLocation(this.program, 'u_world');\n"
       << "            this.uView = gl.getUniformLocation(this.program, 'u_view');\n"
       << "            this.uProj = gl.getUniformLocation(this.program, 'u_proj');\n"
       << "            this.uCameraPos = gl.getUniformLocation(this.program, 'u_cameraPos');\n"
       << "            this.uBaseColor = gl.getUniformLocation(this.program, 'u_baseColor');\n"
       << "            this.uRoughness = gl.getUniformLocation(this.program, 'u_roughness');\n"
       << "            this.uMetallic = gl.getUniformLocation(this.program, 'u_metallic');\n"
       << "            this.uEmissive = gl.getUniformLocation(this.program, 'u_emissive');\n"
       << "            this.uSunDir = gl.getUniformLocation(this.program, 'u_sunDir');\n"
       << "            this.uSunColor = gl.getUniformLocation(this.program, 'u_sunColor');\n"
       << "            this.uSunIntensity = gl.getUniformLocation(this.program, 'u_sunIntensity');\n"
       << "            this.uAmbientColor = gl.getUniformLocation(this.program, 'u_ambientColor');\n\n"
       << "            // Upload primitive geometries\n"
       << "            this.uploadMesh('Cube', Geometry.createCube(1.0));\n"
       << "            this.uploadMesh('Sphere', Geometry.createSphere(0.5, 24, 16));\n"
       << "            this.uploadMesh('Plane', Geometry.createPlane(20.0));\n"
       << "        }\n\n"
       << "        createProgram(vsSrc, fsSrc) {\n"
       << "            const gl = this.gl;\n"
       << "            const vs = gl.createShader(gl.VERTEX_SHADER);\n"
       << "            gl.shaderSource(vs, vsSrc);\n"
       << "            gl.compileShader(vs);\n"
       << "            const fs = gl.createShader(gl.FRAGMENT_SHADER);\n"
       << "            gl.shaderSource(fs, fsSrc);\n"
       << "            gl.compileShader(fs);\n"
       << "            const p = gl.createProgram();\n"
       << "            gl.attachShader(p, vs);\n"
       << "            gl.attachShader(p, fs);\n"
       << "            gl.linkProgram(p);\n"
       << "            return p;\n"
       << "        }\n\n"
       << "        uploadMesh(name, geom) {\n"
       << "            const gl = this.gl;\n"
       << "            const vao = gl.createVertexArray();\n"
       << "            gl.bindVertexArray(vao);\n\n"
       << "            const posBuf = gl.createBuffer();\n"
       << "            gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);\n"
       << "            gl.bufferData(gl.ARRAY_BUFFER, geom.positions, gl.STATIC_DRAW);\n"
       << "            gl.enableVertexAttribArray(0);\n"
       << "            gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);\n\n"
       << "            const normBuf = gl.createBuffer();\n"
       << "            gl.bindBuffer(gl.ARRAY_BUFFER, normBuf);\n"
       << "            gl.bufferData(gl.ARRAY_BUFFER, geom.normals, gl.STATIC_DRAW);\n"
       << "            gl.enableVertexAttribArray(1);\n"
       << "            gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);\n\n"
       << "            const idxBuf = gl.createBuffer();\n"
       << "            gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, idxBuf);\n"
       << "            gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geom.indices, gl.STATIC_DRAW);\n\n"
       << "            gl.bindVertexArray(null);\n"
       << "            this.meshes.set(name, { vao, count: geom.indices.length });\n"
       << "        }\n\n"
       << "        initInputs() {\n"
       << "            window.addEventListener('keydown', (e) => { this.keys[e.code] = true; });\n"
       << "            window.addEventListener('keyup', (e) => { this.keys[e.code] = false; });\n\n"
       << "            this.canvas.addEventListener('click', () => {\n"
       << "                if (!this.isPointerLocked) {\n"
       << "                    this.canvas.requestPointerLock();\n"
       << "                }\n"
       << "            });\n"
       << "            document.addEventListener('pointerlockchange', () => {\n"
       << "                this.isPointerLocked = (document.pointerLockElement === this.canvas);\n"
       << "            });\n"
       << "            document.addEventListener('mousemove', (e) => {\n"
       << "                if (this.isPointerLocked) {\n"
       << "                    const sens = 0.0022;\n"
       << "                    this.cameraRot[1] -= e.movementX * sens; // Yaw\n"
       << "                    this.cameraRot[0] = Math.max(-1.4, Math.min(1.4, this.cameraRot[0] - e.movementY * sens)); // Pitch\n"
       << "                }\n"
       << "            });\n\n"
       << "            // Controls Toolbar Hookups\n"
       << "            const btnFS = document.getElementById('btn-fullscreen');\n"
       << "            if (btnFS) btnFS.onclick = () => {\n"
       << "                if (!document.fullscreenElement) document.documentElement.requestFullscreen();\n"
       << "                else document.exitFullscreen();\n"
       << "            };\n"
       << "            const btnP = document.getElementById('btn-pause');\n"
       << "            if (btnP) btnP.onclick = () => {\n"
       << "                this.isPaused = !this.isPaused;\n"
       << "                btnP.textContent = this.isPaused ? 'Resume' : 'Pause';\n"
       << "            };\n"
       << "            const btnAudio = document.getElementById('btn-audio');\n"
       << "            if (btnAudio) btnAudio.onclick = () => {\n"
       << "                this.audioMuted = !this.audioMuted;\n"
       << "                btnAudio.textContent = this.audioMuted ? 'Audio: OFF' : 'Audio: ON';\n"
       << "            };\n"
       << "        }\n\n"
       << "        loadScene(sceneData) {\n"
       << "            this.nodes = sceneData.nodes || [];\n"
       << "            this.env = sceneData.environment || {};\n\n"
       << "            // Find starting camera\n"
       << "            const camNode = this.nodes.find(n => n.nodeType === 2 || n.name.includes('Camera'));\n"
       << "            if (camNode && camNode.position) {\n"
       << "                this.cameraPos[0] = camNode.position[0];\n"
       << "                this.cameraPos[1] = camNode.position[1];\n"
       << "                this.cameraPos[2] = camNode.position[2];\n"
       << "            }\n"
       << "            console.log(`[TesseraWeb] Loaded scene: ${sceneData.name || 'GameScene'} (${this.nodes.length} nodes)`);\n"
       << "        }\n\n"
       << "        update(dt) {\n"
       << "            // Clean slate runtime: all gameplay and camera controls are defined by the developer via scripts.\n"
       << "        render() {\n"
       << "            const gl = this.gl;\n"
       << "            const w = this.canvas.clientWidth;\n"
       << "            const h = this.canvas.clientHeight;\n"
       << "            if (this.canvas.width !== w || this.canvas.height !== h) {\n"
       << "                this.canvas.width = w;\n"
       << "                this.canvas.height = h;\n"
       << "            }\n"
       << "            gl.viewport(0, 0, w, h);\n"
       << "            gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);\n"
       << "            gl.useProgram(this.program);\n\n"
       << "            // Projection Matrix\n"
       << "            const proj = Mat4.create();\n"
       << "            Mat4.perspective(proj, 60 * Math.PI / 180, w / Math.max(1, h), 0.1, 1000.0);\n"
       << "            gl.uniformMatrix4fv(this.uProj, false, proj);\n\n"
       << "            // View Matrix from Camera Position & Rotation\n"
       << "            const view = Mat4.create();\n"
       << "            const target = Vec3.create(\n"
       << "                this.cameraPos[0] - Math.sin(this.cameraRot[1]) * Math.cos(this.cameraRot[0]),\n"
       << "                this.cameraPos[1] + Math.sin(this.cameraRot[0]),\n"
       << "                this.cameraPos[2] - Math.cos(this.cameraRot[1]) * Math.cos(this.cameraRot[0])\n"
       << "            );\n"
       << "            Mat4.lookAt(view, this.cameraPos, target, [0, 1, 0]);\n"
       << "            gl.uniformMatrix4fv(this.uView, false, view);\n"
       << "            gl.uniform3fv(this.uCameraPos, this.cameraPos);\n\n"
       << "            // Directional Sun Light\n"
       << "            gl.uniform3f(this.uSunDir, 0.5, 0.8, 0.4);\n"
       << "            gl.uniform3f(this.uSunColor, 1.0, 0.95, 0.9);\n"
       << "            gl.uniform1f(this.uSunIntensity, 1.2);\n"
       << "            gl.uniform3f(this.uAmbientColor, 0.18, 0.22, 0.28);\n\n"
       << "            // Draw Scene Nodes\n"
       << "            for (const node of this.nodes) {\n"
       << "                if (node.nodeType === 1 || node.nodeType === 2) continue; // Skip Light / Camera gizmos\n"
       << "                const world = Mat4.create();\n"
       << "                Mat4.fromRotationTranslationScale(world, node.rotation || [0,0,0], node.position || [0,0,0], node.scale || [1,1,1]);\n"
       << "                gl.uniformMatrix4fv(this.uWorld, false, world);\n\n"
       << "                const col = node.color || [0.7, 0.7, 0.75, 1.0];\n"
       << "                gl.uniform4fv(this.uBaseColor, col);\n"
       << "                gl.uniform1f(this.uRoughness, node.roughness || 0.4);\n"
       << "                gl.uniform1f(this.uMetallic, node.metallic || 0.0);\n"
       << "                gl.uniform3fv(this.uEmissive, node.emissive || [0,0,0]);\n\n"
       << "                // Select mesh by shape type\n"
       << "                let meshName = 'Cube';\n"
       << "                if (node.shapeType === 1 || node.type === 1) meshName = 'Sphere';\n"
       << "                else if (node.shapeType === 4 || node.type === 4) meshName = 'Plane';\n"
       << "                const mesh = this.meshes.get(meshName) || this.meshes.get('Cube');\n"
       << "                if (mesh) {\n"
       << "                    gl.bindVertexArray(mesh.vao);\n"
       << "                    gl.drawElements(gl.TRIANGLES, mesh.count, gl.UNSIGNED_SHORT, 0);\n"
       << "                }\n"
       << "            }\n"
       << "        }\n"
       << "    }\n\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    // 5. Bootstrap Game Instance\n"
       << "    // ------------------------------------------------------------------------\n"
       << "    window.addEventListener('DOMContentLoaded', () => {\n"
       << "        const engine = new TesseraEngine('tessera-canvas');\n"
       << "        const splash = document.getElementById('splash-screen');\n"
       << "        const btnStart = document.getElementById('btn-start');\n"
       << "        const spinner = document.getElementById('spinner-box');\n\n"
       << "        // 1. Try reading embedded scene data\n"
       << "        let sceneData = null;\n"
       << "        const embeddedScript = document.getElementById('tessera-scene-data');\n"
       << "        if (embeddedScript && embeddedScript.textContent.trim().length > 0) {\n"
       << "            try {\n"
       << "                sceneData = JSON.parse(embeddedScript.textContent);\n"
       << "            } catch (e) { console.warn('Could not parse embedded scene JSON:', e); }\n"
       << "        }\n\n"
       << "        const startGame = (data) => {\n"
       << "            if (data) engine.loadScene(data);\n"
       << "            if (spinner) spinner.style.display = 'none';\n"
       << "            if (btnStart) {\n"
       << "                btnStart.style.display = 'block';\n"
       << "                btnStart.onclick = () => {\n"
       << "                    if (splash) splash.classList.add('hidden');\n"
       << "                    engine.canvas.focus();\n"
       << "                };\n"
       << "            } else if (splash) {\n"
       << "                splash.classList.add('hidden');\n"
       << "            }\n\n"
       << "            let lastTime = performance.now();\n"
       << "            const loop = (now) => {\n"
       << "                const dt = Math.min((now - lastTime) / 1000.0, 0.1);\n"
       << "                lastTime = now;\n"
       << "                engine.update(dt);\n"
       << "                engine.render();\n"
       << "                requestAnimationFrame(loop);\n"
       << "            };\n"
       << "            requestAnimationFrame(loop);\n"
       << "        };\n\n"
       << "        if (sceneData) {\n"
       << "            startGame(sceneData);\n"
       << "        } else {\n"
       << "            // Fallback fetch over HTTP\n"
       << "            fetch('scenes/main.scene')\n"
       << "                .then(r => r.json())\n"
       << "                .then(data => startGame(data))\n"
       << "                .catch(() => startGame(null));\n"
       << "        }\n"
       << "    });\n"
       << "})();\n";
    return ss.str();
}

} // namespace Export
