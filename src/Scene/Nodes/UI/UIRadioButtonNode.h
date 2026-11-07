#pragma once
#include "UINode.h"

class UIRadioButtonNode : public UINode {
public:
    UIRadioButtonNode(const std::string& label = "Radio Option", const std::string& group = "Group1", bool checked = false, const std::string& nodeName = "", uint32_t pId = 0);
    std::shared_ptr<Node> Clone() const override { return std::make_shared<UIRadioButtonNode>(*this); }
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;
    void DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) override;
};
