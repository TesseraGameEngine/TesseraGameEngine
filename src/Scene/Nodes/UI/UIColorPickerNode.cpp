#include "UIColorPickerNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIColorPickerNode::UIColorPickerNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_ColorPicker;
    parentId = pId;
    name = nodeName.empty() ? "Color Picker" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(160.0f, 32.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.85f, 0.35f, 0.25f, 1.0f);
    uiBorderColor = glm::vec4(0.40f, 0.45f, 0.55f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UIColorPickerNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::ColorPicker4("Color Wheel", glm::value_ptr(uiColor), ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex);
    RenderThemeStyling(state);
}

void UIColorPickerNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    if (uiEnabled && isClicked) {
        if (state.activeColorPickerNodeId == id) state.activeColorPickerNodeId = 0;
        else state.activeColorPickerNodeId = id;
    }
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 curCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, IM_COL32(30, 32, 40, 255), uiRounding);
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);

    float swSz = uiSize.y - 8.0f;
    ImVec2 swMin(rectMin.x + 4.0f, rectMin.y + 4.0f);
    ImVec2 swMax(swMin.x + swSz, swMin.y + swSz);
    drawList->AddRectFilled(swMin, swMax, curCol, 3.0f);
    drawList->AddRect(swMin, swMax, IM_COL32(255, 255, 255, 120), 3.0f);

    char hexCode[16];
    snprintf(hexCode, sizeof(hexCode), "#%02X%02X%02X", (int)(uiColor.r * 255.0f), (int)(uiColor.g * 255.0f), (int)(uiColor.b * 255.0f));
    drawList->AddText(ImVec2(swMax.x + 8.0f, rectMin.y + (uiSize.y - ImGui::GetFontSize()) * 0.5f), IM_COL32(230, 230, 240, 255), hexCode);
}
