#include "UINode.h"
#include "Scene/Scene.h"
#include "UI/NodeInspector.h"
#include "UI/UINodeRenderer.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>

UINode::UINode() {
    uiVisible = true;
    uiEnabled = true;
    uiAnchor = UIAnchor::Center;
    uiPosition = glm::vec2(0.0f, 0.0f);
    uiSize = glm::vec2(100.0f, 40.0f);
    uiColor = glm::vec4(0.2f, 0.2f, 0.25f, 0.9f);
    uiTextColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    uiBorderColor = glm::vec4(0.4f, 0.4f, 0.5f, 1.0f);
    uiBorderThickness = 1.0f;
    uiRounding = 4.0f;
    uiHoverColor = glm::vec4(0.3f, 0.3f, 0.38f, 1.0f);
    uiPressColor = glm::vec4(0.15f, 0.15f, 0.2f, 1.0f);
}

void UINode::ComputeUIRect(const Scene& scene, ImVec2 canvasPos, float w, float h,
                           ImVec2& anchorOrigin, ImVec2& rectMin, ImVec2& rectMax) const {
    ImVec2 origin = canvasPos;
    float contentW = w;
    float contentH = h;

    if (parentId != 0) {
        auto parent = scene.GetNodeById(parentId);
        if (parent && parent->IsUINode()) {
            ImVec2 pAnchor, pMin, pMax;
            auto uiParent = std::dynamic_pointer_cast<UINode>(parent);
            if (uiParent) {
                uiParent->ComputeUIRect(scene, canvasPos, w, h, pAnchor, pMin, pMax);
            } else {
                pMin = canvasPos;
                pMax = ImVec2(canvasPos.x + parent->uiSize.x, canvasPos.y + parent->uiSize.y);
            }

            if (parent->nodeType == NodeType::UI_Window) {
                float titleH = 26.0f;
                origin = ImVec2(pMin.x, pMin.y + titleH);
                contentW = (std::max)(1.0f, pMax.x - pMin.x);
                contentH = (std::max)(1.0f, (pMax.y - pMin.y) - titleH);
            } else {
                origin = pMin;
                contentW = (std::max)(1.0f, pMax.x - pMin.x);
                contentH = (std::max)(1.0f, pMax.y - pMin.y);
            }
        }
    }

    anchorOrigin = origin;
    switch (uiAnchor) {
        case UIAnchor::TopLeft:      anchorOrigin = origin; break;
        case UIAnchor::TopCenter:    anchorOrigin = ImVec2(origin.x + contentW * 0.5f, origin.y); break;
        case UIAnchor::TopRight:     anchorOrigin = ImVec2(origin.x + contentW, origin.y); break;
        case UIAnchor::CenterLeft:   anchorOrigin = ImVec2(origin.x, origin.y + contentH * 0.5f); break;
        case UIAnchor::Center:       anchorOrigin = ImVec2(origin.x + contentW * 0.5f, origin.y + contentH * 0.5f); break;
        case UIAnchor::CenterRight:  anchorOrigin = ImVec2(origin.x + contentW, origin.y + contentH * 0.5f); break;
        case UIAnchor::BottomLeft:   anchorOrigin = ImVec2(origin.x, origin.y + contentH); break;
        case UIAnchor::BottomCenter: anchorOrigin = ImVec2(origin.x + contentW * 0.5f, origin.y + contentH); break;
        case UIAnchor::BottomRight:  anchorOrigin = ImVec2(origin.x + contentW, origin.y + contentH); break;
    }

    rectMin = anchorOrigin;
    switch (uiAnchor) {
        case UIAnchor::TopLeft:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x, anchorOrigin.y + uiPosition.y);
            break;
        case UIAnchor::TopCenter:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x - uiSize.x * 0.5f, anchorOrigin.y + uiPosition.y);
            break;
        case UIAnchor::TopRight:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x - uiSize.x, anchorOrigin.y + uiPosition.y);
            break;
        case UIAnchor::CenterLeft:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x, anchorOrigin.y + uiPosition.y - uiSize.y * 0.5f);
            break;
        case UIAnchor::Center:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x - uiSize.x * 0.5f, anchorOrigin.y + uiPosition.y - uiSize.y * 0.5f);
            break;
        case UIAnchor::CenterRight:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x - uiSize.x, anchorOrigin.y + uiPosition.y - uiSize.y * 0.5f);
            break;
        case UIAnchor::BottomLeft:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x, anchorOrigin.y + uiPosition.y - uiSize.y);
            break;
        case UIAnchor::BottomCenter:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x - uiSize.x * 0.5f, anchorOrigin.y + uiPosition.y - uiSize.y);
            break;
        case UIAnchor::BottomRight:
            rectMin = ImVec2(anchorOrigin.x + uiPosition.x - uiSize.x, anchorOrigin.y + uiPosition.y - uiSize.y);
            break;
    }
    rectMax = ImVec2(rectMin.x + uiSize.x, rectMin.y + uiSize.y);
}

