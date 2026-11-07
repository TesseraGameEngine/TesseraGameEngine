#pragma once

#include <string>
#include <memory>
#include "Scene/Scene.h"
#include <sol/sol.hpp>

// Forward declare lua_State
struct lua_State;
namespace Physics { class PhysicsWorld; }
namespace Animation { class CutscenePlayer; }

namespace Scripting {

struct NodeScriptInstance {
    uint32_t nodeId{0};
    std::string scriptPath{""};
    int envRef{-1}; // LUA_NOREF
    bool hasUpdate{false};
    bool hasInit{false};
    bool hasDestroy{false};
    bool hasError{false};
};

class LuaScriptSystem {
public:
    static LuaScriptSystem& Get();

    LuaScriptSystem();
    ~LuaScriptSystem();

    bool Init();
    void Shutdown();

    // Call lifecycle hooks
    void InitNodeScript(Node& node, Scene& scene);
    void UpdateNodeScript(Node& node, Scene& scene, float dt);
    // Calls the node script's OnCollision(self, otherId), if it has one
    void CollisionNodeScript(Node& node, Scene& scene, uint32_t otherId);
    void DestroyNodeScript(uint32_t nodeId);
    void CallFunction(const std::string& functionName);

    lua_State* GetState() const { return L; }
    sol::state_view GetSolState() { return sol::state_view(L); }
    Scene* GetCurrentScene() const { return m_currentScene; }
    void SetPhysicsWorld(Physics::PhysicsWorld* physics) { m_physicsWorld = physics; }
    Physics::PhysicsWorld* GetPhysicsWorld() const { return m_physicsWorld; }
    void SetCutscenePlayer(Animation::CutscenePlayer* player) { m_cutscenePlayer = player; }
    Animation::CutscenePlayer* GetCutscenePlayer() const { return m_cutscenePlayer; }

    void SetProjectRoot(const std::string& root) { m_projectRoot = root; }
    const std::string& GetProjectRoot() const { return m_projectRoot; }

    const std::map<uint32_t, NodeScriptInstance>& GetInstances() const { return m_instances; }

    void SetInputActive(bool active) { m_inputActive = active; }
    bool IsInputActive() const { return m_inputActive; }

    void SetLocalPeerId(uint32_t id) { m_localPeerId = id; }
    uint32_t GetLocalPeerId() const { return m_localPeerId; }

private:
    void RegisterEngineAPI();

    lua_State* L{nullptr};
    bool m_initialized{false};
    bool m_inputActive{true};
    uint32_t m_localPeerId{1};
    Physics::PhysicsWorld* m_physicsWorld{nullptr};
    Animation::CutscenePlayer* m_cutscenePlayer{nullptr};
    Scene* m_currentScene{nullptr};
    std::string m_projectRoot{""};
    std::map<uint32_t, NodeScriptInstance> m_instances;
};

} // namespace Scripting
