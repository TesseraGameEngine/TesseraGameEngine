#include "InputManager.h"
#include <iostream>
#include <fstream>
#include <algorithm>

namespace Core {

static std::string FormatKeyName(ImGuiKey key) {
    const char* rawName = ImGui::GetKeyName(key);
    if (!rawName || rawName[0] == '\0') {
        return "Unknown Key";
    }

    std::string s(rawName);
    // Format camel-case names nicely
    if (s == "UpArrow") return "Up Arrow";
    if (s == "DownArrow") return "Down Arrow";
    if (s == "LeftArrow") return "Left Arrow";
    if (s == "RightArrow") return "Right Arrow";
    if (s == "LeftShift") return "Left Shift";
    if (s == "RightShift") return "Right Shift";
    if (s == "LeftCtrl") return "Left Ctrl";
    if (s == "RightCtrl") return "Right Ctrl";
    if (s == "LeftAlt") return "Left Alt";
    if (s == "RightAlt") return "Right Alt";
    if (s == "LeftSuper") return "Left Windows";
    if (s == "RightSuper") return "Right Windows";
    if (s == "KeypadDecimal") return "Numpad .";
    if (s == "KeypadDivide") return "Numpad /";
    if (s == "KeypadMultiply") return "Numpad *";
    if (s == "KeypadSubtract") return "Numpad -";
    if (s == "KeypadAdd") return "Numpad +";
    if (s == "KeypadEnter") return "Numpad Enter";

    return s;
}

std::string InputButton::GetName() const {
    if (device == InputDevice::Keyboard) {
        if (code == 0) return "None";
        return FormatKeyName(static_cast<ImGuiKey>(code));
    } else if (device == InputDevice::Mouse) {
        if (code == 0) return "Left Mouse Button";
        if (code == 1) return "Right Mouse Button";
        if (code == 2) return "Middle Mouse Button";
        if (code == 3) return "Mouse Button 4";
        if (code == 4) return "Mouse Button 5";
        return "Mouse Button " + std::to_string(code);
    }
    return "None";
}

bool InputButton::IsDown() const {
    if (ImGui::GetCurrentContext() == nullptr) return false;
    if (device == InputDevice::Keyboard && code != 0) {
        return ImGui::IsKeyDown(static_cast<ImGuiKey>(code));
    } else if (device == InputDevice::Mouse) {
        return ImGui::IsMouseDown(static_cast<ImGuiMouseButton>(code));
    }
    return false;
}

bool InputButton::IsPressed() const {
    if (ImGui::GetCurrentContext() == nullptr) return false;
    if (device == InputDevice::Keyboard && code != 0) {
        return ImGui::IsKeyPressed(static_cast<ImGuiKey>(code), false);
    } else if (device == InputDevice::Mouse) {
        return ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(code));
    }
    return false;
}

bool InputButton::IsReleased() const {
    if (ImGui::GetCurrentContext() == nullptr) return false;
    if (device == InputDevice::Keyboard && code != 0) {
        return ImGui::IsKeyReleased(static_cast<ImGuiKey>(code));
    } else if (device == InputDevice::Mouse) {
        return ImGui::IsMouseReleased(static_cast<ImGuiMouseButton>(code));
    }
    return false;
}

JsonValue InputButton::Serialize() const {
    JsonValue obj = JsonValue::Object();
    std::string devStr = "None";
    if (device == InputDevice::Keyboard) devStr = "Keyboard";
    else if (device == InputDevice::Mouse) devStr = "Mouse";

    obj["device"] = devStr;
    obj["code"] = code;
    return obj;
}

InputButton InputButton::Deserialize(const JsonValue& json) {
    InputButton btn;
    if (!json.IsObject()) return btn;

    std::string dev = json["device"].AsString("None");
    if (dev == "Keyboard") btn.device = InputDevice::Keyboard;
    else if (dev == "Mouse") btn.device = InputDevice::Mouse;
    else btn.device = InputDevice::None;

    btn.code = json["code"].AsInt(0);
    return btn;
}

