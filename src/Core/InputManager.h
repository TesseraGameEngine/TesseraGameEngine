#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <imgui.h>
#include "Utils/SimpleJson.h"

namespace Core {

enum class InputDevice {
    None,
    Keyboard,
    Mouse
};

struct InputButton {
    InputDevice device{InputDevice::None};
    int code{0}; // For Keyboard: ImGuiKey. For Mouse: 0=Left, 1=Right, 2=Middle, 3=X1, 4=X2.

    std::string GetName() const;
    bool IsDown() const;
    bool IsPressed() const;
    bool IsReleased() const;

    bool IsValid() const { return device != InputDevice::None && (device != InputDevice::Keyboard || code != 0); }
    void Clear() { device = InputDevice::None; code = 0; }

    bool operator==(const InputButton& other) const {
        return device == other.device && code == other.code;
    }
    bool operator!=(const InputButton& other) const {
        return !(*this == other);
    }

    JsonValue Serialize() const;
    static InputButton Deserialize(const JsonValue& json);
};

struct InputAction {
    std::string actionId;       // Unique ID, e.g. "MoveForward", "Run", "Jump", "Attack"
    std::string displayName;    // Human readable, e.g. "Move Forward"
    InputButton primary;
    InputButton secondary;

    bool IsDown() const {
        return primary.IsDown() || secondary.IsDown();
    }
    bool IsPressed() const {
        return primary.IsPressed() || secondary.IsPressed();
    }
    bool IsReleased() const {
        return primary.IsReleased() || secondary.IsReleased();
    }

    std::string GetSummaryString() const;

    JsonValue Serialize() const;
    static InputAction Deserialize(const JsonValue& json);
};

class InputManager {
public:
    static InputManager& Get() {
        static InputManager instance;
        return instance;
    }

    InputManager();
    ~InputManager() = default;

    void ResetToDefaults();

    // Query action states
    bool IsActionDown(const std::string& actionId) const;
    bool IsActionPressed(const std::string& actionId) const;
    bool IsActionReleased(const std::string& actionId) const;

    // Action management
    const std::vector<InputAction>& GetActions() const { return m_actions; }
    std::vector<InputAction>& GetActions() { return m_actions; }

    InputAction* FindAction(const std::string& actionId);
    const InputAction* FindAction(const std::string& actionId) const;

    void RegisterAction(const std::string& actionId, const std::string& displayName,
                        const InputButton& primary, const InputButton& secondary = InputButton{});

    void SetPrimaryBinding(const std::string& actionId, const InputButton& button);
    void SetSecondaryBinding(const std::string& actionId, const InputButton& button);

    bool RemoveAction(const std::string& actionId);

    std::string GetActionBindingString(const std::string& actionId) const;

    // Serialization
    JsonValue Serialize() const;
    bool Deserialize(const JsonValue& json);

    bool SaveToFile(const std::string& filePath) const;
    bool LoadFromFile(const std::string& filePath);

    // Helpers to detect any key or mouse press for interactive rebinding
    static bool PollAnyInput(InputButton& outButton, bool allowEscToCancel = true);

private:
    std::vector<InputAction> m_actions;
    std::map<std::string, size_t> m_actionMap;

    void RebuildIndexMap();
};

} // namespace Core

