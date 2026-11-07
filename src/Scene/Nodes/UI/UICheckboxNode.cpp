#include "UICheckboxNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UICheckboxNode::UICheckboxNode(const std::string& label, bool checked, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Checkbox;
    parentId = pId;
    uiText = label;
    uiChecked = checked;
    name = nodeName.empty() ? label : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(150.0f, 24.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.15f, 0.16f, 0.20f, 1.0f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.40f, 0.45f, 0.55f, 1.0f);
    uiBorderThickness = 1.5f;
    uiRounding = 3.0f;
    uiVisible = true;
}

void UICheckboxNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char textBuf[256];
    strncpy(textBuf, uiText.c_str(), sizeof(textBuf));
    textBuf[sizeof(textBuf) - 1] = '\0';
    if (ImGui::InputText("Label", textBuf, sizeof(textBuf))) {
        uiText = textBuf;
    }
    ImGui::Checkbox("Checked", &uiChecked);
    RenderThemeStyling(state);
}

void UICheckboxNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    float boxSize = (std::min)(uiSize.y, 20.0f);
    ImVec2 boxMin(rectMin.x, rectMin.y + (uiSize.y - boxSize) * 0.5f);
    ImVec2 boxMax(boxMin.x + boxSize, boxMin.y + boxSize);

    if (uiEnabled && isClicked) {
        uiChecked = !uiChecked;
    }

    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(boxMin, boxMax, bgCol, 3.0f);
    drawList->AddRect(boxMin, boxMax, brdCol, 3.0f, 0, uiBorderThickness > 0.0f ? uiBorderThickness : 1.5f);

    if (uiChecked) {
        ImVec2 p1(boxMin.x + boxSize * 0.22f, boxMin.y + boxSize * 0.52f);
        ImVec2 p2(boxMin.x + boxSize * 0.45f, boxMin.y + boxSize * 0.78f);
        ImVec2 p3(boxMin.x + boxSize * 0.82f, boxMin.y + boxSize * 0.25f);
        drawList->AddLine(p1, p2, fgCol, 2.5f);
        drawList->AddLine(p2, p3, fgCol, 2.5f);
    }
    ImVec2 textPos(boxMax.x + 8.0f, rectMin.y + (uiSize.y - ImGui::GetFontSize()) * 0.5f);
    drawList->AddText(textPos, fgCol, uiText.c_str());
}