std::string InputAction::GetSummaryString() const {
    std::string s = primary.GetName();
    if (secondary.IsValid()) {
        s += " / " + secondary.GetName();
    }
    return s;
}

JsonValue InputAction::Serialize() const {
    JsonValue obj = JsonValue::Object();
    obj["actionId"] = actionId;
    obj["displayName"] = displayName;
    obj["primary"] = primary.Serialize();
    obj["secondary"] = secondary.Serialize();
    return obj;
}

InputAction InputAction::Deserialize(const JsonValue& json) {
    InputAction act;
    if (!json.IsObject()) return act;

    act.actionId = json["actionId"].AsString("");
    act.displayName = json["displayName"].AsString("");
    act.primary = InputButton::Deserialize(json["primary"]);
    act.secondary = InputButton::Deserialize(json["secondary"]);
    return act;
}

InputManager::InputManager() {
    ResetToDefaults();
}

void InputManager::ResetToDefaults() {
    m_actions.clear();
    m_actionMap.clear();

    // Locomotion
    RegisterAction("MoveForward", "Move Forward",
        InputButton{InputDevice::Keyboard, ImGuiKey_W},
        InputButton{InputDevice::Keyboard, ImGuiKey_UpArrow});

    RegisterAction("MoveBackward", "Move Backward",
        InputButton{InputDevice::Keyboard, ImGuiKey_S},
        InputButton{InputDevice::Keyboard, ImGuiKey_DownArrow});

    RegisterAction("MoveLeft", "Move Left",
        InputButton{InputDevice::Keyboard, ImGuiKey_A},
        InputButton{InputDevice::Keyboard, ImGuiKey_LeftArrow});

    RegisterAction("MoveRight", "Move Right",
        InputButton{InputDevice::Keyboard, ImGuiKey_D},
        InputButton{InputDevice::Keyboard, ImGuiKey_RightArrow});

    // Modifiers & Mobility
    RegisterAction("Jump", "Jump",
        InputButton{InputDevice::Keyboard, ImGuiKey_Space},
        InputButton{InputDevice::None, 0});

    RegisterAction("Run", "Run / Sprint",
        InputButton{InputDevice::Keyboard, ImGuiKey_LeftShift},
        InputButton{InputDevice::Keyboard, ImGuiKey_R}); // Developers can use R for run or Shift!

    // Actions & Interaction
    RegisterAction("Interact", "Interact / Pick Up",
        InputButton{InputDevice::Keyboard, ImGuiKey_E},
        InputButton{InputDevice::None, 0});

    RegisterAction("Attack", "Primary Action / Attack",
        InputButton{InputDevice::Mouse, 0}, // Left Mouse Button
        InputButton{InputDevice::Keyboard, ImGuiKey_F});

    RegisterAction("SecondaryAction", "Secondary Action / Aim",
        InputButton{InputDevice::Mouse, 1}, // Right Mouse Button
        InputButton{InputDevice::Keyboard, ImGuiKey_Q});

    RebuildIndexMap();
}

void InputManager::RebuildIndexMap() {
    m_actionMap.clear();
    for (size_t i = 0; i < m_actions.size(); ++i) {
        m_actionMap[m_actions[i].actionId] = i;
    }
}

InputAction* InputManager::FindAction(const std::string& actionId) {
    auto it = m_actionMap.find(actionId);
    if (it != m_actionMap.end() && it->second < m_actions.size()) {
        return &m_actions[it->second];
    }
    return nullptr;
}

const InputAction* InputManager::FindAction(const std::string& actionId) const {
    auto it = m_actionMap.find(actionId);
    if (it != m_actionMap.end() && it->second < m_actions.size()) {
        return &m_actions[it->second];
    }
    return nullptr;
}

bool InputManager::IsActionDown(const std::string& actionId) const {
    const auto* act = FindAction(actionId);
    return act ? act->IsDown() : false;
}

