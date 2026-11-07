#include "UITextNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UITextNode::UITextNode(const std::string& text, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Text;
    parentId = pId;
    uiText = text;
    name = nodeName.empty() ? ("Text (" + text.substr(0, 10) + ")") : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(160.0f, 28.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderThickness = 0.0f;
    uiRounding = 0.0f;
    uiVisible = true;
}

void UITextNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char textBuf[256];
    strncpy(textBuf, uiText.c_str(), sizeof(textBuf));
    textBuf[sizeof(textBuf) - 1] = '\0';
    if (ImGui::InputText("Label / Text", textBuf, sizeof(textBuf))) {
        uiText = textBuf;
    }
    RenderThemeStyling(state);
}

void UITextNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));
    drawList->AddText(rectMin, fgCol, uiText.c_str());
}
