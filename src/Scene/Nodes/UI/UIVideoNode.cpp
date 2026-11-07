#include "UIVideoNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include "Video/VideoPlayer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIVideoNode::UIVideoNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Video;
    parentId = pId;
    name = nodeName.empty() ? "Video Player" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(320.0f, 180.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.08f, 0.08f, 0.10f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.35f, 0.45f, 1.0f);
    uiBorderThickness = 1.5f;
    uiRounding = 4.0f;
    uiProgress = 0.0f;
    uiVisible = true;
    isVideoScreen = true;
    videoChannelPreset = 1;
    videoPlaying = true;
    videoLoop = true;
}

void UIVideoNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    (void)scene;
    ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "2D UI Video & Cutaway Player:");

    const char* presets[] = {
        "External File / GIF / Atlas / Sequence",
        "Ch 1: Cinema 35mm Countdown & Feature Reel",
        "Ch 2: Live TV News & Weather Broadcast",
        "Ch 3: CCTV Security Camera Surveillance",
        "Ch 4: Smartphone Mobile Call & Feed (9:16)",
        "Ch 5: Retro CRT Color Bars & VHS Static",
        "Ch 6: Sci-Fi Radar & Oscilloscope Telemetry"
    };
    int curPreset = std::clamp(videoChannelPreset, 0, 6);
    if (ImGui::Combo("Video Channel", &curPreset, presets, IM_ARRAYSIZE(presets))) {
        videoChannelPreset = curPreset;
    }

    char pathBuf[260];
    strncpy(pathBuf, videoSourcePath.c_str(), sizeof(pathBuf));
    pathBuf[sizeof(pathBuf) - 1] = '\0';
    if (ImGui::InputText("Video File / GIF / Atlas", pathBuf, sizeof(pathBuf))) {
        videoSourcePath = pathBuf;
        texturePath = pathBuf;
    }

    ImGui::Checkbox("Play Video", &videoPlaying);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &videoLoop);
    ImGui::SliderFloat("Playback Speed", &videoSpeed, 0.1f, 3.0f, "%.2fx");
    ImGui::SliderFloat("Duration (s)", &videoDuration, 1.0f, 120.0f, "%.1f s");
    if (ImGui::SliderFloat("Playhead Progress", &uiProgress, 0.0f, 1.0f, "%.2f")) {
        videoCurrentTime = uiProgress * std::max(0.5f, videoDuration);
    }
    ImGui::SliderFloat("CRT Scanlines", &videoScanlineStrength, 0.0f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::TextDisabled("Quick Layout Presets:");
    if (ImGui::SmallButton("16:9 Player (320x180)")) {
        uiAnchor = UIAnchor::Center;
        uiPosition = glm::vec2(0.0f, 0.0f);
        uiSize = glm::vec2(320.0f, 180.0f);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Corner PiP Feed")) {
        uiAnchor = UIAnchor::TopRight;
        uiPosition = glm::vec2(-16.0f, 16.0f);
        uiSize = glm::vec2(256.0f, 144.0f);
        videoChannelPreset = 3; // CCTV
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Smartphone UI (9:16)")) {
        uiAnchor = UIAnchor::BottomRight;
        uiPosition = glm::vec2(-20.0f, -20.0f);
        uiSize = glm::vec2(180.0f, 320.0f);
        videoChannelPreset = 4; // Smartphone
    }

    ImGui::Spacing();
    RenderThemeStyling(state);
}

void UIVideoNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                         bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                         ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    (void)scene; (void)canvasPos; (void)canvasW; (void)canvasH; (void)isEditorMode; (void)gameLoop; (void)state; (void)isHovered;

    if (isClicked) {
        videoPlaying = !videoPlaying;
    }

    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a));
    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);

    // Draw live video frame grid inside the UIVideoNode rect
    const auto* vState = Video::VideoSystem::Get().GetNodeVideoState(id);
    std::vector<uint8_t> localRgba;
    glm::vec3 avgCol(0.0f);
    const std::vector<uint8_t>* pixelsPtr = nullptr;
    int vW = 96, vH = 54;
    if (vState && !vState->rgbaPixels.empty()) {
        pixelsPtr = &vState->rgbaPixels;
        vW = static_cast<int>(vState->width);
        vH = static_cast<int>(vState->height);
    } else {
        std::string src = !videoSourcePath.empty() ? videoSourcePath : texturePath;
        Video::VideoSystem::RenderFrameRGBA(videoChannelPreset, src, videoCurrentTime, videoFps,
                                            videoAtlasCols, videoAtlasRows, videoScanlineStrength,
                                            static_cast<uint32_t>(vW), static_cast<uint32_t>(vH), localRgba, avgCol);
        pixelsPtr = &localRgba;
    }

    if (pixelsPtr && !pixelsPtr->empty()) {
        const auto& px = *pixelsPtr;
        int gridX = 48;
        int gridY = (videoChannelPreset == 4) ? 64 : 28;
        float cellW = (rectMax.x - rectMin.x - 4.0f) / static_cast<float>(gridX);
        float cellH = (rectMax.y - rectMin.y - 18.0f) / static_cast<float>(gridY);
        for (int gy = 0; gy < gridY; ++gy) {
            int sy = std::clamp((gy * vH) / gridY, 0, vH - 1);
            for (int gx = 0; gx < gridX; ++gx) {
                int sx = std::clamp((gx * vW) / gridX, 0, vW - 1);
                size_t idx = (static_cast<size_t>(sy) * vW + sx) * 4;
                ImU32 c = IM_COL32(px[idx + 0], px[idx + 1], px[idx + 2], 255);
                ImVec2 p0(rectMin.x + 2.0f + gx * cellW, rectMin.y + 2.0f + gy * cellH);
                ImVec2 p1(p0.x + cellW + 0.5f, p0.y + cellH + 0.5f);
                drawList->AddRectFilled(p0, p1, c);
            }
        }
    }

    if (uiBorderThickness > 0.0f) {
        drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);
    }

    // Show Play icon overlay only when paused
    if (!videoPlaying) {
        float playR = 20.0f;
        ImVec2 mid((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y - 14.0f) * 0.5f);
        drawList->AddCircleFilled(mid, playR, IM_COL32(20, 20, 28, 210), 24);
        drawList->AddCircle(mid, playR, IM_COL32(255, 255, 255, 190), 24, 2.0f);
        drawList->AddTriangleFilled(ImVec2(mid.x - 6.0f, mid.y - 9.0f),
                                    ImVec2(mid.x - 6.0f, mid.y + 9.0f),
                                    ImVec2(mid.x + 10.0f, mid.y), IM_COL32(255, 255, 255, 235));
    }

    // Bottom progress / transport bar
    float barH = 5.0f;
    ImVec2 bMin(rectMin.x + 6.0f, rectMax.y - 11.0f);
    ImVec2 bMax(rectMax.x - 6.0f, rectMax.y - 11.0f + barH);
    drawList->AddRectFilled(bMin, bMax, IM_COL32(40, 40, 50, 220), 2.0f);
    float pW = (bMax.x - bMin.x) * std::clamp(uiProgress, 0.0f, 1.0f);
    drawList->AddRectFilled(bMin, ImVec2(bMin.x + pW, bMax.y), IM_COL32(235, 55, 55, 255), 2.0f);
}
