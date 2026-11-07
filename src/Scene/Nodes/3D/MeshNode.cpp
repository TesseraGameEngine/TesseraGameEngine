#include "MeshNode.h"
#include "Scene/Scene.h"

MeshNode::MeshNode(ShapeType shapeType, const std::string& nodeName, uint32_t pId) {
    nodeType = NodeType::Mesh;
    type = shapeType;
    parentId = pId;
    name = nodeName.empty() ? (std::string(ShapeTypeToString(shapeType)) + " Node") : nodeName;

    switch (shapeType) {
        case ShapeType::Cube: color = glm::vec4(0.85f, 0.42f, 0.22f, 1.0f); break;
        case ShapeType::Sphere: color = glm::vec4(0.25f, 0.65f, 0.90f, 1.0f); break;
        case ShapeType::Cone: color = glm::vec4(0.92f, 0.75f, 0.20f, 1.0f); break;
        case ShapeType::Cylinder: color = glm::vec4(0.35f, 0.80f, 0.45f, 1.0f); break;
        case ShapeType::Plane: color = glm::vec4(0.60f, 0.60f, 0.65f, 1.0f); break;
        case ShapeType::Custom: color = glm::vec4(0.75f, 0.75f, 0.80f, 1.0f); break;
    }
}

std::shared_ptr<Node> MeshNode::Clone() const {
    return std::make_shared<MeshNode>(*this);
}

void MeshNode::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    // Mesh PBR materials, Jolt Physics, Inverse Kinematics (IK) & Morph Targets,
    // and Multiplayer Replication are rendered in the unified EditorUI::RenderInspector.
    (void)scene;
    (void)state;
}
