#include "Node.h"
#include "Scene.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

bool Node::IsUINode() const {
    return nodeType == NodeType::UI_Canvas ||
           nodeType == NodeType::UI_Panel ||
           nodeType == NodeType::UI_Text ||
           nodeType == NodeType::UI_Button ||
           nodeType == NodeType::UI_ProgressBar ||
           nodeType == NodeType::UI_Spinner ||
           nodeType == NodeType::UI_TextField ||
           nodeType == NodeType::UI_PasswordField ||
           nodeType == NodeType::UI_Slider ||
           nodeType == NodeType::UI_Checkbox ||
           nodeType == NodeType::UI_RadioButton ||
           nodeType == NodeType::UI_Window ||
           nodeType == NodeType::UI_Image ||
           nodeType == NodeType::UI_Video ||
           nodeType == NodeType::UI_ScrollPane ||
           nodeType == NodeType::UI_Dropdown ||
           nodeType == NodeType::UI_DatePicker ||
           nodeType == NodeType::UI_SelectableList ||
           nodeType == NodeType::UI_ColorPicker ||
           nodeType == NodeType::UI_Table ||
           nodeType == NodeType::UI_Tabs ||
           nodeType == NodeType::UI_MenuBar ||
           nodeType == NodeType::UI_Shape ||
           nodeType == NodeType::UI_BoxContainer ||
           nodeType == NodeType::UI_GridContainer;
}

glm::mat4 Node::GetLocalTransformMatrix() const {
    glm::mat4 mat(1.0f);
    ImGuizmo::RecomposeMatrixFromComponents(
        glm::value_ptr(position),
        glm::value_ptr(rotation),
        glm::value_ptr(scale),
        glm::value_ptr(mat)
    );
    return mat;
}

void Node::SetTransformMatrix(const glm::mat4& matrix) {
    ImGuizmo::DecomposeMatrixToComponents(
        glm::value_ptr(matrix),
        glm::value_ptr(position),
        glm::value_ptr(rotation),
        glm::value_ptr(scale)
    );
}

glm::mat4 Node::GetWorldTransformMatrix(const Scene& scene) const {
    glm::mat4 local = GetLocalTransformMatrix();
    if (parentId == 0) {
        return local;
    }
    auto parent = scene.GetNodeById(parentId);
    if (!parent || parent->id == this->id) {
        return local;
    }
    return parent->GetWorldTransformMatrix(scene) * local;
}

void Node::SetWorldTransformMatrix(const glm::mat4& matrix, const Scene& scene) {
    if (parentId == 0) {
        SetTransformMatrix(matrix);
        return;
    }
    auto parent = scene.GetNodeById(parentId);
    if (!parent || parent->id == this->id) {
        SetTransformMatrix(matrix);
        return;
    }
    glm::mat4 parentWorld = parent->GetWorldTransformMatrix(scene);
    glm::mat4 localMat = glm::inverse(parentWorld) * matrix;
    SetTransformMatrix(localMat);
}

glm::vec3 Node::GetWorldPosition(const Scene& scene) const {
    glm::mat4 world = GetWorldTransformMatrix(scene);
    return glm::vec3(world[3]);
}

glm::mat4 Node::GetCameraViewMatrix(const Scene* scene) const {
    glm::mat4 transform = scene ? GetWorldTransformMatrix(*scene) : GetLocalTransformMatrix();
    glm::vec3 eye = glm::vec3(transform[3]);
    glm::vec3 forward = -glm::normalize(glm::vec3(transform[2]));
    glm::vec3 up = glm::normalize(glm::vec3(transform[1]));
    return glm::lookAt(eye, eye + forward, up);
}

glm::mat4 Node::GetCameraProjectionMatrix(float aspectRatio) const {
    if (aspectRatio <= 0.0f) aspectRatio = 16.0f / 9.0f;
    glm::mat4 proj = glm::perspective(glm::radians(cameraFov), aspectRatio, cameraNear, cameraFar);
    proj[1][1] *= -1.0f; // Vulkan clip space inverted Y
    return proj;
}

std::shared_ptr<Node> Node::Clone() const {
    auto copy = std::make_shared<Node>(*this);
    copy->childrenIds = this->childrenIds;
    return copy;
}

void Node::RenderInspectorProperties(Scene& scene, NodeInspectorState& state) {
    // Base fallback
}

void Node::DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                  bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                  ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked) {
    // Base fallback
}
