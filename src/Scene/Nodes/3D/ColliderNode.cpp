#include "ColliderNode.h"
#include "Scene/Scene.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

ColliderNode::ColliderNode(ColliderType colType, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Collider;
    colliderType = colType;
    parentId = pId;
    name = nodeName.empty() ? (std::string(ColliderTypeToString(colType)) + " Collider") : nodeName;
    switch (colType) {
        case ColliderType::Box: type = ShapeType::Cube; break;
        case ColliderType::Sphere: type = ShapeType::Sphere; break;
        case ColliderType::Capsule: type = ShapeType::Cylinder; break;
        case ColliderType::Mesh: type = ShapeType::Cube; break;
    }
    color = glm::vec4(0.2f, 0.9f, 0.4f, 0.35f);
    colliderSize = glm::vec3(1.0f);
    colliderRadius = 0.5f;
    colliderHeight = 1.0f;
    colliderCenter = glm::vec3(0.0f);
    isTrigger = false;
    bounciness = 0.0f;
    friction = 0.5f;
}

std::shared_ptr<Node> ColliderNode::Clone() const {
    return std::make_shared<ColliderNode>(*this);
}

void ColliderNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader("Collider Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* colTypes[] = { "Box", "Sphere", "Capsule", "Mesh" };
        int currentCol = static_cast<int>(colliderType);
        if (ImGui::Combo("Collider Shape", &currentCol, colTypes, IM_ARRAYSIZE(colTypes))) {
            colliderType = static_cast<ColliderType>(currentCol);
            switch (colliderType) {
                case ColliderType::Box: type = ShapeType::Cube; break;
                case ColliderType::Sphere: type = ShapeType::Sphere; break;
                case ColliderType::Capsule: type = ShapeType::Cylinder; break;
                case ColliderType::Mesh: type = ShapeType::Cube; break;
            }
        }

        ImGui::Checkbox("Is Trigger (Overlap Only)", &isTrigger);

        if (colliderType == ColliderType::Box) {
            ImGui::DragFloat3("Box Size", glm::value_ptr(colliderSize), 0.05f, 0.01f, 100.0f);
        } else if (colliderType == ColliderType::Sphere) {
            ImGui::DragFloat("Sphere Radius", &colliderRadius, 0.02f, 0.01f, 50.0f);
        } else if (colliderType == ColliderType::Capsule) {
            ImGui::DragFloat("Capsule Radius", &colliderRadius, 0.02f, 0.01f, 50.0f);
            ImGui::DragFloat("Capsule Height", &colliderHeight, 0.05f, 0.01f, 50.0f);
        }

        ImGui::DragFloat3("Center Offset", glm::value_ptr(colliderCenter), 0.05f);
        ImGui::SliderFloat("Bounciness", &bounciness, 0.0f, 1.0f);
        ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f);
    }
}
