#include "UIDatePickerNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIDatePickerNode::UIDatePickerNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_DatePicker;
    parentId = pId;
    uiText = "2026-10-25 15:00";
    name = nodeName.empty() ? "Date Time Picker" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(180.0f, 32.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.18f, 0.20f, 0.26f, 1.0f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.40f, 0.45f, 0.55f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UIDatePickerNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char editBuf[128];
    strncpy(editBuf, uiText.c_str(), sizeof(editBuf));
    editBuf[sizeof(editBuf) - 1] = '\0';
    if (ImGui::InputText("DateTime", editBuf, sizeof(editBuf))) uiText = editBuf;
    if (ImGui::Button("Set to Today")) uiText = "2026-10-25 15:00";
    RenderThemeStyling(state);
}

void UIDatePickerNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    if (uiEnabled && isClicked) {
        if (state.activeDatePickerNodeId == id) state.activeDatePickerNodeId = 0;
        else state.activeDatePickerNodeId = id;
    }
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness > 0.0f ? uiBorderThickness : 1.0f);

    float iconX = rectMin.x + 8.0f;
    float iconY = rectMin.y + (uiSize.y - 14.0f) * 0.5f;
    drawList->AddRect(ImVec2(iconX, iconY), ImVec2(iconX + 14.0f, iconY + 14.0f), fgCol, 2.0f, 0, 1.2f);
    drawList->AddLine(ImVec2(iconX, iconY + 4.0f), ImVec2(iconX + 14.0f, iconY + 4.0f), fgCol, 1.0f);
    drawList->AddText(ImVec2(iconX + 20.0f, rectMin.y + (uiSize.y - ImGui::GetFontSize()) * 0.5f), fgCol, uiText.c_str());
}
