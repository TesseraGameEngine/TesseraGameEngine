#include "UIButtonNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIButtonNode::UIButtonNode(const std::string& label, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Button;
    parentId = pId;
    uiText = label;
    name = nodeName.empty() ? label : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(120.0f, 36.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.18f, 0.42f, 0.72f, 1.0f);
    uiTextColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.65f, 0.95f, 1.0f);
    uiBorderThickness = 1.5f;
    uiRounding = 5.0f;
    uiHoverColor = glm::vec4(0.24f, 0.52f, 0.85f, 1.0f);
    uiPressColor = glm::vec4(0.12f, 0.32f, 0.58f, 1.0f);
    uiVisible = true;
}

void UIButtonNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char textBuf[256];
    strncpy(textBuf, uiText.c_str(), sizeof(textBuf));
    textBuf[sizeof(textBuf) - 1] = '\0';
    if (ImGui::InputText("Button Label", textBuf, sizeof(textBuf))) {
        uiText = textBuf;
    }
    char actionBuf[128];
    strncpy(actionBuf, uiOnClickAction.c_str(), sizeof(actionBuf));
    actionBuf[sizeof(actionBuf) - 1] = '\0';
    if (ImGui::InputText("On-Click Action", actionBuf, sizeof(actionBuf))) {
        uiOnClickAction = actionBuf;
    }
    char iconBuf[260];
    strncpy(iconBuf, texturePath.c_str(), sizeof(iconBuf));
    iconBuf[sizeof(iconBuf) - 1] = '\0';
    if (ImGui::InputText("Icon Texture Path", iconBuf, sizeof(iconBuf))) {
        texturePath = iconBuf;
    }
    RenderThemeStyling(state);
}

void UIButtonNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    glm::vec4 curCol = uiColor;
    if (uiEnabled) {
        if (!isEditorMode) {
            if (isClicked) {
                curCol = uiPressColor;
                std::cout << "[2D UI] Button Clicked: " << name << " (Action: " << uiOnClickAction << ")" << std::endl;
                if (uiOnClickAction == "quit" || uiOnClickAction == "exit") {
                    glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
                }
            } else if (isHovered) {
                curCol = uiHoverColor;
            }
        } else if (isHovered && !state.isDraggingUINode) {
            curCol = uiHoverColor;
        }
    }
    ImU32 btnCol = ImGui::ColorConvertFloat4ToU32(ImVec4(curCol.r, curCol.g, curCol.b, curCol.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, btnCol, uiRounding);
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);

    if (!texturePath.empty()) {
        float iconSize = (std::min)(uiSize.y * 0.6f, 20.0f);
        ImVec2 iconPos(rectMin.x + 8.0f, rectMin.y + (uiSize.y - iconSize) * 0.5f);
        drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize), IM_COL32(255, 255, 255, 80), 2.0f);
    }
    ImVec2 textSize = ImGui::CalcTextSize(uiText.c_str());
    ImVec2 textPos = ImVec2(rectMin.x + (uiSize.x - textSize.x) * 0.5f, rectMin.y + (uiSize.y - textSize.y) * 0.5f);
    drawList->AddText(textPos, fgCol, uiText.c_str());
}