void UINode::RenderThemeStyling(NodeInspectorState& state) {
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::TreeNodeEx("Typography & Custom Font (.ttf)", ImGuiTreeNodeFlags_DefaultOpen)) {
        char fontBuf[512];
        std::strncpy(fontBuf, uiFontPath.c_str(), sizeof(fontBuf));
        fontBuf[sizeof(fontBuf) - 1] = '\0';
        if (ImGui::InputText("TrueType Font (.ttf)", fontBuf, sizeof(fontBuf))) {
            uiFontPath = fontBuf;
            if (!uiFontPath.empty()) {
                UINodeRenderer::LoadCustomTTFFont(uiFontPath, uiFontSize);
            }
        }
        ImGui::SliderFloat("Font Size (px)", &uiFontSize, 8.0f, 72.0f, "%.1f px");
        if (!uiFontPath.empty()) {
            auto fontInfo = UINodeRenderer::GetLoadedFontInfo(uiFontPath);
            if (fontInfo.valid) {
                ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "Font: %s (%u glyphs, %u upem)",
                                   fontInfo.familyName.c_str(), fontInfo.numGlyphs, fontInfo.unitsPerEm);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Theme & Color Styling", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.lastHexNodeId != id || !state.isEditingBgHex) {
            snprintf(state.bgHexBuf, sizeof(state.bgHexBuf), "#%02X%02X%02X",
                     (int)(uiColor.r * 255.0f), (int)(uiColor.g * 255.0f), (int)(uiColor.b * 255.0f));
        }
        if (state.lastHexNodeId != id || !state.isEditingFgHex) {
            snprintf(state.fgHexBuf, sizeof(state.fgHexBuf), "#%02X%02X%02X",
                     (int)(uiTextColor.r * 255.0f), (int)(uiTextColor.g * 255.0f), (int)(uiTextColor.b * 255.0f));
        }
        state.lastHexNodeId = id;

        if (ImGui::InputText("Background Hex", state.bgHexBuf, sizeof(state.bgHexBuf))) {
            std::string s = state.bgHexBuf;
            if (!s.empty() && s[0] == '#') s = s.substr(1);
            if (s.length() == 6) {
                try {
                    unsigned int val = std::stoul(s, nullptr, 16);
                    uiColor.r = ((val >> 16) & 0xFF) / 255.0f;
                    uiColor.g = ((val >> 8) & 0xFF) / 255.0f;
                    uiColor.b = (val & 0xFF) / 255.0f;
                } catch (...) {}
            }
        }
        state.isEditingBgHex = ImGui::IsItemActive();

        if (ImGui::InputText("Foreground Hex", state.fgHexBuf, sizeof(state.fgHexBuf))) {
            std::string s = state.fgHexBuf;
            if (!s.empty() && s[0] == '#') s = s.substr(1);
            if (s.length() == 6) {
                try {
                    unsigned int val = std::stoul(s, nullptr, 16);
                    uiTextColor.r = ((val >> 16) & 0xFF) / 255.0f;
                    uiTextColor.g = ((val >> 8) & 0xFF) / 255.0f;
                    uiTextColor.b = (val & 0xFF) / 255.0f;
                } catch (...) {}
            }
        }
        state.isEditingFgHex = ImGui::IsItemActive();

        ImGui::ColorEdit4("Primary / Background Color", glm::value_ptr(uiColor), ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex);
        ImGui::ColorEdit4("Foreground / Text / Fill Color", glm::value_ptr(uiTextColor), ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex);
        ImGui::ColorEdit4("Border Color", glm::value_ptr(uiBorderColor), ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex);
        ImGui::SliderFloat("Border Thickness", &uiBorderThickness, 0.0f, 10.0f, "%.1f px");
        ImGui::SliderFloat("Corner Rounding", &uiRounding, 0.0f, 30.0f, "%.1f px");
        ImGui::ColorEdit4("Hover Color", glm::value_ptr(uiHoverColor));
        ImGui::ColorEdit4("Pressed Color", glm::value_ptr(uiPressColor));
        ImGui::TreePop();
    }
}

void UINode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    RenderThemeStyling(state);
}

void UINode::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                    bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                    ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    // Default fallback draws a styled rectangle
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiColor.r, uiColor.g, uiColor.b, uiColor.a * (uiEnabled ? 1.0f : 0.45f)));
    ImU32 brdCol = ImGui::ColorConvertFloat4ToU32(ImVec4(uiBorderColor.r, uiBorderColor.g, uiBorderColor.b, uiBorderColor.a * (uiEnabled ? 1.0f : 0.45f)));
    drawList->AddRectFilled(rectMin, rectMax, bgCol, uiRounding);
    if (uiBorderThickness > 0.0f) {
        drawList->AddRect(rectMin, rectMax, brdCol, uiRounding, 0, uiBorderThickness);
    }
}
