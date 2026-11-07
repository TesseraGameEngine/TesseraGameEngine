#include "UIGridContainerNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <algorithm>

UIGridContainerNode::UIGridContainerNode(int cols, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_GridContainer;
    parentId = pId;
    columns = std::max(cols, 1);
    name = nodeName.empty() ? "GridContainer" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(280.0f, 280.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.12f, 0.14f, 0.18f, 0.65f);
    uiBorderColor = glm::vec4(0.25f, 0.35f, 0.5f, 0.8f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
    cellWidth = 60.0f;
    cellHeight = 60.0f;
    horizontalSpacing = 8.0f;
    verticalSpacing = 8.0f;
    padding = 8.0f;
    autoResize = true;
}

void UIGridContainerNode::UpdateLayout(Scene& scene) {
    auto children = scene.GetChildren(id);
    if (children.empty()) return;

    int cols = std::max(columns, 1);
    int validChildIdx = 0;

    for (auto& child : children) {
        if (!child || !child->IsUINode()) continue;
        int row = validChildIdx / cols;
        int col = validChildIdx % cols;

        float posX = padding + col * (cellWidth + horizontalSpacing);
        float posY = padding + row * (cellHeight + verticalSpacing);

        child->uiAnchor = UIAnchor::TopLeft;
        child->uiPosition = glm::vec2(posX, posY);
        child->uiSize = glm::vec2(cellWidth, cellHeight);

        validChildIdx++;
    }

    if (autoResize && validChildIdx > 0) {
        int rowCount = (validChildIdx + cols - 1) / cols;
        int activeCols = std::min(validChildIdx, cols);

        float requiredW = padding * 2.0f + activeCols * cellWidth + (activeCols > 1 ? (activeCols - 1) * horizontalSpacing : 0.0f);
        float requiredH = padding * 2.0f + rowCount * cellHeight + (rowCount > 1 ? (rowCount - 1) * verticalSpacing : 0.0f);

        uiSize.x = requiredW;
        uiSize.y = requiredH;
    }
}

void UIGridContainerNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Auto-Layout Grid Container");
    ImGui::Separator();

    if (ImGui::SliderInt("Columns##Grid", &columns, 1, 16)) {
        UpdateLayout(scene);
    }

    if (ImGui::DragFloat2("Cell Size (W x H)##Grid", &cellWidth, 1.0f, 10.0f, 500.0f, "%.0f px")) {
        UpdateLayout(scene);
    }

    if (ImGui::DragFloat("H Spacing##Grid", &horizontalSpacing, 0.5f, 0.0f, 100.0f, "%.1f px")) {
        UpdateLayout(scene);
    }

    if (ImGui::DragFloat("V Spacing##Grid", &verticalSpacing, 0.5f, 0.0f, 100.0f, "%.1f px")) {
        UpdateLayout(scene);
    }

    if (ImGui::DragFloat("Padding##Grid", &padding, 0.5f, 0.0f, 100.0f, "%.1f px")) {
        UpdateLayout(scene);
    }

    if (ImGui::Checkbox("Auto Resize Container##Grid", &autoResize)) {
        UpdateLayout(scene);
    }

    ImGui::Spacing();
    RenderThemeStyling(state);
}

void UIGridContainerNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                                 bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                                 ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    UpdateLayout(scene);

    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    if (uiBorderThickness > 0.0f) {
        drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);
    }
}

