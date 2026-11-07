#include "UISpinnerNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UISpinnerNode::UISpinnerNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Spinner;
    parentId = pId;
    name = nodeName.empty() ? "Spinner" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(40.0f, 40.0f);
    uiValue = 1.0f; // Spin speed
    uiColor = glm::vec4(0.2f, 0.2f, 0.25f, 0.3f);
    uiTextColor = glm::vec4(0.35f, 0.70f, 1.0f, 1.0f);
    uiRounding = 20.0f;
    uiBorderThickness = 3.0f;
    uiVisible = true;
}

void UISpinnerNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::SliderFloat("Spin Speed", &uiValue, 0.1f, 10.0f, "%.1fx");
    RenderThemeStyling(state);
}

void UISpinnerNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    float rad = (std::min)(uiSize.x, uiSize.y) * 0.5f;
    ImVec2 center(rectMin.x + uiSize.x * 0.5f, rectMin.y + uiSize.y * 0.5f);
    float spinSpeed = (uiValue <= 0.0f) ? 1.0f : uiValue;
    float time = (float)ImGui::GetTime() * spinSpeed;
    float aMin = time * 4.0f;
    float aMax = aMin + 1.5f + sinf(time * 2.0f) * 0.8f;

    ImU32 trackCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, 0.35f));
    ImU32 spinCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a));
    drawList->AddCircle(center, rad - 2.0f, trackCol, 24, uiBorderThickness);
    drawList->PathClear();
    drawList->PathArcTo(center, rad - 2.0f, aMin, aMax, 20);
    drawList->PathStroke(spinCol, 0, uiBorderThickness);
}
