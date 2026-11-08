#include "Resource/ResourceManager.h"
#include "LuaScriptSystem.h"
#include "Localization/GameLocalization.h"
#include "Core/InputManager.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Nodes/3D/ParticleNode.h"
#include "Scene/Weather.h"
#include "Scene/Nodes/3D/TerrainNode.h"
#include "Video/VideoPlayer.h"
#include "Renderer/RayTracing/RayTracingManager.h"
#include <lua.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace Scripting {

static LuaScriptSystem* s_instance = nullptr;

LuaScriptSystem& LuaScriptSystem::Get() {
    static LuaScriptSystem instance;
    return instance;
}

LuaScriptSystem::LuaScriptSystem() {
    s_instance = this;
}

LuaScriptSystem::~LuaScriptSystem() {
    Shutdown();
    if (s_instance == this) s_instance = nullptr;
}

// Lua C Functions
static int Lua_Print(lua_State* L) {
    int n = lua_gettop(L);
    std::string out = "[Lua] ";
    for (int i = 1; i <= n; i++) {
        if (i > 1) out += "\t";
        if (lua_isstring(L, i)) {
            out += lua_tostring(L, i);
        } else if (lua_isboolean(L, i)) {
            out += lua_toboolean(L, i) ? "true" : "false";
        } else if (lua_isnumber(L, i)) {
            out += std::to_string(lua_tonumber(L, i));
        } else if (lua_isnil(L, i)) {
            out += "nil";
        } else {
            out += lua_typename(L, lua_type(L, i));
        }
    }
    std::cout << out << std::endl;
    return 0;
}

// Helper to parse key name to ImGuiKey
static bool CheckImGuiKey(const char* keyName, int checkType /* 0: Down, 1: Pressed, 2: Released */) {
    if (!keyName || keyName[0] == '\0') return false;
    std::string s = keyName;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);

    ImGuiKey key = ImGuiKey_None;
    if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'Z') {
        key = static_cast<ImGuiKey>(ImGuiKey_A + (s[0] - 'A'));
    } else if (s.size() == 1 && s[0] >= '0' && s[0] <= '9') {
        key = static_cast<ImGuiKey>(ImGuiKey_0 + (s[0] - '0'));
    } else if (s == "SPACE" || s == "KEY_SPACE") {
        key = ImGuiKey_Space;
    } else if (s == "LEFTSHIFT" || s == "SHIFT" || s == "KEY_LEFT_SHIFT" || s == "LSHIFT") {
        key = ImGuiKey_LeftShift;
    } else if (s == "RIGHTSHIFT" || s == "KEY_RIGHT_SHIFT" || s == "RSHIFT") {
        key = ImGuiKey_RightShift;
    } else if (s == "LEFTCTRL" || s == "CTRL" || s == "KEY_LEFT_CONTROL" || s == "LCTRL") {
        key = ImGuiKey_LeftCtrl;
    } else if (s == "RIGHTCTRL" || s == "KEY_RIGHT_CONTROL" || s == "RCTRL") {
        key = ImGuiKey_RightCtrl;
    } else if (s == "LEFTALT" || s == "ALT" || s == "LALT") {
        key = ImGuiKey_LeftAlt;
    } else if (s == "RIGHTALT" || s == "RALT") {
        key = ImGuiKey_RightAlt;
    } else if (s == "ENTER" || s == "RETURN") {
        key = ImGuiKey_Enter;
    } else if (s == "ESCAPE" || s == "ESC") {
        key = ImGuiKey_Escape;
    } else if (s == "TAB") {
        key = ImGuiKey_Tab;
    } else if (s == "BACKSPACE") {
        key = ImGuiKey_Backspace;
    } else if (s == "UP" || s == "ARROWUP") {
        key = ImGuiKey_UpArrow;
    } else if (s == "DOWN" || s == "ARROWDOWN") {
        key = ImGuiKey_DownArrow;
    } else if (s == "LEFT" || s == "ARROWLEFT") {
        key = ImGuiKey_LeftArrow;
    } else if (s == "RIGHT" || s == "ARROWRIGHT") {
        key = ImGuiKey_RightArrow;
    } else if (s.size() >= 2 && s.size() <= 3 && s[0] == 'F' && std::isdigit(static_cast<unsigned char>(s[1]))) {
        const int n = std::atoi(s.c_str() + 1);
        if (n >= 1 && n <= 12) key = static_cast<ImGuiKey>(ImGuiKey_F1 + (n - 1));
    }

    if (key == ImGuiKey_None) return false;
    if (checkType == 0) return ImGui::IsKeyDown(key);
    if (checkType == 1) return ImGui::IsKeyPressed(key, false);
    if (checkType == 2) return ImGui::IsKeyReleased(key);
    return false;
}

// Input bindings
static int Lua_IsKeyDown(lua_State* L) {
    if (!LuaScriptSystem::Get().IsInputActive()) {
        lua_pushboolean(L, false);
        return 1;
    }
    const char* keyName = luaL_checkstring(L, 1);
    bool down = Core::InputManager::Get().IsActionDown(keyName);
    if (!down) {
        down = CheckImGuiKey(keyName, 0);
    }
    lua_pushboolean(L, down);
    return 1;
}

static int Lua_IsKeyPressed(lua_State* L) {
    if (!LuaScriptSystem::Get().IsInputActive()) {
        lua_pushboolean(L, false);
        return 1;
    }
    const char* keyName = luaL_checkstring(L, 1);
    bool pressed = Core::InputManager::Get().IsActionPressed(keyName);
    if (!pressed) {
        pressed = CheckImGuiKey(keyName, 1);
    }
    lua_pushboolean(L, pressed);
    return 1;
}

static int Lua_IsKeyReleased(lua_State* L) {
    if (!LuaScriptSystem::Get().IsInputActive()) {
        lua_pushboolean(L, false);
        return 1;
    }
    const char* keyName = luaL_checkstring(L, 1);
    bool released = Core::InputManager::Get().IsActionReleased(keyName);
    if (!released) {
        released = CheckImGuiKey(keyName, 2);
    }
    lua_pushboolean(L, released);
    return 1;
}

static int Lua_GetMouseDelta(lua_State* L) {
    ImGuiIO& io = ImGui::GetIO();
    lua_pushnumber(L, io.MouseDelta.x);
    lua_pushnumber(L, io.MouseDelta.y);
    return 2;
}

static int Lua_IsMouseDown(lua_State* L) {
    if (!LuaScriptSystem::Get().IsInputActive()) {
        lua_pushboolean(L, false);
        return 1;
    }
    int btn = 0;
    if (lua_isinteger(L, 1)) btn = static_cast<int>(lua_tointeger(L, 1));
    bool down = ImGui::IsMouseDown(static_cast<ImGuiMouseButton>(btn));
    lua_pushboolean(L, down);
    return 1;
}

// Scene & Node manipulation
static int Lua_GetNodePosition(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        lua_pushnumber(L, node->position.x);
        lua_pushnumber(L, node->position.y);
        lua_pushnumber(L, node->position.z);
        return 3;
    }
    return 0;
}

static int Lua_GetNodeRotation(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        lua_pushnumber(L, node->rotation.x);
        lua_pushnumber(L, node->rotation.y);
        lua_pushnumber(L, node->rotation.z);
        return 3;
    }
    return 0;
}

static int Lua_MoveRelativeNode(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float right = static_cast<float>(luaL_checknumber(L, 2));
    float up = static_cast<float>(luaL_checknumber(L, 3));
    float fwd = static_cast<float>(luaL_checknumber(L, 4));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        glm::mat4 rot = glm::mat4(1.0f);
        rot = glm::rotate(rot, glm::radians(node->rotation.y), glm::vec3(0, 1, 0));
        rot = glm::rotate(rot, glm::radians(node->rotation.x), glm::vec3(1, 0, 0));
        rot = glm::rotate(rot, glm::radians(node->rotation.z), glm::vec3(0, 0, 1));

        glm::vec3 forwardVec = -glm::vec3(rot[2]);
        glm::vec3 rightVec = glm::vec3(rot[0]);
        glm::vec3 upVec = glm::vec3(rot[1]);

        node->position += rightVec * right + upVec * up + forwardVec * fwd;
    }
    return 0;
}

// Helper to get self ID
static uint32_t GetSelfId(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    lua_getfield(L, 1, "id");
    uint32_t id = 0;
    if (lua_isinteger(L, -1)) {
        id = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return id;
}

// Direct methods on self for physics & transforms
static int Self_AddImpulse(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!id || !s_instance) return 0;
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    if (s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->AddImpulse(id, glm::vec3(x, y, z));
    }
    if (s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->MarkLocallyActive();
    }
    return 0;
}

static int Self_AddForce(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!id || !s_instance) return 0;
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    if (s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->AddForce(id, glm::vec3(x, y, z));
    }
    if (s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->MarkLocallyActive();
    }
    return 0;
}

static int Self_SetLinearVelocity(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!id || !s_instance) return 0;
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    if (s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->SetLinearVelocity(id, glm::vec3(x, y, z));
    }
    if (s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->MarkLocallyActive();
    }
    return 0;
}

static int Self_GetLinearVelocity(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!id || !s_instance || !s_instance->GetPhysicsWorld()) return 0;
    glm::vec3 vel = s_instance->GetPhysicsWorld()->GetLinearVelocity(id);
    lua_pushnumber(L, vel.x);
    lua_pushnumber(L, vel.y);
    lua_pushnumber(L, vel.z);
    return 3;
}

static int Self_SetPosition(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    glm::vec3 pos(x, y, z);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            node->position = pos;
            node->MarkLocallyActive();
        }
    }
    if (s_instance && s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id)) {
        s_instance->GetPhysicsWorld()->SetPosition(id, pos);
    }
    return 0;
}

static int Self_GetPosition(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushnumber(L, node->position.x);
            lua_pushnumber(L, node->position.y);
            lua_pushnumber(L, node->position.z);
            return 3;
        }
    }
    return 0;
}

static int Self_SetRotation(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    glm::vec3 rot(x, y, z);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->rotation = rot;
    }
    if (s_instance && s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id)) {
        s_instance->GetPhysicsWorld()->SetRotation(id, rot);
    }
    return 0;
}

static int Self_GetRotation(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushnumber(L, node->rotation.x);
            lua_pushnumber(L, node->rotation.y);
            lua_pushnumber(L, node->rotation.z);
            return 3;
        }
    }
    return 0;
}

static int Self_Translate(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            node->position += glm::vec3(x, y, z);
            node->MarkLocallyActive();
            if (s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id)) {
                s_instance->GetPhysicsWorld()->SetPosition(id, node->position);
            }
        }
    }
    return 0;
}

//  Multiplayer Authority bindings on self
static int Self_SetMultiplayerAuthority(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!id || !s_instance || !s_instance->GetCurrentScene()) return 0;
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        int peerId = static_cast<int>(luaL_checkinteger(L, 2));
        node->SetMultiplayerAuthority(static_cast<uint32_t>(peerId));
    }
    return 0;
}

static int Self_GetMultiplayerAuthority(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!id || !s_instance || !s_instance->GetCurrentScene()) {
        lua_pushinteger(L, 1);
        return 1;
    }
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    lua_pushinteger(L, node ? node->GetMultiplayerAuthority() : 1);
    return 1;
}

static int Self_IsMultiplayerAuthority(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!id || !s_instance || !s_instance->GetCurrentScene()) {
        lua_pushboolean(L, true);
        return 1;
    }
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    uint32_t localPeerId = s_instance->GetLocalPeerId();
    lua_pushboolean(L, node ? node->HasMultiplayerAuthority(localPeerId) : true);
    return 1;
}

static int Lua_Network_GetUniqueId(lua_State* L) {
    lua_pushinteger(L, s_instance ? s_instance->GetLocalPeerId() : 1);
    return 1;
}

static int Lua_Network_IsServer(lua_State* L) {
    lua_pushboolean(L, s_instance ? (s_instance->GetLocalPeerId() == 1) : true);
    return 1;
}

static int Lua_Network_SetAuthority(lua_State* L) {
    uint32_t nodeId = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t peerId = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (s_instance && s_instance->GetCurrentScene()) {
        if (auto n = s_instance->GetCurrentScene()->GetNodeById(nodeId)) {
            n->SetMultiplayerAuthority(peerId);
        }
    }
    return 0;
}

static int Lua_Network_GetAuthority(lua_State* L) {
    uint32_t nodeId = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (s_instance && s_instance->GetCurrentScene()) {
        if (auto n = s_instance->GetCurrentScene()->GetNodeById(nodeId)) {
            lua_pushinteger(L, n->GetMultiplayerAuthority());
            return 1;
        }
    }
    lua_pushinteger(L, 1);
    return 1;
}

