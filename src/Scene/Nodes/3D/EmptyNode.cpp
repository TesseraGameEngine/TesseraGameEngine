#include "EmptyNode.h"
#include "Scene/Scene.h"
#include <imgui.h>

EmptyNode::EmptyNode(const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Empty;
    parentId = pId;
    name = nodeName.empty() ? "Empty Node" : nodeName;
    scale = glm::vec3(1.0f);
}

std::shared_ptr<Node> EmptyNode::Clone() const {
    return std::make_shared<EmptyNode>(*this);
}

void EmptyNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    if (ImGui::CollapsingHeader("Group Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("This is an invisible Empty Group node used to transform and organize child nodes in the hierarchy.");
        ImGui::Spacing();
        ImGui::Text("Child Nodes: %zu", childrenIds.size());
        for (uint32_t cid : childrenIds) {
            auto child = scene.GetNodeById(cid);
            if (child) {
                ImGui::BulletText("%s (ID: %u)", child->name.c_str(), child->id);
            }
        }
    }
}
