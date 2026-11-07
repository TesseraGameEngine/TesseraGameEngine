#include "UISliderNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UISliderNode::UISliderNode(float minVal, float maxVal, float curVal, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Slider;
    parentId = pId;
    uiValue = curVal;
    uiProgress = minVal; // Min
    uiBorderThickness = maxVal; // Max
    name = nodeName.empty() ? "Slider" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(200.0f, 24.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.18f, 0.18f, 0.22f, 1.0f);
    uiTextColor = glm::vec4(0.35f, 0.65f, 0.95f, 1.0f);
    uiBorderColor = glm::vec4(0.4f, 0.4f, 0.5f, 1.0f);
    uiRounding = 3.0f;
    uiVisible = true;
}

void UISliderNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::SliderFloat("Value", &uiValue, uiProgress, uiBorderThickness, "%.1f");
    ImGui::DragFloat("Min Value", &uiProgress, 1.0f);
    ImGui::DragFloat("Max Value", &uiBorderThickness, 1.0f);
    RenderThemeStyling(state);
}

void UISliderNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    float minVal = uiProgress;
    float maxVal = (uiBorderThickness > minVal) ? uiBorderThickness : minVal + 100.0f;
    float fraction = (std::clamp(uiValue, minVal, maxVal) - minVal) / (maxVal - minVal);

    if (uiEnabled && (isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        ImGuiIO& io = ImGui::GetIO();
        float t = std::clamp((io.MousePos.x - rectMin.x) / uiSize.x, 0.0f, 1.0f);
        uiValue = minVal + t * (maxVal - minVal);
        fraction = t;
    }

    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, 1.0f));

    float trackH = 6.0f;
    float midY = rectMin.y + uiSize.y * 0.5f;
    drawList->AddRectFilled(ImVec2(rectMin.x, midY - trackH * 0.5f), ImVec2(rectMax.x, midY + trackH * 0.5f), bgCol, 3.0f);
    float thumbX = rectMin.x + fraction * uiSize.x;
    drawList->AddRectFilled(ImVec2(rectMin.x, midY - trackH * 0.5f), ImVec2(thumbX, midY + trackH * 0.5f), fillCol, 3.0f);

    float thumbR = 8.0f;
    drawList->AddCircleFilled(ImVec2(thumbX, midY), thumbR, IM_COL32(245, 245, 250, 255));
    drawList->AddCircle(ImVec2(thumbX, midY), thumbR, IM_COL32(40, 50, 60, 255), 0, 1.5f);

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%.1f", uiValue);
    ImVec2 valSz = ImGui::CalcTextSize(valBuf);
    drawList->AddText(ImVec2(rectMax.x - valSz.x, rectMin.y - valSz.y - 2.0f), fgCol, valBuf);
}
