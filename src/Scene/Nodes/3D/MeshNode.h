#pragma once
#include "Scene/Node.h"

class Scene;
struct NodeInspectorState;

class MeshNode : public Node {
public:
    MeshNode(ShapeType shapeType = ShapeType::Cube, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;
};
