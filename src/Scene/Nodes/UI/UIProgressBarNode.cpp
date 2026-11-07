#include "UIProgressBarNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIProgressBarNode::UIProgressBarNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_ProgressBar;
    parentId = pId;
    name = nodeName.empty() ? "Progress Bar" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(200.0f, 24.0f);
    uiProgress = 0.65f;
    uiColor = glm::vec4(0.15f, 0.15f, 0.20f, 1.0f);
    uiTextColor = glm::vec4(0.25f, 0.75f, 0.40f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.35f, 0.45f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UIProgressBarNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::SliderFloat("Progress", &uiProgress, 0.0f, 1.0f, "%.2f");
    RenderThemeStyling(state);
}

void UIProgressBarNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 barCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    float fillW = std::clamp(uiProgress, 0.0f, 1.0f) * uiSize.x;
    if (fillW > 0.0f) {
        drawList->AddRectFilled(rectMin, ImVec2(rectMin.x + fillW, rectMax.y), barCol, uiRounding);
    }
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);

    char pctBuf[32];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)(std::clamp(uiProgress, 0.0f, 1.0f) * 100.0f));
    ImVec2 pctSz = ImGui::CalcTextSize(pctBuf);
    drawList->AddText(ImVec2(rectMin.x + (uiSize.x - pctSz.x) * 0.5f, rectMin.y + (uiSize.y - pctSz.y) * 0.5f), IM_COL32(255, 255, 255, 230), pctBuf);
}
