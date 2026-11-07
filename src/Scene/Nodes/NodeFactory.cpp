#include "NodeFactory.h"
#include "AllNodes.h"

std::shared_ptr<Node> NodeFactory::CreateNode(NodeType type, const std::string& name, uint32_t parentId) {
    switch (type) {
        case NodeType::Mesh: return std::make_shared<MeshNode>(ShapeType::Cube, name, parentId);
        case NodeType::Character: return std::make_shared<CharacterNode>(name, parentId);
        case NodeType::Camera: return std::make_shared<CameraNode>(name, parentId);
        case NodeType::Light: return std::make_shared<LightNode>(LightType::Point, name, parentId);
        case NodeType::Collider: return std::make_shared<ColliderNode>(ColliderType::Box, name, parentId);
        case NodeType::Empty: return std::make_shared<EmptyNode>(name, parentId);
        case NodeType::Spline: return std::make_shared<SplineNode>(name, parentId);
        case NodeType::Decal: return std::make_shared<DecalNode>(name, parentId);
        case NodeType::ParticleEmitter: return std::make_shared<ParticleNode>(name, parentId);
        case NodeType::AudioSource: return std::make_shared<AudioNode>(name, parentId);
        case NodeType::Terrain: return std::make_shared<TerrainNode>(name, parentId);

        case NodeType::UI_Canvas: return std::make_shared<UICanvasNode>(name, parentId);
        case NodeType::UI_Panel: return std::make_shared<UIPanelNode>(name, parentId);
        case NodeType::UI_Text: return std::make_shared<UITextNode>("Text Label", name, parentId);
        case NodeType::UI_Button: return std::make_shared<UIButtonNode>("Button", name, parentId);
        case NodeType::UI_ProgressBar: return std::make_shared<UIProgressBarNode>(name, parentId);
        case NodeType::UI_Spinner: return std::make_shared<UISpinnerNode>(name, parentId);
        case NodeType::UI_TextField: return std::make_shared<UITextFieldNode>("Enter text...", name, parentId);
        case NodeType::UI_PasswordField: return std::make_shared<UIPasswordFieldNode>(name, parentId);
        case NodeType::UI_Slider: return std::make_shared<UISliderNode>(0.0f, 100.0f, 50.0f, name, parentId);
        case NodeType::UI_Checkbox: return std::make_shared<UICheckboxNode>("Checkbox", false, name, parentId);
        case NodeType::UI_RadioButton: return std::make_shared<UIRadioButtonNode>("Radio Option", "Group1", false, name, parentId);
        case NodeType::UI_Window: return std::make_shared<UIWindowNode>("Floating Window", name, parentId);
        case NodeType::UI_Image: return std::make_shared<UIImageNode>("", name, parentId);
        case NodeType::UI_Video: return std::make_shared<UIVideoNode>(name, parentId);
        case NodeType::UI_ScrollPane: return std::make_shared<UIScrollPaneNode>(name, parentId);
        case NodeType::UI_Dropdown: return std::make_shared<UIDropdownNode>(std::vector<std::string>{"Option 1", "Option 2", "Option 3"}, name, parentId);
        case NodeType::UI_DatePicker: return std::make_shared<UIDatePickerNode>(name, parentId);
        case NodeType::UI_SelectableList: return std::make_shared<UISelectableListNode>(std::vector<std::string>{"Item 1", "Item 2", "Item 3", "Item 4", "Item 5"}, name, parentId);
        case NodeType::UI_ColorPicker: return std::make_shared<UIColorPickerNode>(name, parentId);
        case NodeType::UI_Table: return std::make_shared<UITableNode>(name, parentId);
        case NodeType::UI_Tabs: return std::make_shared<UITabsNode>(std::vector<std::string>{"Tab 1", "Tab 2", "Tab 3"}, name, parentId);
        case NodeType::UI_MenuBar: return std::make_shared<UIMenuBarNode>(name, parentId);
        case NodeType::UI_Shape: return std::make_shared<UIShapeNode>(UIShapeType::Rectangle, name, parentId);
        case NodeType::UI_BoxContainer: return std::make_shared<UIBoxContainerNode>(BoxOrientation::Vertical, name, parentId);
        case NodeType::UI_GridContainer: return std::make_shared<UIGridContainerNode>(4, name, parentId);
        default: return std::make_shared<MeshNode>(ShapeType::Cube, name, parentId);
    }
}
