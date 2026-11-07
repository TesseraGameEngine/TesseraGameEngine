#pragma once

#include "Scene/Node.h"
#include <glm/glm.hpp>

class Scene;
struct NodeInspectorState;

enum class DecalBlendMode {
    AlphaBlend = 0,
    StainMultiply = 1,
    EmissiveAdditive = 2
};

class DecalNode : public Node {
public:
    DecalNode(const std::string& name = "Decal Projector", uint32_t parentId = 0);

    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;

    // Deferred OBB Decal Projection: projects a 3D world surface point & normal into Decal UV [0,1]^2
    bool ProjectPoint(const Scene& scene,
                      const glm::vec3& worldPoint,
                      const glm::vec3& worldNormal,
                      glm::vec2& outUV,
                      float& outOpacityWeight) const;

    glm::vec3 decalSize{2.0f, 2.0f, 2.0f};
    float opacity{1.0f};
    float fadeDistance{25.0f};
    float normalAngleThreshold{75.0f}; // Degrees
    DecalBlendMode blendMode{DecalBlendMode::AlphaBlend};
    int sortOrder{0};
};

