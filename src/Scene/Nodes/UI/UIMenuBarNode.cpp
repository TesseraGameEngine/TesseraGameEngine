#include "UIMenuBarNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIMenuBarNode::UIMenuBarNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_MenuBar;
    parentId = pId;
    name = nodeName.empty() ? "Menu Bar" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(600.0f, 28.0f);
    uiAnchor = UIAnchor::TopLeft;
    uiColor = glm::vec4(0.12f, 0.13f, 0.16f, 0.98f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.28f, 0.30f, 0.38f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 0.0f;
    uiItems = { "File", "Edit", "View", "Window", "Help" };
    uiVisible = true;
}

void UIMenuBarNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::Text("Top-level Menus: %zu", uiItems.size());
    for (size_t i = 0; i < uiItems.size(); ++i) {
        ImGui::PushID((int)i);
        char mBuf[64];
        strncpy(mBuf, uiItems[i].c_str(), sizeof(mBuf));
        mBuf[sizeof(mBuf) - 1] = '\0';
        if (ImGui::InputText("Menu Title", mBuf, sizeof(mBuf))) uiItems[i] = mBuf;
        ImGui::PopID();
    }
    RenderThemeStyling(state);
}

void UIMenuBarNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, 0.0f);
    if (uiBorderThickness > 0.0f) drawList->AddLine(ImVec2(rectMin.x, rectMax.y), rectMax, brdCol, uiBorderThickness);

    std::vector<std::string> menus = uiItems;
    if (menus.empty()) menus = { "File", "Edit", "View", "Window", "Help" };

    ImGuiIO& io = ImGui::GetIO();
    float curX = rectMin.x + 8.0f;
    for (size_t mi = 0; mi < menus.size(); ++mi) {
        ImVec2 sz = ImGui::CalcTextSize(menus[mi].c_str());
        ImVec2 mMin(curX, rectMin.y);
        ImVec2 mMax(curX + sz.x + 16.0f, rectMax.y);
        bool isMnuHovered = (io.MousePos.x >= mMin.x && io.MousePos.x <= mMax.x &&
                             io.MousePos.y >= mMin.y && io.MousePos.y <= mMax.y);
        if (isMnuHovered) {
            drawList->AddRectFilled(mMin, mMax, IM_COL32(255, 255, 255, 30), 2.0f);
            if (uiEnabled && isClicked) {
                if (state.activeMenuBarNodeId == id && state.activeMenuIndex == (int)mi) {
                    state.activeMenuBarNodeId = 0;
                    state.activeMenuIndex = -1;
                } else {
                    state.activeMenuBarNodeId = id;
                    state.activeMenuIndex = (int)mi;
                }
            }
        }
        drawList->AddText(ImVec2(curX + 8.0f, rectMin.y + (uiSize.y - sz.y) * 0.5f), fgCol, menus[mi].c_str());
        curX += sz.x + 16.0f;
    }
}
