#pragma once
#include "UINode.h"

class UIProgressBarNode : public UINode {
public:
    UIProgressBarNode(const std::string& nodeName = "", uint32_t pId = 0);
    std::shared_ptr<Node> Clone() const override { return std::make_shared<UIProgressBarNode>(*this); }
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;
    void DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) override;
};
