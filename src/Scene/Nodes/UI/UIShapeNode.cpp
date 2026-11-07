#include "UIShapeNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIShapeNode::UIShapeNode(UIShapeType shape, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Shape;
    parentId = pId;
    uiShapeType = shape;
    name = nodeName.empty() ? (std::string(UIShapeTypeToString(shape)) + " Shape") : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(120.0f, 120.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.25f, 0.55f, 0.85f, 0.85f);
    uiBorderColor = glm::vec4(0.90f, 0.95f, 1.0f, 1.0f);
    uiBorderThickness = 2.0f;
    uiRounding = 12.0f;
    uiVisible = true;
}

void UIShapeNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    const char* shapeNames[] = { "Rectangle", "Rounded Rectangle", "Circle", "Triangle" };
    int curShape = static_cast<int>(uiShapeType);
    if (ImGui::Combo("Shape Type", &curShape, shapeNames, IM_ARRAYSIZE(shapeNames))) {
        uiShapeType = static_cast<UIShapeType>(curShape);
    }
    RenderThemeStyling(state);
}

void UIShapeNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));

    switch (uiShapeType) {
        case UIShapeType::Rectangle:
            drawList->AddRectFilled(rectMin, rectMax, bgCol, 0.0f);
            if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, 0.0f, 0, uiBorderThickness);
            break;
        case UIShapeType::RoundedRect:
            drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
            if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);
            break;
        case UIShapeType::Circle: {
            float rad = (std::min)(uiSize.x, uiSize.y) * 0.5f;
            ImVec2 center(rectMin.x + uiSize.x * 0.5f, rectMin.y + uiSize.y * 0.5f);
            drawList->AddCircleFilled(center, rad, bgCol, 36);
            if (uiBorderThickness > 0.0f) drawList->AddCircle(center, rad, brdCol, 36, uiBorderThickness);
            break;
        }
        case UIShapeType::Triangle: {
            ImVec2 p1(rectMin.x + uiSize.x * 0.5f, rectMin.y);
            ImVec2 p2(rectMax.x, rectMax.y);
            ImVec2 p3(rectMin.x, rectMax.y);
            drawList->AddTriangleFilled(p1, p2, p3, bgCol);
            if (uiBorderThickness > 0.0f) drawList->AddTriangle(p1, p2, p3, brdCol, uiBorderThickness);
            break;
        }
    }
}
