#pragma once
#include "UINode.h"

class UIBoxContainerNode : public UINode {
public:
    UIBoxContainerNode(BoxOrientation orient = BoxOrientation::Vertical, const std::string& nodeName = "", uint32_t pId = 0);
    std::shared_ptr<Node> Clone() const override { return std::make_shared<UIBoxContainerNode>(*this); }

    BoxOrientation orientation{BoxOrientation::Vertical};
    float spacing{8.0f};
    float padding{8.0f};
    bool autoResize{true};

    void UpdateLayout(Scene& scene);

    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;
    void DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) override;
};
