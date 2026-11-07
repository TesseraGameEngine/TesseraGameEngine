#pragma once
#include "Scene/Node.h"

class Scene;
struct NodeInspectorState;

class EmptyNode : public Node {
public:
    EmptyNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;
};