static int Lua_Network_HasAuthority(lua_State* L) {
    uint32_t nodeId = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t localPeer = s_instance ? s_instance->GetLocalPeerId() : 1;
    if (s_instance && s_instance->GetCurrentScene()) {
        if (auto n = s_instance->GetCurrentScene()->GetNodeById(nodeId)) {
            lua_pushboolean(L, n->HasMultiplayerAuthority(localPeer));
            return 1;
        }
    }
    lua_pushboolean(L, true);
    return 1;
}
static int Lua_Physics_AddImpulse(lua_State* L) {
    if (!s_instance) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    if (s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->AddImpulse(id, glm::vec3(x, y, z));
    }
    if (s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->MarkLocallyActive();
    }
    return 0;
}

static int Lua_Physics_AddForce(lua_State* L) {
    if (!s_instance || !s_instance->GetPhysicsWorld()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    s_instance->GetPhysicsWorld()->AddForce(id, glm::vec3(x, y, z));
    return 0;
}

static int Lua_Physics_SetLinearVelocity(lua_State* L) {
    if (!s_instance || !s_instance->GetPhysicsWorld()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    s_instance->GetPhysicsWorld()->SetLinearVelocity(id, glm::vec3(x, y, z));
    return 0;
}

static int Lua_Physics_GetLinearVelocity(lua_State* L) {
    if (!s_instance || !s_instance->GetPhysicsWorld()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    glm::vec3 vel = s_instance->GetPhysicsWorld()->GetLinearVelocity(id);
    lua_pushnumber(L, vel.x);
    lua_pushnumber(L, vel.y);
    lua_pushnumber(L, vel.z);
    return 3;
}

static int Lua_Physics_SetAngularVelocity(lua_State* L) {
    if (!s_instance || !s_instance->GetPhysicsWorld()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    s_instance->GetPhysicsWorld()->SetAngularVelocity(id, glm::vec3(x, y, z));
    return 0;
}

static int Lua_Physics_GetAngularVelocity(lua_State* L) {
    if (!s_instance || !s_instance->GetPhysicsWorld()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    glm::vec3 vel = s_instance->GetPhysicsWorld()->GetAngularVelocity(id);
    lua_pushnumber(L, vel.x);
    lua_pushnumber(L, vel.y);
    lua_pushnumber(L, vel.z);
    return 3;
}

static int Lua_Physics_SetPosition(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    glm::vec3 pos(x, y, z);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->position = pos;
    }
    if (s_instance && s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id)) {
        s_instance->GetPhysicsWorld()->SetPosition(id, pos);
    }
    return 0;
}

static int Lua_Physics_GetPosition(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (s_instance && s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id)) {
        glm::vec3 pos = s_instance->GetPhysicsWorld()->GetPosition(id);
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        return 3;
    }
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushnumber(L, node->position.x);
            lua_pushnumber(L, node->position.y);
            lua_pushnumber(L, node->position.z);
            return 3;
        }
    }
    return 0;
}

static int Lua_Physics_HasBody(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool has = (s_instance && s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id));
    lua_pushboolean(L, has);
    return 1;
}

// 2D UI Methods bound directly to self
static int Self_SetText(lua_State* L) {
    uint32_t id = GetSelfId(L);
    const char* text = luaL_checkstring(L, 2);
    lua_pushstring(L, text);
    lua_setfield(L, 1, "uiText");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiText = text;
    }
    return 0;
}

static int Self_SetLocKey(lua_State* L) {
    uint32_t id = GetSelfId(L);
    const char* key = luaL_checkstring(L, 2);
    lua_pushstring(L, key);
    lua_setfield(L, 1, "uiLocKey");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiLocKey = key;
    }
    return 0;
}

static int Self_GetLocKey(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushstring(L, node->uiLocKey.c_str());
            return 1;
        }
    }
    lua_getfield(L, 1, "uiLocKey");
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        lua_pushstring(L, "");
    }
    return 1;
}

static int Self_GetText(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushstring(L, node->uiText.c_str());
            return 1;
        }
    }
    lua_getfield(L, 1, "uiText");
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        lua_pushstring(L, "");
    }
    return 1;
}

static int Self_SetVisible(lua_State* L) {
    uint32_t id = GetSelfId(L);
    bool vis = lua_toboolean(L, 2);
    lua_pushboolean(L, vis);
    lua_setfield(L, 1, "uiVisible");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiVisible = vis;
    }
    return 0;
}

static int Self_GetVisible(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushboolean(L, node->uiVisible);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiVisible");
    bool vis = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, vis);
    return 1;
}

static int Self_SetProgress(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float val = static_cast<float>(luaL_checknumber(L, 2));
    val = std::clamp(val, 0.0f, 1.0f);
    lua_pushnumber(L, val);
    lua_setfield(L, 1, "uiProgress");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiProgress = val;
    }
    return 0;
}

static int Self_GetProgress(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushnumber(L, node->uiProgress);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiProgress");
    float p = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_pushnumber(L, p);
    return 1;
}

static int Self_SetValue(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float val = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushnumber(L, val);
    lua_setfield(L, 1, "uiValue");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiValue = val;
    }
    return 0;
}

static int Self_GetValue(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushnumber(L, node->uiValue);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiValue");
    float v = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_pushnumber(L, v);
    return 1;
}

static int Self_SetChecked(lua_State* L) {
    uint32_t id = GetSelfId(L);
    bool chk = lua_toboolean(L, 2);
    lua_pushboolean(L, chk);
    lua_setfield(L, 1, "uiChecked");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiChecked = chk;
    }
    return 0;
}

static int Self_GetChecked(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushboolean(L, node->uiChecked);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiChecked");
    bool c = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, c);
    return 1;
}

static int Self_SetSelectedIndex(lua_State* L) {
    uint32_t id = GetSelfId(L);
    int idx = static_cast<int>(luaL_checkinteger(L, 2));
    lua_pushinteger(L, idx);
    lua_setfield(L, 1, "uiSelectedIndex");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiSelectedIndex = idx;
    }
    return 0;
}

static int Self_GetSelectedIndex(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushinteger(L, node->uiSelectedIndex);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiSelectedIndex");
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_pushinteger(L, idx);
    return 1;
}

static int Self_SetColor(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float r = static_cast<float>(luaL_checknumber(L, 2));
    float g = static_cast<float>(luaL_checknumber(L, 3));
    float b = static_cast<float>(luaL_checknumber(L, 4));
    float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiColor = glm::vec4(r, g, b, a);
    }
    return 0;
}

static int Self_GetColor(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushnumber(L, node->uiColor.r);
            lua_pushnumber(L, node->uiColor.g);
            lua_pushnumber(L, node->uiColor.b);
            lua_pushnumber(L, node->uiColor.a);
            return 4;
        }
    }
    lua_pushnumber(L, 1.0); lua_pushnumber(L, 1.0); lua_pushnumber(L, 1.0); lua_pushnumber(L, 1.0);
    return 4;
}

static int Self_SetTextColor(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float r = static_cast<float>(luaL_checknumber(L, 2));
    float g = static_cast<float>(luaL_checknumber(L, 3));
    float b = static_cast<float>(luaL_checknumber(L, 4));
    float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiTextColor = glm::vec4(r, g, b, a);
    }
    return 0;
}

static int Self_GetTextColor(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushnumber(L, node->uiTextColor.r);
            lua_pushnumber(L, node->uiTextColor.g);
            lua_pushnumber(L, node->uiTextColor.b);
            lua_pushnumber(L, node->uiTextColor.a);
            return 4;
        }
    }
    lua_pushnumber(L, 1.0); lua_pushnumber(L, 1.0); lua_pushnumber(L, 1.0); lua_pushnumber(L, 1.0);
    return 4;
}

static int Self_SetWindowOpen(lua_State* L) {
    uint32_t id = GetSelfId(L);
    bool open = lua_toboolean(L, 2);
    lua_pushboolean(L, open);
    lua_setfield(L, 1, "uiWindowIsOpen");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiWindowIsOpen = open;
    }
    return 0;
}

static int Self_GetWindowOpen(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushboolean(L, node->uiWindowIsOpen);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiWindowIsOpen");
    bool o = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, o);
    return 1;
}

static int Self_SetWindowMovable(lua_State* L) {
    uint32_t id = GetSelfId(L);
    bool movable = lua_toboolean(L, 2);
    lua_pushboolean(L, movable);
    lua_setfield(L, 1, "uiWindowMovable");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiWindowMovable = movable;
    }
    return 0;
}

static int Self_GetWindowMovable(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushboolean(L, node->uiWindowMovable);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiWindowMovable");
    bool m = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, m);
    return 1;
}

static int Self_SetWindowClosable(lua_State* L) {
    uint32_t id = GetSelfId(L);
    bool closable = lua_toboolean(L, 2);
    lua_pushboolean(L, closable);
    lua_setfield(L, 1, "uiWindowClosable");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiWindowClosable = closable;
    }
    return 0;
}

static int Self_GetWindowClosable(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushboolean(L, node->uiWindowClosable);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiWindowClosable");
    bool c = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, c);
    return 1;
}

static int Self_SetEnabled(lua_State* L) {
    uint32_t id = GetSelfId(L);
    bool enabled = lua_toboolean(L, 2);
    lua_pushboolean(L, enabled);
    lua_setfield(L, 1, "uiEnabled");
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->uiEnabled = enabled;
    }
    return 0;
}

static int Self_GetEnabled(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushboolean(L, node->uiEnabled);
            return 1;
        }
    }
    lua_getfield(L, 1, "uiEnabled");
    bool e = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, e);
    return 1;
}

// Helper to push a Node table

// =========================================================================
//  Feature Bindings: Signals, Bitmasks, Instancing, Replication
// =========================================================================

// Signals
static int Self_Connect(lua_State* L) {
    uint32_t id = GetSelfId(L);
    const char* sigName = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    lua_pushvalue(L, 3);
    int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            uint64_t connId = node->ConnectSignal(sigName, [L, funcRef](const std::vector<std::string>& args) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
                if (lua_isfunction(L, -1)) {
                    for (const auto& a : args) {
                        lua_pushstring(L, a.c_str());
                    }
                    if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
                        const char* err = lua_tostring(L, -1);
                        std::cerr << "[Lua Signal Callback Error]: " << (err ? err : "unknown") << std::endl;
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 1);
                }
            });
            lua_pushinteger(L, static_cast<lua_Integer>(connId));
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

static int Self_Emit(lua_State* L) {
    uint32_t id = GetSelfId(L);
    const char* sigName = luaL_checkstring(L, 2);
    int top = lua_gettop(L);
    std::vector<std::string> args;
    for (int i = 3; i <= top; ++i) {
        if (lua_isstring(L, i) || lua_isnumber(L, i)) {
            args.push_back(lua_tostring(L, i));
        } else if (lua_isboolean(L, i)) {
            args.push_back(lua_toboolean(L, i) ? "true" : "false");
        }
    }
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            node->EmitSignal(sigName, args);
        }
    }
    return 0;
}

static int Self_Disconnect(lua_State* L) {
    uint32_t id = GetSelfId(L);
    const char* sigName = luaL_checkstring(L, 2);
    uint64_t connId = static_cast<uint64_t>(luaL_checkinteger(L, 3));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            bool ok = node->DisconnectSignal(sigName, connId);
            lua_pushboolean(L, ok);
            return 1;
        }
    }
    lua_pushboolean(L, false);
    return 1;
}

// Global Signal dispatch
static int Lua_Signal_Emit(lua_State* L) {
    const char* sigName = luaL_checkstring(L, 1);
    int top = lua_gettop(L);
    std::vector<std::string> args;
    for (int i = 2; i <= top; ++i) {
        if (lua_isstring(L, i) || lua_isnumber(L, i)) {
            args.push_back(lua_tostring(L, i));
        }
    }
    if (s_instance && s_instance->GetCurrentScene()) {
        for (const auto& node : s_instance->GetCurrentScene()->GetNodes()) {
            if (node && node->HasSignal(sigName)) {
                node->EmitSignal(sigName, args);
            }
        }
    }
    return 0;
}

// Collision Layer & Mask
static int Self_SetCollisionLayer(lua_State* L) {
    uint32_t id = GetSelfId(L);
    uint32_t layer = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->collisionLayer = layer;
    }
    if (s_instance && s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->SetCollisionLayer(id, layer);
    }
    return 0;
}

