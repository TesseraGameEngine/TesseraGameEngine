#include "Localization/Localization.h"
#include "LightNode.h"
#include "Scene/Scene.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

LightNode::LightNode(LightType lType, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Light;
    lightType = lType;
    parentId = pId;
    name = nodeName.empty() ? (std::string(LightTypeToString(lType)) + " Light") : nodeName;
    position = glm::vec3(0.0f, 4.0f, 0.0f);
    lightColor = glm::vec3(1.0f, 0.95f, 0.85f);
    color = glm::vec4(lightColor, 1.0f);
    lightIntensity = 2.0f;
    lightRange = 15.0f;
    spotInnerConeAngle = 25.0f;
    spotOuterConeAngle = 35.0f;
    castShadows = true;
}

std::shared_ptr<Node> LightNode::Clone() const {
    return std::make_shared<LightNode>(*this);
}

void LightNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    (void)scene;
    (void)state;
    if (ImGui::CollapsingHeader(TR("Light Properties"), ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* lightTypes[] = { "Directional", "Point", "Spot", "Area (Rect)" };
        int currentType = static_cast<int>(lightType);
        ImGui::Text("%s", TR("Light Type:"));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##LightType", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes))) {
            lightType = static_cast<LightType>(currentType);
        }

        ImGui::Spacing();
        ImGui::Text("%s", TR("Light Color:"));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::ColorEdit3("##LightColor", glm::value_ptr(lightColor))) {
            color = glm::vec4(lightColor, 1.0f);
        }

        ImGui::Spacing();
        ImGui::Text("%s", TR("Intensity:"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##Intensity", &lightIntensity, 0.0f, 25.0f, "%.2f");

        ImGui::Checkbox(TR("Cast Realtime Shadows"), &castShadows);
        if (castShadows) {
            ImGui::Text("%s", TR("Shadow Bias:"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##ShadowBias", &shadowBias, 0.0001f, 0.05f, "%.4f");
        }

        if (lightType == LightType::Point || lightType == LightType::Spot || lightType == LightType::AreaRect) {
            ImGui::Text("%s", TR("Range / Falloff (m):"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##Range", &lightRange, 0.5f, 1.0f, 250.0f, "%.1f m");
        }

        if (lightType == LightType::Spot) {
            ImGui::Text("%s", TR("Inner Cone Angle:"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##InnerCone", &spotInnerConeAngle, 1.0f, 89.0f, "%.1f deg");

            ImGui::Text("%s", TR("Outer Cone Angle:"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##OuterCone", &spotOuterConeAngle, 1.0f, 89.5f, "%.1f deg");
            if (spotOuterConeAngle < spotInnerConeAngle) {
                spotOuterConeAngle = spotInnerConeAngle;
            }
        }
        if (lightType == LightType::AreaRect) {
            ImGui::Text("%s", TR("Area Width / Height (m):"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat2("##AreaSize", glm::value_ptr(areaLightSize), 0.1f, 0.1f, 50.0f, "%.2f m");
            ImGui::Checkbox(TR("Two-Sided Emission"), &areaLightTwoSided);
        }
    }
}
