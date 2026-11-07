#include "UIDropdownNode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

UIDropdownNode::UIDropdownNode(const std::vector<std::string>& items, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::UI_Dropdown;
    parentId = pId;
    uiItems = items;
    uiSelectedIndex = 0;
    uiText = items.empty() ? "" : items[0];
    name = nodeName.empty() ? "Dropdown" : nodeName;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(160.0f, 32.0f);
    uiAnchor = UIAnchor::Center;
    uiColor = glm::vec4(0.18f, 0.20f, 0.26f, 1.0f);
    uiTextColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
    uiBorderColor = glm::vec4(0.40f, 0.45f, 0.55f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiVisible = true;
}

void UIDropdownNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (!uiItems.empty()) {
        std::vector<const char*> itemPtrs;
        for (const auto& itm : uiItems) itemPtrs.push_back(itm.c_str());
        int curSel = uiSelectedIndex;
        if (ImGui::Combo("Select Active Item", &curSel, itemPtrs.data(), (int)itemPtrs.size())) {
            uiSelectedIndex = curSel;
            uiText = uiItems[curSel];
        }
    }
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

void UIDropdownNode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                      bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                      ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    if (uiEnabled && isClicked) {
        if (state.activeDropdownNodeId == id) state.activeDropdownNodeId = 0;
        else state.activeDropdownNodeId = id;
    }
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiTextColor.r, uiTextColor.g, uiTextColor.b, uiTextColor.a * (uiEnabled ? 1.0f : 0.45f)));

    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness > 0.0f ? uiBorderThickness : 1.0f);

    std::string itemText = uiText;
    if (uiSelectedIndex >= 0 && uiSelectedIndex < (int)uiItems.size()) itemText = uiItems[uiSelectedIndex];
    drawList->AddText(ImVec2(rectMin.x + 8.0f, rectMin.y + (uiSize.y - ImGui::GetFontSize()) * 0.5f), fgCol, itemText.c_str());

    float arrX = rectMax.x - 14.0f;
    float arrY = rectMin.y + uiSize.y * 0.5f;
    drawList->AddTriangleFilled(ImVec2(arrX - 4.0f, arrY - 2.0f), ImVec2(arrX + 4.0f, arrY - 2.0f), ImVec2(arrX, arrY + 3.0f), fgCol);
}