static int Self_SetLiquidFill(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float fill = static_cast<float>(luaL_checknumber(L, 2));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->liquidFill = std::clamp(fill, 0.0f, 1.0f);
    }
    return 0;
}

static int Self_GetLiquidFill(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float fill = 0.0f;
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) fill = node->liquidFill;
    }
    lua_pushnumber(L, fill);
    return 1;
}

// Particle emitters: the script's own node (self) as a ParticleNode, or nullptr
static ParticleNode* SelfParticles(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (!s_instance || !s_instance->GetCurrentScene()) return nullptr;
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    return node ? dynamic_cast<ParticleNode*>(node.get()) : nullptr;
}

// self:EmitParticles(n): a burst of n particles now
static int Self_EmitParticles(lua_State* L) {
    const int count = static_cast<int>(luaL_checkinteger(L, 2));
    if (ParticleNode* emitter = SelfParticles(L)) {
        if (count > 0) emitter->EmitBurst(static_cast<uint32_t>(count), *s_instance->GetCurrentScene());
    }
    return 0;
}

// self:SetEmitting(on): starts or stops the steady flow (living particles carry on)
static int Self_SetEmitting(lua_State* L) {
    const bool on = lua_toboolean(L, 2) != 0;
    if (ParticleNode* emitter = SelfParticles(L)) emitter->isEmitting = on;
    return 0;
}

static int Self_GetParticleCount(lua_State* L) {
    ParticleNode* emitter = SelfParticles(L);
    lua_pushinteger(L, emitter ? static_cast<lua_Integer>(emitter->GetAliveParticleCount()) : 0);
    return 1;
}

// self:GetParticleHits(): particles that hit something since the last call, and where the last
// one hit (x, y, z)
static int Self_GetParticleHits(lua_State* L) {
    ParticleNode* emitter = SelfParticles(L);
    const uint32_t hits = emitter ? emitter->TakeCollisionHits() : 0;
    const glm::vec3 at = emitter ? emitter->LastHitPosition() : glm::vec3(0.0f);
    lua_pushinteger(L, static_cast<lua_Integer>(hits));
    lua_pushnumber(L, at.x);
    lua_pushnumber(L, at.y);
    lua_pushnumber(L, at.z);
    return 4;
}

static int Self_ResetParticles(lua_State* L) {
    if (ParticleNode* emitter = SelfParticles(L)) emitter->ResetParticles();
    return 0;
}

// Pour spouts: SetPourFlow(0..1) fixes the flow, SetPourFlow(-1) goes back to pouring by tilt
static int Self_SetPourFlow(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float flow = static_cast<float>(luaL_checknumber(L, 2));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->pourFlow = flow < 0.0f ? -1.0f : std::min(flow, 1.0f);
    }
    return 0;
}

// How fast the spout is pouring this frame (0..1)
static int Self_GetPourFlow(lua_State* L) {
    uint32_t id = GetSelfId(L);
    float flow = 0.0f;
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) flow = node->pourCurrentFlow;
    }
    lua_pushnumber(L, flow);
    return 1;
}

static int Self_GetCollisionLayer(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushinteger(L, node->collisionLayer);
            return 1;
        }
    }
    lua_pushinteger(L, 1);
    return 1;
}

static int Self_SetCollisionMask(lua_State* L) {
    uint32_t id = GetSelfId(L);
    uint32_t mask = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->collisionMask = mask;
    }
    if (s_instance && s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->SetCollisionMask(id, mask);
    }
    return 0;
}

static int Self_GetCollisionMask(lua_State* L) {
    uint32_t id = GetSelfId(L);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            lua_pushinteger(L, node->collisionMask);
            return 1;
        }
    }
    lua_pushinteger(L, 1);
    return 1;
}

static int Lua_Physics_SetCollisionLayer(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t layer = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->collisionLayer = layer;
    }
    if (s_instance && s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->SetCollisionLayer(id, layer);
    }
    return 0;
}

static int Lua_Physics_GetCollisionLayer(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (s_instance && s_instance->GetPhysicsWorld()) {
        lua_pushinteger(L, s_instance->GetPhysicsWorld()->GetCollisionLayer(id));
        return 1;
    }
    lua_pushinteger(L, 1);
    return 1;
}

static int Lua_Physics_SetCollisionMask(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t mask = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->collisionMask = mask;
    }
    if (s_instance && s_instance->GetPhysicsWorld()) {
        s_instance->GetPhysicsWorld()->SetCollisionMask(id, mask);
    }
    return 0;
}

static int Lua_Physics_GetCollisionMask(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (s_instance && s_instance->GetPhysicsWorld()) {
        lua_pushinteger(L, s_instance->GetPhysicsWorld()->GetCollisionMask(id));
        return 1;
    }
    lua_pushinteger(L, 1);
    return 1;
}

static int Lua_Physics_CanCollide(lua_State* L) {
    uint32_t idA = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t idB = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    if (s_instance && s_instance->GetPhysicsWorld()) {
        lua_pushboolean(L, s_instance->GetPhysicsWorld()->CanCollide(idA, idB));
        return 1;
    }
    lua_pushboolean(L, true);
    return 1;
}

static void PushNodeTable(lua_State* L, Node& node);

// Scene Instancing & UI Containers
static int Lua_Scene_Instantiate(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    const char* path = luaL_checkstring(L, 1);
    uint32_t parentId = (lua_gettop(L) >= 2) ? static_cast<uint32_t>(lua_tointeger(L, 2)) : 0;
    auto instanceNode = s_instance->GetCurrentScene()->InstantiateScene(path, parentId);
    if (instanceNode) {
        PushNodeTable(L, *instanceNode);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int Lua_Scene_ReloadInstance(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool ok = s_instance->GetCurrentScene()->ReloadSceneInstance(id);
    lua_pushboolean(L, ok);
    return 1;
}

static int Lua_Scene_SpawnBoxContainer(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    std::string orientStr = (lua_gettop(L) >= 1 && lua_isstring(L, 1)) ? lua_tostring(L, 1) : "Vertical";
    std::string name = (lua_gettop(L) >= 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "VBoxContainer";
    uint32_t parentId = (lua_gettop(L) >= 3) ? static_cast<uint32_t>(lua_tointeger(L, 3)) : 0;
    BoxOrientation orient = StringToBoxOrientation(orientStr);
    auto node = s_instance->GetCurrentScene()->AddUIBoxContainerNode(orient, name, parentId);
    if (node) {
        PushNodeTable(L, *node);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int Lua_Scene_SpawnGridContainer(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    int cols = (lua_gettop(L) >= 1 && lua_isinteger(L, 1)) ? static_cast<int>(lua_tointeger(L, 1)) : 4;
    std::string name = (lua_gettop(L) >= 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "GridContainer";
    uint32_t parentId = (lua_gettop(L) >= 3) ? static_cast<uint32_t>(lua_tointeger(L, 3)) : 0;
    auto node = s_instance->GetCurrentScene()->AddUIGridContainerNode(cols, name, parentId);
    if (node) {
        PushNodeTable(L, *node);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// Declarative Replication
static int Self_AddReplicationRule(lua_State* L) {
    uint32_t id = GetSelfId(L);
    const char* prop = luaL_checkstring(L, 2);
    std::string modeStr = (lua_gettop(L) >= 3 && lua_isstring(L, 3)) ? lua_tostring(L, 3) : "OnChange";
    float hz = (lua_gettop(L) >= 4) ? static_cast<float>(lua_tonumber(L, 4)) : 20.0f;
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) {
            node->replicationConfig.enabled = true;
            node->replicationConfig.AddRule(prop, StringToReplicationMode(modeStr), hz);
        }
    }
    return 0;
}

static int Self_SetReplicationEnabled(lua_State* L) {
    uint32_t id = GetSelfId(L);
    bool en = (lua_toboolean(L, 2) != 0);
    if (s_instance && s_instance->GetCurrentScene()) {
        auto node = s_instance->GetCurrentScene()->GetNodeById(id);
        if (node) node->replicationConfig.enabled = en;
    }
    return 0;
}

// Sky bindings: the time of day and clouds of the running scene (SceneEnvironment)
static SceneEnvironment* SkyEnvironment() {
    return (s_instance && s_instance->GetCurrentScene()) ? &s_instance->GetCurrentScene()->GetEnvironment() : nullptr;
}

// Sky.SetTime(hours): also turns the clock on
static int Lua_Sky_SetTime(lua_State* L) {
    const float hours = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) {
        env->timeOfDayEnabled = true;
        env->timeOfDay = std::fmod(std::fmod(hours, 24.0f) + 24.0f, 24.0f);
    }
    return 0;
}

static int Lua_Sky_GetTime(lua_State* L) {
    SceneEnvironment* env = SkyEnvironment();
    lua_pushnumber(L, env ? env->timeOfDay : 12.0);
    return 1;
}

// Sky.SetDayLength(minutes): real minutes per day (0 stops the clock)
static int Lua_Sky_SetDayLength(lua_State* L) {
    const float minutes = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) env->dayLengthMinutes = std::max(minutes, 0.0f);
    return 0;
}

// Sky.SetDate(dayOfYear): 1..365, the season
static int Lua_Sky_SetDate(lua_State* L) {
    const float day = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) env->dayOfYear = std::fmod(std::fmod(day - 1.0f, 365.0f) + 365.0f, 365.0f) + 1.0f;
    return 0;
}

static int Lua_Sky_GetDate(lua_State* L) {
    SceneEnvironment* env = SkyEnvironment();
    lua_pushnumber(L, env ? env->dayOfYear : 81.0);
    return 1;
}

// Sky.SetMoonPhase(phase): 0 new, 0.5 full
static int Lua_Sky_SetMoonPhase(lua_State* L) {
    const float phase = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) env->moonPhase = std::fmod(std::fmod(phase, 1.0f) + 1.0f, 1.0f);
    return 0;
}

// Sky.SetClouds(coverage [, opacity]): coverage 0 clear .. 1 overcast; turns clouds on
static int Lua_Sky_SetClouds(lua_State* L) {
    const float coverage = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) {
        env->cloudsEnabled = true;
        env->cloudCoverage = std::clamp(coverage, 0.0f, 1.0f);
        if (lua_gettop(L) >= 2) env->cloudOpacity = std::clamp(static_cast<float>(luaL_checknumber(L, 2)), 0.0f, 1.0f);
    }
    return 0;
}

// Sky.SetWind(speed [, directionDegrees])
static int Lua_Sky_SetWind(lua_State* L) {
    const float speed = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) {
        env->windSpeed = std::max(speed, 0.0f);
        if (lua_gettop(L) >= 2) env->windDirection = static_cast<float>(luaL_checknumber(L, 2));
    }
    return 0;
}

// Weather bindings
// Weather.Set(name [, seconds]): "Clear", "Cloudy", "Overcast", "Rain", "Storm" or "Snow"; turns presets on
static int Lua_Weather_Set(lua_State* L) {
    const int preset = Weather::PresetFromName(luaL_checkstring(L, 1));
    if (preset < 0) return luaL_error(L, "Weather.Set: unknown weather '%s'", lua_tostring(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) {
        env->weatherEnabled = true;
        env->weatherPreset = preset;
        if (lua_gettop(L) >= 2) env->weatherTransition = std::max(static_cast<float>(luaL_checknumber(L, 2)), 0.0f);
    }
    return 0;
}

static int Lua_Weather_Get(lua_State* L) {
    SceneEnvironment* env = SkyEnvironment();
    lua_pushstring(L, (env && env->weatherEnabled) ? Weather::PresetName(env->weatherPreset) : "Custom");
    return 1;
}

// Weather.SetRain / SetSnow / SetWetness / SetSnowCover (0..1) and SetLightning (strikes per
// minute) set the weather directly, turning presets off
static int SetWeatherValue(lua_State* L, float SceneEnvironment::*field, float maxValue) {
    const float value = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) {
        env->weatherEnabled = false;
        env->*field = std::clamp(value, 0.0f, maxValue);
    }
    return 0;
}
static int Lua_Weather_SetRain(lua_State* L) { return SetWeatherValue(L, &SceneEnvironment::rainAmount, 1.0f); }
static int Lua_Weather_SetSnow(lua_State* L) { return SetWeatherValue(L, &SceneEnvironment::snowAmount, 1.0f); }
static int Lua_Weather_SetWetness(lua_State* L) { return SetWeatherValue(L, &SceneEnvironment::wetness, 1.0f); }
static int Lua_Weather_SetSnowCover(lua_State* L) { return SetWeatherValue(L, &SceneEnvironment::snowCover, 1.0f); }
static int Lua_Weather_SetLightning(lua_State* L) { return SetWeatherValue(L, &SceneEnvironment::lightningRate, 120.0f); }

