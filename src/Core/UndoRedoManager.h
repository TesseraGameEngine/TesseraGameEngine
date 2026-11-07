#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <glm/glm.hpp>
#include "Utils/SimpleJson.h"

class Scene;

namespace Core {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute(Scene& scene) = 0;
    virtual void Undo(Scene& scene) = 0;
    virtual std::string GetDescription() const = 0;
};

class TransformCommand : public ICommand {
public:
    TransformCommand(uint32_t nodeId,
                     const glm::vec3& oldPos, const glm::vec3& oldRot, const glm::vec3& oldScale,
                     const glm::vec3& newPos, const glm::vec3& newRot, const glm::vec3& newScale,
                     const std::string& desc = "Transform Node");

    void Execute(Scene& scene) override;
    void Undo(Scene& scene) override;
    std::string GetDescription() const override { return m_description; }

private:
    uint32_t m_nodeId{0};
    glm::vec3 m_oldPos{0.0f};
    glm::vec3 m_oldRot{0.0f};
    glm::vec3 m_oldScale{1.0f};
    glm::vec3 m_newPos{0.0f};
    glm::vec3 m_newRot{0.0f};
    glm::vec3 m_newScale{1.0f};
    std::string m_description;
};

class SceneSnapshotCommand : public ICommand {
public:
    SceneSnapshotCommand(const JsonValue& beforeState,
                         const JsonValue& afterState,
                         int32_t beforeSelectedId = -1,
                         int32_t afterSelectedId = -1,
                         const std::string& desc = "Scene Edit");

    void Execute(Scene& scene) override;
    void Undo(Scene& scene) override;
    std::string GetDescription() const override { return m_description; }

private:
    JsonValue m_beforeState;
    JsonValue m_afterState;
    int32_t m_beforeSelectedId{-1};
    int32_t m_afterSelectedId{-1};
    std::string m_description;
};

class LambdaCommand : public ICommand {
public:
    LambdaCommand(std::function<void(Scene&)> executeFn,
                  std::function<void(Scene&)> undoFn,
                  const std::string& desc = "Action");

    void Execute(Scene& scene) override;
    void Undo(Scene& scene) override;
    std::string GetDescription() const override { return m_description; }

private:
    std::function<void(Scene&)> m_executeFn;
    std::function<void(Scene&)> m_undoFn;
    std::string m_description;
};

class UndoRedoManager {
public:
    static UndoRedoManager& Get();

    void ExecuteCommand(std::shared_ptr<ICommand> cmd, Scene& scene);
    void PushCommand(std::shared_ptr<ICommand> cmd);
    void RecordSnapshot(const JsonValue& beforeState,
                        const JsonValue& afterState,
                        int32_t beforeSelectedId = -1,
                        int32_t afterSelectedId = -1,
                        const std::string& description = "Scene Edit");

    bool Undo(Scene& scene);
    bool Redo(Scene& scene);

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    size_t GetUndoCount() const { return m_undoStack.size(); }
    size_t GetRedoCount() const { return m_redoStack.size(); }

    std::string GetUndoDescription() const;
    std::string GetRedoDescription() const;

    const std::vector<std::shared_ptr<ICommand>>& GetUndoStack() const { return m_undoStack; }
    const std::vector<std::shared_ptr<ICommand>>& GetRedoStack() const { return m_redoStack; }

    void Clear();
    void SetMaxHistorySize(size_t maxSize) { m_maxHistorySize = maxSize; }
    size_t GetMaxHistorySize() const { return m_maxHistorySize; }
    uint64_t GetVersion() const { return m_version; }

private:
    std::vector<std::shared_ptr<ICommand>> m_undoStack;
    std::vector<std::shared_ptr<ICommand>> m_redoStack;
    size_t m_maxHistorySize{100};
    uint64_t m_version{0};
};

} // namespace Core

