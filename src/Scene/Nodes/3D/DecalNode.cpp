#include "DecalNode.h"
#include "Scene/Scene.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

DecalNode::DecalNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Decal;
    parentId = pId;
    name = nodeName.empty() ? "Decal Projector" : nodeName;
    color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    decalSize = glm::vec3(2.0f, 2.0f, 2.0f);
}

std::shared_ptr<Node> DecalNode::Clone() const {
    return std::make_shared<DecalNode>(*this);
}

bool DecalNode::ProjectPoint(const Scene& scene,
                             const glm::vec3& worldPoint,
                             const glm::vec3& worldNormal,
                             glm::vec2& outUV,
                             float& outOpacityWeight) const {
    glm::mat4 worldMat = GetWorldTransformMatrix(scene);
    glm::mat4 boxMat = worldMat * glm::scale(glm::mat4(1.0f), glm::max(decalSize, glm::vec3(0.001f)));
    glm::mat4 invBox = glm::inverse(boxMat);

    // Transform world point into unit box [-0.5, 0.5]^3
    glm::vec4 localPos4 = invBox * glm::vec4(worldPoint, 1.0f);
    glm::vec3 localPos(localPos4);

    if (std::abs(localPos.x) > 0.5f || std::abs(localPos.y) > 0.5f || std::abs(localPos.z) > 0.5f) {
        outOpacityWeight = 0.0f;
        return false;
    }

    // Projection axis is local -Y (or +Y) onto XZ plane
    glm::vec3 projAxis = glm::normalize(glm::vec3(worldMat[1]));
    float cosAngle = std::abs(glm::dot(glm::normalize(worldNormal), projAxis));
    float cosThreshold = std::cos(glm::radians(std::clamp(normalAngleThreshold, 1.0f, 89.9f)));
    if (cosAngle < cosThreshold) {
        outOpacityWeight = 0.0f;
        return false;
    }

    outUV = glm::vec2(localPos.x + 0.5f, localPos.z + 0.5f);

    // Smooth vertical box falloff along Y
    float yFade = 1.0f - std::clamp(std::abs(localPos.y) * 2.0f, 0.0f, 1.0f);
    outOpacityWeight = opacity * yFade;
    return outOpacityWeight > 0.0001f;
}

void DecalNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader("Deferred Decal Projector", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Projection Box Size", glm::value_ptr(decalSize), 0.05f, 0.05f, 100.0f);
        ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Normal Angle Cutoff", &normalAngleThreshold, 5.0f, 90.0f, "%.1f deg");
        ImGui::DragFloat("Fade Distance", &fadeDistance, 0.5f, 1.0f, 500.0f, "%.1f m");
        ImGui::DragInt("Sort Priority", &sortOrder, 1, -100, 100);

        const char* blendModes[] = { "Alpha Blend", "Stain / Multiply", "Emissive / Additive" };
        int curBlend = static_cast<int>(blendMode);
        if (ImGui::Combo("Blend Mode", &curBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
            blendMode = static_cast<DecalBlendMode>(curBlend);
        }
    }
}