// Weather.SetSnowDepth(metres) and Weather.SetThunderVolume(0..1): presets stay as they are
static int Lua_Weather_SetSnowDepth(lua_State* L) {
    const float depth = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) env->snowDepth = std::clamp(depth, 0.0f, 5.0f);
    return 0;
}
static int Lua_Weather_SetThunderVolume(lua_State* L) {
    const float volume = static_cast<float>(luaL_checknumber(L, 1));
    if (SceneEnvironment* env = SkyEnvironment()) env->thunderVolume = std::clamp(volume, 0.0f, 1.0f);
    return 0;
}

static int Lua_Weather_Strike(lua_State* L) {
    (void)L;
    if (SceneEnvironment* env = SkyEnvironment()) Weather::Strike(*env);
    return 0;
}

// Weather.GetStrikeCount(): goes up by one with each strike (play thunder when it changes)
static int Lua_Weather_GetStrikeCount(lua_State* L) {
    SceneEnvironment* env = SkyEnvironment();
    lua_pushinteger(L, env ? env->strikeCount : 0);
    return 1;
}

// Terrain bindings: the first terrain under a world point
static std::shared_ptr<TerrainNode> TerrainAt(float x, float z, glm::vec3& outLocal) {
    if (!s_instance || !s_instance->GetCurrentScene()) return nullptr;
    Scene& scene = *s_instance->GetCurrentScene();
    for (const auto& node : scene.GetNodes()) {
        if (!node || node->nodeType != NodeType::Terrain) continue;
        auto terrain = std::dynamic_pointer_cast<TerrainNode>(node);
        if (!terrain) continue;
        const glm::vec3 local = glm::vec3(glm::inverse(terrain->GetWorldTransformMatrix(scene)) * glm::vec4(x, 0.0f, z, 1.0f));
        if (std::abs(local.x) <= terrain->terrainSize.x * 0.5f && std::abs(local.z) <= terrain->terrainSize.y * 0.5f) {
            outLocal = local;
            return terrain;
        }
    }
    return nullptr;
}

// Terrain.GetHeight(x, z): the ground's height there (nil off every terrain)
static int Lua_Terrain_GetHeight(lua_State* L) {
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float z = static_cast<float>(luaL_checknumber(L, 2));
    glm::vec3 local;
    auto terrain = TerrainAt(x, z, local);
    if (!terrain) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, terrain->SampleHeight(x, z, *s_instance->GetCurrentScene()));
    return 1;
}

// Terrain.GetLayer(x, z): the paint layer most present there (1..4) and its name (footsteps)
static int Lua_Terrain_GetLayer(lua_State* L) {
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float z = static_cast<float>(luaL_checknumber(L, 2));
    glm::vec3 local;
    auto terrain = TerrainAt(x, z, local);
    if (!terrain) { lua_pushnil(L); return 1; }
    const glm::vec4 w = terrain->SampleSplatLocal(local.x, local.z);
    int best = 0;
    for (int i = 1; i < 4; ++i) if (w[i] > w[best]) best = i;
    lua_pushinteger(L, best + 1);
    lua_pushstring(L, best < static_cast<int>(terrain->layers.size()) ? terrain->layers[best].name.c_str() : "");
    return 2;
}

// Resource Manager Bindings
static int Lua_Resource_GetLoadedCount(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(Resource::ResourceManager::Get().GetLoadedResourceCount()));
    return 1;
}

static int Lua_Resource_GetMemoryUsage(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(Resource::ResourceManager::Get().GetTotalMemoryUsage()));
    return 1;
}

static int Lua_Resource_UnloadUnused(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(Resource::ResourceManager::Get().UnloadUnused()));
    return 1;
}

static void PushNodeTable(lua_State* L, Node& node) {
    lua_newtable(L);

    lua_pushinteger(L, node.id);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, node.name.c_str());
    lua_setfield(L, -2, "name");

    // Position table
    lua_newtable(L);
    lua_pushnumber(L, node.position.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, node.position.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, node.position.z);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "position");

    // Rotation table
    lua_newtable(L);
    lua_pushnumber(L, node.rotation.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, node.rotation.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, node.rotation.z);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "rotation");

    // Scale table
    lua_newtable(L);
    lua_pushnumber(L, node.scale.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, node.scale.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, node.scale.z);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "scale");

    // Direct methods available on self for all nodes
    lua_pushcfunction(L, Self_AddImpulse); lua_setfield(L, -2, "AddImpulse");
    lua_pushcfunction(L, Self_AddForce); lua_setfield(L, -2, "AddForce");
    lua_pushcfunction(L, Self_SetLinearVelocity); lua_setfield(L, -2, "SetLinearVelocity");
    lua_pushcfunction(L, Self_GetLinearVelocity); lua_setfield(L, -2, "GetLinearVelocity");
    lua_pushcfunction(L, Self_SetPosition); lua_setfield(L, -2, "SetPosition");
    lua_pushcfunction(L, Self_GetPosition); lua_setfield(L, -2, "GetPosition");
    lua_pushcfunction(L, Self_SetRotation); lua_setfield(L, -2, "SetRotation");
    lua_pushcfunction(L, Self_GetRotation); lua_setfield(L, -2, "GetRotation");
    lua_pushcfunction(L, Self_Translate); lua_setfield(L, -2, "Translate");
    lua_pushcfunction(L, Self_SetMultiplayerAuthority); lua_setfield(L, -2, "set_multiplayer_authority");
    lua_pushcfunction(L, Self_SetMultiplayerAuthority); lua_setfield(L, -2, "SetMultiplayerAuthority");
    lua_pushcfunction(L, Self_GetMultiplayerAuthority); lua_setfield(L, -2, "get_multiplayer_authority");
    lua_pushcfunction(L, Self_GetMultiplayerAuthority); lua_setfield(L, -2, "GetMultiplayerAuthority");
    lua_pushcfunction(L, Self_IsMultiplayerAuthority); lua_setfield(L, -2, "is_multiplayer_authority");
    lua_pushcfunction(L, Self_IsMultiplayerAuthority); lua_setfield(L, -2, "IsMultiplayerAuthority");
    // Signals 
    lua_pushcfunction(L, Self_Connect); lua_setfield(L, -2, "Connect");
    lua_pushcfunction(L, Self_Connect); lua_setfield(L, -2, "connect");
    lua_pushcfunction(L, Self_Emit); lua_setfield(L, -2, "Emit");
    lua_pushcfunction(L, Self_Emit); lua_setfield(L, -2, "emit_signal");
    lua_pushcfunction(L, Self_Disconnect); lua_setfield(L, -2, "Disconnect");
    lua_pushcfunction(L, Self_Disconnect); lua_setfield(L, -2, "disconnect");

    // Particle emitters
    lua_pushcfunction(L, Self_EmitParticles); lua_setfield(L, -2, "EmitParticles");
    lua_pushcfunction(L, Self_SetEmitting); lua_setfield(L, -2, "SetEmitting");
    lua_pushcfunction(L, Self_GetParticleCount); lua_setfield(L, -2, "GetParticleCount");
    lua_pushcfunction(L, Self_GetParticleHits); lua_setfield(L, -2, "GetParticleHits");
    lua_pushcfunction(L, Self_ResetParticles); lua_setfield(L, -2, "ResetParticles");

    // Liquids and pour spouts
    lua_pushcfunction(L, Self_SetLiquidFill); lua_setfield(L, -2, "SetLiquidFill");
    lua_pushcfunction(L, Self_GetLiquidFill); lua_setfield(L, -2, "GetLiquidFill");
    lua_pushcfunction(L, Self_SetPourFlow); lua_setfield(L, -2, "SetPourFlow");
    lua_pushcfunction(L, Self_GetPourFlow); lua_setfield(L, -2, "GetPourFlow");

    // Collision bitmasks
    lua_pushcfunction(L, Self_SetCollisionLayer); lua_setfield(L, -2, "SetCollisionLayer");
    lua_pushcfunction(L, Self_GetCollisionLayer); lua_setfield(L, -2, "GetCollisionLayer");
    lua_pushcfunction(L, Self_SetCollisionMask); lua_setfield(L, -2, "SetCollisionMask");
    lua_pushcfunction(L, Self_GetCollisionMask); lua_setfield(L, -2, "GetCollisionMask");

    // Declarative replication
    lua_pushcfunction(L, Self_AddReplicationRule); lua_setfield(L, -2, "AddReplicationRule");
    lua_pushcfunction(L, Self_SetReplicationEnabled); lua_setfield(L, -2, "SetReplicationEnabled");


    // 2D UI properties & method bindings if UI node
    if (node.IsUINode()) {
        lua_pushstring(L, node.uiText.c_str());
        lua_setfield(L, -2, "uiText");

        lua_pushnumber(L, node.uiProgress);
        lua_setfield(L, -2, "uiProgress");

        lua_pushnumber(L, node.uiValue);
        lua_setfield(L, -2, "uiValue");

        lua_pushboolean(L, node.uiChecked);
        lua_setfield(L, -2, "uiChecked");

        lua_pushinteger(L, node.uiSelectedIndex);
        lua_setfield(L, -2, "uiSelectedIndex");

        lua_pushboolean(L, node.uiWindowIsOpen);
        lua_setfield(L, -2, "uiWindowIsOpen");

        lua_pushboolean(L, node.uiWindowMovable);
        lua_setfield(L, -2, "uiWindowMovable");

        lua_pushboolean(L, node.uiWindowClosable);
        lua_setfield(L, -2, "uiWindowClosable");

        lua_pushboolean(L, node.uiVisible);
        lua_setfield(L, -2, "uiVisible");

        lua_pushboolean(L, node.uiEnabled);
        lua_setfield(L, -2, "uiEnabled");

        // Direct methods on self:
        lua_pushcfunction(L, Self_SetText); lua_setfield(L, -2, "SetText");
        lua_pushcfunction(L, Self_GetText); lua_setfield(L, -2, "GetText");
        lua_pushcfunction(L, Self_SetLocKey); lua_setfield(L, -2, "SetLocKey");
        lua_pushcfunction(L, Self_GetLocKey); lua_setfield(L, -2, "GetLocKey");
        lua_pushcfunction(L, Self_SetVisible); lua_setfield(L, -2, "SetVisible");
        lua_pushcfunction(L, Self_GetVisible); lua_setfield(L, -2, "GetVisible");
        lua_pushcfunction(L, Self_SetProgress); lua_setfield(L, -2, "SetProgress");
        lua_pushcfunction(L, Self_GetProgress); lua_setfield(L, -2, "GetProgress");
        lua_pushcfunction(L, Self_SetValue); lua_setfield(L, -2, "SetValue");
        lua_pushcfunction(L, Self_GetValue); lua_setfield(L, -2, "GetValue");
        lua_pushcfunction(L, Self_SetChecked); lua_setfield(L, -2, "SetChecked");
        lua_pushcfunction(L, Self_GetChecked); lua_setfield(L, -2, "GetChecked");
        lua_pushcfunction(L, Self_SetSelectedIndex); lua_setfield(L, -2, "SetSelectedIndex");
        lua_pushcfunction(L, Self_GetSelectedIndex); lua_setfield(L, -2, "GetSelectedIndex");
        lua_pushcfunction(L, Self_SetColor); lua_setfield(L, -2, "SetColor");
        lua_pushcfunction(L, Self_GetColor); lua_setfield(L, -2, "GetColor");
        lua_pushcfunction(L, Self_SetTextColor); lua_setfield(L, -2, "SetTextColor");
        lua_pushcfunction(L, Self_GetTextColor); lua_setfield(L, -2, "GetTextColor");
        lua_pushcfunction(L, Self_SetWindowOpen); lua_setfield(L, -2, "SetWindowOpen");
        lua_pushcfunction(L, Self_GetWindowOpen); lua_setfield(L, -2, "GetWindowOpen");
        lua_pushcfunction(L, Self_SetWindowMovable); lua_setfield(L, -2, "SetWindowMovable");
        lua_pushcfunction(L, Self_GetWindowMovable); lua_setfield(L, -2, "GetWindowMovable");
        lua_pushcfunction(L, Self_SetWindowClosable); lua_setfield(L, -2, "SetWindowClosable");
        lua_pushcfunction(L, Self_GetWindowClosable); lua_setfield(L, -2, "GetWindowClosable");
        lua_pushcfunction(L, Self_SetEnabled); lua_setfield(L, -2, "SetEnabled");
        lua_pushcfunction(L, Self_GetEnabled); lua_setfield(L, -2, "GetEnabled");
    }
}

