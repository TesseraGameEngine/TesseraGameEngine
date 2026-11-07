#pragma once

#include <memory>
#include <string>
#include <vector>
#include <imgui.h>
#include <glm/glm.hpp>
#include "Scene/Scene.h"
#include "Scene/Node.h"

struct NodeInspectorState {
    bool isEditingBgHex{false};
    bool isEditingFgHex{false};
    bool isEditingPickerHex{false};
    char bgHexBuf[32]{"#333333"};
    char fgHexBuf[32]{"#FFFFFF"};
    char pickerHexBuf[32]{"#FFFFFF"};
    uint32_t lastHexNodeId{0};
};

class NodeInspector {
public:
    static void RenderTransform(Scene& scene, const std::shared_ptr<Node>& node);
    static void RenderUIProperties(Scene& scene, const std::shared_ptr<Node>& node, NodeInspectorState& state);
    static void RenderColliderProperties(const std::shared_ptr<Node>& node);
    static void RenderCameraProperties(Scene& scene, const std::shared_ptr<Node>& node);
    static void RenderLightProperties(const std::shared_ptr<Node>& node);
    static void RenderEmptyGroupProperties(Scene& scene, const std::shared_ptr<Node>& node);
};

