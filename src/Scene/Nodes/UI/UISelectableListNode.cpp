#include "UISelectableListNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UISelectableListNode::UISelectableListNode(const std::vector<std::string>& items, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_SelectableList;
    parentId = pId;
    uiItems = items;
    uiSelectedIndex = 0;
    name = nodeName.empty() ? "Selectable List" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(180.0f, 150.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.12f, 0.13f, 0.16f, 0.95f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.35f, 0.38f, 0.48f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UISelectableListNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    ImGui::DragInt("Selected Index", &uiSelectedIndex, 0.2f, 0, (int)uiItems.size() - 1);
    ImGui::Text("Items Count: %d", (int)uiItems.size());
    for (size_t i = 0; i < uiItems.size(); ++i) {
        ImGui::PushID((int)i);
        char itemBuf[128];
        strncpy(itemBuf, uiItems[i].c_str(), sizeof(itemBuf));
        itemBuf[sizeof(itemBuf) - 1] = '\0';
        std::string itemLbl = "##Item" + std::to_string(i);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
        if (ImGui::InputText(itemLbl.c_str(), itemBuf, sizeof(itemBuf))) uiItems[i] = itemBuf;
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            uiItems.erase(uiItems.begin() + i);
            if (uiSelectedIndex >= (int)uiItems.size()) uiSelectedIndex = (int)uiItems.size() - 1;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    static char newItemBuf[128] = "";
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    ImGui::InputText("##NewItem", newItemBuf, sizeof(newItemBuf));
    ImGui::SameLine();
    if (ImGui::Button("Add Item") && strlen(newItemBuf) > 0) {
        uiItems.push_back(newItemBuf);
        newItemBuf[0] = '\0';
    }
    RenderThemeStyling(state);
}

void UISelectableListNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    if (uiBorderThickness > 0.0f) drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);

    float itemH = 24.0f;
    ImGuiIO& io = ImGui::GetIO();
    for (size_t i = 0; i < uiItems.size(); ++i) {
        ImVec2 iMin(rectMin.x + 2.0f, rectMin.y + 2.0f + i * itemH);
        ImVec2 iMax(rectMax.x - 2.0f, iMin.y + itemH);
        if (iMax.y > rectMax.y) break;

        bool isItemHovered = (io.MousePos.x >= iMin.x && io.MousePos.x <= iMax.x &&
                              io.MousePos.y >= iMin.y && io.MousePos.y <= iMax.y);
        bool isSel = (uiSelectedIndex == (int)i);
        if (isSel) {
            drawList->AddRectFilled(iMin, iMax, IM_COL32(50, 110, 190, 220), 2.0f);
        } else if (isItemHovered) {
            drawList->AddRectFilled(iMin, iMax, IM_COL32(255, 255, 255, 25), 2.0f);
        }
        if (uiEnabled && isItemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            uiSelectedIndex = (int)i;
        }
        drawList->AddText(ImVec2(iMin.x + 8.0f, iMin.y + (itemH - ImGui::GetFontSize()) * 0.5f), isSel ? IM_COL32(255, 255, 255, 255) : fgCol, uiItems[i].c_str());
    }
}