static void SyncSelfTable(lua_State* L, int selfIndex, const Node& node) {
    if (selfIndex < 0) selfIndex = lua_gettop(L) + selfIndex + 1;

    lua_pushinteger(L, node.id);
    lua_setfield(L, selfIndex, "id");

    lua_pushstring(L, node.name.c_str());
    lua_setfield(L, selfIndex, "name");

    lua_getfield(L, selfIndex, "position");
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, node.position.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, node.position.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, node.position.z); lua_setfield(L, -2, "z");
    }
    lua_pop(L, 1);

    lua_getfield(L, selfIndex, "rotation");
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, node.rotation.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, node.rotation.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, node.rotation.z); lua_setfield(L, -2, "z");
    }
    lua_pop(L, 1);

    lua_getfield(L, selfIndex, "scale");
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, node.scale.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, node.scale.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, node.scale.z); lua_setfield(L, -2, "z");
    }
    lua_pop(L, 1);

    if (node.IsUINode()) {
        lua_pushstring(L, node.uiText.c_str()); lua_setfield(L, selfIndex, "uiText");
        lua_pushnumber(L, node.uiProgress); lua_setfield(L, selfIndex, "uiProgress");
        lua_pushnumber(L, node.uiValue); lua_setfield(L, selfIndex, "uiValue");
        lua_pushboolean(L, node.uiChecked); lua_setfield(L, selfIndex, "uiChecked");
        lua_pushinteger(L, node.uiSelectedIndex); lua_setfield(L, selfIndex, "uiSelectedIndex");
        lua_pushboolean(L, node.uiWindowIsOpen); lua_setfield(L, selfIndex, "uiWindowIsOpen");
        lua_pushboolean(L, node.uiWindowMovable); lua_setfield(L, selfIndex, "uiWindowMovable");
        lua_pushboolean(L, node.uiWindowClosable); lua_setfield(L, selfIndex, "uiWindowClosable");
        lua_pushboolean(L, node.uiVisible); lua_setfield(L, selfIndex, "uiVisible");
        lua_pushboolean(L, node.uiEnabled); lua_setfield(L, selfIndex, "uiEnabled");
    }
}

static void SyncNodeFromSelf(lua_State* L, int selfIndex, Node& node) {
    if (selfIndex < 0) selfIndex = lua_gettop(L) + selfIndex + 1;
    glm::vec3 prevPos = node.position;
    glm::vec3 prevRot = node.rotation;

    lua_getfield(L, selfIndex, "position");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "x"); if (lua_isnumber(L, -1)) node.position.x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, -1, "y"); if (lua_isnumber(L, -1)) node.position.y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, -1, "z"); if (lua_isnumber(L, -1)) node.position.z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, selfIndex, "rotation");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "x"); if (lua_isnumber(L, -1)) node.rotation.x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, -1, "y"); if (lua_isnumber(L, -1)) node.rotation.y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, -1, "z"); if (lua_isnumber(L, -1)) node.rotation.z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, selfIndex, "scale");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "x"); if (lua_isnumber(L, -1)) node.scale.x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, -1, "y"); if (lua_isnumber(L, -1)) node.scale.y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, -1, "z"); if (lua_isnumber(L, -1)) node.scale.z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    }
    lua_pop(L, 1);

    if (node.IsUINode()) {
        lua_getfield(L, selfIndex, "uiText"); if (lua_isstring(L, -1)) node.uiText = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiProgress"); if (lua_isnumber(L, -1)) node.uiProgress = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiValue"); if (lua_isnumber(L, -1)) node.uiValue = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiChecked"); if (lua_isboolean(L, -1)) node.uiChecked = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiSelectedIndex"); if (lua_isinteger(L, -1)) node.uiSelectedIndex = static_cast<int>(lua_tointeger(L, -1)); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiWindowIsOpen"); if (lua_isboolean(L, -1)) node.uiWindowIsOpen = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiWindowMovable"); if (lua_isboolean(L, -1)) node.uiWindowMovable = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiWindowClosable"); if (lua_isboolean(L, -1)) node.uiWindowClosable = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiVisible"); if (lua_isboolean(L, -1)) node.uiVisible = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getfield(L, selfIndex, "uiEnabled"); if (lua_isboolean(L, -1)) node.uiEnabled = lua_toboolean(L, -1); lua_pop(L, 1);
    }

    if (node.position != prevPos || node.rotation != prevRot) {
        node.MarkLocallyActive();
    }
    if (s_instance && s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(node.id)) {
        if (node.position != prevPos) {
            s_instance->GetPhysicsWorld()->SetPosition(node.id, node.position);
        }
        if (node.rotation != prevRot) {
            s_instance->GetPhysicsWorld()->SetRotation(node.id, node.rotation);
        }
    }
}

// Helper to update Node from Lua table
static void UpdateNodeFromTable(lua_State* L, int index, Node& node) {
    if (!lua_istable(L, index)) return;

    // Check position
    lua_getfield(L, index, "position");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "x");
        if (lua_isnumber(L, -1)) node.position.x = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, -1, "y");
        if (lua_isnumber(L, -1)) node.position.y = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, -1, "z");
        if (lua_isnumber(L, -1)) node.position.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // Check rotation
    lua_getfield(L, index, "rotation");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "x");
        if (lua_isnumber(L, -1)) node.rotation.x = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, -1, "y");
        if (lua_isnumber(L, -1)) node.rotation.y = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, -1, "z");
        if (lua_isnumber(L, -1)) node.rotation.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // Check scale
    lua_getfield(L, index, "scale");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "x");
        if (lua_isnumber(L, -1)) node.scale.x = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, -1, "y");
        if (lua_isnumber(L, -1)) node.scale.y = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, -1, "z");
        if (lua_isnumber(L, -1)) node.scale.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // Check UI properties
    if (node.IsUINode()) {
        lua_getfield(L, index, "uiText");
        if (lua_isstring(L, -1)) node.uiText = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "uiProgress");
        if (lua_isnumber(L, -1)) node.uiProgress = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "uiValue");
        if (lua_isnumber(L, -1)) node.uiValue = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "uiChecked");
        if (lua_isboolean(L, -1)) node.uiChecked = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "uiSelectedIndex");
        if (lua_isinteger(L, -1)) node.uiSelectedIndex = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "uiWindowIsOpen");
        if (lua_isboolean(L, -1)) node.uiWindowIsOpen = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "uiWindowMovable");
        if (lua_isboolean(L, -1)) node.uiWindowMovable = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "uiWindowClosable");
        if (lua_isboolean(L, -1)) node.uiWindowClosable = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "uiVisible");
        if (lua_isboolean(L, -1)) node.uiVisible = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "uiEnabled");
        if (lua_isboolean(L, -1)) node.uiEnabled = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
}

// C-functions for node API inside Lua
static int Lua_SetNodePosition(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        node->position = glm::vec3(x, y, z);
        node->MarkLocallyActive();
        if (s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id)) {
            s_instance->GetPhysicsWorld()->SetPosition(id, node->position);
        }
    }
    return 0;
}

static int Lua_SetNodeRotation(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        node->rotation = glm::vec3(x, y, z);
    }
    return 0;
}

static int Lua_SetNodeScale(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        node->scale = glm::vec3(x, y, z);
    }
    return 0;
}

static int Lua_TranslateNode(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float dx = static_cast<float>(luaL_checknumber(L, 2));
    float dy = static_cast<float>(luaL_checknumber(L, 3));
    float dz = static_cast<float>(luaL_checknumber(L, 4));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        node->position += glm::vec3(dx, dy, dz);
        if (s_instance->GetPhysicsWorld() && s_instance->GetPhysicsWorld()->HasBodyForNode(id)) {
            s_instance->GetPhysicsWorld()->SetPosition(id, node->position);
        }
    }
    return 0;
}

static int Lua_RotateNode(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float rx = static_cast<float>(luaL_checknumber(L, 2));
    float ry = static_cast<float>(luaL_checknumber(L, 3));
    float rz = static_cast<float>(luaL_checknumber(L, 4));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        node->rotation += glm::vec3(rx, ry, rz);
    }
    return 0;
}

static int Lua_SetUIText(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* text = luaL_checkstring(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiText = text;
    }
    return 0;
}

static int Lua_SetUILocKey(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiLocKey = key;
    }
    return 0;
}

static int Lua_GetUILocKey(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushstring(L, "");
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushstring(L, node->uiLocKey.c_str());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

static int Lua_Loc_Translate(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    const char* defVal = (lua_gettop(L) >= 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "";
    std::string res = GameLocalization::Get().Translate(key, defVal);
    lua_pushstring(L, res.c_str());
    return 1;
}

static int Lua_Loc_SetLanguage(lua_State* L) {
    const char* code = luaL_checkstring(L, 1);
    bool ok = GameLocalization::Get().SetLanguage(code);
    lua_pushboolean(L, ok);
    return 1;
}

static int Lua_Loc_GetLanguage(lua_State* L) {
    std::string cur = GameLocalization::Get().GetCurrentLanguage();
    lua_pushstring(L, cur.c_str());
    return 1;
}

static int Lua_Loc_GetAvailableLanguages(lua_State* L) {
    const auto& langs = GameLocalization::Get().GetAvailableLanguages();
    lua_createtable(L, static_cast<int>(langs.size()), 0);
    for (size_t i = 0; i < langs.size(); ++i) {
        lua_createtable(L, 0, 2);
        lua_pushstring(L, langs[i].code.c_str());
        lua_setfield(L, -2, "code");
        lua_pushstring(L, langs[i].displayName.c_str());
        lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

static int Lua_GetUIText(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushstring(L, "");
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushstring(L, node->uiText.c_str());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

static int Lua_SetUIProgress(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float val = static_cast<float>(luaL_checknumber(L, 2));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiProgress = std::clamp(val, 0.0f, 1.0f);
    }
    return 0;
}

static int Lua_GetUIProgress(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushnumber(L, node->uiProgress);
    } else {
        lua_pushnumber(L, 0.0);
    }
    return 1;
}

static int Lua_SetUIValue(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float val = static_cast<float>(luaL_checknumber(L, 2));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiValue = val;
    }
    return 0;
}

static int Lua_GetUIValue(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushnumber(L, node->uiValue);
    } else {
        lua_pushnumber(L, 0.0);
    }
    return 1;
}

static int Lua_SetUIChecked(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool chk = lua_toboolean(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiChecked = chk;
    }
    return 0;
}

static int Lua_GetUIChecked(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushboolean(L, false);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushboolean(L, node->uiChecked);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetUISelectedIndex(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    int idx = static_cast<int>(luaL_checkinteger(L, 2));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiSelectedIndex = idx;
    }
    return 0;
}

static int Lua_GetUISelectedIndex(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushinteger(L, node->uiSelectedIndex);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int Lua_SetUIColor(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float r = static_cast<float>(luaL_checknumber(L, 2));
    float g = static_cast<float>(luaL_checknumber(L, 3));
    float b = static_cast<float>(luaL_checknumber(L, 4));
    float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiColor = glm::vec4(r, g, b, a);
    }
    return 0;
}

static int Lua_GetUIColor(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushnumber(L, node->uiColor.r);
        lua_pushnumber(L, node->uiColor.g);
        lua_pushnumber(L, node->uiColor.b);
        lua_pushnumber(L, node->uiColor.a);
        return 4;
    }
    return 0;
}

static int Lua_SetUITextColor(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float r = static_cast<float>(luaL_checknumber(L, 2));
    float g = static_cast<float>(luaL_checknumber(L, 3));
    float b = static_cast<float>(luaL_checknumber(L, 4));
    float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiTextColor = glm::vec4(r, g, b, a);
    }
    return 0;
}

static int Lua_GetUITextColor(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushnumber(L, node->uiTextColor.r);
        lua_pushnumber(L, node->uiTextColor.g);
        lua_pushnumber(L, node->uiTextColor.b);
        lua_pushnumber(L, node->uiTextColor.a);
        return 4;
    }
    return 0;
}

static int Lua_SetWindowOpen(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool open = lua_toboolean(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiWindowIsOpen = open;
    }
    return 0;
}

