#include "Localization/Localization.h"
#include "CameraNode.h"
#include "Scene/Scene.h"
#include <imgui.h>

CameraNode::CameraNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Camera;
    parentId = pId;
    name = nodeName.empty() ? "Camera" : nodeName;
    position = glm::vec3(0.0f, 2.0f, 5.0f);
    rotation = glm::vec3(-15.0f, 0.0f, 0.0f);
    cameraFov = 60.0f;
    cameraNear = 0.1f;
    cameraFar = 1000.0f;
}

std::shared_ptr<Node> CameraNode::Clone() const {
    return std::make_shared<CameraNode>(*this);
}

void CameraNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader(TR("Camera Lens & Frustum"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%s", TR("Field of View:"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##FOV", &cameraFov, 25.0f, 110.0f, "%.1f deg");

        ImGui::Text("%s", TR("Near Clip Plane:"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##Near", &cameraNear, 0.05f, 0.01f, 10.0f, "%.2f m");

        ImGui::Text("%s", TR("Far Clip Plane:"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##Far", &cameraFar, 1.0f, 10.0f, 1000.0f, "%.1f m");

        ImGui::Spacing();
        if (ImGui::Button(TR("Set as Active Preview Camera"), ImVec2(-1, 26))) {
            scene.SetActiveCamera(id);
        }
    }
}
