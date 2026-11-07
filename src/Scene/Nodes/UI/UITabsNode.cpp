#include "UITabsNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UITabsNode::UITabsNode(const std::vector<std::string>& tabs, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Tabs;
    parentId = pId;
    uiItems = tabs;
    uiSelectedIndex = 0;
    name = nodeName.empty() ? "Tabs Control" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(320.0f, 34.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.14f, 0.15f, 0.18f, 1.0f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.38f, 0.48f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UITabsNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::DragInt("Active Tab Index", &uiSelectedIndex, 0.2f, 0, (int)uiItems.size() - 1);
    for (size_t i = 0; i < uiItems.size(); ++i) {
        ImGui::PushID((int)i);
        char tBuf[64];
        strncpy(tBuf, uiItems[i].c_str(), sizeof(tBuf));
        tBuf[sizeof(tBuf) - 1] = '\0';
        if (ImGui::InputText("Tab Label", tBuf, sizeof(tBuf))) uiItems[i] = tBuf;
        ImGui::PopID();
    }
    RenderThemeStyling(state);
}

void UITabsNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    std::vector<std::string> tabs = uiItems;
    if (tabs.empty()) tabs = { "Tab 1", "Tab 2" };
    float tabW = uiSize.x / (float)tabs.size();

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a));

    for (size_t t = 0; t < tabs.size(); ++t) {
        ImVec2 tMin(rectMin.x + t * tabW, rectMin.y);
        ImVec2 tMax(tMin.x + tabW, rectMax.y);
        bool isAct = (uiSelectedIndex == (int)t);

        drawList->AddRectFilled(tMin, tMax, isAct ? IM_COL32(45, 55, 75, 255) : IM_COL32(22, 24, 30, 240), uiRounding, ImDrawFlags_RoundCornersTop);
        drawList->AddRect(tMin, tMax, brdCol, uiRounding, ImDrawFlags_RoundCornersTop, 1.0f);
        if (isAct) {
            drawList->AddLine(ImVec2(tMin.x, tMax.y), tMax, IM_COL32(60, 140, 240, 255), 2.5f);
        }

        ImVec2 sz = ImGui::CalcTextSize(tabs[t].c_str());
        ImVec2 pos(tMin.x + (tabW - sz.x) * 0.5f, tMin.y + (uiSize.y - sz.y) * 0.5f);
        drawList->AddText(pos, isAct ? fgCol : IM_COL32(180, 180, 190, 220), tabs[t].c_str());

        bool isTabHovered = (mousePos.x >= tMin.x && mousePos.x <= tMax.x &&
                            mousePos.y >= tMin.y && mousePos.y <= tMax.y);
        if (uiEnabled && isTabHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            uiSelectedIndex = (int)t;
        }
    }
}