static int Lua_GetWindowOpen(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushboolean(L, false);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushboolean(L, node->uiWindowIsOpen);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetWindowMovable(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool movable = lua_toboolean(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiWindowMovable = movable;
    }
    return 0;
}

static int Lua_GetWindowMovable(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushboolean(L, false);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushboolean(L, node->uiWindowMovable);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetWindowClosable(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool closable = lua_toboolean(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiWindowClosable = closable;
    }
    return 0;
}

static int Lua_GetWindowClosable(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushboolean(L, false);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushboolean(L, node->uiWindowClosable);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_SetUIEnabled(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool enabled = lua_toboolean(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiEnabled = enabled;
    }
    return 0;
}

static int Lua_GetUIEnabled(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushboolean(L, false);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushboolean(L, node->uiEnabled);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static int Lua_GetUIVisible(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushboolean(L, false);
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        lua_pushboolean(L, node->uiVisible);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

// Animation Tree & State Machine API
static int Lua_SetAnimState(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* stateName = luaL_checkstring(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        node->currentAnimState = stateName;
        std::cout << "[Lua AnimationTree] Node " << node->name << " transitioned to anim state: " << stateName << std::endl;
    }
    return 0;
}

static int Lua_GetAnimState(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushstring(L, "");
        return 1;
    }
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node) {
        lua_pushstring(L, node->currentAnimState.c_str());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

static int Lua_SetUIVisible(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool vis = lua_toboolean(L, 2);

    auto node = s_instance->GetCurrentScene()->GetNodeById(id);
    if (node && node->IsUINode()) {
        node->uiVisible = vis;
    }
    return 0;
}

static int Lua_FindNodeByName(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushnil(L);
        return 1;
    }
    const char* name = luaL_checkstring(L, 1);
    for (const auto& node : s_instance->GetCurrentScene()->GetNodes()) {
        if (node->name == name) {
            PushNodeTable(L, *node);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int Lua_DeleteNode(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool ok = s_instance->GetCurrentScene()->DeleteNode(id);
    lua_pushboolean(L, ok);
    return 1;
}

static int Lua_SpawnMeshNode(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) {
        lua_pushnil(L);
        return 1;
    }
    const char* name = luaL_optstring(L, 1, "SpawnedMesh");
    const char* shapeStr = luaL_optstring(L, 2, "Cube");

    ShapeType shape = StringToShapeType(shapeStr);
    auto node = s_instance->GetCurrentScene()->AddNode(shape, name);
    if (node) {
        PushNodeTable(L, *node);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int Lua_Cutscene_Play(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    bool ok = false;
    if (s_instance && s_instance->GetCurrentScene() && s_instance->GetCutscenePlayer()) {
        ok = s_instance->GetCutscenePlayer()->Start(*s_instance->GetCurrentScene(), name);
    }
    lua_pushboolean(L, ok);
    return 1;
}

static int Lua_Cutscene_Skip(lua_State* L) {
    bool ok = false;
    if (s_instance && s_instance->GetCurrentScene() && s_instance->GetCutscenePlayer()) {
        ok = s_instance->GetCutscenePlayer()->Skip(*s_instance->GetCurrentScene(), true);
    }
    lua_pushboolean(L, ok);
    return 1;
}

static int Lua_Cutscene_Stop(lua_State* L) {
    if (s_instance && s_instance->GetCurrentScene() && s_instance->GetCutscenePlayer()) {
        s_instance->GetCutscenePlayer()->Stop(*s_instance->GetCurrentScene(), true);
    }
    return 0;
}

static int Lua_Cutscene_IsPlaying(lua_State* L) {
    bool playing = false;
    if (s_instance && s_instance->GetCutscenePlayer()) {
        playing = s_instance->GetCutscenePlayer()->IsPlaying();
    }
    lua_pushboolean(L, playing);
    return 1;
}

static int Lua_Cutscene_GetTime(lua_State* L) {
    float t = 0.0f;
    if (s_instance && s_instance->GetCutscenePlayer()) {
        t = s_instance->GetCutscenePlayer()->GetCurrentTime();
    }
    lua_pushnumber(L, t);
    return 1;
}

static int Lua_Cutscene_GetName(lua_State* L) {
    std::string name;
    if (s_instance && s_instance->GetCutscenePlayer()) {
        name = s_instance->GetCutscenePlayer()->GetActiveCutsceneName();
    }
    lua_pushstring(L, name.c_str());
    return 1;
}

static std::shared_ptr<Node> ResolveVideoTargetNode(lua_State* L, int argIdx = 1) {
    if (!s_instance || !s_instance->GetCurrentScene()) return nullptr;
    Scene* sc = s_instance->GetCurrentScene();
    if (lua_isnumber(L, argIdx)) {
        return sc->GetNodeById(static_cast<uint32_t>(lua_tointeger(L, argIdx)));
    }
    if (lua_isstring(L, argIdx)) {
        std::string name = lua_tostring(L, argIdx);
        for (const auto& n : sc->GetNodes()) {
            if (n && n->name == name) return n;
        }
    }
    return nullptr;
}

static int Lua_Video_Play(lua_State* L) {
    auto node = ResolveVideoTargetNode(L, 1);
    if (node) {
        node->isVideoScreen = true;
        node->videoPlaying = true;
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int Lua_Video_Pause(lua_State* L) {
    auto node = ResolveVideoTargetNode(L, 1);
    if (node) {
        node->videoPlaying = false;
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int Lua_Video_Stop(lua_State* L) {
    auto node = ResolveVideoTargetNode(L, 1);
    if (node) {
        node->videoPlaying = false;
        node->videoCurrentTime = 0.0f;
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int Lua_Video_SetChannel(lua_State* L) {
    auto node = ResolveVideoTargetNode(L, 1);
    int ch = static_cast<int>(luaL_checkinteger(L, 2));
    if (node) {
        node->isVideoScreen = true;
        node->videoChannelPreset = std::clamp(ch, 0, 6);
        node->videoCurrentTime = 0.0f;
        node->videoPlaying = true;
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int Lua_Video_SetSource(lua_State* L) {
    auto node = ResolveVideoTargetNode(L, 1);
    const char* path = luaL_checkstring(L, 2);
    if (node && path) {
        node->isVideoScreen = true;
        node->videoSourcePath = path;
        node->videoCurrentTime = 0.0f;
        node->videoPlaying = true;
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int Lua_Video_Seek(lua_State* L) {
    auto node = ResolveVideoTargetNode(L, 1);
    float t = static_cast<float>(luaL_checknumber(L, 2));
    if (node) {
        node->videoCurrentTime = std::max(0.0f, t);
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int Lua_Video_IsPlaying(lua_State* L) {
    auto node = ResolveVideoTargetNode(L, 1);
    lua_pushboolean(L, (node && node->videoPlaying) ? 1 : 0);
    return 1;
}

static int Lua_Video_PlayCutaway(lua_State* L) {
    int ch = 1;
    std::string path = "";
    if (lua_isnumber(L, 1)) {
        ch = static_cast<int>(lua_tointeger(L, 1));
    } else if (lua_isstring(L, 1)) {
        path = lua_tostring(L, 1);
        ch = 0;
    }
    float dur = lua_isnumber(L, 2) ? static_cast<float>(lua_tonumber(L, 2)) : 6.0f;
    bool skip = lua_isboolean(L, 3) ? (lua_toboolean(L, 3) != 0) : true;
    bool pip = lua_isboolean(L, 4) ? (lua_toboolean(L, 4) != 0) : false;
    std::string title = lua_isstring(L, 5) ? lua_tostring(L, 5) : "VIDEO CUTAWAY";
    Video::VideoSystem::Get().StartCutaway(ch, path, dur, skip, pip, title);
    return 0;
}

static int Lua_Video_StopCutaway(lua_State* /*L*/) {
    Video::VideoSystem::Get().StopCutaway();
    return 0;
}

static int Lua_Video_IsCutawayPlaying(lua_State* L) {
    lua_pushboolean(L, Video::VideoSystem::Get().IsCutawayPlaying() ? 1 : 0);
    return 1;
}

// ---- Render: hardware ray tracing switches and frame statistics ----
static bool* RTFeature(Graphics::RayTracing::RayTracingSettings& s, const std::string& name) {
    if (name == "all" || name == "rt" || name == "raytracing") return &s.enableHardwareRT;
    if (name == "shadows") return &s.enableRTS;
    if (name == "reflections") return &s.enableRTR;
    if (name == "ao" || name == "occlusion") return &s.enableRTAO;
    return nullptr;
}
// Render.SetRayTracing(feature, on): feature "all", "shadows", "reflections" or "ao"
static int Lua_Render_SetRayTracing(lua_State* L) {
    const std::string feature = luaL_checkstring(L, 1);
    const bool on = lua_toboolean(L, 2) != 0;
    if (bool* flag = RTFeature(Graphics::RayTracing::RayTracingManager::Get().GetSettings(), feature)) *flag = on;
    return 0;
}
// Render.GetRayTracing(feature) -> on (false where the GPU can't ray trace)
static int Lua_Render_GetRayTracing(lua_State* L) {
    const std::string feature = luaL_optstring(L, 1, "all");
    auto& rt = Graphics::RayTracing::RayTracingManager::Get();
    bool* flag = RTFeature(rt.GetSettings(), feature);
    lua_pushboolean(L, flag && *flag && rt.IsHardwareRayTracingSupported() && rt.GetSettings().enableHardwareRT);
    return 1;
}
// Render.GetRayTracingStats() -> { supported, blas, instances, memoryMB, buildMs, device }
static int Lua_Render_GetRayTracingStats(lua_State* L) {
    const auto stats = Graphics::RayTracing::RayTracingManager::Get().GetStats();
    lua_newtable(L);
    lua_pushboolean(L, stats.supported); lua_setfield(L, -2, "supported");
    lua_pushinteger(L, stats.totalBLASCount); lua_setfield(L, -2, "blas");
    lua_pushinteger(L, stats.activeTLASInstances); lua_setfield(L, -2, "instances");
    lua_pushnumber(L, static_cast<double>(stats.bvhMemoryBytes) / (1024.0 * 1024.0)); lua_setfield(L, -2, "memoryMB");
    lua_pushnumber(L, stats.lastBuildTimeMs); lua_setfield(L, -2, "buildMs");
    lua_pushstring(L, stats.deviceName.c_str()); lua_setfield(L, -2, "device");
    return 1;
}
// Render.GetFPS() -> frames per second (averaged)
static int Lua_Render_GetFPS(lua_State* L) {
    lua_pushnumber(L, ImGui::GetCurrentContext() ? ImGui::GetIO().Framerate : 0.0f);
    return 1;
}
// Streaming.GetStats() -> { sectors, active, loading, ready, nodes, streamedIn, streamedOut, mbRead, lastMs, maxMs }
static int Lua_Streaming_GetStats(lua_State* L) {
    lua_newtable(L);
    const auto& provider = LuaScriptSystem::Get().GetStreamingStatsProvider();
    if (!provider) return 1;
    for (const auto& [name, value] : provider()) {
        lua_pushnumber(L, value);
        lua_setfield(L, -2, name.c_str());
    }
    return 1;
}
// NodeAPI.SetLight(id, intensity [, r, g, b]) / NodeAPI.GetLight(id) -> intensity, r, g, b
static int Lua_SetLight(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    auto node = s_instance->GetCurrentScene()->GetNodeById(static_cast<uint32_t>(luaL_checkinteger(L, 1)));
    if (!node || node->nodeType != NodeType::Light) return 0;
    node->lightIntensity = static_cast<float>(luaL_checknumber(L, 2));
    if (lua_gettop(L) >= 5) {
        node->lightColor = glm::vec3(static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4)),
                                     static_cast<float>(luaL_checknumber(L, 5)));
    }
    return 0;
}
static int Lua_GetLight(lua_State* L) {
    if (!s_instance || !s_instance->GetCurrentScene()) return 0;
    auto node = s_instance->GetCurrentScene()->GetNodeById(static_cast<uint32_t>(luaL_checkinteger(L, 1)));
    if (!node || node->nodeType != NodeType::Light) return 0;
    lua_pushnumber(L, node->lightIntensity);
    lua_pushnumber(L, node->lightColor.r);
    lua_pushnumber(L, node->lightColor.g);
    lua_pushnumber(L, node->lightColor.b);
    return 4;
}

void LuaScriptSystem::RegisterEngineAPI() {
    if (!L) return;

    // Override print
    lua_register(L, "print", Lua_Print);

    // Input table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_IsKeyDown);
    lua_setfield(L, -2, "IsDown");
    lua_pushcfunction(L, Lua_IsKeyPressed);
    lua_setfield(L, -2, "IsPressed");
    lua_pushcfunction(L, Lua_IsKeyReleased);
    lua_setfield(L, -2, "IsReleased");
    lua_pushcfunction(L, Lua_GetMouseDelta);
    lua_setfield(L, -2, "GetMouseDelta");
    lua_pushcfunction(L, Lua_IsMouseDown);
    lua_setfield(L, -2, "IsMouseDown");
    lua_setglobal(L, "Input");

    // Physics table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Physics_AddImpulse);
    lua_setfield(L, -2, "AddImpulse");
    lua_pushcfunction(L, Lua_Physics_AddForce);
    lua_setfield(L, -2, "AddForce");
    lua_pushcfunction(L, Lua_Physics_SetLinearVelocity);
    lua_setfield(L, -2, "SetLinearVelocity");
    lua_pushcfunction(L, Lua_Physics_GetLinearVelocity);
    lua_setfield(L, -2, "GetLinearVelocity");
    lua_pushcfunction(L, Lua_Physics_SetAngularVelocity);
    lua_setfield(L, -2, "SetAngularVelocity");
    lua_pushcfunction(L, Lua_Physics_GetAngularVelocity);
    lua_setfield(L, -2, "GetAngularVelocity");
    lua_pushcfunction(L, Lua_Physics_SetPosition);
    lua_setfield(L, -2, "SetPosition");
    lua_pushcfunction(L, Lua_Physics_GetPosition);
    lua_setfield(L, -2, "GetPosition");
    lua_pushcfunction(L, Lua_Physics_HasBody);
    lua_setfield(L, -2, "HasBody");
    lua_setglobal(L, "Physics");

    // Scene table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_FindNodeByName);
    lua_setfield(L, -2, "FindNode");
    lua_pushcfunction(L, Lua_DeleteNode);
    lua_setfield(L, -2, "DeleteNode");
    lua_pushcfunction(L, Lua_SpawnMeshNode);
    lua_setfield(L, -2, "SpawnMesh");
    lua_setglobal(L, "Scene");

    // Node API table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_GetNodePosition);
    lua_setfield(L, -2, "GetPosition");
    lua_pushcfunction(L, Lua_SetNodePosition);
    lua_setfield(L, -2, "SetPosition");
    lua_pushcfunction(L, Lua_GetNodeRotation);
    lua_setfield(L, -2, "GetRotation");
    lua_pushcfunction(L, Lua_SetNodeRotation);
    lua_setfield(L, -2, "SetRotation");
    lua_pushcfunction(L, Lua_SetNodeScale);
    lua_setfield(L, -2, "SetScale");
    lua_pushcfunction(L, Lua_TranslateNode);
    lua_setfield(L, -2, "Translate");
    lua_pushcfunction(L, Lua_Physics_AddImpulse);
    lua_setfield(L, -2, "AddImpulse");
    lua_pushcfunction(L, Lua_Physics_AddForce);
    lua_setfield(L, -2, "AddForce");
    lua_pushcfunction(L, Lua_Physics_SetLinearVelocity);
    lua_setfield(L, -2, "SetLinearVelocity");
    lua_pushcfunction(L, Lua_Physics_GetLinearVelocity);
    lua_setfield(L, -2, "GetLinearVelocity");
    lua_pushcfunction(L, Lua_MoveRelativeNode);
    lua_setfield(L, -2, "MoveRelative");
    lua_pushcfunction(L, Lua_RotateNode);
    lua_setfield(L, -2, "Rotate");
    lua_pushcfunction(L, Lua_SetUIText);
    lua_setfield(L, -2, "SetUIText");
    lua_pushcfunction(L, Lua_SetUIText);
    lua_setfield(L, -2, "SetText");
    lua_pushcfunction(L, Lua_GetUIText);
    lua_setfield(L, -2, "GetUIText");
    lua_pushcfunction(L, Lua_GetUIText);
    lua_setfield(L, -2, "GetText");
    lua_pushcfunction(L, Lua_SetUILocKey); lua_setfield(L, -2, "SetLocKey");
    lua_pushcfunction(L, Lua_GetUILocKey); lua_setfield(L, -2, "GetLocKey");

    lua_pushcfunction(L, Lua_SetUIProgress);
    lua_setfield(L, -2, "SetUIProgress");
    lua_pushcfunction(L, Lua_SetUIProgress);
    lua_setfield(L, -2, "SetProgress");
    lua_pushcfunction(L, Lua_GetUIProgress);
    lua_setfield(L, -2, "GetUIProgress");
    lua_pushcfunction(L, Lua_GetUIProgress);
    lua_setfield(L, -2, "GetProgress");

    lua_pushcfunction(L, Lua_SetUIValue);
    lua_setfield(L, -2, "SetValue");
    lua_pushcfunction(L, Lua_GetUIValue);
    lua_setfield(L, -2, "GetValue");

    lua_pushcfunction(L, Lua_SetUIChecked);
    lua_setfield(L, -2, "SetChecked");
    lua_pushcfunction(L, Lua_GetUIChecked);
    lua_setfield(L, -2, "GetChecked");

    lua_pushcfunction(L, Lua_SetUISelectedIndex);
    lua_setfield(L, -2, "SetSelectedIndex");
    lua_pushcfunction(L, Lua_GetUISelectedIndex);
    lua_setfield(L, -2, "GetSelectedIndex");

    lua_pushcfunction(L, Lua_SetUIColor);
    lua_setfield(L, -2, "SetColor");
    lua_pushcfunction(L, Lua_GetUIColor);
    lua_setfield(L, -2, "GetColor");

    lua_pushcfunction(L, Lua_SetUITextColor);
    lua_setfield(L, -2, "SetTextColor");
    lua_pushcfunction(L, Lua_GetUITextColor);
    lua_setfield(L, -2, "GetTextColor");

    lua_pushcfunction(L, Lua_SetWindowOpen);
    lua_setfield(L, -2, "SetWindowOpen");
    lua_pushcfunction(L, Lua_GetWindowOpen);
    lua_setfield(L, -2, "GetWindowOpen");

    lua_pushcfunction(L, Lua_SetWindowMovable);
    lua_setfield(L, -2, "SetWindowMovable");
    lua_pushcfunction(L, Lua_GetWindowMovable);
    lua_setfield(L, -2, "GetWindowMovable");

    lua_pushcfunction(L, Lua_SetWindowClosable);
    lua_setfield(L, -2, "SetWindowClosable");
    lua_pushcfunction(L, Lua_GetWindowClosable);
    lua_setfield(L, -2, "GetWindowClosable");

    lua_pushcfunction(L, Lua_SetUIEnabled);
    lua_setfield(L, -2, "SetEnabled");
    lua_pushcfunction(L, Lua_GetUIEnabled);
    lua_setfield(L, -2, "GetEnabled");

    lua_pushcfunction(L, Lua_SetUIVisible);
    lua_setfield(L, -2, "SetUIVisible");
    lua_pushcfunction(L, Lua_SetUIVisible);
    lua_setfield(L, -2, "SetVisible");
    lua_pushcfunction(L, Lua_GetUIVisible);
    lua_setfield(L, -2, "GetUIVisible");
    lua_pushcfunction(L, Lua_GetUIVisible);
    lua_setfield(L, -2, "GetVisible");
    lua_pushcfunction(L, Lua_SetAnimState);
    lua_setfield(L, -2, "SetAnimState");
    lua_pushcfunction(L, Lua_GetAnimState);
    lua_setfield(L, -2, "GetAnimState");
    lua_pushcfunction(L, Lua_SetLight); lua_setfield(L, -2, "SetLight");
    lua_pushcfunction(L, Lua_GetLight); lua_setfield(L, -2, "GetLight");
    lua_setglobal(L, "NodeAPI");

    // Render: ray tracing switches, statistics
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Render_SetRayTracing); lua_setfield(L, -2, "SetRayTracing");
    lua_pushcfunction(L, Lua_Render_GetRayTracing); lua_setfield(L, -2, "GetRayTracing");
    lua_pushcfunction(L, Lua_Render_GetRayTracingStats); lua_setfield(L, -2, "GetRayTracingStats");
    lua_pushcfunction(L, Lua_Render_GetFPS); lua_setfield(L, -2, "GetFPS");
    lua_setglobal(L, "Render");

    // Streaming: world streaming statistics
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Streaming_GetStats); lua_setfield(L, -2, "GetStats");
    lua_setglobal(L, "Streaming");

    // Multiplayer & Network table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Network_GetUniqueId);
    lua_setfield(L, -2, "get_unique_id");
    lua_pushcfunction(L, Lua_Network_GetUniqueId);
    lua_setfield(L, -2, "GetUniqueId");
    lua_pushcfunction(L, Lua_Network_IsServer);
    lua_setfield(L, -2, "is_server");
    lua_pushcfunction(L, Lua_Network_IsServer);
    lua_setfield(L, -2, "IsServer");
    lua_pushcfunction(L, Lua_Network_SetAuthority);
    lua_setfield(L, -2, "set_multiplayer_authority");
    lua_pushcfunction(L, Lua_Network_SetAuthority);
    lua_setfield(L, -2, "SetMultiplayerAuthority");
    lua_pushcfunction(L, Lua_Network_GetAuthority);
    lua_setfield(L, -2, "get_multiplayer_authority");
    lua_pushcfunction(L, Lua_Network_GetAuthority);
    lua_setfield(L, -2, "GetMultiplayerAuthority");
    lua_pushcfunction(L, Lua_Network_HasAuthority);
    lua_setfield(L, -2, "has_multiplayer_authority");
    lua_pushcfunction(L, Lua_Network_HasAuthority);
    lua_setfield(L, -2, "HasMultiplayerAuthority");
    lua_setglobal(L, "Network");

    // Signal global table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Signal_Emit);
    lua_setfield(L, -2, "Emit");
    lua_pushcfunction(L, Lua_Signal_Emit);
    lua_setfield(L, -2, "emit_signal");
    lua_setglobal(L, "Signal");

    // Resource global table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Resource_GetLoadedCount);
    lua_setfield(L, -2, "GetLoadedCount");
    lua_pushcfunction(L, Lua_Resource_GetMemoryUsage);
    lua_setfield(L, -2, "GetMemoryUsage");
    lua_pushcfunction(L, Lua_Resource_UnloadUnused);
    lua_setfield(L, -2, "UnloadUnused");
    lua_setglobal(L, "Resource");

    // Sky global table (time of day, moon, clouds)
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Sky_SetTime); lua_setfield(L, -2, "SetTime");
    lua_pushcfunction(L, Lua_Sky_GetTime); lua_setfield(L, -2, "GetTime");
    lua_pushcfunction(L, Lua_Sky_SetDayLength); lua_setfield(L, -2, "SetDayLength");
    lua_pushcfunction(L, Lua_Sky_SetMoonPhase); lua_setfield(L, -2, "SetMoonPhase");
    lua_pushcfunction(L, Lua_Sky_SetDate); lua_setfield(L, -2, "SetDate");
    lua_pushcfunction(L, Lua_Sky_GetDate); lua_setfield(L, -2, "GetDate");
    lua_pushcfunction(L, Lua_Sky_SetClouds); lua_setfield(L, -2, "SetClouds");
    lua_pushcfunction(L, Lua_Sky_SetWind); lua_setfield(L, -2, "SetWind");
    lua_setglobal(L, "Sky");

    // Weather global table (presets, rain, snow, wetness, lightning)
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Weather_Set); lua_setfield(L, -2, "Set");
    lua_pushcfunction(L, Lua_Weather_Get); lua_setfield(L, -2, "Get");
    lua_pushcfunction(L, Lua_Weather_SetRain); lua_setfield(L, -2, "SetRain");
    lua_pushcfunction(L, Lua_Weather_SetSnow); lua_setfield(L, -2, "SetSnow");
    lua_pushcfunction(L, Lua_Weather_SetWetness); lua_setfield(L, -2, "SetWetness");
    lua_pushcfunction(L, Lua_Weather_SetSnowCover); lua_setfield(L, -2, "SetSnowCover");
    lua_pushcfunction(L, Lua_Weather_SetLightning); lua_setfield(L, -2, "SetLightning");
    lua_pushcfunction(L, Lua_Weather_SetSnowDepth); lua_setfield(L, -2, "SetSnowDepth");
    lua_pushcfunction(L, Lua_Weather_SetThunderVolume); lua_setfield(L, -2, "SetThunderVolume");
    lua_pushcfunction(L, Lua_Weather_Strike); lua_setfield(L, -2, "Strike");
    lua_pushcfunction(L, Lua_Weather_GetStrikeCount); lua_setfield(L, -2, "GetStrikeCount");
    lua_setglobal(L, "Weather");

    // Terrain global table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Terrain_GetHeight); lua_setfield(L, -2, "GetHeight");
    lua_pushcfunction(L, Lua_Terrain_GetLayer); lua_setfield(L, -2, "GetLayer");
    lua_setglobal(L, "Terrain");

    // Cutscene global table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Cutscene_Play); lua_setfield(L, -2, "Play");
    lua_pushcfunction(L, Lua_Cutscene_Skip); lua_setfield(L, -2, "Skip");
    lua_pushcfunction(L, Lua_Cutscene_Stop); lua_setfield(L, -2, "Stop");
    lua_pushcfunction(L, Lua_Cutscene_IsPlaying); lua_setfield(L, -2, "IsPlaying");
    lua_pushcfunction(L, Lua_Cutscene_GetTime); lua_setfield(L, -2, "GetTime");
    lua_pushcfunction(L, Lua_Cutscene_GetName); lua_setfield(L, -2, "GetName");
    lua_setglobal(L, "Cutscene");

    // Video global table (TV sets, Cinema screens, Mobile phones, UI Video, and Cutaways)
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Video_Play); lua_setfield(L, -2, "Play");
    lua_pushcfunction(L, Lua_Video_Pause); lua_setfield(L, -2, "Pause");
    lua_pushcfunction(L, Lua_Video_Stop); lua_setfield(L, -2, "Stop");
    lua_pushcfunction(L, Lua_Video_SetChannel); lua_setfield(L, -2, "SetChannel");
    lua_pushcfunction(L, Lua_Video_SetSource); lua_setfield(L, -2, "SetSource");
    lua_pushcfunction(L, Lua_Video_Seek); lua_setfield(L, -2, "Seek");
    lua_pushcfunction(L, Lua_Video_IsPlaying); lua_setfield(L, -2, "IsPlaying");
    lua_pushcfunction(L, Lua_Video_PlayCutaway); lua_setfield(L, -2, "PlayCutaway");
    lua_pushcfunction(L, Lua_Video_StopCutaway); lua_setfield(L, -2, "StopCutaway");
    lua_pushcfunction(L, Lua_Video_IsCutawayPlaying); lua_setfield(L, -2, "IsCutawayPlaying");
    lua_setglobal(L, "Video");

    // Game Localization global table
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Loc_Translate); lua_setfield(L, -2, "Translate");
    lua_pushcfunction(L, Lua_Loc_Translate); lua_setfield(L, -2, "Get");
    lua_pushcfunction(L, Lua_Loc_SetLanguage); lua_setfield(L, -2, "SetLanguage");
    lua_pushcfunction(L, Lua_Loc_GetLanguage); lua_setfield(L, -2, "GetLanguage");
    lua_pushcfunction(L, Lua_Loc_GetAvailableLanguages); lua_setfield(L, -2, "GetAvailableLanguages");
    lua_pushcfunction(L, Lua_Loc_GetAvailableLanguages); lua_setfield(L, -2, "GetLanguages");
    lua_setglobal(L, "Localization");

    // Shortcut global TR()
    lua_register(L, "TR", Lua_Loc_Translate);

}

bool LuaScriptSystem::Init() {
    if (m_initialized && L) return true;
    if (L) Shutdown();

    L = luaL_newstate();
    if (!L) {
        std::cerr << "[Lua] Failed to allocate Lua state!" << std::endl;
        return false;
    }

    luaL_openlibs(L);
    RegisterEngineAPI();
    m_instances.clear();
    m_initialized = true;

    std::cout << "[Lua] Lua 5.4 scripting runtime initialized successfully." << std::endl;
    return true;
}

void LuaScriptSystem::Shutdown() {
    if (L) {
        for (auto& [id, inst] : m_instances) {
            if (inst.hasDestroy && inst.envRef != -1) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, inst.envRef);
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "OnDestroy");
                    if (lua_isfunction(L, -1)) {
                        lua_getfield(L, -2, "self");
                        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                            lua_pop(L, 1);
                        }
                    } else {
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            if (inst.envRef != -1) {
                luaL_unref(L, LUA_REGISTRYINDEX, inst.envRef);
                inst.envRef = -1;
            }
        }
        m_instances.clear();
        lua_close(L);
        L = nullptr;
    }
    m_initialized = false;
    m_currentScene = nullptr;
}

void LuaScriptSystem::DestroyNodeScript(uint32_t nodeId) {
    auto it = m_instances.find(nodeId);
    if (it != m_instances.end()) {
        auto& inst = it->second;
        if (L && inst.hasDestroy && inst.envRef != -1) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, inst.envRef);
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "OnDestroy");
                if (lua_isfunction(L, -1)) {
                    lua_getfield(L, -2, "self");
                    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
        if (L && inst.envRef != -1) {
            luaL_unref(L, LUA_REGISTRYINDEX, inst.envRef);
        }
        m_instances.erase(it);
    }
}

static std::string ResolveScriptPath(const std::string& path, const std::string& projectRoot = "") {
    if (path.empty()) return "";
    namespace fs = std::filesystem;
    if (fs::exists(path)) return path;
    if (!projectRoot.empty()) {
        fs::path pRoot(projectRoot);
        if (fs::exists(pRoot / path)) return (pRoot / path).string();
        if (fs::exists(pRoot / "scripts" / path)) return (pRoot / "scripts" / path).string();
    }
    if (fs::exists("scripts/" + path)) return "scripts/" + path;
    if (fs::exists("projects/Headland/" + path)) return "projects/Headland/" + path;
    if (fs::exists("projects/Headland/scripts/" + path)) return "projects/Headland/scripts/" + path;
    return "";
}

void LuaScriptSystem::InitNodeScript(Node& node, Scene& scene) {
    if (node.scriptPath.empty()) return;
    if (!L) Init();
    if (!L) return;

    std::string resolved = ResolveScriptPath(node.scriptPath, m_projectRoot);
    if (resolved.empty()) {
        std::cerr << "[Lua Error] Could not find script file: " << node.scriptPath << " for node " << node.name << std::endl;
        return;
    }

    m_currentScene = &scene;

    // Clean up any existing script instance for this node
    DestroyNodeScript(node.id);

    // 1. Compile script file chunk onto stack (-1)
    if (luaL_loadfile(L, resolved.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::cerr << "[Lua Compile Error in " << resolved << " for Node '" << node.name << "']: "
                  << (err ? err : "unknown error") << std::endl;
        lua_pop(L, 1);
        return;
    }

    // 2. Create isolated instance environment table (instEnv)
    lua_newtable(L); // instEnv at index -1, chunk is at -2

    // Set metatable with __index = _G so global engine libraries (Input, NodeAPI, Scene, print, math, etc.) are accessible
    lua_newtable(L);        // metatable at index -1
    lua_pushglobaltable(L); // _G at index -1
    lua_setfield(L, -2, "__index"); // metatable.__index = _G
    lua_setmetatable(L, -2); // setmetatable(instEnv, metatable)

    // 3. Create persistent `self` table inside instEnv
    PushNodeTable(L, node); // self table at -1
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "self"); // instEnv.self = self
    lua_pop(L, 1); // pop self

    // 4. Set instEnv as upvalue 1 (_ENV) of the compiled chunk
    lua_pushvalue(L, -1); // copy instEnv to top
    // chunk is at -3, instEnv is at -2, copyOfInstEnv is at -1
    lua_setupvalue(L, -3, 1); // binds chunk's _ENV to instEnv and pops copyOfInstEnv!

    // 5. Store instEnv in Lua registry for this node
    int envRef = luaL_ref(L, LUA_REGISTRYINDEX); // pops instEnv!
    // Now chunk is at index -1

    // 6. Execute the chunk in its sandboxed environment
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::cerr << "[Lua Execution Error in " << resolved << " for Node '" << node.name << "']: "
                  << (err ? err : "unknown error") << std::endl;
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, envRef);
        return;
    }

    // 7. Check for lifecycle functions inside instEnv
    lua_rawgeti(L, LUA_REGISTRYINDEX, envRef); // push instEnv

    lua_getfield(L, -1, "OnUpdate");
    bool hasUpdate = lua_isfunction(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, -1, "OnInit");
    bool hasInit = lua_isfunction(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, -1, "OnDestroy");
    bool hasDestroy = lua_isfunction(L, -1);
    lua_pop(L, 1);

    // Call OnInit(self) if present
    if (hasInit) {
        lua_getfield(L, -1, "OnInit");
        lua_getfield(L, -2, "self");
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            std::cerr << "[Lua Error in OnInit for Node '" << node.name << "']: "
                      << (err ? err : "unknown error") << std::endl;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1); // pop instEnv

    // Register active instance
    NodeScriptInstance inst;
    inst.nodeId = node.id;
    inst.scriptPath = node.scriptPath;
    inst.envRef = envRef;
    inst.hasUpdate = hasUpdate;
    inst.hasInit = hasInit;
    inst.hasDestroy = hasDestroy;
    inst.hasError = false;
    m_instances[node.id] = inst;
}

void LuaScriptSystem::UpdateNodeScript(Node& node, Scene& scene, float dt) {
    if (node.scriptPath.empty() || !L) return;

    auto it = m_instances.find(node.id);
    if (it == m_instances.end()) {
        InitNodeScript(node, scene);
        it = m_instances.find(node.id);
        if (it == m_instances.end()) return;
    }

    auto& inst = it->second;
    if (!inst.hasUpdate || inst.hasError || inst.envRef == -1) return;

    m_currentScene = &scene;

    // Push instEnv
    lua_rawgeti(L, LUA_REGISTRYINDEX, inst.envRef);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    // Get self table
    lua_getfield(L, -1, "self");
    if (lua_istable(L, -1)) {
        int selfIdx = lua_gettop(L);
        SyncSelfTable(L, selfIdx, node);

        // Get OnUpdate function from this node's isolated environment
        lua_getfield(L, -2, "OnUpdate");
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, selfIdx); // arg 1: self
            lua_pushnumber(L, dt);     // arg 2: dt

            if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                const char* err = lua_tostring(L, -1);
                std::cerr << "[Lua Runtime Error in OnUpdate for Node '" << node.name << "' (ID " << node.id << ")]: "
                          << (err ? err : "unknown error") << std::endl;
                std::cerr << "--> Script execution paused for this node to prevent error spam." << std::endl;
                inst.hasError = true;
                lua_pop(L, 1); // pop error
            } else {
                // If the function returned a table, update from it (legacy compatibility)
                if (lua_istable(L, -1)) {
                    UpdateNodeFromTable(L, -1, node);
                } else {
                    // Sync back from self directly
                    SyncNodeFromSelf(L, selfIdx, node);
                }
                lua_pop(L, 1); // pop return value
            }
        } else {
            lua_pop(L, 1); // pop non-function
        }
        lua_pop(L, 1); // pop self
    } else {
        lua_pop(L, 1); // pop non-table self
    }

    lua_pop(L, 1); // pop instEnv
}

void LuaScriptSystem::CollisionNodeScript(Node& node, Scene& scene, uint32_t otherId) {
    if (node.scriptPath.empty() || !L) return;
    auto it = m_instances.find(node.id);
    if (it == m_instances.end() || it->second.hasError || it->second.envRef == -1) return;
    m_currentScene = &scene;

    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.envRef);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, "OnCollision");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2); // non-function, instEnv
        return;
    }
    lua_getfield(L, -2, "self");
    int selfIdx = lua_gettop(L);
    if (lua_istable(L, selfIdx)) SyncSelfTable(L, selfIdx, node);
    lua_pushinteger(L, static_cast<lua_Integer>(otherId));
    // Stack: instEnv, OnCollision, self, otherId
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::cerr << "[Lua Runtime Error in OnCollision for Node '" << node.name << "' (ID " << node.id << ")]: "
                  << (err ? err : "unknown error") << std::endl;
        it->second.hasError = true;
        lua_pop(L, 1); // error
    } else {
        lua_getfield(L, -1, "self");
        if (lua_istable(L, -1)) SyncNodeFromSelf(L, lua_gettop(L), node);
        lua_pop(L, 1); // self
    }
    lua_pop(L, 1); // instEnv
}

void LuaScriptSystem::CallFunction(const std::string& functionName) {
    if (!L || functionName.empty()) return;
    lua_getglobal(L, functionName.c_str());
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            std::cerr << "[Lua Error calling " << functionName << "]: " << (err ? err : "unknown error") << std::endl;
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}

} // namespace Scripting
