#include "UIScrollPaneNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIScrollPaneNode::UIScrollPaneNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_ScrollPane;
    parentId = pId;
    name = nodeName.empty() ? "Scroll Pane" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(240.0f, 180.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.12f, 0.12f, 0.15f, 0.95f);
    uiBorderColor = glm::vec4(0.35f, 0.35f, 0.45f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiValue = 25.0f; // Scroll percentage
    uiVisible = true;
}

void UIScrollPaneNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::SliderFloat("Scroll Position", &uiValue, 0.0f, 100.0f, "%.1f %%");
    RenderThemeStyling(state);
}

void UIScrollPaneNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a));
    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);

    float sbW = 8.0f;
    ImVec2 sbMin(rectMax.x - sbW - 2.0f, rectMin.y + 2.0f);
    ImVec2 sbMax(rectMax.x - 2.0f, rectMax.y - 2.0f);
    drawList->AddRectFilled(sbMin, sbMax, IM_COL32(35, 35, 45, 180), 4.0f);

    float thumbH = (std::max)(20.0f, (sbMax.y - sbMin.y) * 0.3f);
    float tFrac = std::clamp(uiValue / 100.0f, 0.0f, 1.0f);
    float thumbY = sbMin.y + tFrac * ((sbMax.y - sbMin.y) - thumbH);
    drawList->AddRectFilled(ImVec2(sbMin.x, thumbY), ImVec2(sbMax.x, thumbY + thumbH), IM_COL32(120, 130, 150, 220), 4.0f);
}