bool InputManager::IsActionPressed(const std::string& actionId) const {
    const auto* act = FindAction(actionId);
    return act ? act->IsPressed() : false;
}

bool InputManager::IsActionReleased(const std::string& actionId) const {
    const auto* act = FindAction(actionId);
    return act ? act->IsReleased() : false;
}

void InputManager::RegisterAction(const std::string& actionId, const std::string& displayName,
                                  const InputButton& primary, const InputButton& secondary) {
    auto* existing = FindAction(actionId);
    if (existing) {
        existing->displayName = displayName;
        existing->primary = primary;
        existing->secondary = secondary;
        return;
    }

    InputAction act;
    act.actionId = actionId;
    act.displayName = displayName;
    act.primary = primary;
    act.secondary = secondary;

    m_actions.push_back(act);
    m_actionMap[actionId] = m_actions.size() - 1;
}

void InputManager::SetPrimaryBinding(const std::string& actionId, const InputButton& button) {
    auto* act = FindAction(actionId);
    if (act) {
        act->primary = button;
    }
}

void InputManager::SetSecondaryBinding(const std::string& actionId, const InputButton& button) {
    auto* act = FindAction(actionId);
    if (act) {
        act->secondary = button;
    }
}

bool InputManager::RemoveAction(const std::string& actionId) {
    auto it = m_actionMap.find(actionId);
    if (it == m_actionMap.end() || it->second >= m_actions.size()) return false;

    m_actions.erase(m_actions.begin() + it->second);
    RebuildIndexMap();
    return true;
}

std::string InputManager::GetActionBindingString(const std::string& actionId) const {
    const auto* act = FindAction(actionId);
    if (!act) return "Unbound";
    return act->GetSummaryString();
}

JsonValue InputManager::Serialize() const {
    JsonValue arr = JsonValue::Array();
    for (const auto& a : m_actions) {
        arr.Push(a.Serialize());
    }
    return arr;
}

bool InputManager::Deserialize(const JsonValue& json) {
    if (!json.IsArray()) return false;

    m_actions.clear();
    m_actionMap.clear();
    for (const auto& item : json.arrVal) {
        InputAction act = InputAction::Deserialize(item);
        if (!act.actionId.empty()) {
            m_actions.push_back(act);
        }
    }
    RebuildIndexMap();
    return true;
}

bool InputManager::SaveToFile(const std::string& filePath) const {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;

    JsonValue json = Serialize();
    file << json.Serialize(2);
    file.close();
    std::cout << "[InputManager] Saved input key bindings to: " << filePath << std::endl;
    return true;
}

bool InputManager::LoadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    JsonValue root = JsonValue::Parse(buffer.str());
    bool ok = Deserialize(root);
    if (ok) {
        std::cout << "[InputManager] Loaded custom input key bindings from: " << filePath << std::endl;
    }
    return ok;
}

bool InputManager::PollAnyInput(InputButton& outButton, bool allowEscToCancel) {
    // 1. Check keyboard keys
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        ImGuiKey imKey = static_cast<ImGuiKey>(key);
        if (ImGui::IsKeyPressed(imKey, false)) {
            if (allowEscToCancel && imKey == ImGuiKey_Escape) {
                return false; // User cancelled
            }
            if (imKey == ImGuiKey_Delete || imKey == ImGuiKey_Backspace) {
                outButton.Clear(); // Cleared to None
                return true;
            }
            outButton.device = InputDevice::Keyboard;
            outButton.code = key;
            return true;
        }
    }

    // 2. Check mouse buttons (Left = 0, Right = 1, Middle = 2, Button4 = 3, Button5 = 4)
    for (int btn = 0; btn < ImGuiMouseButton_COUNT; ++btn) {
        if (ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(btn))) {
            outButton.device = InputDevice::Mouse;
            outButton.code = btn;
            return true;
        }
    }

    return false;
}

} // namespace Core
