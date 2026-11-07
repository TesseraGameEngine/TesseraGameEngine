#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <imgui.h>
#include <glm/glm.hpp>

class Scene;
struct Node;

namespace Gameplay {
    class GameLoop;
}

struct TTFFontInfo {
    bool valid{false};
    std::string filePath{""};
    std::string familyName{"DefaultFont"};
    uint16_t unitsPerEm{1000};
    int16_t ascender{800};
    int16_t descender{-200};
    uint16_t numGlyphs{256};
    float requestedSizePx{16.0f};
    ImFont* imguiFont{nullptr};
};

struct UINodeRendererState {
    uint32_t activeDropdownNodeId{0};
    uint32_t activeColorPickerNodeId{0};
    uint32_t activeDatePickerNodeId{0};
    uint32_t focusedTextNodeId{0};
    uint32_t activeMenuBarNodeId{0};
    int activeMenuIndex{-1};
    int calendarYear{2026};
    int calendarMonth{10};
    bool dropdownConsumedClick{false};
    uint32_t draggingWindowId{0};
    bool isDraggingUINode{false};

    // Hex direct input state for color picker
    bool isEditingPickerHex{false};
    char pickerHexBuf[32]{"#FFFFFF"};
    uint32_t lastHexNodeId{0};

    void ResetPopups() {
        activeDropdownNodeId = 0;
        activeColorPickerNodeId = 0;
        activeDatePickerNodeId = 0;
        draggingWindowId = 0;
        dropdownConsumedClick = false;
    }
};

class UINodeRenderer {
public:
    static void ComputeUIRect(const Scene& scene, const std::shared_ptr<Node>& uiNode,
                              ImVec2 canvasPos, float w, float h,
                              ImVec2& anchorOrigin, ImVec2& rectMin, ImVec2& rectMax);

    static void Render(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize,
                       bool isEditorMode, Gameplay::GameLoop* gameLoop,
                       UINodeRendererState& state, uint32_t selectedId = 0);

    // Custom TrueType (.ttf) Font Upload & Inspection API
    static bool LoadCustomTTFFont(const std::string& filePath, float sizePx = 16.0f);
    static bool LoadCustomTTFFromMemory(const std::string& virtualPath, const std::vector<uint8_t>& ttfBytes, float sizePx = 16.0f);
    static TTFFontInfo GetLoadedFontInfo(const std::string& filePath);
    static std::vector<uint8_t> GenerateMinimalValidTTF(const std::string& familyName = "CustomGameFont",
                                                        uint16_t unitsPerEm = 2048,
                                                        uint16_t numGlyphs = 128);
};
