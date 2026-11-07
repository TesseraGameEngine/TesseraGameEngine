#include "NodeInspector.h"
#include "UINodeRenderer.h"
#include "Core/UndoRedoManager.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <vector>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

void NodeInspector::RenderTransform(Scene& scene, const std::shared_ptr<Node>& node) {
    if (!node) return;

    // Quick History & Clipboard Toolbar (Wraps cleanly if inspector is narrow)
    auto sameLineIfRoom = [](float reqW) {
        if (ImGui::GetContentRegionAvail().x >= reqW) {
            ImGui::SameLine();
        }
    };

    ImGui::BeginDisabled(!Core::UndoRedoManager::Get().CanUndo());
    if (ImGui::SmallButton("Undo")) {
        Core::UndoRedoManager::Get().Undo(scene);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && Core::UndoRedoManager::Get().CanUndo()) {
        ImGui::SetTooltip("Undo: %s (Ctrl+Z)", Core::UndoRedoManager::Get().GetUndoDescription().c_str());
    }

    sameLineIfRoom(48.0f);
    ImGui::BeginDisabled(!Core::UndoRedoManager::Get().CanRedo());
    if (ImGui::SmallButton("Redo")) {
        Core::UndoRedoManager::Get().Redo(scene);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && Core::UndoRedoManager::Get().CanRedo()) {
        ImGui::SetTooltip("Redo: %s (Ctrl+Y)", Core::UndoRedoManager::Get().GetRedoDescription().c_str());
    }

    sameLineIfRoom(48.0f);
    if (ImGui::SmallButton("Copy")) {
        scene.CopyNode(node->id);
    }

    sameLineIfRoom(52.0f);
    ImGui::BeginDisabled(!scene.HasClipboardData());
    if (ImGui::SmallButton("Paste")) {
        JsonValue before = scene.Serialize();
        int32_t beforeSel = scene.GetSelectedId();
        scene.PasteNode();
        Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Paste Node");
    }
    ImGui::EndDisabled();

    sameLineIfRoom(75.0f);
    if (ImGui::SmallButton("Duplicate")) {
        JsonValue before = scene.Serialize();
        int32_t beforeSel = scene.GetSelectedId();
        scene.DuplicateNode(node->id);
        Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Duplicate Node");
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        static glm::vec3 s_dragStartPos{0.0f}, s_dragStartRot{0.0f}, s_dragStartScale{1.0f};
        static uint32_t s_dragNodeId = 0;

        auto recordTransformIfFinished = [&]() {
            if (ImGui::IsItemActivated()) {
                s_dragNodeId = node->id;
                s_dragStartPos = node->position;
                s_dragStartRot = node->rotation;
                s_dragStartScale = node->scale;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && s_dragNodeId == node->id) {
                auto cmd = std::make_shared<Core::TransformCommand>(
                    node->id,
                    s_dragStartPos, s_dragStartRot, s_dragStartScale,
                    node->position, node->rotation, node->scale,
                    "Transform " + node->name
                );
                Core::UndoRedoManager::Get().PushCommand(cmd);
            }
        };

        ImGui::Text("Local Position:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat3("##Position", glm::value_ptr(node->position), 0.05f);
        recordTransformIfFinished();

        ImGui::Text("Local Rotation (Degrees):");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat3("##Rotation", glm::value_ptr(node->rotation), 0.5f);
        recordTransformIfFinished();

        if (node->nodeType != NodeType::Camera && node->nodeType != NodeType::Light) {
            ImGui::Text("Local Scale:");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat3("##Scale", glm::value_ptr(node->scale), 0.05f, 0.001f, 100.0f);
            recordTransformIfFinished();
        }

        glm::vec3 worldPos = node->GetWorldPosition(scene);
        ImGui::Spacing();
        ImGui::TextWrapped("World Position: (%.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z);

        ImGui::Spacing();
        if (ImGui::Button("Reset Transform")) {
            auto cmd = std::make_shared<Core::TransformCommand>(
                node->id,
                node->position, node->rotation, node->scale,
                glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f),
                "Reset Transform " + node->name
            );
            Core::UndoRedoManager::Get().ExecuteCommand(cmd, scene);
        }
    }
}

void NodeInspector::RenderUIProperties(Scene& scene, const std::shared_ptr<Node>& node, NodeInspectorState& state) {
    if (!node || !node->IsUINode()) return;
    if (ImGui::CollapsingHeader("2D UI Component Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Visible", &node->uiVisible);
        ImGui::SameLine();
        ImGui::Checkbox("Enabled", &node->uiEnabled);

        const char* anchorNames[] = {
            "Top-Left", "Top-Center", "Top-Right",
            "Center-Left", "Center", "Center-Right",
            "Bottom-Left", "Bottom-Center", "Bottom-Right"
        };
        int curAnchor = static_cast<int>(node->uiAnchor);
        ImGui::Text("Anchor Preset:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##AnchorPreset", &curAnchor, anchorNames, IM_ARRAYSIZE(anchorNames))) {
            node->uiAnchor = static_cast<UIAnchor>(curAnchor);
        }

        ImGui::Text("Canvas Offset (X, Y):");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat2("##CanvasOffset", glm::value_ptr(node->uiPosition), 1.0f);

        ImGui::Text("Size (W, H):");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat2("##UISize", glm::value_ptr(node->uiSize), 1.0f, 1.0f, 4000.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Custom TrueType Font (.ttf):");
        char fontBuf[512];
        std::strncpy(fontBuf, node->uiFontPath.c_str(), sizeof(fontBuf));
        fontBuf[sizeof(fontBuf) - 1] = '\0';
        float availFontW = ImGui::GetContentRegionAvail().x;
        if (availFontW < 220.0f) {
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##UIFontPath", fontBuf, sizeof(fontBuf))) {
                node->uiFontPath = fontBuf;
                if (!node->uiFontPath.empty()) {
                    UINodeRenderer::LoadCustomTTFFont(node->uiFontPath, node->uiFontSize);
                }
            }
            if (ImGui::Button("Browse .ttf", ImVec2(-1.0f, 0.0f))) {
#ifdef _WIN32
                OPENFILENAMEA ofn;
                char szFile[MAX_PATH] = "";
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "TrueType Fonts (*.ttf)\0*.ttf\0All Files (*.*)\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrTitle = "Select Custom TrueType Font (.ttf)";
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameA(&ofn) == TRUE) {
                    node->uiFontPath = ofn.lpstrFile;
                    std::replace(node->uiFontPath.begin(), node->uiFontPath.end(), '\\', '/');
                    UINodeRenderer::LoadCustomTTFFont(node->uiFontPath, node->uiFontSize);
                }
#endif
            }
        } else {
            ImGui::SetNextItemWidth(availFontW - 95.0f);
            if (ImGui::InputText("##UIFontPath", fontBuf, sizeof(fontBuf))) {
                node->uiFontPath = fontBuf;
                if (!node->uiFontPath.empty()) {
                    UINodeRenderer::LoadCustomTTFFont(node->uiFontPath, node->uiFontSize);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse .ttf", ImVec2(90.0f, 0.0f))) {
#ifdef _WIN32
                OPENFILENAMEA ofn;
                char szFile[MAX_PATH] = "";
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "TrueType Fonts (*.ttf)\0*.ttf\0All Files (*.*)\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrTitle = "Select Custom TrueType Font (.ttf)";
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameA(&ofn) == TRUE) {
                    node->uiFontPath = ofn.lpstrFile;
                    std::replace(node->uiFontPath.begin(), node->uiFontPath.end(), '\\', '/');
                    UINodeRenderer::LoadCustomTTFFont(node->uiFontPath, node->uiFontSize);
                }
#endif
            }
        }
        ImGui::Text("Font Size (px):");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##UIFontSize", &node->uiFontSize, 8.0f, 72.0f, "%.1f px")) {
            if (!node->uiFontPath.empty()) {
                UINodeRenderer::LoadCustomTTFFont(node->uiFontPath, node->uiFontSize);
            }
        }
        if (!node->uiFontPath.empty()) {
            TTFFontInfo fInfo = UINodeRenderer::GetLoadedFontInfo(node->uiFontPath);
            if (fInfo.valid) {
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f),
                    "Loaded TTF: %.1fpx | Ascent: %d | Descent: %d",
                    fInfo.requestedSizePx, (int)fInfo.ascender, (int)fInfo.descender);
            }
        }
        ImGui::Separator();
        ImGui::Spacing();

        node->RenderInspectorProperties(scene, state);
    }
}

void NodeInspector::RenderColliderProperties(const std::shared_ptr<Node>& node) {
    if (node) {
        NodeInspectorState dummyState;
        node->RenderInspectorProperties(*static_cast<Scene*>(nullptr), dummyState);
    }
}

void NodeInspector::RenderCameraProperties(Scene& scene, const std::shared_ptr<Node>& node) {
    if (node) {
        NodeInspectorState dummyState;
        node->RenderInspectorProperties(scene, dummyState);
    }
}

void NodeInspector::RenderLightProperties(const std::shared_ptr<Node>& node) {
    if (node) {
        NodeInspectorState dummyState;
        node->RenderInspectorProperties(*static_cast<Scene*>(nullptr), dummyState);
    }
}

void NodeInspector::RenderEmptyGroupProperties(Scene& scene, const std::shared_ptr<Node>& node) {
    if (node) {
        NodeInspectorState dummyState;
        node->RenderInspectorProperties(scene, dummyState);
    }
}
