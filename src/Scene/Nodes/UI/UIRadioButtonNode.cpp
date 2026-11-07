#include "UIRadioButtonNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIRadioButtonNode::UIRadioButtonNode(const std::string& label, const std::string& group, bool checked, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_RadioButton;
    parentId = pId;
    uiText = label;
    uiRadioGroup = group;
    uiChecked = checked;
    name = nodeName.empty() ? label : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(150.0f, 24.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.15f, 0.16f, 0.20f, 1.0f);
    uiTextColor = glm::vec4(0.35f, 0.70f, 1.0f, 1.0f);
    uiBorderColor = glm::vec4(0.40f, 0.45f, 0.55f, 1.0f);
    uiBorderThickness = 1.5f;
    uiRounding = 12.0f;
    uiVisible = true;
}

void UIRadioButtonNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char textBuf[256];
    strncpy(textBuf, uiText.c_str(), sizeof(textBuf));
    textBuf[sizeof(textBuf) - 1] = '\0';
    if (ImGui::InputText("Label", textBuf, sizeof(textBuf))) {
        uiText = textBuf;
    }
    char grpBuf[64];
    strncpy(grpBuf, uiRadioGroup.c_str(), sizeof(grpBuf));
    grpBuf[sizeof(grpBuf) - 1] = '\0';
    if (ImGui::InputText("Radio Group", grpBuf, sizeof(grpBuf))) {
        uiRadioGroup = grpBuf;
    }
    if (ImGui::Checkbox("Selected", &uiChecked)) {
        if (uiChecked) {
            for (auto& other : scene.GetNodes()) {
                if (other->id != id && other->nodeType == NodeType::UI_RadioButton && other->uiRadioGroup == uiRadioGroup) {
                    other->uiChecked = false;
                }
            }
        }
    }
    RenderThemeStyling(state);
}

void UIRadioButtonNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    float rad = (std::min)(uiSize.y, 20.0f) * 0.5f;
    ImVec2 center(rectMin.x + rad, rectMin.y + uiSize.y * 0.5f);

    if (uiEnabled && isClicked) {
        uiChecked = true;
        for (auto& other : scene.GetNodes()) {
            if (other->id != id && other->nodeType == NodeType::UI_RadioButton && other->uiRadioGroup == uiRadioGroup) {
                other->uiChecked = false;
            }
        }
    }

    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddCircleFilled(center, rad, bgCol, 20);
    drawList->AddCircle(center, rad, brdCol, 20, uiBorderThickness > 0.0f ? uiBorderThickness : 1.5f);
    if (uiChecked) drawList->AddCircleFilled(center, rad * 0.55f, fgCol, 16);

    ImVec2 textPos(center.x + rad + 8.0f, rectMin.y + (uiSize.y - ImGui::GetFontSize()) * 0.5f);
    drawList->AddText(textPos, fgCol, uiText.c_str());
}
