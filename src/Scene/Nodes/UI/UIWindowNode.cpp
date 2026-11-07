#include "UIWindowNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIWindowNode::UIWindowNode(const std::string& title, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Window;
    parentId = pId;
    uiText = title;
    name = nodeName.empty() ? title : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(320.0f, 240.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.14f, 0.15f, 0.18f, 0.96f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.38f, 0.48f, 1.0f);
    uiBorderThickness = 1.5f;
    uiRounding = 6.0f;
    uiWindowIsOpen = true;
    uiWindowMovable = true;
    uiWindowClosable = true;
    uiVisible = true;
}

void UIWindowNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char textBuf[256];
    strncpy(textBuf, uiText.c_str(), sizeof(textBuf));
    textBuf[sizeof(textBuf) - 1] = '\0';
    if (ImGui::InputText("Window Title", textBuf, sizeof(textBuf))) {
        uiText = textBuf;
    }
    ImGui::Checkbox("Window Is Open", &uiWindowIsOpen);
    ImGui::Checkbox("Allow Moving / Dragging", &uiWindowMovable);
    ImGui::Checkbox("Enable Close 'X' Button", &uiWindowClosable);
    RenderThemeStyling(state);
}

void UIWindowNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    if (!uiWindowIsOpen && !isEditorMode) return;
    float titleH = 26.0f;
    ImVec2 titleMin = rectMin;
    ImVec2 titleMax(rectMax.x, rectMin.y + titleH);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    bool isTitleHovered = (mousePos.x >= titleMin.x && mousePos.x <= titleMax.x &&
                          mousePos.y >= titleMin.y && mousePos.y <= titleMax.y);

    if (uiEnabled && uiWindowMovable) {
        if (state.draggingWindowId == id) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                uiPosition.x += io.MouseDelta.x;
                uiPosition.y += io.MouseDelta.y;
            } else {
                state.draggingWindowId = 0;
            }
        } else if (isTitleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            state.draggingWindowId = id;
        }
    }

    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, 1.0f));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    drawList->AddRectFilled(titleMin, titleMax, IM_COL32(45, 50, 65, 255), uiRounding, ImDrawFlags_RoundCornersTop);
    drawList->AddLine(ImVec2(titleMin.x, titleMax.y), titleMax, brdCol, 1.0f);
    drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness > 0.0f ? uiBorderThickness : 1.5f);

    drawList->AddText(ImVec2(titleMin.x + 8.0f, titleMin.y + 5.0f), fgCol, uiText.c_str());

    if (uiWindowClosable) {
        ImVec2 xMin(rectMax.x - 22.0f, rectMin.y + 4.0f);
        ImVec2 xMax(rectMax.x - 4.0f, rectMin.y + 22.0f);
        bool isCloseHovered = (mousePos.x >= xMin.x && mousePos.x <= xMax.x &&
                               mousePos.y >= xMin.y && mousePos.y <= xMax.y);
        if (isCloseHovered) {
            drawList->AddRectFilled(xMin, xMax, IM_COL32(220, 50, 50, 200), 3.0f);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) uiWindowIsOpen = false;
        }
        drawList->AddText(ImVec2(xMin.x + 4.0f, xMin.y + 1.0f), IM_COL32(255, 255, 255, 255), "X");
    }
}
