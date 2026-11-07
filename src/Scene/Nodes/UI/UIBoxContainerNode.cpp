#include "UIBoxContainerNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <algorithm>

UIBoxContainerNode::UIBoxContainerNode(BoxOrientation orient, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_BoxContainer;
    parentId = pId;
    orientation = orient;
    name = nodeName.empty() ? (orient == BoxOrientation::Vertical ? "VBoxContainer" : "HBoxContainer") : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(220.0f, 180.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.12f, 0.14f, 0.18f, 0.65f);
    uiBorderColor = glm::vec4(0.25f, 0.35f, 0.5f, 0.8f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
    spacing = 8.0f;
    padding = 8.0f;
    autoResize = true;
}

void UIBoxContainerNode::UpdateLayout(Scene& scene) {
    auto children = scene.GetChildren(id);
    if (children.empty()) return;

    if (orientation == BoxOrientation::Vertical) {
        float currentY = padding;
        float maxW = 0.0f;
        for (auto& child : children) {
            if (!child || !child->IsUINode()) continue;
            child->uiAnchor = UIAnchor::TopLeft;
            child->uiPosition = glm::vec2(padding, currentY);
            currentY += child->uiSize.y + spacing;
            maxW = std::max(maxW, child->uiSize.x);
        }
        if (autoResize && currentY > padding) {
            uiSize.y = (currentY - spacing) + padding;
            uiSize.x = std::max(uiSize.x, maxW + padding * 2.0f);
        }
    } else {
        float currentX = padding;
        float maxH = 0.0f;
        for (auto& child : children) {
            if (!child || !child->IsUINode()) continue;
            child->uiAnchor = UIAnchor::TopLeft;
            child->uiPosition = glm::vec2(currentX, padding);
            currentX += child->uiSize.x + spacing;
            maxH = std::max(maxH, child->uiSize.y);
        }
        if (autoResize && currentX > padding) {
            uiSize.x = (currentX - spacing) + padding;
            uiSize.y = std::max(uiSize.y, maxH + padding * 2.0f);
        }
    }
}

void UIBoxContainerNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Auto-Layout Box Container");
    ImGui::Separator();

    const char* orientations[] = { "Horizontal", "Vertical" };
    int currentOrient = (orientation == BoxOrientation::Horizontal) ? 0 : 1;
    if (ImGui::Combo("Orientation##Box", &currentOrient, orientations, IM_ARRAYSIZE(orientations))) {
        orientation = (currentOrient == 0) ? BoxOrientation::Horizontal : BoxOrientation::Vertical;
        UpdateLayout(scene);
    }

    if (ImGui::DragFloat("Spacing##Box", &spacing, 0.5f, 0.0f, 100.0f, "%.1f px")) {
        UpdateLayout(scene);
    }

    if (ImGui::DragFloat("Padding##Box", &padding, 0.5f, 0.0f, 100.0f, "%.1f px")) {
        UpdateLayout(scene);
    }

    if (ImGui::Checkbox("Auto Resize Container##Box", &autoResize)) {
        UpdateLayout(scene);
    }

    ImGui::Spacing();
    RenderThemeStyling(state);
}

void UIBoxContainerNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
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

