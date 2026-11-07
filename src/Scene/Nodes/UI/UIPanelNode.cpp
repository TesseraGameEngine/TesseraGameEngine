#include "UIPanelNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIPanelNode::UIPanelNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Panel;
    parentId = pId;
    name = nodeName.empty() ? "Panel" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(300.0f, 200.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.12f, 0.12f, 0.15f, 0.92f);
    uiBorderColor = glm::vec4(0.25f, 0.25f, 0.32f, 1.0f);
    uiBorderThickness = 1.5f;
    uiRounding = 6.0f;
    uiVisible = true;
}

void UIPanelNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    RenderThemeStyling(state);
}

void UIPanelNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);
}
