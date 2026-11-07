#pragma once

#include "Scene/Node.h"
#include <imgui.h>

class Scene;
struct NodeInspectorState;
struct UINodeRendererState;
namespace Gameplay { class GameLoop; }

class UINode : public Node {
public:
    UINode();
    virtual ~UINode() = default;

    bool IsUINode() const override { return true; }

    void ComputeUIRect(const Scene& scene, ImVec2 canvasPos, float w, float h,
                       ImVec2& anchorOrigin, ImVec2& rectMin, ImVec2& rectMax) const;

    void RenderThemeStyling(NodeInspectorState& state);

    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;

    void DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) override;
};

