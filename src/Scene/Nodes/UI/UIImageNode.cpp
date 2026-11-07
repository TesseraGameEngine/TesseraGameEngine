#include "UIImageNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIImageNode::UIImageNode(const std::string& texPath, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Image;
    parentId = pId;
    texturePath = texPath;
    name = nodeName.empty() ? "Image" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(160.0f, 160.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.2f, 0.2f, 0.25f, 0.9f);
    uiBorderColor = glm::vec4(0.4f, 0.4f, 0.5f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UIImageNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    char pathBuf[260];
    strncpy(pathBuf, texturePath.c_str(), sizeof(pathBuf));
    pathBuf[sizeof(pathBuf) - 1] = '\0';
    if (ImGui::InputText("Texture Path", pathBuf, sizeof(pathBuf))) {
        texturePath = pathBuf;
    }
    RenderThemeStyling(state);
}

void UIImageNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a));
    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);

    std::string tag = texturePath.empty() ? "[No Texture]" : ("[Image: " + texturePath.substr(texturePath.find_last_of("/\\") + 1) + "]");
    ImVec2 tagSz = ImGui::CalcTextSize(tag.c_str());
    drawList->AddText(ImVec2(rectMin.x + (uiSize.x - tagSz.x) * 0.5f, rectMin.y + (uiSize.y - tagSz.y) * 0.5f), IM_COL32(200, 210, 225, 200), tag.c_str());
}
