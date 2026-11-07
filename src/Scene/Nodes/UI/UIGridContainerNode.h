#pragma once
#include "UINode.h"

class UIGridContainerNode : public UINode {
public:
    UIGridContainerNode(int cols = 4, const std::string& nodeName = "", uint32_t pId = 0);
    std::shared_ptr<Node> Clone() const override { return std::make_shared<UIGridContainerNode>(*this); }

    int columns{4};
    float cellWidth{60.0f};
    float cellHeight{60.0f};
    float horizontalSpacing{8.0f};
    float verticalSpacing{8.0f};
    float padding{8.0f};
    bool autoResize{true};

    void UpdateLayout(Scene& scene);

    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;
    void DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) override;
};

