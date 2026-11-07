#include "UITextFieldNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UITextFieldNode::UITextFieldNode(const std::string& placeholder, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_TextField;
    parentId = pId;
    uiText = "";
    uiPlaceholder = placeholder;
    uiOnClickAction = placeholder;
    name = nodeName.empty() ? "Text Field" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(200.0f, 32.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.12f, 0.13f, 0.16f, 1.0f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.38f, 0.48f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UITextFieldNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char textBuf[256];
    strncpy(textBuf, uiText.c_str(), sizeof(textBuf));
    textBuf[sizeof(textBuf) - 1] = '\0';
    if (ImGui::InputText("Field Text", textBuf, sizeof(textBuf))) {
        uiText = textBuf;
    }
    char phBuf[128];
    strncpy(phBuf, uiOnClickAction.c_str(), sizeof(phBuf));
    phBuf[sizeof(phBuf) - 1] = '\0';
    if (ImGui::InputText("Placeholder", phBuf, sizeof(phBuf))) {
        uiOnClickAction = phBuf;
    }
    RenderThemeStyling(state);
}

void UITextFieldNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    bool isFocused = (state.focusedTextNodeId == id);
    if (uiEnabled && isClicked) state.focusedTextNodeId = id;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isHovered && isFocused) state.focusedTextNodeId = 0;

    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = isFocused ? IM_COL32(80, 160, 250, 255) : ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, isFocused ? 2.0f : uiBorderThickness);

    if (isFocused && uiEnabled && !isEditorMode) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            unsigned int c = io.InputQueueCharacters[i];
            if (c >= 32 && c < 127) uiText += (char)c;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !uiText.empty()) uiText.pop_back();
    }

    std::string disp = uiText;
    if (disp.empty() && !isFocused) {
        drawList->AddText(ImVec2(rectMin.x + 8.0f, rectMin.y + (uiSize.y - ImGui::GetFontSize()) * 0.5f), IM_COL32(140, 140, 150, 160), uiOnClickAction.c_str());
    } else {
        drawList->AddText(ImVec2(rectMin.x + 8.0f, rectMin.y + (uiSize.y - ImGui::GetFontSize()) * 0.5f), fgCol, disp.c_str());
        if (isFocused && fmodf((float)ImGui::GetTime(), 1.0f) < 0.5f) {
            ImVec2 sz = ImGui::CalcTextSize(disp.c_str());
            drawList->AddLine(ImVec2(rectMin.x + 8.0f + sz.x + 1.0f, rectMin.y + 6.0f), ImVec2(rectMin.x + 8.0f + sz.x + 1.0f, rectMax.y - 6.0f), fgCol, 1.5f);
        }
    }
}
