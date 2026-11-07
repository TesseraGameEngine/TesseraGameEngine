#include "UICanvasNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UICanvasNode::UICanvasNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Canvas;
    parentId = pId;
    name = nodeName.empty() ? "Canvas" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(1280.0f, 720.0f);
    uiAnchor = UIAnchor::TopLeft;
    uiColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    uiBorderColor = glm::vec4(0.3f, 0.6f, 0.9f, 0.5f);
    uiBorderThickness = 1.0f;
    uiRounding = 0.0f;
    uiVisible = true;
}

void UICanvasNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::Text("Root Canvas container for 2D UI hierarchy.");
    RenderThemeStyling(state);
}

void UICanvasNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    if (isEditorMode) {
        ImU32 frameCol = IM_COL32(80, 160, 240, 100);
        float thick = 1.0f;
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasW, canvasPos.y + canvasH), frameCol, 0.0f, 0, thick);
        std::string tag = "[UI Canvas] " + name;
        drawList->AddText(ImVec2(canvasPos.x + 10.0f, canvasPos.y + 10.0f), frameCol, tag.c_str());
    }
}
