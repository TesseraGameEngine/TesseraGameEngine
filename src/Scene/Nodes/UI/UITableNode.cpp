#include "UITableNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UITableNode::UITableNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Table;
    parentId = pId;
    name = nodeName.empty() ? "Table View" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(320.0f, 160.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.12f, 0.13f, 0.16f, 0.95f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.38f, 0.48f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiItems = { "Header 1", "Header 2", "Header 3", "Alpha", "100", "$5.00", "Beta", "250", "$12.50", "Gamma", "50", "$2.20" };
    uiVisible = true;
}

void UITableNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::Text("Table Entries: %zu (3 columns)", uiItems.size());
    RenderThemeStyling(state);
}

void UITableNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);

    int cols = 3;
    float colW = uiSize.x / (float)cols;
    float rowH = 24.0f;
    drawList->AddRectFilled(rectMin, ImVec2(rectMax.x, rectMin.y + rowH), IM_COL32(40, 48, 62, 255), uiRounding, ImDrawFlags_RoundCornersTop);
    drawList->AddLine(ImVec2(rectMin.x, rectMin.y + rowH), ImVec2(rectMax.x, rectMin.y + rowH), brdCol, 1.0f);

    for (size_t idx = 0; idx < uiItems.size(); ++idx) {
        int r = (int)(idx / cols);
        int c = (int)(idx % cols);
        float cellX = rectMin.x + c * colW + 6.0f;
        float cellY = rectMin.y + r * rowH + (rowH - ImGui::GetFontSize()) * 0.5f;
        if (cellY + rowH > rectMax.y) break;
        drawList->AddText(ImVec2(cellX, cellY), (r == 0) ? IM_COL32(255, 220, 100, 255) : fgCol, uiItems[idx].c_str());
    }
}
