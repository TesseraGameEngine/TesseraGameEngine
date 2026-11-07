#include "UndoRedoManager.h"
#include "Scene/Scene.h"
#include "Scene/Node.h"

namespace Core {

TransformCommand::TransformCommand(uint32_t nodeId,
                                   const glm::vec3& oldPos, const glm::vec3& oldRot, const glm::vec3& oldScale,
                                   const glm::vec3& newPos, const glm::vec3& newRot, const glm::vec3& newScale,
                                   const std::string& desc)
    : m_nodeId(nodeId)
    , m_oldPos(oldPos), m_oldRot(oldRot), m_oldScale(oldScale)
    , m_newPos(newPos), m_newRot(newRot), m_newScale(newScale)
    , m_description(desc)
{}

void TransformCommand::Execute(Scene& scene) {
    auto node = scene.GetNodeById(m_nodeId);
    if (node) {
        node->position = m_newPos;
        node->rotation = m_newRot;
        node->scale = m_newScale;
    }
}

void TransformCommand::Undo(Scene& scene) {
    auto node = scene.GetNodeById(m_nodeId);
    if (node) {
        node->position = m_oldPos;
        node->rotation = m_oldRot;
        node->scale = m_oldScale;
    }
}

SceneSnapshotCommand::SceneSnapshotCommand(const JsonValue& beforeState,
                                           const JsonValue& afterState,
                                           int32_t beforeSelectedId,
                                           int32_t afterSelectedId,
                                           const std::string& desc)
    : m_beforeState(beforeState)
    , m_afterState(afterState)
    , m_beforeSelectedId(beforeSelectedId)
    , m_afterSelectedId(afterSelectedId)
    , m_description(desc)
{}

void SceneSnapshotCommand::Execute(Scene& scene) {
    scene.Deserialize(m_afterState);
    if (m_afterSelectedId >= 0) {
        scene.SelectNode(m_afterSelectedId);
    } else {
        scene.ClearSelection();
    }
}

void SceneSnapshotCommand::Undo(Scene& scene) {
    scene.Deserialize(m_beforeState);
    if (m_beforeSelectedId >= 0) {
        scene.SelectNode(m_beforeSelectedId);
    } else {
        scene.ClearSelection();
    }
}

LambdaCommand::LambdaCommand(std::function<void(Scene&)> executeFn,
                             std::function<void(Scene&)> undoFn,
                             const std::string& desc)
    : m_executeFn(std::move(executeFn))
    , m_undoFn(std::move(undoFn))
    , m_description(desc)
{}

void LambdaCommand::Execute(Scene& scene) {
    if (m_executeFn) m_executeFn(scene);
}

void LambdaCommand::Undo(Scene& scene) {
    if (m_undoFn) m_undoFn(scene);
}

UndoRedoManager& UndoRedoManager::Get() {
    static UndoRedoManager instance;
    return instance;
}

void UndoRedoManager::ExecuteCommand(std::shared_ptr<ICommand> cmd, Scene& scene) {
    if (!cmd) return;
    cmd->Execute(scene);
    PushCommand(cmd);
}

void UndoRedoManager::PushCommand(std::shared_ptr<ICommand> cmd) {
    if (!cmd) return;
    m_undoStack.push_back(cmd);
    if (m_undoStack.size() > m_maxHistorySize) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
    m_version++;
}

void UndoRedoManager::RecordSnapshot(const JsonValue& beforeState,
                                     const JsonValue& afterState,
                                     int32_t beforeSelectedId,
                                     int32_t afterSelectedId,
                                     const std::string& description) {
    auto cmd = std::make_shared<SceneSnapshotCommand>(
        beforeState, afterState, beforeSelectedId, afterSelectedId, description);
    PushCommand(cmd);
}

bool UndoRedoManager::Undo(Scene& scene) {
    if (m_undoStack.empty()) return false;
    auto cmd = m_undoStack.back();
    m_undoStack.pop_back();
    cmd->Undo(scene);
    m_redoStack.push_back(cmd);
    m_version++;
    return true;
}

bool UndoRedoManager::Redo(Scene& scene) {
    if (m_redoStack.empty()) return false;
    auto cmd = m_redoStack.back();
    m_redoStack.pop_back();
    cmd->Execute(scene);
    m_undoStack.push_back(cmd);
    m_version++;
    return true;
}

std::string UndoRedoManager::GetUndoDescription() const {
    if (m_undoStack.empty()) return "";
    return m_undoStack.back()->GetDescription();
}

std::string UndoRedoManager::GetRedoDescription() const {
    if (m_redoStack.empty()) return "";
    return m_redoStack.back()->GetDescription();
}

void UndoRedoManager::Clear() {
    m_undoStack.clear();
    m_redoStack.clear();
    m_version++;
}

} // namespace Core

