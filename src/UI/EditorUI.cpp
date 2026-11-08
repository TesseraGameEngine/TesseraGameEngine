#include "../Standalone/BundleFormat.h"
#include <GLFW/glfw3.h>
#include "Core/InputManager.h"
#include "Core/UndoRedoManager.h"
#include "EditorUI.h"
#include "Scene/Nodes/3D/CharacterNode.h"
#include "Scene/Nodes/AllNodes.h"
#include "Audio/AudioEngine.h"
#include "Renderer/RenderEffects.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/Collision.h"
#include "Project/Project.h"
#include "Scripting/LuaScriptSystem.h"
#include "Monitoring/GameProfiler.h"
#include "Renderer/VulkanGpuProfiler.h"
#include "Export/WebExporter.h"
#include "Export/LinuxExporter.h"
#include "Video/VideoPlayer.h"
#include "Renderer/RayTracing/RayTracingManager.h"
#include "Renderer/Meshlet/MeshletManager.h"
#include "Renderer/Pipeline/VulkanPipelineCache.h"
#include "Renderer/Upscaling/TemporalUpscaler.h"
#include "Core/LargeWorldCoordinates.h"
#include "Streaming/WorldPartitionManager.h"
#include "Animation/MotionMatching.h"
#include "Physics/XPBDClothSim.h"
#include "Audio/AcousticRayTracer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

void EditorUI::Init() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Setup modern dark theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.16f, 0.16f, 0.19f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.26f, 0.26f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.26f, 0.26f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.32f, 0.32f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.40f, 0.40f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.35f, 0.65f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.35f, 0.65f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.45f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.38f, 0.38f, 0.48f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.28f, 0.32f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.35f, 0.40f, 0.52f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.35f, 0.65f, 0.95f, 1.00f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.45f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.35f, 0.65f, 0.95f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.45f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.28f, 0.32f, 0.42f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.24f, 0.28f, 0.36f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_DockingPreview]        = ImVec4(0.35f, 0.65f, 0.95f, 0.40f);
    colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

    GameLocalization::Get().Init(m_projectRoot);
}

void EditorUI::Render(Scene& scene, Camera& camera, Graphics::IRenderTarget& offscreen,
                     Graphics::IRenderTarget& cameraPreviewOffscreen, Gameplay::GameLoop& gameLoop) {
    // 1. Render custom top bar for ALL startup states
    RenderCustomTitleBar(scene, camera, gameLoop);

    if (m_startupState == StartupState::Splash) {
        RenderSplashScreen(ImGui::GetIO().DeltaTime);
        return;
    }

    if (m_startupState == StartupState::ProjectLauncher) {
        RenderProjectLauncher(scene, camera);
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float customTitleBarHeight = 32.0f;
    ImVec2 dockPos = ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + customTitleBarHeight);
    ImVec2 dockSize = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - customTitleBarHeight);

    ImGui::SetNextWindowPos(dockPos);
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("EngineDockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    static bool firstLayoutInit = true;
    if (firstLayoutInit || m_resetDockLayoutRequested) {
        firstLayoutInit = false;
        m_resetDockLayoutRequested = false;
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        if (node != nullptr) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
        }
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, dockSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.25f, nullptr, &dockMain);
        ImGuiID dockLeftBottom = ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.55f, nullptr, &dockLeft);
        ImGuiID dockBottomRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.32f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockLeftBottom);
        ImGui::DockBuilderDockWindow("Project Files", dockBottomRight);
        ImGui::DockBuilderDockWindow("Camera Preview", dockBottomRight);
        ImGui::DockBuilderDockWindow("Animation Timeline", dockBottomRight);
        ImGui::DockBuilderDockWindow("Game Profiler & Resource Monitor", dockBottomRight);
        ImGui::DockBuilderDockWindow("3D Viewport", dockMain);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    // Global workspace shortcuts (Ctrl+1 through Ctrl+8)
    if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) m_showHierarchyWindow = !m_showHierarchyWindow;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) m_showInspectorWindow = !m_showInspectorWindow;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) m_showViewportWindow = !m_showViewportWindow;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) m_showCameraPreviewWindow = !m_showCameraPreviewWindow;
        if (ImGui::IsKeyPressed(ImGuiKey_5)) m_showProjectFilesWindow = !m_showProjectFilesWindow;
        if (ImGui::IsKeyPressed(ImGuiKey_6)) m_showTimelineWindow = !m_showTimelineWindow;
        if (ImGui::IsKeyPressed(ImGuiKey_7)) m_showScriptEditor = !m_showScriptEditor;
        if (ImGui::IsKeyPressed(ImGuiKey_8)) m_showGameProfilerWindow = !m_showGameProfilerWindow;
        if (ImGui::IsKeyPressed(ImGuiKey_9)) m_showGpuProfilerWindow = !m_showGpuProfilerWindow;
    }

    // Shortcut 'M' toggles 3D Ruler & Measurement Tool
    if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            ToggleRuler();
        }
    }

    RenderLeftPanel(scene, camera);
    if (m_showViewportWindow) {
        RenderViewport(scene, camera, offscreen, gameLoop);
    }
    if (m_showCameraPreviewWindow) {
        RenderCameraPreview(scene, camera, cameraPreviewOffscreen, gameLoop);
    }
    if (m_showTimelineWindow) {
        RenderAnimationTimeline(scene, gameLoop.GetAnimationPlayer());
    }
    if (m_showProjectFilesWindow) {
        RenderProjectFilesWindow(scene);
    }
    if (m_showGameProfilerWindow) {
        RenderGameProfilerWindow(scene, gameLoop);
    }
    RenderAnimationStateMachine(scene);
    RenderScriptEditor();
    RenderGameWindow(scene, cameraPreviewOffscreen, gameLoop);
    RenderImportModal();
    RenderNewProjectModal();
    RenderSaveProjectAsModal();
    RenderNewSceneModal();
    RenderSaveSceneAsModal();
    RenderInputSettingsModal();
    RenderExportGameModal(scene);
    RenderAboutModal();
    if (m_rulerToolState.showRulerWindow) {
        RenderRulerWindow(scene, camera);
    }
    // Only replicate the standalone game when the game is actively running in Play Mode.
    // The editor scene and viewport are never replicated or modified over the network.
    if (gameLoop.IsPlaying() || gameLoop.IsPaused()) {
        if (auto playScene = gameLoop.GetPlayScene()) {
            Scripting::LuaScriptSystem::Get().SetLocalPeerId(m_networkManager.GetLocalPeerId());
            m_networkManager.Update(*playScene, ImGui::GetIO().DeltaTime);
        }
    }
    RenderEnvironmentAndPostProcessWindow(scene);
    RenderCutsceneSequencerWindow(scene, camera, gameLoop);
    RenderShaderGraphWindow(scene);
    RenderAudioMixerWindow(scene);
    RenderGameLocalizationWindow();
    RenderNavMeshWindow(scene);
    RenderGpuProfilerWindow();
    RenderNetworkingWindow(scene, &gameLoop);

    // Global Project Shortcuts
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
        if (!ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            Core::UndoRedoManager::Get().Undo(scene);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))) {
            Core::UndoRedoManager::Get().Redo(scene);
        } else if (!ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_C)) {
            scene.CopySelected();
        } else if (!ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_V)) {
            if (scene.HasClipboardData()) {
                JsonValue before = scene.Serialize();
                int32_t beforeSel = scene.GetSelectedId();
                scene.PasteNode();
                Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Paste Node");
            }
        } else if (!ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_D)) {
            if (scene.GetSelectedId() > 0) {
                JsonValue before = scene.Serialize();
                int32_t beforeSel = scene.GetSelectedId();
                scene.DuplicateNode(static_cast<uint32_t>(beforeSel));
                Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Duplicate Node");
            }
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_B)) {
        m_showExportGameModal = true;
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L)) {
        m_showGameLocalizationWindow = !m_showGameLocalizationWindow;
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_K)) {
        m_showInputSettingsModal = true;
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (m_onSaveProject) m_onSaveProject();
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        OpenProjectDialog();
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N)) {
        OpenNewProjectDialog();
    }
    // Universal Automatic Undo/Redo Tracker: records any finished slider drag, color edit, checkbox,
    // combo selection, text input, button action, 3D gizmo transform, or hierarchy edit.
    if (gameLoop.IsPlaying() || gameLoop.GetAnimationPlayer().IsPlaying() || gameLoop.GetCutscenePlayer().IsPlaying()) {
        m_undoBaselineInitialized = false;
    } else {
        uint64_t curVer = Core::UndoRedoManager::Get().GetVersion();
        if (curVer != m_lastSeenUndoVersion) {
            m_lastCommittedSceneState = scene.Serialize();
            m_lastCommittedSceneJson = m_lastCommittedSceneState.Serialize(0);
            m_lastCommittedSelectedId = scene.GetSelectedId();
            m_lastSeenUndoVersion = curVer;
            m_undoBaselineInitialized = true;
        } else if (!ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing()) {
            JsonValue currentSceneState = scene.Serialize();
            std::string currentSceneJson = currentSceneState.Serialize(0);
            if (!m_undoBaselineInitialized) {
                m_lastCommittedSceneState = currentSceneState;
                m_lastCommittedSceneJson = currentSceneJson;
                m_lastCommittedSelectedId = scene.GetSelectedId();
                m_lastSeenUndoVersion = curVer;
                m_undoBaselineInitialized = true;
            } else if (currentSceneJson != m_lastCommittedSceneJson) {
                std::string desc = "Edit Scene";
                if (auto sel = scene.GetSelectedNode()) {
                    desc = "Edit " + sel->name;
                }
                Core::UndoRedoManager::Get().RecordSnapshot(
                    m_lastCommittedSceneState,
                    currentSceneState,
                    m_lastCommittedSelectedId,
                    scene.GetSelectedId(),
                    desc
                );
                m_lastCommittedSceneState = currentSceneState;
                m_lastCommittedSceneJson = currentSceneJson;
                m_lastCommittedSelectedId = scene.GetSelectedId();
                m_lastSeenUndoVersion = Core::UndoRedoManager::Get().GetVersion();
            } else {
                m_lastCommittedSelectedId = scene.GetSelectedId();
            }
        }
    }

    if (io.KeyShift && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        m_showCutsceneWindow = !m_showCutsceneWindow;
    }
    if (io.KeyShift && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_M)) {
        m_showAnimStateMachineWindow = !m_showAnimStateMachineWindow;
    }

    ImGui::End();
}

void EditorUI::OpenNewProjectDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[512] = "MyGameProject.tess";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Tessera Project (*.tess)\0*.tess\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Create New Tessera Project";
    ofn.lpstrDefExt = "tess";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        std::filesystem::path p(ofn.lpstrFile);
        std::string parentDir = p.parent_path().string();
        std::string name = p.stem().string();
        if (m_onNewProject) {
            m_onNewProject(parentDir, name);
        }
    }
#else
    m_showNewProjectModal = true;
#endif
}

void EditorUI::OpenSaveProjectAsDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[512] = "MyGameProjectCopy.tess";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Tessera Project (*.tess)\0*.tess\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Save Tessera Project As";
    ofn.lpstrDefExt = "tess";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        std::filesystem::path p(ofn.lpstrFile);
        std::string parentDir = p.parent_path().string();
        std::string name = p.stem().string();
        if (m_onSaveProjectAs) {
            m_onSaveProjectAs(parentDir, name);
        }
    }
#else
    m_showSaveAsModal = true;
#endif
}

void EditorUI::OpenProjectDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[512] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Tessera Project (*.tess)\0*.tess\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Open Tessera Project";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        if (m_onOpenProject) {
            m_onOpenProject(std::string(ofn.lpstrFile));
        }
    }
#endif
}

void EditorUI::RenderNewProjectModal() {
    if (!m_showNewProjectModal) return;

    ImGui::OpenPopup(TR("Create New Project"));
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TR("Create New Project"), &m_showNewProjectModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", TR("Specify Project Name and Destination Directory:"));
        ImGui::Spacing();

        ImGui::Text("%s", TR("Project Name:"));
        ImGui::SetNextItemWidth(480.0f);
        ImGui::InputText("##NewProjName", m_newProjectName, sizeof(m_newProjectName));

        ImGui::Spacing();
        ImGui::Text("%s", TR("Project Location (Parent Directory):"));
        ImGui::SetNextItemWidth(480.0f);
        ImGui::InputText("##NewProjDir", m_newProjectDir, sizeof(m_newProjectDir));

        std::string fullPreview = (std::filesystem::path(m_newProjectDir) / m_newProjectName).string();
        ImGui::Spacing();
        ImGui::TextDisabled("Project Path: %s", fullPreview.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(TR("Create Project"), ImVec2(130, 30))) {
            if (m_onNewProject) {
                m_onNewProject(std::string(m_newProjectDir), std::string(m_newProjectName));
            }
            m_showNewProjectModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(100, 30))) {
            m_showNewProjectModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderSaveProjectAsModal() {
    if (!m_showSaveAsModal) return;

    ImGui::OpenPopup(TR("Save Project As"));
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TR("Save Project As"), &m_showSaveAsModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", TR("Save copy of project to a new location:"));
        ImGui::Spacing();

        ImGui::Text("%s", TR("New Project Name:"));
        ImGui::SetNextItemWidth(480.0f);
        ImGui::InputText("##SaveAsName", m_saveAsProjectName, sizeof(m_saveAsProjectName));

        ImGui::Spacing();
        ImGui::Text("%s", TR("Destination Directory:"));
        ImGui::SetNextItemWidth(480.0f);
        ImGui::InputText("##SaveAsDir", m_saveAsProjectDir, sizeof(m_saveAsProjectDir));

        std::string fullPreview = (std::filesystem::path(m_saveAsProjectDir) / m_saveAsProjectName).string();
        ImGui::Spacing();
        ImGui::TextDisabled("New Path: %s", fullPreview.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(TR("Save Copy"), ImVec2(120, 30))) {
            if (m_onSaveProjectAs) {
                m_onSaveProjectAs(std::string(m_saveAsProjectDir), std::string(m_saveAsProjectName));
            }
            m_showSaveAsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(100, 30))) {
            m_showSaveAsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderNewSceneModal() {
    if (!m_showNewSceneModal) return;

    ImGui::OpenPopup(TR("Create New Scene"));
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 190), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TR("Create New Scene"), &m_showNewSceneModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", TR("Enter name for the new scene (.scene):"));
        ImGui::Spacing();

        ImGui::SetNextItemWidth(400.0f);
        ImGui::InputText("##NewSceneName", m_newSceneName, sizeof(m_newSceneName));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(TR("Create Scene"), ImVec2(120, 28))) {
            if (m_onNewScene) {
                m_onNewScene(std::string(m_newSceneName));
            }
            m_showNewSceneModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(90, 28))) {
            m_showNewSceneModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderSaveSceneAsModal() {
    if (!m_showSaveSceneAsModal) return;

    ImGui::OpenPopup(TR("Save Scene As"));
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 190), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TR("Save Scene As"), &m_showSaveSceneAsModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", TR("Enter new name for current scene:"));
        ImGui::Spacing();

        ImGui::SetNextItemWidth(400.0f);
        ImGui::InputText("##SaveSceneAsName", m_saveSceneAsName, sizeof(m_saveSceneAsName));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(TR("Save Scene Copy"), ImVec2(130, 28))) {
            if (m_onSaveSceneAs) {
                m_onSaveSceneAs(std::string(m_saveSceneAsName));
            }
            m_showSaveSceneAsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(90, 28))) {
            m_showSaveSceneAsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}


static bool OpenSceneFileDialog(std::string& outPath, const char* title = "Select Scene to Instantiate") {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = ofn.lpstrFile;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        return true;
    }
#endif
    return false;
}

#ifdef _WIN32
static bool BrowseForFolderDialog(std::string& outPath, const char* title = "Select Folder") {
    char folder[MAX_PATH] = { 0 };
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        if (SHGetPathFromIDListA(pidl, folder)) {
            outPath = folder;
            std::replace(outPath.begin(), outPath.end(), '\\', '/');
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
    return false;
}

static std::string GetDesktopDirectoryPath() {
    char path[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, path))) {
        std::string s = path;
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }
    return "";
}

static std::string GetDocumentsDirectoryPath() {
    char path[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path))) {
        std::string s = path;
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }
    return "";
}
#else
static bool BrowseForFolderDialog(std::string& outPath, const char* title = "Select Folder") { return false; }
static std::string GetDesktopDirectoryPath() { return ""; }
static std::string GetDocumentsDirectoryPath() { return ""; }
#endif

void EditorUI::OpenImportDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[512] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Supported 3D Models (*.fbx;*.obj;*.gltf;*.glb)\0*.fbx;*.obj;*.gltf;*.glb\0glTF 2.0 Models (*.gltf;*.glb)\0*.gltf;*.glb\0FBX Models (*.fbx)\0*.fbx\0OBJ Models (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Import 3D Model";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        if (m_onImportObj) {
            m_onImportObj(std::string(ofn.lpstrFile));
        }
    } else {
        m_showImportModal = true;
    }
#else
    m_showImportModal = true;
#endif
}

void EditorUI::RenderImportModal() {
    if (!m_showImportModal) return;

    ImGui::OpenPopup(TR("Import 3D Model"));
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 240), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TR("Import 3D Model"), &m_showImportModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", TR("Enter the path to a 3D model file (.obj, .fbx, .gltf, .glb):"));
        ImGui::Spacing();

        ImGui::SetNextItemWidth(450.0f);
        ImGui::InputText("##ObjPath", m_importPathBuffer, sizeof(m_importPathBuffer));

        ImGui::Spacing();
        ImGui::TextDisabled("Quick Sample Models:");
        if (ImGui::Button("Sample Teapot")) {
            strncpy(m_importPathBuffer, "assets/models/teapot.obj", sizeof(m_importPathBuffer));
        }
        ImGui::SameLine();
        if (ImGui::Button("Sample Torus")) {
            strncpy(m_importPathBuffer, "assets/models/torus.obj", sizeof(m_importPathBuffer));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(TR("Import Model"), ImVec2(120, 28))) {
            if (m_onImportObj) {
                m_onImportObj(std::string(m_importPathBuffer));
            }
            m_showImportModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(100, 28))) {
            m_showImportModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderMainMenuBar(Scene& scene, Camera& camera, Gameplay::GameLoop& gameLoop) {
        if (ImGui::BeginMenu(TR("File"))) {
            if (ImGui::MenuItem(TR("Project Launcher / Hub..."))) {
                m_startupState = StartupState::ProjectLauncher;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("New Project..."), "Ctrl+N")) {
                OpenNewProjectDialog();
            }
            if (ImGui::MenuItem(TR("Open Project..."), "Ctrl+O")) {
                OpenProjectDialog();
            }
            if (ImGui::MenuItem(TR("Save Project"), "Ctrl+S")) {
                if (m_onSaveProject) m_onSaveProject();
            }
            if (ImGui::MenuItem(TR("Save Project As..."))) {
                OpenSaveProjectAsDialog();
            }

            ImGui::Separator();
            if (ImGui::BeginMenu(TR("Recent Projects"))) {
                auto recents = Project::GetRecentProjects();
                if (recents.empty()) {
                    ImGui::TextDisabled("%s", TR("No recent projects"));
                } else {
                    for (const auto& r : recents) {
                        if (ImGui::MenuItem(r.c_str())) {
                            if (m_onOpenProject) m_onOpenProject(r);
                        }
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(TR("Export Standalone Game..."), "Ctrl+B")) {
                m_showExportGameModal = true;
            }
            if (ImGui::MenuItem(TR("Project Input & Key Bindings..."), "Ctrl+K")) {
                m_showInputSettingsModal = true;
            }
            if (ImGui::MenuItem(TR("Game Localization..."), "Ctrl+L")) {
                m_showGameLocalizationWindow = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Import 3D Model..."))) {
                OpenImportDialog();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Edit"))) {
            std::string undoLabel = Core::UndoRedoManager::Get().CanUndo()
                ? ("Undo " + Core::UndoRedoManager::Get().GetUndoDescription())
                : "Undo";
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, Core::UndoRedoManager::Get().CanUndo())) {
                Core::UndoRedoManager::Get().Undo(scene);
            }

            std::string redoLabel = Core::UndoRedoManager::Get().CanRedo()
                ? ("Redo " + Core::UndoRedoManager::Get().GetRedoDescription())
                : "Redo";
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, Core::UndoRedoManager::Get().CanRedo())) {
                Core::UndoRedoManager::Get().Redo(scene);
            }

            ImGui::Separator();
            bool hasSelection = (scene.GetSelectedId() > 0);
            if (ImGui::MenuItem(TR("Copy Selected Node"), "Ctrl+C", false, hasSelection)) {
                scene.CopySelected();
            }
            if (ImGui::MenuItem(TR("Paste Node"), "Ctrl+V", false, scene.HasClipboardData())) {
                JsonValue before = scene.Serialize();
                int32_t beforeSel = scene.GetSelectedId();
                scene.PasteNode();
                Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Paste Node");
            }
            if (ImGui::MenuItem(TR("Duplicate Selected Node"), "Ctrl+D", false, hasSelection)) {
                JsonValue before = scene.Serialize();
                int32_t beforeSel = scene.GetSelectedId();
                scene.DuplicateNode(static_cast<uint32_t>(beforeSel));
                Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Duplicate Node");
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Delete Selected Node"), "Del", false, hasSelection)) {
                scene.DeleteSelected();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Scene"))) {
            if (ImGui::MenuItem(TR("New Scene..."))) {
                OpenNewSceneModal();
            }
            if (ImGui::BeginMenu(TR("Open Scene"))) {
                std::vector<std::string> scenes;
                if (m_onGetScenes) scenes = m_onGetScenes();
                if (scenes.empty()) {
                    ImGui::TextDisabled("%s", TR("No other scenes in project"));
                } else {
                    std::string active = m_onGetActiveScene ? m_onGetActiveScene() : "";
                    for (const auto& sc : scenes) {
                        bool isCurrent = (sc == active);
                        if (ImGui::MenuItem(sc.c_str(), nullptr, isCurrent)) {
                            if (m_onOpenScene) m_onOpenScene(sc);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(TR("Save Scene"))) {
                if (m_onSaveScene) m_onSaveScene();
            }
            if (ImGui::MenuItem(TR("Save Scene As..."))) {
                OpenSaveSceneAsModal();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Instantiate Subscene / Prefab (.scene)..."), "Ctrl+Shift+I")) {
                std::string scenePath;
                if (OpenSceneFileDialog(scenePath, "Select Scene to Instantiate")) {
                    uint32_t targetParent = scene.GetSelectedId() > 0 ? static_cast<uint32_t>(scene.GetSelectedId()) : 0;
                    scene.InstantiateScene(scenePath, targetParent);
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem(TR("Add Node / Entity..."), "Ctrl+A")) {
                m_openAddNodePopup = true;
            }
            if (ImGui::MenuItem(TR("Cutscene & Cinematic Sequencer..."), "Shift+C")) {
                m_showCutsceneWindow = true;
            }
            if (ImGui::BeginMenu(TR("Add In-Game Video Screen..."))) {
                if (ImGui::MenuItem(TR("TV Set (16:9 Live News Broadcast)"))) {
                    auto tv = scene.AddNode(ShapeType::Cube, "TV Set Screen");
                    tv->position = glm::vec3(0.0f, 1.5f, 0.0f);
                    tv->scale = glm::vec3(1.6f, 0.9f, 0.06f);
                    tv->color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    tv->roughness = 0.12f;
                    tv->isVideoScreen = true;
                    tv->videoChannelPreset = 2; // TV News
                    tv->videoPlaying = true;
                    tv->videoScreenBrightness = 1.6f;
                }
                if (ImGui::MenuItem(TR("Cinema Screen (2.39:1 Feature Film Reel)"))) {
                    auto cin = scene.AddNode(ShapeType::Cube, "Cinema Screen");
                    cin->position = glm::vec3(0.0f, 3.2f, -4.0f);
                    cin->scale = glm::vec3(7.2f, 3.0f, 0.08f);
                    cin->color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    cin->roughness = 0.35f;
                    cin->isVideoScreen = true;
                    cin->videoChannelPreset = 1; // Cinema Reel
                    cin->videoPlaying = true;
                    cin->videoScreenBrightness = 2.2f;
                    cin->videoScanlineStrength = 0.05f;
                }
                if (ImGui::MenuItem(TR("Smartphone Screen (9:16 Incoming Call)"))) {
                    auto ph = scene.AddNode(ShapeType::Cube, "Smartphone Screen");
                    ph->position = glm::vec3(0.0f, 1.2f, 1.0f);
                    ph->scale = glm::vec3(0.18f, 0.32f, 0.02f);
                    ph->color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    ph->roughness = 0.06f;
                    ph->clearCoat = 0.8f;
                    ph->isVideoScreen = true;
                    ph->videoChannelPreset = 4; // Smartphone UI
                    ph->videoPlaying = true;
                    ph->videoScreenBrightness = 1.8f;
                    ph->videoScanlineStrength = 0.0f;
                }
                if (ImGui::MenuItem(TR("Security CCTV Monitor"))) {
                    auto cctv = scene.AddNode(ShapeType::Cube, "CCTV Monitor");
                    cctv->position = glm::vec3(0.0f, 1.4f, 0.0f);
                    cctv->scale = glm::vec3(0.8f, 0.6f, 0.08f);
                    cctv->color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    cctv->roughness = 0.2f;
                    cctv->isVideoScreen = true;
                    cctv->videoChannelPreset = 3; // CCTV
                    cctv->videoPlaying = true;
                    cctv->videoScreenBrightness = 1.4f;
                    cctv->videoScanlineStrength = 0.35f;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TR("2D UI / Cutaway Video Player"))) {
                    scene.AddUIVideoNode("UI Video Player");
                }
                if (ImGui::MenuItem(TR("Test Full-Screen Video Cutaway Now"))) {
                    Video::VideoSystem::Get().StartCutaway(1, "", 8.0f, true, false, "CINEMA CUTAWAY REEL");
                }
                if (ImGui::MenuItem(TR("Test Corner PiP Transmission Now"))) {
                    Video::VideoSystem::Get().StartCutaway(3, "", 8.0f, true, true, "INCOMING CCTV FEED");
                }
                ImGui::EndMenu();
            }
            if (false) {
                m_openAddNodePopup = true;
            }
            if (ImGui::BeginMenu(TR("Add Primitive"))) {
                if (ImGui::MenuItem(TR("Cube"))) scene.AddNode(ShapeType::Cube);
                if (ImGui::MenuItem(TR("Sphere"))) scene.AddNode(ShapeType::Sphere);
                if (ImGui::MenuItem(TR("Cone"))) scene.AddNode(ShapeType::Cone);
                if (ImGui::MenuItem(TR("Cylinder"))) scene.AddNode(ShapeType::Cylinder);
                if (ImGui::MenuItem(TR("Plane"))) scene.AddNode(ShapeType::Plane);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("Add Entity"))) {
                if (ImGui::MenuItem(TR("Camera"))) scene.AddCameraNode();
                if (ImGui::MenuItem(TR("Point Light"))) scene.AddLightNode(LightType::Point);
                if (ImGui::MenuItem(TR("Directional Light"))) scene.AddLightNode(LightType::Directional);
                if (ImGui::MenuItem(TR("Spot Light"))) scene.AddLightNode(LightType::Spot);
                if (ImGui::MenuItem(TR("Audio Source (2D / 3D)"))) scene.AddAudioNode();
                if (ImGui::MenuItem(TR("Particle Emitter (CPU / Niagara)"))) scene.AddParticleNode();
                if (ImGui::MenuItem(TR("Terrain (Landscape)"))) scene.AddTerrainNode();
                if (ImGui::MenuItem(TR("Fluid Volume"))) {
                    auto fluid = scene.AddEmptyNode("Fluid Volume");
                    if (fluid) fluid->isFluidVolume = true;
                }
                if (ImGui::MenuItem(TR("Pour Spout"))) {
                    auto spout = scene.AddEmptyNode("Pour Spout");
                    if (spout) {
                        spout->isPourSpout = true;
                        spout->rotation = glm::vec3(90.0f, 0.0f, 0.0f); // facing up: not pouring until tipped
                    }
                }
                if (ImGui::MenuItem(TR("Ocean"))) {
                    auto ocean = scene.AddNode(ShapeType::Plane, "Ocean");
                    ocean->isOcean = true;
                    ocean->lightmapStatic = false;
                    ocean->castShadows = false;
                }
                if (ImGui::MenuItem(TR("Decal Projector"))) scene.AddDecalNode();
                if (ImGui::MenuItem(TR("Spline / 3D Curve"))) scene.AddSplineNode();
                if (ImGui::MenuItem(TR("Empty Group Node"))) scene.AddEmptyNode();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("Add 2D UI Component"))) {
                if (ImGui::BeginMenu(TR("Containers & Windows"))) {
                    if (ImGui::MenuItem(TR("Canvas (UI Root)"))) scene.AddUICanvasNode();
                    if (ImGui::MenuItem(TR("Panel"))) scene.AddUIPanelNode();
                    if (ImGui::MenuItem(TR("Floating Window"))) scene.AddUIWindowNode();
                    if (ImGui::MenuItem(TR("Scroll Pane"))) scene.AddUIScrollPaneNode();
                    if (ImGui::MenuItem(TR("Tabs View"))) scene.AddUITabsNode();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Basic & Text"))) {
                    if (ImGui::MenuItem(TR("Text Label"))) scene.AddUITextNode();
                    if (ImGui::MenuItem(TR("Button"))) scene.AddUIButtonNode();
                    if (ImGui::MenuItem(TR("Text Field"))) scene.AddUITextFieldNode();
                    if (ImGui::MenuItem(TR("Password Field"))) scene.AddUIPasswordFieldNode();
                    if (ImGui::MenuItem(TR("Progress Bar"))) scene.AddUIProgressBarNode();
                    if (ImGui::MenuItem(TR("Spinner"))) scene.AddUISpinnerNode();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Inputs & Controls"))) {
                    if (ImGui::MenuItem(TR("Slider"))) scene.AddUISliderNode();
                    if (ImGui::MenuItem(TR("Checkbox"))) scene.AddUICheckboxNode();
                    if (ImGui::MenuItem(TR("Radio Button"))) scene.AddUIRadioButtonNode();
                    if (ImGui::MenuItem(TR("Dropdown Box"))) scene.AddUIDropdownNode();
                    if (ImGui::MenuItem(TR("Date/Time Picker"))) scene.AddUIDatePickerNode();
                    if (ImGui::MenuItem(TR("Color Picker"))) scene.AddUIColorPickerNode();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Data & Media"))) {
                    if (ImGui::MenuItem(TR("Image"))) scene.AddUIImageNode();
                    if (ImGui::MenuItem(TR("Video Player"))) scene.AddUIVideoNode();
                    if (ImGui::MenuItem(TR("Selectable List"))) scene.AddUISelectableListNode();
                    if (ImGui::MenuItem(TR("Table Grid"))) scene.AddUITableNode();
                    if (ImGui::MenuItem(TR("Menu Bar"))) scene.AddUIMenuBarNode();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Vector Shapes"))) {
                    if (ImGui::MenuItem(TR("Rectangle"))) scene.AddUIShapeNode(UIShapeType::Rectangle, "Rectangle");
                    if (ImGui::MenuItem(TR("Rounded Rectangle"))) scene.AddUIShapeNode(UIShapeType::RoundedRect, "RoundedRect");
                    if (ImGui::MenuItem(TR("Circle"))) scene.AddUIShapeNode(UIShapeType::Circle, "Circle");
                    if (ImGui::MenuItem(TR("Triangle"))) scene.AddUIShapeNode(UIShapeType::Triangle, "Triangle");
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("Add Collider"))) {
                if (ImGui::MenuItem(TR("Box Collider"))) scene.AddColliderNode(ColliderType::Box);
                if (ImGui::MenuItem(TR("Sphere Collider"))) scene.AddColliderNode(ColliderType::Sphere);
                if (ImGui::MenuItem(TR("Capsule Collider"))) scene.AddColliderNode(ColliderType::Capsule);
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(TR("Delete Selected"), "Del", false, scene.GetSelectedId() >= 0)) {
                scene.DeleteSelected();
            }
            if (ImGui::MenuItem(TR("Reset Scene to Default"))) {
                scene.ResetToDefault();
                camera.Reset();
            }
            if (ImGui::MenuItem(TR("Clear All"))) {
                scene.Clear();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Camera"))) {
            if (ImGui::MenuItem(TR("Reset View"))) {
                camera.Reset();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Gizmo"))) {
            if (ImGui::MenuItem(TR("Translate"), "W", m_currentGizmoOperation == ImGuizmo::TRANSLATE)) m_currentGizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::MenuItem(TR("Rotate"), "E", m_currentGizmoOperation == ImGuizmo::ROTATE)) m_currentGizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::MenuItem(TR("Scale"), "R", m_currentGizmoOperation == ImGuizmo::SCALE)) m_currentGizmoOperation = ImGuizmo::SCALE;
            ImGui::Separator();
            if (ImGui::MenuItem(TR("World Coordinates"), nullptr, m_currentGizmoMode == ImGuizmo::WORLD)) m_currentGizmoMode = ImGuizmo::WORLD;
            if (ImGui::MenuItem(TR("Local Coordinates"), nullptr, m_currentGizmoMode == ImGuizmo::LOCAL)) m_currentGizmoMode = ImGuizmo::LOCAL;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Window"))) {
            ImGui::TextDisabled("%s", TR("Workspace Panels"));
            if (ImGui::MenuItem(TR("Scene Hierarchy"), "Ctrl+1", m_showHierarchyWindow)) {
                m_showHierarchyWindow = !m_showHierarchyWindow;
            }
            if (ImGui::MenuItem(TR("Inspector"), "Ctrl+2", m_showInspectorWindow)) {
                m_showInspectorWindow = !m_showInspectorWindow;
            }
            if (ImGui::MenuItem(TR("3D Viewport"), "Ctrl+3", m_showViewportWindow)) {
                m_showViewportWindow = !m_showViewportWindow;
            }
            if (ImGui::MenuItem(TR("Camera Preview"), "Ctrl+4", m_showCameraPreviewWindow)) {
                m_showCameraPreviewWindow = !m_showCameraPreviewWindow;
            }
            if (ImGui::MenuItem(TR("Project Files (File Viewer)"), "Ctrl+5", m_showProjectFilesWindow)) {
                m_showProjectFilesWindow = !m_showProjectFilesWindow;
            }
            if (ImGui::MenuItem(TR("Animation Timeline"), "Ctrl+6", m_showTimelineWindow)) {
                m_showTimelineWindow = !m_showTimelineWindow;
            }
            if (ImGui::MenuItem(TR("Script Editor"), "Ctrl+7", m_showScriptEditor)) {
                m_showScriptEditor = !m_showScriptEditor;
            }
            if (ImGui::MenuItem(TR("Game Profiler & Resource Monitor"), "Ctrl+8", m_showGameProfilerWindow)) {
                m_showGameProfilerWindow = !m_showGameProfilerWindow;
            }
            if (ImGui::MenuItem(TR("GPU Profiler"), "Ctrl+9", m_showGpuProfilerWindow)) {
                m_showGpuProfilerWindow = !m_showGpuProfilerWindow;
            }

            ImGui::Separator();
            ImGui::TextDisabled("%s", TR("Specialized Tools"));
            if (ImGui::MenuItem(TR("Rendering, GI & Post-Processing"), "Shift+R", m_showEnvironmentWindow)) {
                m_showEnvironmentWindow = !m_showEnvironmentWindow;
            }
            if (ImGui::MenuItem(TR("Visual Shader Graph Editor"), "Shift+G", m_showShaderGraphWindow)) {
                m_showShaderGraphWindow = !m_showShaderGraphWindow;
            }
            if (ImGui::MenuItem(TR("Audio Mixer & Reverb Zones"), "Shift+A", m_showAudioMixerWindow)) {
                m_showAudioMixerWindow = !m_showAudioMixerWindow;
            }
            if (ImGui::MenuItem(TR("Navigation & NavMesh Baker"), "Shift+N", m_showNavMeshWindow)) {
                m_showNavMeshWindow = !m_showNavMeshWindow;
            }
            if (ImGui::MenuItem(TR("Multiplayer & Networking"), "Shift+P", m_showNetworkingWindow)) {
                m_showNetworkingWindow = !m_showNetworkingWindow;
            }
            if (ImGui::MenuItem(TR("Animation State Machine Graph"), "Shift+M", m_showAnimStateMachineWindow)) {
                m_showAnimStateMachineWindow = !m_showAnimStateMachineWindow;
            }
            if (ImGui::MenuItem(TR("Cutscene & Cinematic Sequencer"), "Shift+C", m_showCutsceneWindow)) {
                m_showCutsceneWindow = !m_showCutsceneWindow;
            }
            if (false) {
                m_showAnimStateMachineWindow = !m_showAnimStateMachineWindow;
            }
            if (ImGui::MenuItem(TR("3D Ruler & Measurement Tool"), "M", m_rulerToolState.showRulerWindow || m_rulerToolState.active)) {
                bool newState = !(m_rulerToolState.showRulerWindow || m_rulerToolState.active);
                m_rulerToolState.showRulerWindow = newState;
                SetRulerActive(newState);
            }

            ImGui::Separator();
            ImGui::TextDisabled("%s", TR("Standalone Game View"));
            if (ImGui::MenuItem(TR("Game Localization Editor"), "Ctrl+L", m_showGameLocalizationWindow)) {
                m_showGameLocalizationWindow = !m_showGameLocalizationWindow;
            }
            if (ImGui::MenuItem(TR("Game [Standalone Window]"), "F11", m_showGameWindow)) {
                m_showGameWindow = !m_showGameWindow;
            }

            ImGui::Separator();
            ImGui::TextDisabled("%s", TR("Layout Options"));
            if (ImGui::MenuItem(TR("Reset to Default Window Layout"))) {
                m_resetDockLayoutRequested = true;
                m_showHierarchyWindow = true;
                m_showInspectorWindow = true;
                m_showViewportWindow = true;
                m_showCameraPreviewWindow = true;
                m_showProjectFilesWindow = true;
                m_showTimelineWindow = true;
            }
            if (ImGui::MenuItem(TR("Show All Workspace Panels"))) {
                m_showHierarchyWindow = true;
                m_showInspectorWindow = true;
                m_showViewportWindow = true;
                m_showCameraPreviewWindow = true;
                m_showProjectFilesWindow = true;
                m_showTimelineWindow = true;
            }
            if (ImGui::MenuItem(TR("Close All Secondary Windows"))) {
                m_showEnvironmentWindow = false;
                m_showShaderGraphWindow = false;
                m_showAudioMixerWindow = false;
                m_showNavMeshWindow = false;
                m_showNetworkingWindow = false;
                m_showAnimStateMachineWindow = false;
                m_showCutsceneWindow = false;
                m_showScriptEditor = false;
                m_showGameProfilerWindow = false;
                m_showGameWindow = false;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Render"))) {
            auto& env = scene.GetEnvironment();
            auto& rt = Graphics::RayTracing::RayTracingManager::Get();
            auto& rtSettings = rt.GetSettings();
            auto& meshletMgr = Graphics::Meshlet::MeshletManager::Get();
            auto& meshletSettings = meshletMgr.GetSettings();
            ImGui::MenuItem(TR("Enable Meshlet GPU Virtualization"), nullptr, &meshletSettings.enableMeshletPipeline);

            auto& upscaler = Graphics::Upscaling::TemporalUpscaler::Get();
            auto& upscalerSettings = upscaler.GetSettings();
            ImGui::MenuItem(TR("Enable Temporal Upscaling (FSR 3 / DLSS)"), nullptr, &upscalerSettings.enableUpscaling);
            if (ImGui::MenuItem(TR("Enable Hardware Ray Tracing (Vulkan / Metal)"), nullptr, &rtSettings.enableHardwareRT)) {
                if (rtSettings.enableHardwareRT) {
                    rtSettings.enableRTGI = true;
                    rtSettings.enableRTAO = true;
                    rtSettings.enableRTR = true;
                    env.vxgiEnabled = true;
                }
            }
            ImGui::MenuItem(TR("Enable Voxel Global Illumination (VXGI)"), nullptr, &env.vxgiEnabled);
            ImGui::MenuItem(TR("Enable Spherical Harmonics Sky GI"), nullptr, &env.enableGI);
            ImGui::MenuItem(TR("Enable Realtime Shadows & CSM"), nullptr, &env.enableShadows);
            ImGui::MenuItem(TR("Enable ACES Filmic Tonemapping"), nullptr, &env.enableACES);
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Bake Static Lighting"), nullptr, false, !m_giBaking)) {
                m_giBakeRequested = true;
            }
            if (ImGui::MenuItem(TR("Recapture Reflection Probes"))) {
                m_probeRecaptureRequests++;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Environment, GI & Post-Processing..."), "Shift+R", m_showEnvironmentWindow)) {
                m_showEnvironmentWindow = !m_showEnvironmentWindow;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Language"))) {
            const auto& langs = Localization::Get().GetAvailableLanguages();
            for (const auto& l : langs) {
                bool selected = (l.code == Localization::Get().GetCurrentLanguage());
                if (ImGui::MenuItem(l.displayName.c_str(), nullptr, selected)) {
                    Localization::Get().SetLanguage(l.code);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Reload Languages"))) {
                Localization::Get().ReloadCurrentLanguage();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Help"))) {
            if (ImGui::MenuItem(TR("Open HTML Documentation..."))) {
#ifdef _WIN32
                ShellExecuteA(NULL, "open", "docs\\index.html", NULL, NULL, SW_SHOWNORMAL);
#elif defined(__linux__)
                (void)system("xdg-open docs/index.html &");
#endif
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("About Tessera Game Engine..."))) {
                m_showAboutModal = true;
            }
            ImGui::EndMenu();
        }

}

void EditorUI::RenderLeftPanel(Scene& scene, Camera& camera) {
    if (m_showHierarchyWindow) {
        if (ImGui::Begin(TR("Scene Hierarchy###HierarchyWindow"), &m_showHierarchyWindow)) {
            RenderHierarchy(scene, camera);
        }
        ImGui::End();
    }

    if (m_showInspectorWindow) {
        if (ImGui::Begin(TR("Inspector###InspectorWindow"), &m_showInspectorWindow)) {
            RenderInspector(scene, camera);
        }
        ImGui::End();
    }
}

void EditorUI::RenderNodeTree(Scene& scene, uint32_t nodeId, uint32_t& nodeToDelete, bool& requestDelete) {
    auto node = scene.GetNodeById(nodeId);
    if (!node) return;

    ImGui::PushID(node->id);

    bool isSelected = (scene.GetSelectedId() == static_cast<int32_t>(node->id));
    bool hasChildren = !node->childrenIds.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    std::string tag = "";
    if (node->isSceneInstanceRoot) tag = "[Prefab] ";
    else if (node->nodeType == NodeType::UI_BoxContainer) tag = "[UI BoxContainer] ";
    else if (node->nodeType == NodeType::UI_GridContainer) tag = "[UI GridContainer] ";
    else if (node->nodeType == NodeType::Camera) tag = "[Camera] ";
    else if (node->nodeType == NodeType::Light) tag = (node->lightType == LightType::Point) ? "[Point Light] " : ((node->lightType == LightType::Spot) ? "[Spot Light] " : "[Dir Light] ");
    else if (node->nodeType == NodeType::AudioSource) tag = "[Audio] ";
    else if (node->nodeType == NodeType::ParticleEmitter) tag = "[Particles] ";
    else if (node->nodeType == NodeType::Decal) tag = "[Decal] ";
    else if (node->nodeType == NodeType::Spline) tag = "[Spline] ";
    else if (node->nodeType == NodeType::Collider) tag = "[" + std::string(ColliderTypeToString(node->colliderType)) + " Col] ";
    else if (node->nodeType == NodeType::UI_Canvas) tag = "[UI Canvas] ";
    else if (node->nodeType == NodeType::UI_Panel) tag = "[UI Panel] ";
    else if (node->nodeType == NodeType::UI_Text) tag = "[UI Text] ";
    else if (node->nodeType == NodeType::UI_Button) tag = "[UI Button] ";
    else if (node->nodeType == NodeType::UI_ProgressBar) tag = "[UI Progress] ";
    else if (node->nodeType == NodeType::Empty) tag = "[Group] ";
    else if (node->hasSkeleton) tag = "[Rigged FBX] ";
    else if (node->type == ShapeType::Custom) {
        std::string ext = "";
        if (!node->customMeshName.empty()) {
            ext = std::filesystem::path(node->customMeshName).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }
        tag = (ext == ".fbx") ? "[FBX] " : "[OBJ] ";
    }
    else tag = "[" + std::string(ShapeTypeToString(node->type)) + "] ";

    std::string label = tag + node->name;
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)node->id, flags, "%s", label.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        scene.SelectNode(node->id);
    }

    // Drag and drop source: drag node to reparent
    if (ImGui::BeginDragDropSource()) {
        uint32_t payloadId = node->id;
        ImGui::SetDragDropPayload("DND_SCENE_NODE", &payloadId, sizeof(uint32_t));
        ImGui::Text("Move %s", node->name.c_str());
        ImGui::EndDragDropSource();
    }

    // Drag and drop target: drop another node onto this node to reorder (before/after) or reparent (child)
    if (ImGui::BeginDragDropTarget()) {
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImVec2 itemMax = ImGui::GetItemRectMax();
        float mouseY = ImGui::GetMousePos().y;
        float h = itemMax.y - itemMin.y;
        float ratio = (h > 0.0f) ? ((mouseY - itemMin.y) / h) : 0.5f;

        // Visual feedback based on mouse position relative to node rect:
        // Top 25%: Insert BEFORE (horizontal drop indicator line on top)
        // Bottom 25%: Insert AFTER (horizontal drop indicator line on bottom)
        // Middle 50%: Reparent as CHILD (highlight box around the node)
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (ratio < 0.25f) {
            drawList->AddLine(
                ImVec2(itemMin.x, itemMin.y),
                ImVec2(itemMax.x, itemMin.y),
                IM_COL32(80, 165, 255, 255), 2.5f
            );
        } else if (ratio > 0.75f) {
            drawList->AddLine(
                ImVec2(itemMin.x, itemMax.y),
                ImVec2(itemMax.x, itemMax.y),
                IM_COL32(80, 165, 255, 255), 2.5f
            );
        } else {
            drawList->AddRect(
                itemMin, itemMax,
                IM_COL32(80, 165, 255, 180), 2.0f, 0, 1.5f
            );
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE")) {
            uint32_t draggedId = *(const uint32_t*)payload->Data;
            if (draggedId != node->id) {
                if (ratio < 0.25f) {
                    scene.ReorderNodeBefore(draggedId, node->id);
                } else if (ratio > 0.75f) {
                    scene.ReorderNodeAfter(draggedId, node->id);
                } else {
                    scene.SetParent(draggedId, node->id);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Context menu on right-click
    if (ImGui::BeginPopupContextItem("HierarchyNodeContextMenu")) {
        scene.SelectNode(node->id);

        if (ImGui::BeginMenu("Reorder")) {
            if (ImGui::MenuItem("Move Up", "Alt+Up")) {
                scene.MoveNodeUp(node->id);
            }
            if (ImGui::MenuItem("Move Down", "Alt+Down")) {
                scene.MoveNodeDown(node->id);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Move to Top")) {
                scene.MoveNodeToTop(node->id);
            }
            if (ImGui::MenuItem("Move to Bottom")) {
                scene.MoveNodeToBottom(node->id);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();

        if (node->isSceneInstanceRoot) {
            if (ImGui::MenuItem(TR("Reload Prefab From Disk"))) {
                scene.ReloadSceneInstance(node->id);
            }
            ImGui::Separator();
        }

        if (node->parentId != 0) {
            if (ImGui::MenuItem(TR("Unparent (Move to Root)"))) {
                scene.Unparent(node->id);
            }
            ImGui::Separator();
        }

        if (ImGui::BeginMenu(TR("Add Child"))) {
            if (ImGui::MenuItem("Instantiate Subscene / Prefab (.scene)...")) {
                std::string scenePath;
                if (OpenSceneFileDialog(scenePath, "Select Scene to Instantiate as Child")) {
                    scene.InstantiateScene(scenePath, node->id);
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu(TR("3D Objects & Primitives"))) {
                if (ImGui::MenuItem("Character NPC")) scene.AddCharacterNode("", node->id);
                if (ImGui::MenuItem("Cube")) scene.AddNode(ShapeType::Cube, "", node->id);
                if (ImGui::MenuItem("Sphere")) scene.AddNode(ShapeType::Sphere, "", node->id);
                if (ImGui::MenuItem("Cylinder")) scene.AddNode(ShapeType::Cylinder, "", node->id);
                if (ImGui::MenuItem("Cone")) scene.AddNode(ShapeType::Cone, "", node->id);
                if (ImGui::MenuItem("Plane")) scene.AddNode(ShapeType::Plane, "", node->id);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("2D User Interface"))) {
                if (ImGui::BeginMenu(TR("Containers & Windows"))) {
                    if (ImGui::MenuItem("Canvas (UI Root)")) scene.AddUICanvasNode("", node->id);
                    if (ImGui::MenuItem("Panel")) scene.AddUIPanelNode("", node->id);
                    if (ImGui::MenuItem("Floating Window")) scene.AddUIWindowNode("Floating Window", "", node->id);
                    if (ImGui::MenuItem("Scroll Pane")) scene.AddUIScrollPaneNode("", node->id);
                    if (ImGui::MenuItem("Tabs View")) scene.AddUITabsNode({"Tab 1", "Tab 2", "Tab 3"}, "", node->id);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Basic & Text"))) {
                    if (ImGui::MenuItem("Text Label")) scene.AddUITextNode("Text Label", "", node->id);
                    if (ImGui::MenuItem("Button")) scene.AddUIButtonNode("Button", "", node->id);
                    if (ImGui::MenuItem("Text Field")) scene.AddUITextFieldNode("Enter text...", "", node->id);
                    if (ImGui::MenuItem("Password Field")) scene.AddUIPasswordFieldNode("", node->id);
                    if (ImGui::MenuItem("Progress Bar")) scene.AddUIProgressBarNode("", node->id);
                    if (ImGui::MenuItem("Spinner")) scene.AddUISpinnerNode("", node->id);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Inputs & Controls"))) {
                    if (ImGui::MenuItem("Slider")) scene.AddUISliderNode(0.0f, 100.0f, 50.0f, "", node->id);
                    if (ImGui::MenuItem("Checkbox")) scene.AddUICheckboxNode("Checkbox", false, "", node->id);
                    if (ImGui::MenuItem("Radio Button")) scene.AddUIRadioButtonNode("Radio Option", "Group1", false, "", node->id);
                    if (ImGui::MenuItem("Dropdown Box")) scene.AddUIDropdownNode({"Option 1", "Option 2", "Option 3"}, "", node->id);
                    if (ImGui::MenuItem("Date/Time Picker")) scene.AddUIDatePickerNode("", node->id);
                    if (ImGui::MenuItem("Color Picker")) scene.AddUIColorPickerNode("", node->id);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Data & Media"))) {
                    if (ImGui::MenuItem("Image")) scene.AddUIImageNode("", "", node->id);
                    if (ImGui::MenuItem("Video Player")) scene.AddUIVideoNode("", node->id);
                    if (ImGui::MenuItem("Selectable List")) scene.AddUISelectableListNode({"Item 1", "Item 2", "Item 3"}, "", node->id);
                    if (ImGui::MenuItem("Table Grid")) scene.AddUITableNode("", node->id);
                    if (ImGui::MenuItem("Menu Bar")) scene.AddUIMenuBarNode("", node->id);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Vector Shapes"))) {
                    if (ImGui::MenuItem("Rectangle")) scene.AddUIShapeNode(UIShapeType::Rectangle, "Rectangle", node->id);
                    if (ImGui::MenuItem("Rounded Rectangle")) scene.AddUIShapeNode(UIShapeType::RoundedRect, "RoundedRect", node->id);
                    if (ImGui::MenuItem("Circle")) scene.AddUIShapeNode(UIShapeType::Circle, "Circle", node->id);
                    if (ImGui::MenuItem("Triangle")) scene.AddUIShapeNode(UIShapeType::Triangle, "Triangle", node->id);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("Entities & Lighting"))) {
                if (ImGui::MenuItem("Camera")) scene.AddCameraNode("", node->id);
                if (ImGui::MenuItem("Point Light")) scene.AddLightNode(LightType::Point, "", node->id);
                if (ImGui::MenuItem("Directional Light")) scene.AddLightNode(LightType::Directional, "", node->id);
                if (ImGui::MenuItem("Spot Light")) scene.AddLightNode(LightType::Spot, "", node->id);
                if (ImGui::MenuItem("Audio Source")) scene.AddAudioNode("", node->id);
                if (ImGui::MenuItem("Particle Emitter")) scene.AddParticleNode("", node->id);
                if (ImGui::MenuItem("Terrain (Landscape)")) scene.AddTerrainNode("", node->id);
                if (ImGui::MenuItem("Decal Projector")) scene.AddDecalNode("", node->id);
                if (ImGui::MenuItem("Spline / Curve")) scene.AddSplineNode("", node->id);
                if (ImGui::MenuItem("Empty Group Node")) scene.AddEmptyNode("", node->id);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("Physics & Colliders"))) {
                if (ImGui::MenuItem("Box Collider")) scene.AddColliderNode(ColliderType::Box, "", node->id);
                if (ImGui::MenuItem("Sphere Collider")) scene.AddColliderNode(ColliderType::Sphere, "", node->id);
                if (ImGui::MenuItem("Capsule Collider")) scene.AddColliderNode(ColliderType::Capsule, "", node->id);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(TR("Delete Node"))) {
            nodeToDelete = node->id;
            requestDelete = true;
        }
        ImGui::EndPopup();
    }

    // Render children recursively if tree node is expanded
    if (nodeOpen) {
        std::vector<uint32_t> children = node->childrenIds;
        for (uint32_t childId : children) {
            RenderNodeTree(scene, childId, nodeToDelete, requestDelete);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void EditorUI::RenderAddNodePopup(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("AddNodePopupWindow")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.78f, 1.0f, 1.0f));
        ImGui::TextUnformatted("CREATE NEW NODE");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Search Filter
        static char s_filterBuf[64] = "";
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##AddNodeFilter", "Search node types...", s_filterBuf, sizeof(s_filterBuf));
        ImGui::PopItemWidth();
        ImGui::Spacing();

        // Parenting option
        auto selectedNode = scene.GetSelectedNode();
        static bool s_addAsChild = false;
        if (selectedNode) {
            std::string childLabel = "Add as child of \"" + selectedNode->name + "\"";
            ImGui::Checkbox(childLabel.c_str(), &s_addAsChild);
        } else {
            s_addAsChild = false;
            ImGui::TextDisabled("Location: Scene Root (no parent selected)");
        }
        uint32_t targetParentId = (selectedNode && s_addAsChild) ? selectedNode->id : 0;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Node item definitions
        struct NodeDef {
            const char* category;
            const char* name;
            const char* description;
            const char* tag;
            std::function<void(uint32_t)> create;
        };

        const std::vector<NodeDef> nodeDefs = {
            // 3D Objects & Primitives
            { "3D Objects & Primitives", "Character NPC", "3D Humanoid character NPC with waypoints, dialogue & patrol", "[3D]",
              [&](uint32_t pid) { auto n = scene.AddCharacterNode("Humanoid NPC", pid); if (n) scene.SelectNode(n->id); } },
            { "3D Objects & Primitives", "Cube", "Box / Cube polygon mesh", "[3D]",
              [&](uint32_t pid) { auto n = scene.AddNode(ShapeType::Cube, "", pid); if (n) scene.SelectNode(n->id); } },
            { "3D Objects & Primitives", "Sphere", "UV Sphere polygon mesh", "[3D]",
              [&](uint32_t pid) { auto n = scene.AddNode(ShapeType::Sphere, "", pid); if (n) scene.SelectNode(n->id); } },
            { "3D Objects & Primitives", "Cylinder", "Cylinder polygon mesh", "[3D]",
              [&](uint32_t pid) { auto n = scene.AddNode(ShapeType::Cylinder, "", pid); if (n) scene.SelectNode(n->id); } },
            { "3D Objects & Primitives", "Cone", "Cone polygon mesh", "[3D]",
              [&](uint32_t pid) { auto n = scene.AddNode(ShapeType::Cone, "", pid); if (n) scene.SelectNode(n->id); } },
            { "3D Objects & Primitives", "Plane", "Ground / Floor plane polygon mesh", "[3D]",
              [&](uint32_t pid) { auto n = scene.AddNode(ShapeType::Plane, "", pid); if (n) scene.SelectNode(n->id); } },

            // 2D UI: Containers & Layout
            { "2D UI: Containers & Layout", "Canvas (UI Root)", "Screen-space Canvas root container", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUICanvasNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Containers & Layout", "Panel", "2D Background / Container panel", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIPanelNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Containers & Layout", "Floating Window", "Movable & closable floating UI window", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIWindowNode("Floating Window", "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Containers & Layout", "Scroll Pane", "Scrollable content container with scrollbars", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIScrollPaneNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Containers & Layout", "Tabs View", "Tabbed container view for page switching", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUITabsNode({"Tab 1", "Tab 2", "Tab 3"}, "", pid); if (n) scene.SelectNode(n->id); } },

            // 2D UI: Basic & Text
            { "2D UI: Basic & Text", "Text Label", "2D Text / Label element", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUITextNode("Text Label", "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Basic & Text", "Button", "2D Interactive clickable button", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIButtonNode("Button", "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Basic & Text", "Text Field", "Single-line interactive text input field", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUITextFieldNode("Enter text...", "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Basic & Text", "Password Field", "Masked password entry input field", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIPasswordFieldNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Basic & Text", "Progress Bar", "Status / progress indicator bar", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIProgressBarNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Basic & Text", "Spinner", "Rotating loading activity spinner", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUISpinnerNode("", pid); if (n) scene.SelectNode(n->id); } },

            // 2D UI: Inputs & Controls
            { "2D UI: Inputs & Controls", "Slider", "Continuous value slider control", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUISliderNode(0.0f, 100.0f, 50.0f, "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Inputs & Controls", "Checkbox", "Toggleable boolean checkbox", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUICheckboxNode("Checkbox", false, "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Inputs & Controls", "Radio Button", "Grouped mutually-exclusive radio button", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIRadioButtonNode("Radio Option", "Group1", false, "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Inputs & Controls", "Dropdown Box", "Drop-down selection menu", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIDropdownNode({"Option 1", "Option 2", "Option 3"}, "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Inputs & Controls", "Date/Time Picker", "Calendar date & time picker widget", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIDatePickerNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Inputs & Controls", "Color Picker", "Live color picker swatch and wheel", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIColorPickerNode("", pid); if (n) scene.SelectNode(n->id); } },

            // 2D UI: Data & Media
            { "2D UI: Data & Media", "Image", "2D Static image / sprite display", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIImageNode("", "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Data & Media", "Video Player", "Video playback display frame", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIVideoNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Data & Media", "Selectable List", "Scrollable list with selectable rows", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUISelectableListNode({"Item 1", "Item 2", "Item 3", "Item 4", "Item 5"}, "", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Data & Media", "Table Grid", "Multi-column data table with headers", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUITableNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Data & Media", "Menu Bar", "Horizontal top menu bar with submenus", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIMenuBarNode("", pid); if (n) scene.SelectNode(n->id); } },

            // 2D UI: Vector Shapes
            { "2D UI: Vector Shapes", "Shape: Rectangle", "2D vector filled / bordered rectangle", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIShapeNode(UIShapeType::Rectangle, "Rectangle", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Vector Shapes", "Shape: Rounded Rect", "2D vector rounded rectangle with corner radius", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIShapeNode(UIShapeType::RoundedRect, "RoundedRect", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Vector Shapes", "Shape: Circle", "2D vector circle / disc shape", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIShapeNode(UIShapeType::Circle, "Circle", pid); if (n) scene.SelectNode(n->id); } },
            { "2D UI: Vector Shapes", "Shape: Triangle", "2D vector triangle shape", "[2D]",
              [&](uint32_t pid) { auto n = scene.AddUIShapeNode(UIShapeType::Triangle, "Triangle", pid); if (n) scene.SelectNode(n->id); } },

            // Entities & Lighting
            { "Entities & Lighting", "Camera", "Perspective 3D scene camera", "[CAM]",
              [&](uint32_t pid) { auto n = scene.AddCameraNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Point Light", "Omnidirectional point light source", "[LGT]",
              [&](uint32_t pid) { auto n = scene.AddLightNode(LightType::Point, "", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Spot Light", "Focused cone spotlight with inner/outer cutoff angles", "[LGT]",
              [&](uint32_t pid) { auto n = scene.AddLightNode(LightType::Spot, "", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Audio Source (2D / 3D)", "Spatial 3D or 2D audio clip player with Doppler & Reverb", "[SND]",
              [&](uint32_t pid) { auto n = scene.AddAudioNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Particle Emitter", "CPU / Niagara particle VFX emitter (Fire, Smoke, Sparks)", "[VFX]",
              [&](uint32_t pid) { auto n = scene.AddParticleNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Terrain (Landscape)", "Subdivided landscape grid with heightmap sculpting & mountains", "[TRN]",
              [&](uint32_t pid) { auto n = scene.AddTerrainNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Decal Projector", "3D OBB surface decal projector with normal blending", "[DCL]",
              [&](uint32_t pid) { auto n = scene.AddDecalNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Spline / 3D Curve", "3D Catmull-Rom / Bezier spline path with control points", "[SPL]",
              [&](uint32_t pid) { auto n = scene.AddSplineNode("", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Directional Light", "Sun / Directional light source", "[LGT]",
              [&](uint32_t pid) { auto n = scene.AddLightNode(LightType::Directional, "", pid); if (n) scene.SelectNode(n->id); } },
            { "Entities & Lighting", "Empty Group", "Empty transform node for grouping", "[GRP]",
              [&](uint32_t pid) { auto n = scene.AddEmptyNode("", pid); if (n) scene.SelectNode(n->id); } },

            // Physics & Colliders
            { "Physics & Colliders", "Box Collider", "3D Box collision shape", "[COL]",
              [&](uint32_t pid) { auto n = scene.AddColliderNode(ColliderType::Box, "", pid); if (n) scene.SelectNode(n->id); } },
            { "Physics & Colliders", "Sphere Collider", "3D Sphere collision shape", "[COL]",
              [&](uint32_t pid) { auto n = scene.AddColliderNode(ColliderType::Sphere, "", pid); if (n) scene.SelectNode(n->id); } },
            { "Physics & Colliders", "Capsule Collider", "3D Capsule collision shape", "[COL]",
              [&](uint32_t pid) { auto n = scene.AddColliderNode(ColliderType::Capsule, "", pid); if (n) scene.SelectNode(n->id); } },

            // 3D Model Assets
            { "3D Model Assets", "Import 3D Model...", "Load external FBX / OBJ model", "[OBJ]",
              [&](uint32_t) { OpenImportDialog(); } }
        };

        std::string filterStr = s_filterBuf;
        std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

        ImGui::BeginChild("NodeListScroll", ImVec2(0, 400), true);

        if (!filterStr.empty()) {
            // Filtered list
            int matchCount = 0;
            for (const auto& item : nodeDefs) {
                std::string nameLower = item.name;
                std::string catLower = item.category;
                std::string descLower = item.description;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                std::transform(catLower.begin(), catLower.end(), catLower.begin(), ::tolower);
                std::transform(descLower.begin(), descLower.end(), descLower.begin(), ::tolower);

                if (nameLower.find(filterStr) != std::string::npos ||
                    catLower.find(filterStr) != std::string::npos ||
                    descLower.find(filterStr) != std::string::npos) {
                    matchCount++;
                    std::string label = std::string(item.tag) + "  " + item.name + "##filtered";
                    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 22))) {
                        JsonValue before = scene.Serialize();
                        int32_t beforeSel = scene.GetSelectedId();
                        item.create(targetParentId);
                        Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), std::string("Add ") + item.name);
                        s_filterBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\nCategory: %s", item.description, item.category);
                    }
                }
            }
            if (matchCount == 0) {
                ImGui::TextDisabled("No node types matching '%s'", s_filterBuf);
            }
        } else {
            // Grouped by Category with subheadings
            std::string currentCategory = "";
            for (const auto& item : nodeDefs) {
                if (item.category != currentCategory) {
                    currentCategory = item.category;
                    if (!currentCategory.empty()) {
                        ImGui::Spacing();
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.70f, 0.95f, 1.0f));
                    ImGui::TextDisabled("--- %s ---", currentCategory.c_str());
                    ImGui::PopStyleColor();
                }

                std::string label = std::string(item.tag) + "  " + item.name;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 22))) {
                    JsonValue before = scene.Serialize();
                    int32_t beforeSel = scene.GetSelectedId();
                    item.create(targetParentId);
                    Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), std::string("Add ") + item.name);
                    s_filterBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", item.description);
                }
            }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

void EditorUI::RenderHierarchy(Scene& scene, Camera& camera) {
    {
        struct ScopedHierarchyHeaderWrap {
            ScopedHierarchyHeaderWrap() { ImGui::PushTextWrapPos(0.0f); }
            ~ScopedHierarchyHeaderWrap() { ImGui::PopTextWrapPos(); }
        } headerWrap;

        // Toolbar: clean Add Node button and Import button
        float availWidth = ImGui::GetContentRegionAvail().x;
        float addBtnWidth = (availWidth > 220.0f) ? (availWidth - 110.0f) : -1.0f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.54f, 0.84f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.36f, 0.60f, 1.0f));
        if (ImGui::Button(TR("+ Add Node..."), ImVec2(addBtnWidth, 26))) {
            ImGui::OpenPopup("AddNodePopupWindow");
        }
        ImGui::PopStyleColor(3);

        if (availWidth > 220.0f) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.36f, 0.28f, 1.0f));
            if (ImGui::Button(TR("Import 3D..."), ImVec2(-1, 26))) {
                OpenImportDialog();
            }
            ImGui::PopStyleColor(3);
        }

        if (m_openAddNodePopup || (ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))) {
            ImGui::OpenPopup("AddNodePopupWindow");
            m_openAddNodePopup = false;
        }

        // Render the Add Node popup window
        RenderAddNodePopup(scene);

        // Quick Undo / Redo / Clipboard Toolbar in Hierarchy (always visible even when no node is selected)
        ImGui::Spacing();
        ImGui::BeginDisabled(!Core::UndoRedoManager::Get().CanUndo());
        if (ImGui::SmallButton(TR("Undo"))) {
            Core::UndoRedoManager::Get().Undo(scene);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && Core::UndoRedoManager::Get().CanUndo()) {
            ImGui::SetTooltip("Undo: %s (Ctrl+Z)", Core::UndoRedoManager::Get().GetUndoDescription().c_str());
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!Core::UndoRedoManager::Get().CanRedo());
        if (ImGui::SmallButton(TR("Redo"))) {
            Core::UndoRedoManager::Get().Redo(scene);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && Core::UndoRedoManager::Get().CanRedo()) {
            ImGui::SetTooltip("Redo: %s (Ctrl+Y)", Core::UndoRedoManager::Get().GetRedoDescription().c_str());
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(scene.GetSelectedId() <= 0);
        if (ImGui::SmallButton(TR("Copy"))) {
            scene.CopySelected();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!scene.HasClipboardData());
        if (ImGui::SmallButton(TR("Paste"))) {
            JsonValue before = scene.Serialize();
            int32_t beforeSel = scene.GetSelectedId();
            scene.PasteNode();
            Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Paste Node");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(scene.GetSelectedId() <= 0);
        if (ImGui::SmallButton(TR("Duplicate"))) {
            JsonValue before = scene.Serialize();
            int32_t beforeSel = scene.GetSelectedId();
            scene.DuplicateNode(static_cast<uint32_t>(beforeSel));
            Core::UndoRedoManager::Get().RecordSnapshot(before, scene.Serialize(), beforeSel, scene.GetSelectedId(), "Duplicate Node");
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("%s (%zu %s)", TR("Scene Hierarchy"), scene.GetNodes().size(), TR("nodes"));
        if (scene.GetSelectedId() > 0) {
            float availX = ImGui::GetContentRegionAvail().x;
            if (availX > 60.0f) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 65.0f);
                if (ImGui::ArrowButton("##ReorderUp", ImGuiDir_Up)) {
                    scene.MoveNodeUp((uint32_t)scene.GetSelectedId());
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move Selected Node Up (Alt+Up)");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##ReorderDown", ImGuiDir_Down)) {
                    scene.MoveNodeDown((uint32_t)scene.GetSelectedId());
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move Selected Node Down (Alt+Down)");
            }
        }
        ImGui::TextDisabled("Right-click or drag (edges: reorder, center: nest)");
    }
    ImGui::Spacing();

    ImGui::BeginChild("NodeList", ImVec2(0, 0), true);

    uint32_t nodeToDelete = 0;
    bool requestDelete = false;

    // Render root nodes (nodes with parentId == 0)
    auto rootNodes = scene.GetRootNodes();
    for (const auto& rootNode : rootNodes) {
        RenderNodeTree(scene, rootNode->id, nodeToDelete, requestDelete);
    }

    // Drag-and-drop onto empty list area unparents back to root
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->ContentRegionRect, ImGui::GetID("NodeListDropRoot"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_NODE")) {
            uint32_t draggedId = *(const uint32_t*)payload->Data;
            scene.Unparent(draggedId);
        }
        ImGui::EndDragDropTarget();
    }

    // Context menu on empty area
    if (ImGui::BeginPopupContextWindow("HierarchyEmptyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem(TR("Instantiate Subscene / Prefab (.scene)..."))) {
            std::string scenePath;
            if (OpenSceneFileDialog(scenePath, "Select Scene to Instantiate")) {
                scene.InstantiateScene(scenePath, 0);
            }
        }
        ImGui::Separator();
        if (ImGui::BeginMenu(TR("Add Node / Entity..."))) {
            if (ImGui::BeginMenu(TR("3D Objects & Primitives"))) {
                if (ImGui::MenuItem(TR("Cube"))) { auto n = scene.AddNode(ShapeType::Cube); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Sphere"))) { auto n = scene.AddNode(ShapeType::Sphere); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Cylinder"))) { auto n = scene.AddNode(ShapeType::Cylinder); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Cone"))) { auto n = scene.AddNode(ShapeType::Cone); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Plane"))) { auto n = scene.AddNode(ShapeType::Plane); if (n) scene.SelectNode(n->id); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("2D User Interface"))) {
                if (ImGui::BeginMenu(TR("Containers & Windows"))) {
                    if (ImGui::MenuItem(TR("Canvas (UI Root)"))) { auto n = scene.AddUICanvasNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Panel"))) { auto n = scene.AddUIPanelNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Floating Window"))) { auto n = scene.AddUIWindowNode("Floating Window"); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Scroll Pane"))) { auto n = scene.AddUIScrollPaneNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Tabs View"))) { auto n = scene.AddUITabsNode({"Tab 1", "Tab 2", "Tab 3"}); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem("Box Container (Vertical)")) { auto n = scene.AddUIBoxContainerNode(BoxOrientation::Vertical); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem("Box Container (Horizontal)")) { auto n = scene.AddUIBoxContainerNode(BoxOrientation::Horizontal); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem("Grid Container")) { auto n = scene.AddUIGridContainerNode(4); if (n) scene.SelectNode(n->id); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Basic & Text"))) {
                    if (ImGui::MenuItem(TR("Text Label"))) { auto n = scene.AddUITextNode("Text Label"); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Button"))) { auto n = scene.AddUIButtonNode("Button"); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Text Field"))) { auto n = scene.AddUITextFieldNode("Enter text..."); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Password Field"))) { auto n = scene.AddUIPasswordFieldNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Progress Bar"))) { auto n = scene.AddUIProgressBarNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Spinner"))) { auto n = scene.AddUISpinnerNode(); if (n) scene.SelectNode(n->id); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Inputs & Controls"))) {
                    if (ImGui::MenuItem(TR("Slider"))) { auto n = scene.AddUISliderNode(0.0f, 100.0f, 50.0f); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Checkbox"))) { auto n = scene.AddUICheckboxNode("Checkbox", false); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Radio Button"))) { auto n = scene.AddUIRadioButtonNode("Radio Option", "Group1", false); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Dropdown Box"))) { auto n = scene.AddUIDropdownNode({"Option 1", "Option 2", "Option 3"}); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Date/Time Picker"))) { auto n = scene.AddUIDatePickerNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Color Picker"))) { auto n = scene.AddUIColorPickerNode(); if (n) scene.SelectNode(n->id); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Data & Media"))) {
                    if (ImGui::MenuItem(TR("Image"))) { auto n = scene.AddUIImageNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Video Player"))) { auto n = scene.AddUIVideoNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Selectable List"))) { auto n = scene.AddUISelectableListNode({"Item 1", "Item 2", "Item 3"}); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Table Grid"))) { auto n = scene.AddUITableNode(); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Menu Bar"))) { auto n = scene.AddUIMenuBarNode(); if (n) scene.SelectNode(n->id); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TR("Vector Shapes"))) {
                    if (ImGui::MenuItem(TR("Rectangle"))) { auto n = scene.AddUIShapeNode(UIShapeType::Rectangle, "Rectangle"); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Rounded Rectangle"))) { auto n = scene.AddUIShapeNode(UIShapeType::RoundedRect, "RoundedRect"); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Circle"))) { auto n = scene.AddUIShapeNode(UIShapeType::Circle, "Circle"); if (n) scene.SelectNode(n->id); }
                    if (ImGui::MenuItem(TR("Triangle"))) { auto n = scene.AddUIShapeNode(UIShapeType::Triangle, "Triangle"); if (n) scene.SelectNode(n->id); }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("Entities & Lighting"))) {
                if (ImGui::MenuItem(TR("Camera"))) { auto n = scene.AddCameraNode(); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Point Light"))) { auto n = scene.AddLightNode(LightType::Point); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Directional Light"))) { auto n = scene.AddLightNode(LightType::Directional); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Spot Light"))) { auto n = scene.AddLightNode(LightType::Spot); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Audio Source (2D / 3D)"))) { auto n = scene.AddAudioNode(); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Particle Emitter (CPU / Niagara)"))) { auto n = scene.AddParticleNode(); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Terrain (Landscape)"))) { auto n = scene.AddTerrainNode(); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Decal Projector"))) { auto n = scene.AddDecalNode(); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Spline / 3D Curve"))) { auto n = scene.AddSplineNode(); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Empty Group Node"))) { auto n = scene.AddEmptyNode(); if (n) scene.SelectNode(n->id); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR("Physics & Colliders"))) {
                if (ImGui::MenuItem(TR("Box Collider"))) { auto n = scene.AddColliderNode(ColliderType::Box); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Sphere Collider"))) { auto n = scene.AddColliderNode(ColliderType::Sphere); if (n) scene.SelectNode(n->id); }
                if (ImGui::MenuItem(TR("Capsule Collider"))) { auto n = scene.AddColliderNode(ColliderType::Capsule); if (n) scene.SelectNode(n->id); }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Import 3D Model..."))) {
                OpenImportDialog();
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (requestDelete) {
        scene.DeleteNode(nodeToDelete);
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
        scene.DeleteSelected();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && scene.GetSelectedId() > 0) {
        if (ImGui::GetIO().KeyAlt) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                scene.MoveNodeUp((uint32_t)scene.GetSelectedId());
            } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                scene.MoveNodeDown((uint32_t)scene.GetSelectedId());
            }
        }
    }

    ImGui::EndChild();
}

static bool OpenTextureFileDialog(std::string& outPath, const char* title) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Supported Images (*.png;*.jpg;*.jpeg;*.tga;*.bmp)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0PNG Images (*.png)\0*.png\0JPEG Images (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = ofn.lpstrFile;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        return true;
    }
#endif
    return false;
}

static bool OpenScriptFileDialog(std::string& outPath, const char* title) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Lua Script (*.lua)\0*.lua\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = ofn.lpstrFile;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        return true;
    }
#endif
    return false;
}

static bool OpenModelFileDialog(std::string& outPath, const char* title) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Supported 3D Models (*.fbx;*.obj;*.gltf;*.glb)\0*.fbx;*.obj;*.gltf;*.glb\0FBX Models (*.fbx)\0*.fbx\0glTF 2.0 Models (*.gltf;*.glb)\0*.gltf;*.glb\0OBJ Models (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = ofn.lpstrFile;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        return true;
    }
#endif
    return false;
}

static bool OpenLUTFileDialog(std::string& outPath, const char* title) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Colour LUTs (*.cube;*.png;*.jpg;*.tga)\0*.cube;*.png;*.jpg;*.tga\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = ofn.lpstrFile;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        return true;
    }
#endif
    return false;
}

// Shader graph files (*.shadergraph, JSON). save = true shows a Save dialog.
static bool ShaderGraphFileDialog(std::string& outPath, const char* title, bool save) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";
    if (save) strncpy(szFile, outPath.c_str(), sizeof(szFile) - 1);
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Shader Graph (*.shadergraph)\0*.shadergraph\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "shadergraph";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if ((save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn)) == TRUE) {
        outPath = ofn.lpstrFile;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        return true;
    }
#endif
    return false;
}

static bool OpenHDRFileDialog(std::string& outPath, const char* title) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "HDR Images (*.hdr)\0*.hdr\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = ofn.lpstrFile;
        std::replace(outPath.begin(), outPath.end(), '\\', '/');
        return true;
    }
#endif
    return false;
}

// Pour spout settings (empty nodes; Scene > Add Entity > Pour Spout)
static void RenderPourSpoutProperties(Scene& scene, Node& node) {
    ImGui::Checkbox("Pour Spout", &node.isPourSpout);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Liquid streams out along this node's forward axis (-Z) once that axis turns\n"
                                                  "far enough from straight up. Put it at the lip of a jug, as a child of the jug,\n"
                                                  "facing out of the opening (up when the jug stands upright).\n"
                                                  "The stream fills a Liquid it lands in (Play mode).");
    if (!node.isPourSpout) return;
    bool automatic = node.pourFlow < 0.0f;
    if (ImGui::Checkbox("Flow From Tilt", &automatic)) node.pourFlow = automatic ? -1.0f : 1.0f;
    if (automatic && node.pourSourceId != 0) {
        ImGui::TextDisabled("Pours when the source liquid rises above the spout:\nput it on the rim, where liquid would spill over.");
    } else if (automatic) {
        ImGui::SliderFloat("Start Angle", &node.pourStartAngle, 0.0f, 150.0f, "%.0f deg");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far the spout must turn from facing straight up before liquid comes out.\nFull flow 25 degrees further. With a Source Liquid it pours by the liquid's level instead.");
    } else {
        ImGui::SliderFloat("Flow", &node.pourFlow, 0.0f, 1.0f, "%.2f");
    }
    float radiusMm = node.pourStreamRadius * 1000.0f;
    if (ImGui::SliderFloat("Stream Radius (mm)", &radiusMm, 1.0f, 100.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) node.pourStreamRadius = radiusMm / 1000.0f;
    ImGui::SliderFloat("Pour Speed (m/s)", &node.pourSpeed, 0.1f, 5.0f, "%.2f");
    ImGui::ColorEdit4("Liquid Colour", glm::value_ptr(node.pourColor));
    // The liquid it drains
    std::string current = "(never runs out)";
    if (auto source = scene.GetNodeById(node.pourSourceId)) current = source->name;
    if (ImGui::BeginCombo("Source Liquid", current.c_str())) {
        if (ImGui::Selectable("(never runs out)", node.pourSourceId == 0)) node.pourSourceId = 0;
        for (const auto& other : scene.GetNodes()) {
            if (!other || !other->isLiquid) continue;
            if (ImGui::Selectable((other->name + "##" + std::to_string(other->id)).c_str(), node.pourSourceId == other->id)) node.pourSourceId = other->id;
        }
        ImGui::EndCombo();
    }
    if (node.pourCurrentFlow > 0.0f) ImGui::TextDisabled("Pouring: %.0f%%", node.pourCurrentFlow * 100.0f);
}

// Fluid volume settings (empty nodes; Scene > Add Entity > Fluid Volume)
static void RenderFluidVolumeProperties(Node& node) {
    ImGui::Checkbox("Fluid Volume", &node.isFluidVolume);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Real simulated fluid in a box centred on this node: it flows, splashes and\n"
                                                  "settles, and solid objects in the box push it around.");
    if (!node.isFluidVolume) return;
    ImGui::DragFloat3("Box Size (m)", &node.fluidSize.x, 0.01f, 0.05f, 20.0f, "%.2f");
    const char* starts[] = { "Layer on the floor", "Column on one side" };
    ImGui::Combo("Starts As", &node.fluidStart, starts, 2);
    ImGui::SliderFloat("Fill", &node.fluidFill, 0.02f, 0.9f, "%.2f");
    float spacingCm = node.fluidSpacing * 100.0f;
    if (ImGui::SliderFloat("Particle Spacing (cm)", &spacingCm, 0.5f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) node.fluidSpacing = spacingCm / 100.0f;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smaller = more detail but more particles (slower). Simulated on the GPU, up to 262,144 particles;\n"
                                                  "the spacing grows if the fill would need more.");
    ImGui::SliderFloat("Viscosity", &node.fluidViscosity, 0.0f, 0.5f, "%.2f");
    ImGui::ColorEdit3("Fluid Colour", glm::value_ptr(node.fluidColor));
    float depthCm = node.fluidColorDepth * 100.0f;
    if (ImGui::SliderFloat("Colour Depth (cm)", &depthCm, 1.0f, 500.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) node.fluidColorDepth = depthCm / 100.0f;
    if (ImGui::Button("Reset Fluid")) node.fluidResetRequested = true;
}

// Ocean surface settings (Scene > Add Entity > Ocean)
static void RenderOceanProperties(Node& node) {
    if (!ImGui::CollapsingHeader("Ocean", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Checkbox("Ocean Surface", &node.isOcean);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws this node as a wavy water surface at its height, following the\n"
                                                  "camera across the whole area below. Rotation and scale don't apply.");
    ImGui::DragFloat2("Size (m)", &node.oceanSize.x, 5.0f, 10.0f, 100000.0f, "%.0f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Area the water covers (x, z), centred on the node.");
    ImGui::TextDisabled("Waves");
    ImGui::SliderFloat("Wave Height (m)", &node.waveHeight, 0.0f, 10.0f, "%.2f");
    ImGui::SliderFloat("Wavelength (m)", &node.waveLength, 2.0f, 300.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Length of the longest waves. Shorter waves are added on top.");
    ImGui::SliderFloat("Wind Direction", &node.waveDirection, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("Choppiness", &node.waveChoppiness, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = rounded swell, 1 = sharp crests and flat troughs.");
    ImGui::SliderFloat("Wave Speed", &node.waveSpeed, 0.0f, 3.0f, "%.2f");
    ImGui::TextDisabled("Water");
    ImGui::ColorEdit3("Shallow Tint", glm::value_ptr(node.waterShallowColor));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tint of the sand, rocks and objects seen through shallow water.");
    ImGui::ColorEdit3("Deep Colour", glm::value_ptr(node.waterDeepColor));
    ImGui::SliderFloat("Clarity (m)", &node.waterClarity, 0.2f, 50.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far you can see into the water. Low = murky, high = tropical.");
    ImGui::SliderFloat("Surface Roughness", &node.waterRoughness, 0.01f, 0.5f, "%.2f");
    ImGui::TextDisabled("Foam");
    ImGui::SliderFloat("Whitecaps", &node.whitecaps, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foam where wave crests bunch up. Rises with Choppiness and Wave Height.");
    ImGui::SliderFloat("Shore Foam Width (m)", &node.shoreFoamWidth, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Sound Volume", &node.oceanSoundVolume, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The surf's wash near the water (a muffled rumble under it) and waves\ncrashing on rocks, in Play.");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Band of foam where the water meets beaches, rocks and objects. 0 = none.");
}

// Reflection probe toggle, shown for meshes and empty nodes
static void RenderReflectionProbeProperties(Node& node) {
    ImGui::Checkbox("Reflection Probe", &node.isReflectionProbe);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Captures the surroundings from this object's position, so shiny\nsurfaces nearby reflect the room instead of the sky.\nPlace it in open space (e.g. the middle of a room), not inside an object.\nUp to 4 probes per scene. Recaptured when a probe moves,\nor with 'Recapture Probes' in the Rendering window.");
    if (node.isReflectionProbe) {
        ImGui::Checkbox("Box Projection", &node.probeBoxProjection);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("For rooms and corridors: lines reflections up with the walls and floor.\nSet the box to the room's size; it is centred on the probe, and the\nprobe affects everything inside it (instead of the radius).");
        if (node.probeBoxProjection) {
            ImGui::DragFloat3("Box Size (m)", &node.probeBoxSize.x, 0.1f, 0.5f, 500.0f, "%.1f");
        } else {
            ImGui::SliderFloat("Probe Radius (m)", &node.probeRadius, 1.0f, 100.0f, "%.1f");
        }
    }
}

static void RenderTextureSlot(const char* label, const char* id, std::string& path, const char* browseTitle, std::function<std::string(const std::string&)> onImport = nullptr) {
    ImGui::TextWrapped("%s", TR(label));
    ImGui::PushID(id);

    char buf[512];
    strncpy(buf, path.c_str(), sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    float availW = ImGui::GetContentRegionAvail().x;
    float buttonW = 68.0f;
    float clearW = 24.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float inputW = availW - buttonW - clearW - (spacing * 2.0f);

    if (inputW < 60.0f) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Path", buf, sizeof(buf))) {
            path = buf;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !path.empty() && onImport) {
            path = onImport(path);
        }
        if (ImGui::IsItemHovered() && !path.empty()) {
            ImGui::SetTooltip("%s", path.c_str());
        }
        if (ImGui::Button(TR("Browse..."), ImVec2(availW - clearW - spacing, 0))) {
            std::string chosen;
            if (OpenTextureFileDialog(chosen, browseTitle)) {
                path = onImport ? onImport(chosen) : chosen;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(clearW, 0))) {
            path.clear();
        }
    } else {
        ImGui::SetNextItemWidth(inputW);
        if (ImGui::InputText("##Path", buf, sizeof(buf))) {
            path = buf;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !path.empty() && onImport) {
            path = onImport(path);
        }
        if (ImGui::IsItemHovered() && !path.empty()) {
            ImGui::SetTooltip("%s", path.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Browse..."), ImVec2(buttonW, 0))) {
            std::string chosen;
            if (OpenTextureFileDialog(chosen, browseTitle)) {
                path = onImport ? onImport(chosen) : chosen;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(clearW, 0))) {
            path.clear();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear this texture slot");
    }

    ImGui::PopID();
}

void EditorUI::RenderInspector(Scene& scene, Camera& camera) {
    struct ScopedInspectorWrap {
        ScopedInspectorWrap() { ImGui::PushTextWrapPos(0.0f); }
        ~ScopedInspectorWrap() { ImGui::PopTextWrapPos(); }
    } inspectorWrap;

    auto selectedNode = scene.GetSelectedNode();

    // Camera Orbit Pivot Section (Always available)
    if (ImGui::CollapsingHeader(TR("Camera Pivot & Orbit Center"), ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 pivot = camera.GetTarget();
        ImGui::Text("%s", TR("Orbit Pivot Point:"));
        if (ImGui::DragFloat3("##Pivot", glm::value_ptr(pivot), 0.1f)) {
            camera.SetTarget(pivot);
        }

        if (selectedNode) {
            if (ImGui::Button(TR("Focus Pivot on Selected Node [F]"), ImVec2(-1, 26))) {
                camera.FocusOn(selectedNode->GetWorldPosition(scene));
            }
        }
        if (ImGui::Button(TR("Reset Pivot to Origin (0,0,0)"), ImVec2(-1, 24))) {
            camera.SetTarget(glm::vec3(0.0f));
        }

        ImGui::Spacing();
        ImGui::Checkbox(TR("Show 3D Pivot Marker"), &m_showPivotAxisMarker);
        ImGui::SameLine();
        ImGui::Checkbox(TR("Show Ground Grid"), &m_showGrid);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!selectedNode) {
        ImGui::BeginDisabled(!Core::UndoRedoManager::Get().CanUndo());
        if (ImGui::Button(TR("Undo (Ctrl+Z)"))) {
            Core::UndoRedoManager::Get().Undo(scene);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!Core::UndoRedoManager::Get().CanRedo());
        if (ImGui::Button(TR("Redo (Ctrl+Y)"))) {
            Core::UndoRedoManager::Get().Redo(scene);
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::TextDisabled("%s", TR("No node selected."));
        ImGui::TextWrapped("%s", TR("Click any node in Scene Hierarchy or Viewport to inspect properties and transforms."));
        return;
    }

    ImGui::Text("Node ID: %u", selectedNode->id);
    ImGui::SameLine();
    if (selectedNode->parentId != 0) {
        auto parent = scene.GetNodeById(selectedNode->parentId);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "| Parent: %s", parent ? parent->name.c_str() : "Root");
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Unparent"))) {
            scene.Unparent(selectedNode->id);
        }
    } else {
        ImGui::TextDisabled("| Root Node");
    }
    float availRight = ImGui::GetContentRegionAvail().x;
    if (availRight > 55.0f) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
        if (ImGui::ArrowButton("##InspUp", ImGuiDir_Up)) {
            scene.MoveNodeUp(selectedNode->id);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move Up in Hierarchy (Alt+Up)");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##InspDown", ImGuiDir_Down)) {
            scene.MoveNodeDown(selectedNode->id);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move Down in Hierarchy (Alt+Down)");
    }

    ImGui::Spacing();

    // Name editor
    char nameBuffer[128];
    strncpy(nameBuffer, selectedNode->name.c_str(), sizeof(nameBuffer));
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
    ImGui::Text("%s", TR("Node Name:"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##NodeName", nameBuffer, sizeof(nameBuffer))) {
        selectedNode->name = nameBuffer;
    }

    if (selectedNode->nodeType == NodeType::Camera) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Node Type: Scene Camera");
    } else if (selectedNode->nodeType == NodeType::Light) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Node Type: Scene Light (%s)",
                           selectedNode->lightType == LightType::Point ? "Point" : (selectedNode->lightType == LightType::Spot ? "Spot" : "Directional"));
    } else if (selectedNode->nodeType == NodeType::AudioSource) {
        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.75f, 1.0f), "Node Type: Spatial / 2D Audio Source");
    } else if (selectedNode->nodeType == NodeType::ParticleEmitter) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Node Type: Particle Emitter (VFX)");
    } else if (selectedNode->nodeType == NodeType::Terrain) {
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.45f, 1.0f), "Node Type: Terrain Landscape");
    } else if (selectedNode->nodeType == NodeType::Decal) {
        ImGui::TextColored(ImVec4(0.85f, 0.55f, 1.0f, 1.0f), "Node Type: 3D Decal Projector");
    } else if (selectedNode->nodeType == NodeType::Spline) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Node Type: 3D Spline / Curve");
    } else if (selectedNode->nodeType == NodeType::Collider) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.5f, 1.0f), "Node Type: Collider (%s)",
                           ColliderTypeToString(selectedNode->colliderType));
    } else if (selectedNode->nodeType == NodeType::Empty) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "Node Type: Empty Group Node");
    } else {
        // 3D Mesh / Character Node inspector header & Model Asset slot
        if (selectedNode->nodeType == NodeType::Character) {
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Node Type: Humanoid Character NPC");
        } else if (selectedNode->type == ShapeType::Custom) {
            std::string ext = "";
            if (!selectedNode->customMeshName.empty()) {
                ext = std::filesystem::path(selectedNode->customMeshName).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            }
            const char* kind = (ext == ".fbx")
                ? (selectedNode->hasSkeleton ? "Rigged FBX Character / Model" : "Imported FBX Mesh")
                : ((ext == ".gltf" || ext == ".glb") ? "Imported glTF 2.0 Model" : "Imported OBJ Mesh");
            ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.60f, 1.0f), "Node Type: %s", kind);
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "Node Type: 3D Mesh (%s)", ShapeTypeToString(selectedNode->type));
        }

        // Shape Type selector combo
        const char* shapeOptions[] = { "Cube", "Sphere", "Cylinder", "Cone", "Plane", "Custom 3D Model Asset" };
        int currentShapeIdx = (selectedNode->type == ShapeType::Custom) ? 5 : static_cast<int>(selectedNode->type);
        ImGui::Text("%s", TR("Shape Type:"));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##ShapeType", &currentShapeIdx, shapeOptions, IM_ARRAYSIZE(shapeOptions))) {
            if (currentShapeIdx == 5) {
                selectedNode->type = ShapeType::Custom;
            } else {
                selectedNode->type = static_cast<ShapeType>(currentShapeIdx);
                selectedNode->customMeshName.clear();
                selectedNode->hasSkeleton = false;
            }
        }

        // 3D Model Asset Slot (Rendered for Custom meshes or Character NPCs)
        if (selectedNode->type == ShapeType::Custom || selectedNode->nodeType == NodeType::Character) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "%s", TR("3D Model Asset (.fbx, .gltf, .glb, .obj):"));
            ImGui::PushID("ModelAssetSlot");

            char modelBuf[512];
            strncpy(modelBuf, selectedNode->customMeshName.c_str(), sizeof(modelBuf));
            modelBuf[sizeof(modelBuf) - 1] = '\0';

            float availW = ImGui::GetContentRegionAvail().x;
            float buttonW = 68.0f;
            float clearW = 24.0f;
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float inputW = availW - buttonW - clearW - (spacing * 2.0f);

            if (inputW < 60.0f) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##ModelPath", modelBuf, sizeof(modelBuf))) {
                    selectedNode->customMeshName = modelBuf;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && !selectedNode->customMeshName.empty() && m_onAssignModel) {
                    m_onAssignModel(selectedNode->customMeshName, *selectedNode);
                }
                if (ImGui::IsItemHovered() && !selectedNode->customMeshName.empty()) {
                    ImGui::SetTooltip("%s", selectedNode->customMeshName.c_str());
                }
                if (ImGui::Button(TR("Browse..."), ImVec2(availW - clearW - spacing, 0))) {
                    std::string chosen;
                    if (OpenModelFileDialog(chosen, "Select 3D Model Asset (*.fbx, *.gltf, *.glb, *.obj)")) {
                        if (m_onAssignModel) m_onAssignModel(chosen, *selectedNode);
                        else { selectedNode->customMeshName = chosen; selectedNode->type = ShapeType::Custom; }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("X", ImVec2(clearW, 0))) {
                    selectedNode->customMeshName.clear();
                    selectedNode->type = ShapeType::Cube;
                    selectedNode->hasSkeleton = false;
                    selectedNode->vertexCount = 0;
                    selectedNode->triangleCount = 0;
                }
            } else {
                ImGui::SetNextItemWidth(inputW);
                if (ImGui::InputText("##ModelPath", modelBuf, sizeof(modelBuf))) {
                    selectedNode->customMeshName = modelBuf;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && !selectedNode->customMeshName.empty() && m_onAssignModel) {
                    m_onAssignModel(selectedNode->customMeshName, *selectedNode);
                }
                if (ImGui::IsItemHovered() && !selectedNode->customMeshName.empty()) {
                    ImGui::SetTooltip("%s", selectedNode->customMeshName.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(TR("Browse..."), ImVec2(buttonW, 0))) {
                    std::string chosen;
                    if (OpenModelFileDialog(chosen, "Select 3D Model Asset (*.fbx, *.gltf, *.glb, *.obj)")) {
                        if (m_onAssignModel) m_onAssignModel(chosen, *selectedNode);
                        else { selectedNode->customMeshName = chosen; selectedNode->type = ShapeType::Custom; }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("X", ImVec2(clearW, 0))) {
                    selectedNode->customMeshName.clear();
                    selectedNode->type = ShapeType::Cube;
                    selectedNode->hasSkeleton = false;
                    selectedNode->vertexCount = 0;
                    selectedNode->triangleCount = 0;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear custom model and revert to default Cube");
            ImGui::PopID();

            // Quick Dropdown for Available Project Models
            if (m_onGetAvailableModels) {
                std::vector<std::string> available = m_onGetAvailableModels();
                if (!available.empty()) {
                    int selectedIdx = -1;
                    std::vector<std::string> comboItems;
                    comboItems.push_back("-- Select Available Project Model --");
                    for (size_t i = 0; i < available.size(); ++i) {
                        comboItems.push_back(available[i]);
                        if (available[i] == selectedNode->customMeshName) {
                            selectedIdx = static_cast<int>(i) + 1;
                        }
                    }

                    int currentCombo = (selectedIdx >= 0) ? selectedIdx : 0;
                    std::vector<const char*> itemPtrs;
                    for (const auto& item : comboItems) itemPtrs.push_back(item.c_str());

                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##AvailableModelsCombo", &currentCombo, itemPtrs.data(), static_cast<int>(itemPtrs.size()))) {
                        if (currentCombo > 0 && currentCombo <= static_cast<int>(available.size())) {
                            std::string chosenModel = available[currentCombo - 1];
                            if (m_onAssignModel) {
                                m_onAssignModel(chosenModel, *selectedNode);
                            } else {
                                selectedNode->customMeshName = chosenModel;
                                selectedNode->type = ShapeType::Custom;
                            }
                        }
                    }
                }
            }

            if (selectedNode->type == ShapeType::Custom && !selectedNode->customMeshName.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.0f), "%u vertices  |  %u triangles%s",
                                   selectedNode->vertexCount, selectedNode->triangleCount,
                                   selectedNode->hasSkeleton ? "  |  Rigged Armature" : "");
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Transform Section
    // Transform Section

    // Nested Scene Instance (Prefab) Banner
    if (selectedNode->IsSceneInstance()) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.45f, 0.55f, 0.8f));
        if (ImGui::CollapsingHeader(TR("Nested Scene Instance (Prefab)"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Source Scene: %s", selectedNode->instanceScenePath.c_str());
            if (ImGui::Button(TR("Reload Prefab From Disk"), ImVec2(-1, 26))) {
                scene.ReloadSceneInstance(selectedNode->id);
            }
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    NodeInspector::RenderTransform(scene, selectedNode);

    // Level of Detail (LOD) & Draw Distance Culling Section (for Mesh nodes)
    if (selectedNode->nodeType == NodeType::Mesh || selectedNode->nodeType == NodeType::Character) {
        if (ImGui::CollapsingHeader(TR("Level of Detail (LOD) & Mesh Decimation"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox(TR("Enable Distance-Based LOD"), &selectedNode->enableLOD);
            ImGui::SameLine();
            ImGui::TextDisabled("(Phase 1 & 2)");

            ImGui::DragFloat("Max Draw Distance (Culling)", &selectedNode->maxDrawDistance, 1.0f, 0.0f, 1000.0f, "%.1f m (0 = Infinite)");

            // Real-time camera distance indicator
            float distToCam = glm::distance(camera.GetPosition(), selectedNode->GetWorldPosition(scene));
            std::string activeMesh = selectedNode->GetActiveLODMeshKey(distToCam);
            int activeTier = selectedNode->currentActiveLOD;

            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Camera Distance: %.1f m", distToCam);
            ImGui::SameLine();
            if (activeTier == 0) {
                ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.5f, 1.0f), "| Active: [LOD0 (Base)]");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "| Active: [LOD%d (%s)]", activeTier, activeMesh.c_str());
            }

            // Forced LOD preview combo
            const char* forcedLODOptions[] = { "Automatic (Distance)", "Force LOD0 (Base)", "Force LOD1", "Force LOD2", "Force LOD3" };
            int currentForcedIdx = selectedNode->forcedLOD + 1;
            if (ImGui::Combo("LOD Preview Override", &currentForcedIdx, forcedLODOptions, IM_ARRAYSIZE(forcedLODOptions))) {
                selectedNode->forcedLOD = currentForcedIdx - 1;
            }

            // Automatic In-Engine Mesh Decimation Button (Phase 2)
            if (!selectedNode->customMeshName.empty()) {
                if (ImGui::Button("Auto-Generate 3-Tier LODs (50% / 25% / 10%)", ImVec2(-1, 28))) {
                    if (m_onGenerateLODs) {
                        m_onGenerateLODs(selectedNode->customMeshName, *selectedNode);
                    }
                }
            }

            // Table of active LOD Tiers
            if (!selectedNode->lodLevels.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Configured LOD Levels (%zu):", selectedNode->lodLevels.size());
                if (ImGui::BeginTable("##LODTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("LOD Tier", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                    ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                    ImGui::TableSetupColumn("Tris Target", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                    ImGui::TableSetupColumn("Mesh Asset Key");
                    ImGui::TableHeadersRow();

                    // LOD0 Row
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("LOD0");
                    ImGui::TableSetColumnIndex(1); ImGui::Text("0.0 m");
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%u (100%%)", selectedNode->triangleCount);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%s", selectedNode->customMeshName.c_str());

                    for (size_t i = 0; i < selectedNode->lodLevels.size(); ++i) {
                        auto& lod = selectedNode->lodLevels[i];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (activeTier == static_cast<int>(i + 1)) {
                            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "* LOD%zu", i + 1);
                        } else {
                            ImGui::Text("  LOD%zu", i + 1);
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::SetNextItemWidth(80.0f);
                        ImGui::DragFloat("##dist", &lod.distance, 1.0f, 0.0f, 500.0f, "%.0fm");
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%u (%.0f%%)", lod.triangleCount, lod.reductionRatio * 100.0f);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%s", lod.meshKey.c_str());
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Node Type Specific Properties
    if (selectedNode->IsUINode()) {
        NodeInspector::RenderUIProperties(scene, selectedNode, m_inspectorState);
    } else if (selectedNode->nodeType == NodeType::Collider) {
        NodeInspector::RenderColliderProperties(selectedNode);
    } else if (selectedNode->nodeType == NodeType::Camera) {
        NodeInspector::RenderCameraProperties(scene, selectedNode);
    } else if (selectedNode->nodeType == NodeType::Light) {
        NodeInspector::RenderLightProperties(selectedNode);
    } else if (selectedNode->nodeType == NodeType::Empty) {
        NodeInspector::RenderEmptyGroupProperties(scene, selectedNode);
        RenderPourSpoutProperties(scene, *selectedNode);
        RenderFluidVolumeProperties(*selectedNode);
        RenderReflectionProbeProperties(*selectedNode);
        ImGui::Checkbox("GI Volume (Baked Lighting)", &selectedNode->isGIVolume);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The box that Rendering > Baked Lighting bakes, centred on this node.\nWithout one, the bake covers the whole scene.");
        if (selectedNode->isGIVolume) {
            ImGui::DragFloat3("GI Volume Size (m)", &selectedNode->giVolumeSize.x, 0.1f, 1.0f, 1000.0f, "%.1f");
        }
    } else if (selectedNode->nodeType == NodeType::AudioSource ||
               selectedNode->nodeType == NodeType::ParticleEmitter ||
               selectedNode->nodeType == NodeType::Decal ||
               selectedNode->nodeType == NodeType::Spline) {
        // Dedicated properties rendered via selectedNode->RenderInspectorProperties below
    } else if (selectedNode->isOcean) {
        RenderOceanProperties(*selectedNode);
    } else {
        if (ImGui::CollapsingHeader(TR("Material & Appearance (PBR)"), ImGuiTreeNodeFlags_DefaultOpen)) {
            // A preview of the material on a sphere, cube or plane in a lit studio
            DrawMaterialPreview("inspector", *selectedNode, 128.0f, m_materialPreviewShape);
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextDisabled("%s", TR("Preview"));
            ImGui::RadioButton(TR("Sphere"), &m_materialPreviewShape, 0);
            ImGui::RadioButton(TR("Cube"), &m_materialPreviewShape, 1);
            ImGui::RadioButton(TR("Plane"), &m_materialPreviewShape, 2);
            ImGui::EndGroup();
            ImGui::Text("%s", TR("Base Color / Tint"));
            ImGui::ColorEdit4("##Color", glm::value_ptr(selectedNode->color));
            ImGui::SliderFloat(TR("Roughness (Default)"), &selectedNode->roughness, 0.0f, 1.0f);
            ImGui::SliderFloat(TR("Metallic (Default)"), &selectedNode->metallic, 0.0f, 1.0f);
            ImGui::SliderFloat(TR("Alpha Cutoff"), &selectedNode->alphaCutoff, 0.0f, 1.0f, selectedNode->alphaCutoff > 0.0f ? "%.2f" : "Off");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cuts out pixels whose alpha is below this value.\nUse for leaves, fences and hair cards. 0 = off.");
            ImGui::Checkbox(TR("Double-Sided"), &selectedNode->doubleSided);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw and light both sides of each face.\nTurn off for closed meshes to hide back faces.");
            ImGui::SameLine();
            ImGui::Checkbox(TR("Cast Shadows"), &selectedNode->castShadows);
            ImGui::SameLine();
            ImGui::Checkbox(TR("Lightmap Static"), &selectedNode->lightmapStatic);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("This object never moves, so Bake Lighting gives it a lightmap:\n"
                                                          "detailed bounce light and soft sky shadowing. Turn off for objects\n"
                                                          "that move (they are lit by the light probes instead).");
            // One-click see-through materials
            if (ImGui::SmallButton(TR("Water"))) {
                selectedNode->color = glm::vec4(0.75f, 0.88f, 0.95f, 0.25f);
                selectedNode->roughness = 0.02f;
                selectedNode->metallic = 0.0f;
                selectedNode->alphaCutoff = 0.0f;
                selectedNode->refractionIOR = 1.33f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Glass"))) {
                selectedNode->color = glm::vec4(0.9f, 0.95f, 0.97f, 0.15f);
                selectedNode->roughness = 0.03f;
                selectedNode->metallic = 0.0f;
                selectedNode->alphaCutoff = 0.0f;
                selectedNode->refractionIOR = 1.5f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("presets"));
            ImGui::Checkbox(TR("Liquid Fill"), &selectedNode->isLiquid);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("This mesh is liquid inside a container: it is cut off at the fill level,\n"
                                                          "shows a flat top surface, and sloshes when the container moves.\n"
                                                          "Use a mesh slightly smaller than the container, e.g. a cylinder in a glass.");
            if (selectedNode->isLiquid) {
                ImGui::SliderFloat(TR("Fill"), &selectedNode->liquidFill, 0.0f, 1.0f, "%.2f");
                ImGui::ColorEdit4(TR("Surface Colour"), glm::value_ptr(selectedNode->liquidTopColor));
                ImGui::SliderFloat(TR("Slosh"), &selectedNode->liquidSlosh, 0.0f, 3.0f, "%.2f");
                float colorDepthCm = selectedNode->liquidColorDepth * 100.0f;
                if (ImGui::SliderFloat(TR("Colour Depth (cm)"), &colorDepthCm, 0.2f, 200.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
                    selectedNode->liquidColorDepth = colorDepthCm / 100.0f;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much liquid shows its colour (and opacity). Thinner parts look paler,\n"
                                                              "thicker parts deeper and more opaque.");
            }
            if ((selectedNode->color.a < 0.99f && selectedNode->alphaCutoff <= 0.0f) || selectedNode->isLiquid) {
                ImGui::SliderFloat(TR("Refraction (IOR)"), &selectedNode->refractionIOR, 1.0f, 2.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much the see-through surface bends what's behind it.\n1.0 = none, 1.33 = water, 1.5 = glass, 2.4 = diamond.");
            }
            ImGui::Checkbox(TR("Mirror"), &selectedNode->isMirror);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Planar mirror: shows the scene reflected across this surface.\nBest on a Plane or Quad. The colour tints the reflection.\nOnly the nearest visible mirror is rendered.");
            RenderReflectionProbeProperties(*selectedNode);
            ImGui::Checkbox(TR("In-Game Video Screen (TV / Cinema / Phone / Monitor)"), &selectedNode->isVideoScreen);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns this 3D surface into a live glowing video screen (TV set, cinema screen,\nsmartphone screen, CCTV monitor, or billboard). Drives both Albedo and HDR Emissive\nso the screen illuminates dark rooms via Bloom, SSR, and Real-Time VXGI.");
            if (selectedNode->isVideoScreen) {
                ImGui::Indent(10.0f);
                ImGui::TextDisabled("%s", TR("1-Click Screen Form-Factor Presets:"));
                if (ImGui::SmallButton(TR("TV Set (16:9)"))) {
                    selectedNode->scale = glm::vec3(1.6f, 0.9f, 0.06f);
                    selectedNode->color = glm::vec4(1.0f);
                    selectedNode->roughness = 0.12f;
                    selectedNode->videoChannelPreset = 2;
                    selectedNode->videoScreenBrightness = 1.6f;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Cinema (2.39:1)"))) {
                    selectedNode->scale = glm::vec3(7.2f, 3.0f, 0.08f);
                    selectedNode->color = glm::vec4(1.0f);
                    selectedNode->roughness = 0.35f;
                    selectedNode->videoChannelPreset = 1;
                    selectedNode->videoScreenBrightness = 2.2f;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Smartphone (9:16)"))) {
                    selectedNode->scale = glm::vec3(0.18f, 0.32f, 0.02f);
                    selectedNode->color = glm::vec4(1.0f);
                    selectedNode->roughness = 0.06f;
                    selectedNode->clearCoat = 0.8f;
                    selectedNode->videoChannelPreset = 4;
                    selectedNode->videoScreenBrightness = 1.8f;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("CCTV Monitor"))) {
                    selectedNode->scale = glm::vec3(0.8f, 0.6f, 0.08f);
                    selectedNode->color = glm::vec4(1.0f);
                    selectedNode->videoChannelPreset = 3;
                    selectedNode->videoScanlineStrength = 0.35f;
                }

                const char* vChannels[] = {
                    "External File / GIF / Atlas / Sequence",
                    "Ch 1: Cinema 35mm Countdown & Feature Reel",
                    "Ch 2: Live TV News & Weather Broadcast",
                    "Ch 3: CCTV Security Camera Surveillance",
                    "Ch 4: Smartphone Mobile Call & Feed (9:16)",
                    "Ch 5: Retro CRT Color Bars & VHS Static",
                    "Ch 6: Sci-Fi Radar & Oscilloscope Telemetry"
                };
                int chIdx = std::clamp(selectedNode->videoChannelPreset, 0, 6);
                if (ImGui::Combo(TR("Video Channel"), &chIdx, vChannels, IM_ARRAYSIZE(vChannels))) {
                    selectedNode->videoChannelPreset = chIdx;
                }
                RenderTextureSlot("External Video / GIF / Atlas / Frame Folder", "videoSrc", selectedNode->videoSourcePath, "Select Animated GIF or Video Atlas");
                if (!selectedNode->videoSourcePath.empty()) {
                    ImGui::SliderInt("Atlas Columns", &selectedNode->videoAtlasCols, 1, 16);
                    ImGui::SliderInt("Atlas Rows", &selectedNode->videoAtlasRows, 1, 16);
                    ImGui::SliderFloat("Video FPS", &selectedNode->videoFps, 1.0f, 60.0f, "%.1f fps");
                }
                ImGui::Checkbox("Playing##Vid", &selectedNode->videoPlaying);
                ImGui::SameLine();
                ImGui::Checkbox("Loop##Vid", &selectedNode->videoLoop);
                ImGui::SameLine();
                if (ImGui::SmallButton("Restart##Vid")) selectedNode->videoCurrentTime = 0.0f;
                ImGui::SliderFloat("Playhead (s)##Vid", &selectedNode->videoCurrentTime, 0.0f, std::max(1.0f, selectedNode->videoDuration), "%.2f s");
                ImGui::SliderFloat("Duration (s)##Vid", &selectedNode->videoDuration, 1.0f, 120.0f, "%.1f s");
                ImGui::SliderFloat("Playback Speed##Vid", &selectedNode->videoSpeed, 0.1f, 3.0f, "%.2fx");
                ImGui::SliderFloat(TR("Screen Glow (HDR Emissive)"), &selectedNode->videoScreenBrightness, 0.0f, 5.0f, "%.2f");
                ImGui::SliderFloat("CRT Scanlines##Vid", &selectedNode->videoScanlineStrength, 0.0f, 1.0f, "%.2f");
                RenderTextureSlot("Synced Audio (.wav, optional)", "videoAud", selectedNode->videoAudioPath, "Select Synced Video Audio (.wav)");
                if (!selectedNode->videoAudioPath.empty()) {
                    ImGui::Checkbox("3D Spatial Screen Audio", &selectedNode->videoSpatialAudio);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::SliderFloat("Volume##VidAud", &selectedNode->videoAudioVolume, 0.0f, 1.0f, "%.2f");
                }
                if (ImGui::SmallButton("Launch Full-Screen Cutaway With This Channel")) {
                    Video::VideoSystem::Get().StartCutaway(selectedNode->videoChannelPreset, selectedNode->videoSourcePath,
                                                           selectedNode->videoDuration, true, false, selectedNode->name);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Launch Corner PiP")) {
                    Video::VideoSystem::Get().StartCutaway(selectedNode->videoChannelPreset, selectedNode->videoSourcePath,
                                                           selectedNode->videoDuration, true, true, selectedNode->name);
                }
                ImGui::Unindent(10.0f);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "PBR Texture Maps (Optional):");

            RenderTextureSlot("Albedo / Diffuse Map", "diffuse", selectedNode->texturePath, "Select Diffuse / Albedo Texture", m_onImportTexture);
            RenderTextureSlot("Normal Map (Tangent Space)", "normal", selectedNode->normalMapPath, "Select Normal Map", m_onImportTexture);
            RenderTextureSlot("Metallic Map", "metallic", selectedNode->metallicMapPath, "Select Metallic Map", m_onImportTexture);
            RenderTextureSlot("Roughness Map", "roughness", selectedNode->roughnessMapPath, "Select Roughness Map", m_onImportTexture);
            RenderTextureSlot("Ambient Occlusion (AO) Map", "ao", selectedNode->aoMapPath, "Select AO Map", m_onImportTexture);
            ImGui::SliderFloat(TR("AO Strength"), &selectedNode->aoStrength, 0.0f, 1.0f);
            RenderTextureSlot("Emissive / Glow Map", "emissive", selectedNode->emissiveMapPath, "Select Emissive Map", m_onImportTexture);
            ImGui::ColorEdit3(TR("Emissive Color"), glm::value_ptr(selectedNode->emissiveColor));
            ImGui::DragFloat(TR("Emissive Intensity (HDR)"), &selectedNode->emissiveIntensity, 0.1f, 0.0f, 50.0f);
            RenderTextureSlot("Height / Parallax Map", "height", selectedNode->heightMapPath, "Select Height Map", m_onImportTexture);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "%s", TR("Advanced Material:"));
            const char* models[] = { "Standard", "Skin (Subsurface)", "Hair (Strands)", "Eye" };
            ImGui::Combo(TR("Material Model"), &selectedNode->materialModel, models, IM_ARRAYSIZE(models));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skin: light scatters under the surface (softer shading, warm edges).\nHair: streaky strand highlights for hair cards (use Alpha Cutoff).\nEye: refracting cornea over the iris; UVs must centre the iris at (0.5, 0.5).");
            if (selectedNode->materialModel == 1) {
                ImGui::ColorEdit3("Scatter Colour", glm::value_ptr(selectedNode->subsurfaceColor));
                ImGui::SliderFloat("Scatter Strength", &selectedNode->subsurfaceStrength, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Translucency", &selectedNode->subsurfaceTranslucency, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Light glowing through thin parts (ears, nostrils, fingers) when lit from behind.");
            } else if (selectedNode->materialModel == 2) {
                ImGui::ColorEdit3("Highlight Tint", glm::value_ptr(selectedNode->hairHighlightTint));
                ImGui::SliderFloat("Highlight Shift", &selectedNode->hairHighlightShift, -0.5f, 0.5f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves the highlights along the strands. Strands should run along the texture's V direction.");
            } else if (selectedNode->materialModel == 3) {
                ImGui::SliderFloat("Iris Radius (UV)", &selectedNode->irisRadius, 0.02f, 0.5f, "%.3f");
                ImGui::SliderFloat("Iris Depth (UV)", &selectedNode->irisDepth, 0.0f, 0.15f, "%.3f");
                ImGui::SliderFloat("Cornea IOR", &selectedNode->corneaIOR, 1.0f, 1.6f, "%.3f");
            }
            ImGui::SliderFloat(TR("Clear Coat"), &selectedNode->clearCoat, 0.0f, 1.0f, selectedNode->clearCoat > 0.0f ? "%.2f" : "Off");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A glossy transparent top layer: car paint, varnished wood, lacquer, wet surfaces.");
            if (selectedNode->clearCoat > 0.0f) {
                ImGui::SliderFloat(TR("Clear Coat Roughness"), &selectedNode->clearCoatRoughness, 0.02f, 1.0f, "%.2f");
            }
            ImGui::Checkbox(TR("Triplanar Mapping"), &selectedNode->triplanarMapping);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Projects the textures from the world X, Y and Z axes instead of the mesh UVs:\nno stretching on terrain, cliffs and rocks. Textures stay put if the object moves.");
            if (selectedNode->triplanarMapping) {
                ImGui::SliderFloat("Tile Size (m)", &selectedNode->triplanarTileSize, 0.1f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Blend Sharpness", &selectedNode->triplanarSharpness, 1.0f, 16.0f, "%.1f");
            }
            RenderTextureSlot("Detail Albedo (tiled, 50% grey = no change)", "detailAlbedo", selectedNode->detailAlbedoPath, "Select Detail Albedo", m_onImportTexture);
            RenderTextureSlot("Detail Normal (tiled)", "detailNormal", selectedNode->detailNormalPath, "Select Detail Normal Map", m_onImportTexture);
            if (!selectedNode->detailAlbedoPath.empty() || !selectedNode->detailNormalPath.empty()) {
                ImGui::SliderFloat("Detail Tiling", &selectedNode->detailTiling, 1.0f, 64.0f, "%.1f");
                ImGui::SliderFloat("Detail Strength", &selectedNode->detailStrength, 0.0f, 1.0f, "%.2f");
            }
            ImGui::Checkbox("Use Vertex Colours", &selectedNode->useVertexColors);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiply the base colour by colours painted on the mesh (from glTF / FBX).");
            ImGui::SliderFloat("Parallax Height Scale", &selectedNode->heightScale, 0.0f, 0.2f);
            ImGui::Spacing();
            // Shader graph material: replaces this material's colour / normal / metal / roughness / AO /
            // emission with the graph's output (lighting, shadows and fog stay the same)
            ImGui::TextWrapped("Shader Graph Material: %s", selectedNode->shaderGraphPath.empty() ? "(none)" : selectedNode->shaderGraphPath.c_str());
            if (ImGui::Button("Load Graph...##node")) {
                std::string picked;
                if (ShaderGraphFileDialog(picked, "Use Shader Graph", false)) selectedNode->shaderGraphPath = picked;
            }
            if (!selectedNode->shaderGraphPath.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##graph")) selectedNode->shaderGraphPath.clear();
                ImGui::SameLine();
                if (ImGui::Button("Edit##graph")) {
                    if (m_shaderGraph.LoadFromFile(selectedNode->shaderGraphPath)) {
                        m_shaderGraphPath = selectedNode->shaderGraphPath;
                        m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
                    }
                    m_showShaderGraphWindow = true;
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.42f, 0.68f, 1.0f));
            if (ImGui::Button(TR("Open Visual Shader Graph Editor >>"), ImVec2(-1, 26))) {
                m_showShaderGraphWindow = true;
            }
            ImGui::PopStyleColor();
        }

        std::string customExt = "";
        if (!selectedNode->customMeshName.empty()) {
            customExt = std::filesystem::path(selectedNode->customMeshName).extension().string();
            std::transform(customExt.begin(), customExt.end(), customExt.begin(), ::tolower);
        }

        if (selectedNode->hasSkeleton || customExt == ".fbx" || selectedNode->type == ShapeType::Custom) {
            if (ImGui::CollapsingHeader(TR("Armature & Animation Tree"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox(TR("Armature / Skeleton Enabled"), &selectedNode->hasSkeleton);

                if (selectedNode->hasSkeleton) {
                    char skelBuf[128];
                    strncpy(skelBuf, selectedNode->skeletonName.c_str(), sizeof(skelBuf));
                    skelBuf[sizeof(skelBuf) - 1] = '\0';
                    if (ImGui::InputText("Skeleton Name", skelBuf, sizeof(skelBuf))) {
                        selectedNode->skeletonName = skelBuf;
                    }

                    if (ImGui::Checkbox(TR("Play Animation"), &selectedNode->playAnimation)) {
                        if (m_onSkeletalScrub) m_onSkeletalScrub(selectedNode->customMeshName, selectedNode->currentAnimState, 0.0f, selectedNode->playAnimation);
                    }
                    ImGui::SliderFloat(TR("Animation Speed"), &selectedNode->animationSpeed, 0.0f, 3.0f, "%.2fx");
                    ImGui::DragFloat(TR("Start Offset (s)"), &selectedNode->animationStartOffset, 0.01f, 0.0f, 600.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each character has its own animation clock. Give copies of the same\ncharacter different offsets or speeds so a crowd doesn't move in step.");

                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Bones: %u detected",
                                       (uint32_t)selectedNode->boneNames.size());

                    if (!selectedNode->boneNames.empty()) {
                        if (ImGui::TreeNodeEx("Bones Hierarchy / Joints", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::BeginChild("BoneListScroll", ImVec2(0, 110), true);
                            for (size_t i = 0; i < selectedNode->boneNames.size(); ++i) {
                                ImGui::BulletText("[%zu] %s", i, selectedNode->boneNames[i].c_str());
                            }
                            ImGui::EndChild();
                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextDisabled("No bone joints detected in mesh.");
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // Animation Tree State Machine Section
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Animation Tree State Machine:");
                    char stateBuf[64];
                    strncpy(stateBuf, selectedNode->currentAnimState.c_str(), sizeof(stateBuf));
                    stateBuf[sizeof(stateBuf) - 1] = '\0';
                    if (ImGui::InputText("Active State##Anim", stateBuf, sizeof(stateBuf))) {
                        selectedNode->currentAnimState = stateBuf;
                    }

                    ImGui::Text("%s", TR("Quick Switch:"));
                    ImGui::SameLine();
                    if (ImGui::SmallButton(TR("Idle##Quick"))) selectedNode->currentAnimState = "Idle";
                    ImGui::SameLine();
                    if (ImGui::SmallButton(TR("Walk##Quick"))) selectedNode->currentAnimState = "Walk";
                    ImGui::SameLine();
                    if (ImGui::SmallButton(TR("Run##Quick"))) selectedNode->currentAnimState = "Run";
                    ImGui::SameLine();
                    if (ImGui::SmallButton(TR("Attack##Quick"))) selectedNode->currentAnimState = "Attack";

                    if (!selectedNode->animClipNames.empty()) {
                        ImGui::Spacing();
                        ImGui::Text("Embedded Animation Clips (%zu):", selectedNode->animClipNames.size());
                        for (const auto& clip : selectedNode->animClipNames) {
                            bool isCur = (selectedNode->currentAnimState == clip);
                            if (isCur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.35f, 1.0f));
                            if (ImGui::Button(clip.c_str())) {
                                selectedNode->currentAnimState = clip;
                            }
                            if (isCur) ImGui::PopStyleColor();
                            ImGui::SameLine();
                        }
                        ImGui::NewLine();
                    }

                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.65f, 1.0f));
                    if (ImGui::Button(TR("Visual State Machine Graph >>"), ImVec2(-1, 28))) {
                        m_showAnimStateMachineWindow = true;
                    }
                    ImGui::PopStyleColor();

                    ImGui::Spacing();
                    ImGui::TextDisabled("Lua API: NodeAPI.SetAnimState(id, \"%s\", 0.2)", selectedNode->currentAnimState.c_str());
                } else {
                    ImGui::TextDisabled("Check box above to enable skeleton armature & animation tree.");
                }
            }
        }
    } // End nodeType-specific properties (Collider, Camera, Light, Mesh)

    // Custom node inspector (MeshNode, CharacterNode, etc.)
    if (!selectedNode->IsUINode() && selectedNode->nodeType != NodeType::Collider && selectedNode->nodeType != NodeType::Camera && selectedNode->nodeType != NodeType::Light && selectedNode->nodeType != NodeType::Empty) {
        selectedNode->RenderInspectorProperties(scene, m_inspectorState);
    }

    // Physics & Dynamics (JoltPhysics)
    if (!selectedNode->IsUINode() && selectedNode->nodeType != NodeType::Camera && selectedNode->nodeType != NodeType::Light) {
        if (ImGui::CollapsingHeader(TR("Physics & Dynamics (JoltPhysics)"))) {
            const char* rbTypes[] = { "None", "Static (Immovable)", "Kinematic (Animated/Scripted)", "Dynamic (RigidBody Simulation)" };
            int curRb = static_cast<int>(selectedNode->rigidBodyType);
            if (ImGui::Combo(TR("RigidBody Mode"), &curRb, rbTypes, IM_ARRAYSIZE(rbTypes))) {
                selectedNode->rigidBodyType = static_cast<RigidBodyType>(curRb);
            }
            if (selectedNode->rigidBodyType != RigidBodyType::None) {
                ImGui::DragFloat(TR("Mass (kg)"), &selectedNode->mass, 0.1f, 0.01f, 10000.0f);
                ImGui::Checkbox(TR("Use Gravity"), &selectedNode->useGravity);
                if (selectedNode->rigidBodyType == RigidBodyType::Dynamic) {
                    ImGui::Checkbox(TR("Floats on Water"), &selectedNode->floatsOnWater);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("In an ocean, the body bobs and tilts with the waves (Play mode).");
                    if (selectedNode->floatsOnWater) {
                        ImGui::SliderFloat(TR("Buoyancy"), &selectedNode->buoyancy, 0.0f, 3.0f, "%.2f");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 = neither floats nor sinks, below 1 sinks, above 1 rides higher.");
                    }
                }
                ImGui::SliderFloat(TR("Friction"), &selectedNode->friction, 0.0f, 1.0f);
                ImGui::SliderFloat(TR("Restitution (Bounciness)"), &selectedNode->bounciness, 0.0f, 1.0f);
                ImGui::DragFloat3(TR("Linear Velocity"), glm::value_ptr(selectedNode->linearVelocity), 0.1f);
                ImGui::DragFloat3(TR("Angular Velocity"), glm::value_ptr(selectedNode->angularVelocity), 0.1f);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Joints, Constraints & Ragdoll:");
            const char* constraintTypes[] = { "None", "Fixed Joint", "Hinge Joint", "Slider Joint", "Distance Spring", "Cone-Twist (Ragdoll)" };
            ImGui::Combo(TR("Joint / Constraint"), &selectedNode->constraintType, constraintTypes, IM_ARRAYSIZE(constraintTypes));
            if (selectedNode->constraintType > 0) {
                int targetId = static_cast<int>(selectedNode->constraintTargetNodeId);
                if (ImGui::InputInt("Connected Node ID", &targetId)) {
                    selectedNode->constraintTargetNodeId = (targetId > 0) ? static_cast<uint32_t>(targetId) : 0;
                }
            }
            ImGui::Checkbox(TR("Enable Humanoid Ragdoll Physics"), &selectedNode->enableRagdoll);
            if (selectedNode->enableRagdoll) {
                if (ImGui::Button(TR("Apply Upward Ragdoll Impulse Test"), ImVec2(-1, 24))) {
                    selectedNode->rigidBodyType = RigidBodyType::Dynamic;
                    selectedNode->useGravity = true;
                    selectedNode->linearVelocity = glm::vec3(0.0f, 6.5f, -2.0f);
                    selectedNode->angularVelocity = glm::vec3(1.5f, 0.8f, -1.2f);
                }
            }
        }

        if (ImGui::CollapsingHeader(TR("Inverse Kinematics (IK) & Morph Targets"))) {
            ImGui::Checkbox(TR("Enable Two-Bone IK Solver"), &selectedNode->enableIK);
            if (selectedNode->enableIK) {
                char ikBoneBuf[128];
                strncpy(ikBoneBuf, selectedNode->ikTargetBone.c_str(), sizeof(ikBoneBuf));
                ikBoneBuf[sizeof(ikBoneBuf) - 1] = '\0';
                if (ImGui::InputText("End-Effector Bone", ikBoneBuf, sizeof(ikBoneBuf))) {
                    selectedNode->ikTargetBone = ikBoneBuf;
                }
                ImGui::DragFloat3("IK Target World Pos", glm::value_ptr(selectedNode->ikTargetPosition), 0.05f);
                ImGui::DragFloat3("IK Pole Vector", glm::value_ptr(selectedNode->ikPoleVector), 0.05f);
                ImGui::SliderFloat("IK Blend Weight", &selectedNode->ikWeight, 0.0f, 1.0f);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Blend Shapes / Morph Targets (Deforms Mesh Live):");
            if (selectedNode->morphWeights.empty()) {
                if (ImGui::Button(TR("+ Initialize Default Facial & Body Morph Targets"), ImVec2(-1, 24))) {
                    selectedNode->morphWeights["Smile / Facial"] = 0.0f;
                    selectedNode->morphWeights["Blink"] = 0.0f;
                    selectedNode->morphWeights["Breathing / Bulk"] = 0.0f;
                }
            } else {
                for (auto& kv : selectedNode->morphWeights) {
                    ImGui::SliderFloat(kv.first.c_str(), &kv.second, 0.0f, 1.0f);
                }
                static char s_newMorphName[64] = "CustomMorph";
                ImGui::SetNextItemWidth(160.0f);
                ImGui::InputText("##NewMorphName", s_newMorphName, sizeof(s_newMorphName));
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("+ Add Morph Target"))) {
                    if (s_newMorphName[0] != '\0') {
                        selectedNode->morphWeights[s_newMorphName] = 0.0f;
                    }
                }
                if (ImGui::SmallButton(TR("Reset All Morph Weights"))) {
                    for (auto& kv : selectedNode->morphWeights) kv.second = 0.0f;
                }
            }
        }

        if (ImGui::CollapsingHeader(TR("Multiplayer & Authority"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox(TR("Replicate Transform Over Network"), &selectedNode->replicateTransform);
            int netId = static_cast<int>(selectedNode->networkId);
            if (ImGui::InputInt("Network Entity ID", &netId)) {
                selectedNode->networkId = (netId >= 0) ? static_cast<uint32_t>(netId) : 0;
            }
            int authId = static_cast<int>(selectedNode->multiplayerAuthority);
            if (ImGui::InputInt("Multiplayer Authority (Peer ID)", &authId)) {
                selectedNode->multiplayerAuthority = (authId >= 0) ? static_cast<uint32_t>(authId) : 1;
            }
            ImGui::TextDisabled("1 = Server/Host (Default), 2 = Client 1, 0 = Shared / Anyone");
            uint32_t localPeer = m_networkManager.GetLocalPeerId();
            if (selectedNode->HasMultiplayerAuthority(localPeer)) {
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "[AUTHORITY: LOCAL] Broadcasts to peers");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.25f, 1.0f), "[AUTHORITY: REMOTE Peer #%u] Listens to peer", selectedNode->multiplayerAuthority);
            }
            // Declarative Property Sync
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Declarative Property Replication:");
            ImGui::Checkbox(TR("Enable Declarative Sync"), &selectedNode->replicationConfig.enabled);
            if (selectedNode->replicationConfig.enabled) {
                bool syncPos = selectedNode->replicationConfig.HasRule("position");
                if (ImGui::Checkbox("Sync Position (OnChange, 20Hz)", &syncPos)) {
                    if (syncPos) selectedNode->replicationConfig.AddRule("position", ReplicationMode::OnChange, 20.0f);
                    else selectedNode->replicationConfig.RemoveRule("position");
                }
                bool syncRot = selectedNode->replicationConfig.HasRule("rotation");
                if (ImGui::Checkbox("Sync Rotation (OnChange, 20Hz)", &syncRot)) {
                    if (syncRot) selectedNode->replicationConfig.AddRule("rotation", ReplicationMode::OnChange, 20.0f);
                    else selectedNode->replicationConfig.RemoveRule("rotation");
                }
                bool syncVel = selectedNode->replicationConfig.HasRule("linearVelocity");
                if (ImGui::Checkbox("Sync Velocity (Always, 30Hz)", &syncVel)) {
                    if (syncVel) selectedNode->replicationConfig.AddRule("linearVelocity", ReplicationMode::Always, 30.0f);
                    else selectedNode->replicationConfig.RemoveRule("linearVelocity");
                }
            }

            // Collision Filtering
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Physics Collision Filtering:");
            int colLayer = static_cast<int>(selectedNode->collisionLayer);
            int colMask = static_cast<int>(selectedNode->collisionMask);
            if (ImGui::InputInt("Collision Layer (Bitmask)", &colLayer)) {
                selectedNode->collisionLayer = static_cast<uint32_t>(colLayer);
            }
            if (ImGui::InputInt("Collision Mask (Bitmask)", &colMask)) {
                selectedNode->collisionMask = static_cast<uint32_t>(colMask);
            }

            if (ImGui::Button(TR("Open Multiplayer & Networking Panel >>"), ImVec2(-1, 24))) {
                m_showNetworkingWindow = true;
            }
        }
    }

    // Script Component (Universal: available for Camera, Mesh, Light, Empty, etc.)
    if (ImGui::CollapsingHeader(TR("Script Component"), ImGuiTreeNodeFlags_DefaultOpen)) {
        float availW = ImGui::GetContentRegionAvail().x;
        float btnW = 68.0f;
        float clearW = 24.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float inputW = availW - btnW - clearW - (spacing * 2.0f);

        char scriptBuf[512];
        strncpy(scriptBuf, selectedNode->scriptPath.c_str(), sizeof(scriptBuf));
        scriptBuf[sizeof(scriptBuf) - 1] = '\0';

        if (inputW < 60.0f) {
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##ScriptFile", scriptBuf, sizeof(scriptBuf))) {
                selectedNode->scriptPath = scriptBuf;
            }
            if (ImGui::Button(TR("Browse..."), ImVec2(availW - clearW - spacing, 0))) {
                std::string chosen;
                if (OpenScriptFileDialog(chosen, "Select Lua Script")) {
                    selectedNode->scriptPath = chosen;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearScript", ImVec2(clearW, 0))) {
                selectedNode->scriptPath.clear();
            }
        } else {
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText("##ScriptFile", scriptBuf, sizeof(scriptBuf))) {
                selectedNode->scriptPath = scriptBuf;
            }
            if (ImGui::IsItemHovered() && !selectedNode->scriptPath.empty()) {
                ImGui::SetTooltip("%s", selectedNode->scriptPath.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button(TR("Browse...##Script"), ImVec2(btnW, 0))) {
                std::string chosen;
                if (OpenScriptFileDialog(chosen, "Select Lua Script")) {
                    selectedNode->scriptPath = chosen;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearScript", ImVec2(clearW, 0))) {
                selectedNode->scriptPath.clear();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Detach script from this node");
        }
        
        // Dropdown of existing scripts in project scripts/ directory
        namespace fs = std::filesystem;
        fs::path scriptsDir = !m_projectRoot.empty() ? (fs::path(m_projectRoot) / "scripts") : fs::path("scripts");
        std::vector<std::string> availScripts;
        if (fs::exists(scriptsDir)) {
            for (const auto& entry : fs::directory_iterator(scriptsDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                    availScripts.push_back(entry.path().filename().string());
                }
            }
        }

        if (!availScripts.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Project Scripts:");
            ImGui::SameLine();
            std::string comboLabel = selectedNode->scriptPath.empty() ? "-- Select Script --" : selectedNode->scriptPath;
            if (ImGui::BeginCombo("##ProjectScriptsCombo", comboLabel.c_str())) {
                for (const auto& sName : availScripts) {
                    bool isSelected = (selectedNode->scriptPath == sName);
                    if (ImGui::Selectable(sName.c_str(), isSelected)) {
                        selectedNode->scriptPath = sName;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Camera-specific Quick Preset
        if (selectedNode->nodeType == NodeType::Camera) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Camera Quick Presets:");
            if (ImGui::Button(TR("+ Create & Attach Free-Fly Camera Script"), ImVec2(-1, 26))) {
                fs::path targetDir = !m_projectRoot.empty() ? (fs::path(m_projectRoot) / "scripts") : fs::path("scripts");
                try {
                    fs::create_directories(targetDir);
                    fs::path scriptFile = targetDir / "FreeCamera.lua";
                    if (!fs::exists(scriptFile)) {
                        std::ofstream out(scriptFile);
                        out << "-- FreeCamera.lua\n"
                            << "-- First-Person / Free-Flight Camera Controller\n"
                            << "local moveSpeed = 12.0\n"
                            << "local lookSpeed = 0.25\n\n"
                            << "function OnInit(node)\n"
                            << "    print(\"[FreeCam] Attached to camera: \" .. node.name)\n"
                            << "end\n\n"
                            << "function OnUpdate(node, dt)\n"
                            << "    local moveX = 0.0\n"
                            << "    local moveY = 0.0\n"
                            << "    local moveZ = 0.0\n\n"
                            << "    -- WASD keyboard movement\n"
                            << "    if Input.IsDown(\"MoveForward\") or Input.IsDown(\"W\") then moveZ = moveZ + 1.0 end\n"
                            << "    if Input.IsDown(\"MoveBackward\") or Input.IsDown(\"S\") then moveZ = moveZ - 1.0 end\n"
                            << "    if Input.IsDown(\"MoveLeft\") or Input.IsDown(\"A\") then moveX = moveX - 1.0 end\n"
                            << "    if Input.IsDown(\"MoveRight\") or Input.IsDown(\"D\") then moveX = moveX + 1.0 end\n"
                            << "    if Input.IsDown(\"Jump\") or Input.IsDown(\"Space\") then moveY = moveY + 1.0 end\n"
                            << "    if Input.IsDown(\"C\") or Input.IsDown(\"LeftShift\") then moveY = moveY - 1.0 end\n\n"
                            << "    -- Move relative to camera orientation\n"
                            << "    if moveX ~= 0.0 or moveY ~= 0.0 or moveZ ~= 0.0 then\n"
                            << "        NodeAPI.MoveRelative(node.id, moveX * moveSpeed * dt, moveY * moveSpeed * dt, moveZ * moveSpeed * dt)\n"
                            << "    end\n\n"
                            << "    -- Right-click + mouse drag to look around\n"
                            << "    if Input.IsMouseDown(1) then\n"
                            << "        local dx, dy = Input.GetMouseDelta()\n"
                            << "        if dx ~= 0.0 or dy ~= 0.0 then\n"
                            << "            NodeAPI.Rotate(node.id, -dy * lookSpeed, -dx * lookSpeed, 0.0)\n"
                            << "        end\n"
                            << "    end\n"
                            << "end\n";
                        out.close();
                    }
                    selectedNode->scriptPath = "FreeCamera.lua";
                } catch (const std::exception& e) {
                    std::cerr << "Failed to create FreeCamera.lua: " << e.what() << std::endl;
                }
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Scripts run in Play Mode [Play] via Lua lifecycle (OnInit, OnUpdate).");
    }

    // Prominent Red Delete Button (Works for all nodes including Camera)
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.28f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.15f, 0.15f, 1.0f));
    std::string deleteBtnLabel = std::string(TR("Delete")) + " "" + selectedNode->name + "" " + TR("from Scene [Del]");
    if (ImGui::Button(deleteBtnLabel.c_str(), ImVec2(-1, 32))) {
        scene.DeleteNode(selectedNode->id);
    }
    ImGui::PopStyleColor(3);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
        scene.DeleteSelected();
    }

    ImGui::Spacing();
}


void EditorUI::RenderUIElements(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize, bool isEditorMode, Gameplay::GameLoop* gameLoop) {
    m_uiRendererState.isDraggingUINode = m_isDraggingUINode;
    UINodeRenderer::Render(scene, drawList, canvasPos, canvasSize, isEditorMode, gameLoop, m_uiRendererState, (uint32_t)scene.GetSelectedId());

    // Render Cinematic Cutscene Overlay (2.39:1 Letterbox bars, Subtitle Banner, and Skip Cutscene Button)
    if (drawList && gameLoop && gameLoop->GetCutscenePlayer().IsPlaying()) {
        auto& csPlayer = gameLoop->GetCutscenePlayer();
        const auto* seq = csPlayer.GetActiveSequence();
        float barRatio = csPlayer.GetLetterboxBarRatio();
        float barH = std::max(38.0f, canvasSize.y * barRatio);

        ImVec2 topMin = canvasPos;
        ImVec2 topMax = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + barH);
        ImVec2 botMin = ImVec2(canvasPos.x, canvasPos.y + canvasSize.y - barH);
        ImVec2 botMax = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

        drawList->PushClipRect(canvasPos, botMax, true);
        drawList->AddRectFilled(topMin, topMax, IM_COL32(6, 6, 10, 245));
        drawList->AddRectFilled(botMin, botMax, IM_COL32(6, 6, 10, 245));
        drawList->AddLine(ImVec2(topMin.x, topMax.y), ImVec2(topMax.x, topMax.y), IM_COL32(195, 165, 95, 140), 1.2f);
        drawList->AddLine(ImVec2(botMin.x, botMin.y), ImVec2(botMax.x, botMin.y), IM_COL32(195, 165, 95, 140), 1.2f);

        if (seq) {
            char headerBuf[160];
            snprintf(headerBuf, sizeof(headerBuf), "[CINEMATIC] %s   |   %s   |   %s   |   %.1fs / %.1fs",
                     seq->name.c_str(),
                     Animation::CutsceneColorFilterToString(seq->colorFilter),
                     seq->silentAudio ? "Silent Mode" : "Audio Active",
                     csPlayer.GetPlaybackTime(),
                     seq->duration);
            drawList->AddText(ImVec2(topMin.x + 16.0f, topMin.y + (barH - ImGui::GetTextLineHeight()) * 0.5f),
                              IM_COL32(215, 195, 140, 230), headerBuf);
        }

        // Speaker & Dialogue Subtitle Cue in Bottom Letterbox Bar
        if (const auto* cue = csPlayer.GetActiveSubtitle()) {
            if (!cue->text.empty()) {
                std::string fullSub = cue->speaker.empty() ? cue->text : (cue->speaker + ": \"" + cue->text + "\"");
                ImVec2 txtSz = ImGui::CalcTextSize(fullSub.c_str());
                float subX = canvasPos.x + (canvasSize.x - txtSz.x) * 0.5f;
                float subY = botMin.y + (barH - txtSz.y) * 0.5f;
                drawList->AddRectFilled(ImVec2(subX - 14.0f, subY - 5.0f),
                                        ImVec2(subX + txtSz.x + 14.0f, subY + txtSz.y + 5.0f),
                                        IM_COL32(18, 20, 28, 210), 5.0f);
                drawList->AddText(ImVec2(subX, subY), IM_COL32(255, 240, 195, 255), fullSub.c_str());
            }
        }

        // Interactive [Space / Enter] Skip Cutscene >> Button in Bottom-Right
        if (csPlayer.IsSkippable()) {
            const char* skipLabel = "[Space / Enter] Skip Cutscene >>";
            ImVec2 lblSz = ImGui::CalcTextSize(skipLabel);
            float btnW = lblSz.x + 24.0f;
            float btnH = std::min(barH - 10.0f, lblSz.y + 12.0f);
            ImVec2 btnMin(botMax.x - btnW - 16.0f, botMin.y + (barH - btnH) * 0.5f);
            ImVec2 btnMax(btnMin.x + btnW, btnMin.y + btnH);

            ImVec2 mPos = ImGui::GetIO().MousePos;
            bool hovered = (mPos.x >= btnMin.x && mPos.x <= btnMax.x && mPos.y >= btnMin.y && mPos.y <= btnMax.y);
            drawList->AddRectFilled(btnMin, btnMax, hovered ? IM_COL32(65, 120, 195, 240) : IM_COL32(32, 38, 52, 220), 6.0f);
            drawList->AddRect(btnMin, btnMax, hovered ? IM_COL32(140, 205, 255, 255) : IM_COL32(180, 160, 110, 190), 6.0f, 0, 1.5f);
            drawList->AddText(ImVec2(btnMin.x + 12.0f, btnMin.y + (btnH - lblSz.y) * 0.5f),
                              IM_COL32(250, 250, 255, 255), skipLabel);

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                csPlayer.Skip(scene);
            }
        }
        drawList->PopClipRect();
    }

    // In-Game Video Cutaway Overlay (Full-Screen or Picture-in-Picture)
    if (Video::VideoSystem::Get().IsCutawayPlaying() && canvasSize.x > 64.0f && canvasSize.y > 64.0f) {
        const auto& cw = Video::VideoSystem::Get().GetCutawayState();
        drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        ImVec2 boxMin, boxMax;
        if (cw.pictureInPicture) {
            float pipW = std::min(canvasSize.x * 0.30f, 340.0f);
            float pipH = (cw.channelPreset == 4) ? (pipW * 1.45f) : (pipW * 9.0f / 16.0f);
            boxMin = ImVec2(canvasPos.x + canvasSize.x - pipW - 18.0f, canvasPos.y + 18.0f);
            boxMax = ImVec2(boxMin.x + pipW, boxMin.y + pipH);
        } else {
            drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(4, 5, 8, 235));
            float padX = canvasSize.x * 0.08f;
            float padY = canvasSize.y * 0.10f;
            boxMin = ImVec2(canvasPos.x + padX, canvasPos.y + padY);
            boxMax = ImVec2(canvasPos.x + canvasSize.x - padX, canvasPos.y + canvasSize.y - padY);
        }

        drawList->AddRectFilled(boxMin, boxMax, IM_COL32(12, 14, 20, 255), 6.0f);
        if (!cw.rgbaPixels.empty() && cw.width > 0 && cw.height > 0) {
            int gridX = cw.pictureInPicture ? 48 : 80;
            int gridY = cw.pictureInPicture ? 28 : 45;
            float cellW = (boxMax.x - boxMin.x - 6.0f) / static_cast<float>(gridX);
            float cellH = (boxMax.y - boxMin.y - 30.0f) / static_cast<float>(gridY);
            for (int gy = 0; gy < gridY; ++gy) {
                int sy = std::clamp((gy * static_cast<int>(cw.height)) / gridY, 0, static_cast<int>(cw.height) - 1);
                for (int gx = 0; gx < gridX; ++gx) {
                    int sx = std::clamp((gx * static_cast<int>(cw.width)) / gridX, 0, static_cast<int>(cw.width) - 1);
                    size_t idx = (static_cast<size_t>(sy) * cw.width + sx) * 4;
                    ImU32 c = IM_COL32(cw.rgbaPixels[idx + 0], cw.rgbaPixels[idx + 1], cw.rgbaPixels[idx + 2], 255);
                    ImVec2 p0(boxMin.x + 3.0f + gx * cellW, boxMin.y + 24.0f + gy * cellH);
                    drawList->AddRectFilled(p0, ImVec2(p0.x + cellW + 0.6f, p0.y + cellH + 0.6f), c);
                }
            }
        }
        drawList->AddRect(boxMin, boxMax, IM_COL32(110, 195, 255, 220), 6.0f, 0, 2.0f);
        drawList->AddText(ImVec2(boxMin.x + 10.0f, boxMin.y + 4.0f), IM_COL32(140, 220, 255, 255), cw.title.c_str());

        if (cw.skippable) {
            const char* skipTxt = "[Space / Enter] Close Video >>";
            ImVec2 sz = ImGui::CalcTextSize(skipTxt);
            ImVec2 bMin(boxMax.x - sz.x - 16.0f, boxMin.y + 2.0f);
            ImVec2 bMax(boxMax.x - 4.0f, boxMin.y + 22.0f);
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool hov = (mp.x >= bMin.x && mp.x <= bMax.x && mp.y >= bMin.y && mp.y <= bMax.y);
            drawList->AddRectFilled(bMin, bMax, hov ? IM_COL32(215, 55, 60, 240) : IM_COL32(45, 52, 68, 220), 4.0f);
            drawList->AddText(ImVec2(bMin.x + 6.0f, bMin.y + 2.0f), IM_COL32(255, 255, 255, 255), skipTxt);
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                Video::VideoSystem::Get().SkipCutaway();
            }
        }
        if (false) {
            if (false) {
            }
        }
        drawList->PopClipRect();
    }
}

void EditorUI::RenderViewport(Scene& scene, Camera& camera, Graphics::IRenderTarget& offscreen, Gameplay::GameLoop& gameLoop) {
    if (!m_showViewportWindow) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool open = ImGui::Begin(TR("3D Viewport###ViewportWindow"), &m_showViewportWindow, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (!open) {
        ImGui::End();
        return;
    }

    m_viewportHovered = ImGui::IsWindowHovered();
    m_viewportFocused = ImGui::IsWindowFocused();

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_viewportSize = glm::vec2(viewportPanelSize.x, viewportPanelSize.y);

    if (m_viewportSize.x > 0.0f && m_viewportSize.y > 0.0f) {
        camera.SetViewportSize(m_viewportSize.x, m_viewportSize.y);
    }

    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 minBound = ImGui::GetWindowContentRegionMin();
    m_viewportPos = glm::vec2(windowPos.x + minBound.x, windowPos.y + minBound.y);

    // Render Vulkan Offscreen image into Viewport
    ImTextureID viewportTexture = (ImTextureID)(intptr_t)offscreen.GetTextureId();
    if (viewportTexture != 0 && viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
        ImGui::Image(viewportTexture, viewportPanelSize);
    }

    // Drag-and-drop 3D models or textures directly onto Viewport
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("DND_MODEL_PATH")) {
            std::string mPath = (const char*)p->Data;
            if (m_onImportObj) {
                m_onImportObj(mPath);
            }
        } else if (const ImGuiPayload* p2 = ImGui::AcceptDragDropPayload("DND_TEXTURE_PATH")) {
            std::string tPath = (const char*)p2->Data;
            auto sel = scene.GetSelectedNode();
            if (sel) {
                sel->texturePath = tPath;
            }
        } else if (const ImGuiPayload* p3 = ImGui::AcceptDragDropPayload("DND_SCRIPT_PATH")) {
            std::string sPath = (const char*)p3->Data;
            auto sel = scene.GetSelectedNode();
            if (sel) {
                sel->scriptPath = sPath;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Render 2D Scene UI Overlay in Viewport
    ImDrawList* viewportDrawList = ImGui::GetWindowDrawList();
    RenderUIElements(scene, viewportDrawList,
                     ImVec2(m_viewportPos.x, m_viewportPos.y),
                     ImVec2(m_viewportSize.x, m_viewportSize.y),
                     true, &gameLoop);



    // Object picking via Raycasting from Mouse Click
    ImGuiIO& io = ImGui::GetIO();
    bool isGameRunning = (gameLoop.IsPlaying() || gameLoop.IsPaused());
    if (m_viewportHovered && !isGameRunning) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            ImGui::SetWindowFocus();
        }

        if (m_uiRendererState.dropdownConsumedClick) {
            m_uiRendererState.dropdownConsumedClick = false;
        } else if (m_uiRendererState.activeDropdownNodeId != 0 && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            // Dropdown menu active: clicks handled by dropdown overlay
            m_isRotatingCamera = false;
        } else if (io.KeyShift && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
            m_isPanningCamera = true;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            m_isPanningCamera = true;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_isRotatingCamera = true;
        } else if (m_touchNavMode == TouchNavMode::Orbit && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            m_isRotatingCamera = true;
        } else if (m_touchNavMode == TouchNavMode::Pan && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            m_isPanningCamera = true;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && [&]() -> bool {
            auto sel = scene.GetSelectedNode();
            if (sel && sel->nodeType == NodeType::Terrain) {
                auto tNode = std::dynamic_pointer_cast<TerrainNode>(sel);
                if (tNode && tNode->sculptingEnabled) {
                    ImVec2 mPos = io.MousePos;
                    float lX = mPos.x - m_viewportPos.x;
                    float lY = mPos.y - m_viewportPos.y;
                    if (lX >= 0.0f && lX <= m_viewportSize.x && lY >= 0.0f && lY <= m_viewportSize.y) {
                        float ndcX = (lX / m_viewportSize.x) * 2.0f - 1.0f;
                        float ndcY = (lY / m_viewportSize.y) * 2.0f - 1.0f;
                        glm::mat4 invVP = glm::inverse(camera.GetProjectionMatrix() * camera.GetViewMatrix());
                        glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                        if (nearW.w != 0.0f) nearW /= nearW.w;
                        glm::vec4 farW = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                        if (farW.w != 0.0f) farW /= farW.w;
                        glm::vec3 rO = glm::vec3(nearW);
                        glm::vec3 rD = glm::normalize(glm::vec3(farW - nearW));
                        glm::vec3 hitP;
                        if (tNode->Raycast(rO, rD, scene, hitP)) {
                            tNode->ApplyBrush(hitP.x, hitP.z, io.DeltaTime, scene);
                            return true;
                        }
                    }
                }
            }
            return false;
        }()) {
            // Consumed by terrain sculpt
        } else if (m_rulerToolState.active && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || m_rulerToolState.isDragging) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            // Mouse interactions are processed by 3D Ruler & Measurement Tool
        } else if (m_touchNavMode == TouchNavMode::Select && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            ImVec2 mousePos = io.MousePos;
            float localX = mousePos.x - m_viewportPos.x;
            float localY = mousePos.y - m_viewportPos.y;

            if (localX >= 0.0f && localX <= m_viewportSize.x && localY >= 0.0f && localY <= m_viewportSize.y) {
                // First test hit against 2D UI elements in viewport (in reverse hierarchical order so children on top are clicked first)
                bool hit2DUI = false;
                std::vector<std::shared_ptr<Node>> uiHitList;
                std::unordered_set<uint32_t> hitVisited;
                std::function<void(const std::shared_ptr<Node>&)> collectHitHierarchy = [&](const std::shared_ptr<Node>& node) {
                    if (!node || !node->IsUINode() || hitVisited.count(node->id)) return;
                    hitVisited.insert(node->id);
                    uiHitList.push_back(node);
                    for (uint32_t cid : node->childrenIds) {
                        auto child = scene.GetNodeById(cid);
                        if (child && child->IsUINode()) {
                            collectHitHierarchy(child);
                        }
                    }
                };
                for (const auto& node : scene.GetNodes()) {
                    if (!node->IsUINode()) continue;
                    auto parent = (node->parentId != 0) ? scene.GetNodeById(node->parentId) : nullptr;
                    if (!parent || !parent->IsUINode()) {
                        collectHitHierarchy(node);
                    }
                }
                for (const auto& node : scene.GetNodes()) {
                    if (node->IsUINode() && !hitVisited.count(node->id)) {
                        collectHitHierarchy(node);
                    }
                }

                for (auto it = uiHitList.rbegin(); it != uiHitList.rend(); ++it) {
                    const auto& uiNode = *it;
                    if (!uiNode->IsUINode() || !uiNode->uiVisible || uiNode->nodeType == NodeType::UI_Canvas) continue;
                    ImVec2 anchorOrigin, rectMin, rectMax;
                    UINodeRenderer::ComputeUIRect(scene, uiNode, ImVec2(m_viewportPos.x, m_viewportPos.y), m_viewportSize.x, m_viewportSize.y, anchorOrigin, rectMin, rectMax);
                    if (mousePos.x >= rectMin.x && mousePos.x <= rectMax.x &&
                        mousePos.y >= rectMin.y && mousePos.y <= rectMax.y) {
                        scene.SelectNode(uiNode->id);
                        if (uiNode->nodeType == NodeType::UI_Window) {
                            float titleH = 26.0f;
                            bool isTitle = (mousePos.y <= rectMin.y + titleH);
                            bool isClose = uiNode->uiWindowClosable && (mousePos.x >= rectMax.x - 22.0f && mousePos.y <= rectMin.y + 22.0f);
                            if (isClose) {
                                uiNode->uiWindowIsOpen = false;
                            } else if (isTitle && uiNode->uiWindowMovable) {
                                m_uiRendererState.draggingWindowId = uiNode->id;
                                m_isDraggingUINode = false;
                            } else {
                                m_isDraggingUINode = true;
                                m_draggedUINodeId = uiNode->id;
                            }
                        } else {
                            m_isDraggingUINode = true;
                            m_draggedUINodeId = uiNode->id;
                        }
                        hit2DUI = true;
                        m_isRotatingCamera = false;
                        break;
                    }
                }

                if (!hit2DUI) {
                float ndcX = (2.0f * localX / m_viewportSize.x) - 1.0f;
                float ndcY = 1.0f - (2.0f * localY / m_viewportSize.y);

                glm::mat4 view = camera.GetViewMatrix();
                glm::mat4 proj = camera.GetProjectionMatrix();
                proj[1][1] *= -1.0f;
                glm::mat4 invVP = glm::inverse(proj * view);

                glm::vec4 nearPointNDC(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 farPointNDC(ndcX, ndcY, 1.0f, 1.0f);

                glm::vec4 nearWorld = invVP * nearPointNDC;
                if (nearWorld.w != 0.0f) nearWorld /= nearWorld.w;

                glm::vec4 farWorld = invVP * farPointNDC;
                if (farWorld.w != 0.0f) farWorld /= farWorld.w;

                glm::vec3 rayOrigin = glm::vec3(nearWorld);
                glm::vec3 rayDir = glm::normalize(glm::vec3(farWorld - nearWorld));

                // Raycast against all nodes using World Transform
                float closestDist = std::numeric_limits<float>::max();
                std::shared_ptr<Node> hitNode = nullptr;

                for (const auto& node : scene.GetNodes()) {
                    if (node->IsUINode() || node->nodeType == NodeType::Empty) continue;
                    glm::mat4 model = node->GetWorldTransformMatrix(scene);
                    glm::mat4 invModel = glm::inverse(model);

                    glm::vec3 localRayOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
                    glm::vec3 localRayDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));

                    glm::vec3 bMin(-0.6f);
                    glm::vec3 bMax(0.6f);

                    if (node->nodeType == NodeType::Light) {
                        bMin = glm::vec3(-1.4f);
                        bMax = glm::vec3(1.4f);
                    } else if (node->nodeType == NodeType::Camera) {
                        bMin = glm::vec3(-0.9f, -0.9f, -1.4f);
                        bMax = glm::vec3(0.9f, 0.9f, 0.9f);
                    } else if (node->nodeType == NodeType::Collider && node->colliderType == ColliderType::Box) {
                        bMin = -node->colliderSize * 0.5f;
                        bMax = node->colliderSize * 0.5f;
                    } else if (node->type == ShapeType::Plane) {
                        bMin = glm::vec3(-0.6f, -0.2f, -0.6f);
                        bMax = glm::vec3(0.6f, 0.2f, 0.6f);
                    } else if (node->type == ShapeType::Custom) {
                        bMin = node->boundsMin * 1.15f;
                        bMax = node->boundsMax * 1.15f;
                    }

                    glm::vec3 invD = 1.0f / localRayDir;
                    glm::vec3 t0 = (bMin - localRayOrigin) * invD;
                    glm::vec3 t1 = (bMax - localRayOrigin) * invD;
                    glm::vec3 tSmall = glm::min(t0, t1);
                    glm::vec3 tBig = glm::max(t0, t1);

                    float tNear = std::max(std::max(tSmall.x, tSmall.y), tSmall.z);
                    float tFar = std::min(std::min(tBig.x, tBig.y), tBig.z);

                    if (tNear <= tFar && tFar >= 0.0f) {
                        float hitT = (tNear >= 0.0f) ? tNear : tFar;
                        glm::vec3 localHit = localRayOrigin + hitT * localRayDir;
                        glm::vec3 worldHit = glm::vec3(model * glm::vec4(localHit, 1.0f));
                        float dist = glm::length(worldHit - rayOrigin);
                        if (dist < closestDist) {
                            closestDist = dist;
                            hitNode = node;
                        }
                    }
                }

                if (hitNode) {
                    scene.SelectNode(hitNode->id);
                    m_isRotatingCamera = false;
                } else {
                    m_isRotatingCamera = true;
                }
                } // end if (!hit2DUI)
            }
        }
    }

    // 2D UI dragging update
    if (m_uiRendererState.draggingWindowId != 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_isRotatingCamera = false;
            auto winNode = scene.GetNodeById(m_uiRendererState.draggingWindowId);
            if (winNode && winNode->IsUINode() && winNode->uiWindowMovable) {
                winNode->uiPosition.x += io.MouseDelta.x;
                winNode->uiPosition.y += io.MouseDelta.y;
            }
        } else {
            m_uiRendererState.draggingWindowId = 0;
        }
    } else if (m_isDraggingUINode && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_isRotatingCamera = false;
        auto draggedNode = scene.GetNodeById(m_draggedUINodeId);
        if (draggedNode && draggedNode->IsUINode()) {
            draggedNode->uiPosition.x += io.MouseDelta.x;
            draggedNode->uiPosition.y += io.MouseDelta.y;
        }
    } else {
        m_isDraggingUINode = false;
        m_draggedUINodeId = 0;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !(m_touchNavMode == TouchNavMode::Orbit && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        m_isRotatingCamera = false;
    }
    if (m_uiRendererState.draggingWindowId != 0 || m_isDraggingUINode) {
        m_isRotatingCamera = false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
        !(io.KeyShift && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left))) &&
        !(m_touchNavMode == TouchNavMode::Pan && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        m_isPanningCamera = false;
    }

    if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) {
        m_isRotatingCamera = false;
    }

    // Camera Navigation
    if (m_isRotatingCamera && !m_isPanningCamera && !ImGuizmo::IsUsing()) {
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            camera.Rotate(-io.MouseDelta.x * 0.4f, -io.MouseDelta.y * 0.4f);
        }
    }

    if (m_isPanningCamera) {
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            camera.Pan(io.MouseDelta.x, io.MouseDelta.y);
        }
    }

    if (m_viewportHovered && io.MouseWheel != 0.0f) {
        camera.Zoom(io.MouseWheel);
    }

    // Keyboard shortcuts
    if (m_viewportFocused || m_viewportHovered) {
        if (!ImGuizmo::IsUsing()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_currentGizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_currentGizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_currentGizmoOperation = ImGuizmo::SCALE;
            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                auto sel = scene.GetSelectedNode();
                if (sel) camera.FocusOn(sel->GetWorldPosition(scene));
            }
        }
    }

    // 3D Pivot Crosshair Marker
    if (m_showPivotAxisMarker) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 proj = camera.GetProjectionMatrix();
        proj[1][1] *= -1.0f;
        glm::mat4 vp = proj * view;

        auto ProjectPoint = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
            glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.001f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            screenPos.x = m_viewportPos.x + (ndc.x * 0.5f + 0.5f) * m_viewportSize.x;
            screenPos.y = m_viewportPos.y + (0.5f - ndc.y * 0.5f) * m_viewportSize.y;
            return true;
        };

        glm::vec3 pivot = camera.GetTarget();
        ImVec2 screenOrigin, screenX, screenY, screenZ;
        if (ProjectPoint(pivot, screenOrigin)) {
            float arm = 0.5f;
            if (ProjectPoint(pivot + glm::vec3(arm, 0.0f, 0.0f), screenX)) {
                drawList->AddLine(screenOrigin, screenX, IM_COL32(245, 65, 65, 240), 2.5f);
            }
            if (ProjectPoint(pivot + glm::vec3(0.0f, arm, 0.0f), screenY)) {
                drawList->AddLine(screenOrigin, screenY, IM_COL32(65, 230, 65, 240), 2.5f);
            }
            if (ProjectPoint(pivot + glm::vec3(0.0f, 0.0f, arm), screenZ)) {
                drawList->AddLine(screenOrigin, screenZ, IM_COL32(65, 130, 255, 240), 2.5f);
            }
            drawList->AddCircleFilled(screenOrigin, 4.0f, IM_COL32(255, 225, 70, 255));
        }
    }

    // 3D Scene Overlays: Particles, Splines, Decals, Spot Lights, Audio Sources, NavMesh
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 proj = camera.GetProjectionMatrix();
        proj[1][1] *= -1.0f;
        glm::mat4 vp = proj * view;

        auto Project3D = [&](const glm::vec3& worldPos, ImVec2& screenPos, float* outClipW = nullptr) -> bool {
            glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.05f) return false;
            if (outClipW) *outClipW = clip.w;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            screenPos.x = m_viewportPos.x + (ndc.x * 0.5f + 0.5f) * m_viewportSize.x;
            screenPos.y = m_viewportPos.y + (0.5f - ndc.y * 0.5f) * m_viewportSize.y;
            return true;
        };

        for (const auto& n : scene.GetNodes()) {
            glm::vec3 worldPos = n->GetWorldPosition(scene);
            bool isSel = (scene.GetSelectedId() == static_cast<int32_t>(n->id));

            // 1. Live Particle System rendering in 3D Viewport
            if (n->nodeType == NodeType::ParticleEmitter) {
                auto pNode = std::dynamic_pointer_cast<ParticleNode>(n);
                if (pNode) { ImVec2 emSp; if (Project3D(worldPos, emSp)) { drawList->AddCircleFilled(emSp, 5.0f, IM_COL32(255, 165, 60, 230)); drawList->AddText(ImVec2(emSp.x + 8.0f, emSp.y - 8.0f), IM_COL32(255, 190, 90, 240), "[Particles]"); } }
                if (false && pNode) {
                    for (const auto& p : pNode->GetParticles()) {
                        if (!p.alive) continue;
                        ImVec2 sp;
                        float clipW = 1.0f;
                        if (Project3D(p.position, sp, &clipW)) {
                            float radiusPx = std::clamp((p.size * 260.0f) / std::max(0.5f, clipW), 2.0f, 36.0f);
                            ImU32 col = IM_COL32(
                                (int)(std::clamp(p.color.r, 0.0f, 1.0f) * 255.0f),
                                (int)(std::clamp(p.color.g, 0.0f, 1.0f) * 255.0f),
                                (int)(std::clamp(p.color.b, 0.0f, 1.0f) * 255.0f),
                                (int)(std::clamp(p.color.a, 0.0f, 1.0f) * 230.0f)
                            );
                            drawList->AddCircleFilled(sp, radiusPx, col, 12);
                        }
                    }
                }
            }

            // 2. 3D Spline / Curve rendering
            if (n->nodeType == NodeType::Spline) {
                auto sNode = std::dynamic_pointer_cast<SplineNode>(n);
                int steps = 48;
                ImVec2 prevPt;
                bool hasPrev = false;
                for (int i = 0; i <= steps; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(steps);
                    glm::vec3 wp = sNode ? sNode->EvaluateWorld(scene, t) : worldPos;
                    ImVec2 curPt;
                    if (Project3D(wp, curPt)) {
                        if (hasPrev) {
                            drawList->AddLine(prevPt, curPt, isSel ? IM_COL32(80, 220, 255, 255) : IM_COL32(60, 175, 220, 200), isSel ? 3.0f : 2.0f);
                        }
                        prevPt = curPt;
                        hasPrev = true;
                    } else {
                        hasPrev = false;
                    }
                }
                if (sNode) for (const auto& cp : sNode->controlPoints) {
                    ImVec2 cpScreen;
                    if (Project3D(worldPos + cp, cpScreen)) {
                        drawList->AddCircleFilled(cpScreen, 4.5f, IM_COL32(255, 200, 60, 240));
                    }
                }
            }

            // 3b. Terrain Brush Radius Circle Gizmo
            if (n->nodeType == NodeType::Terrain) {
                auto tNode = std::dynamic_pointer_cast<TerrainNode>(n);
                if (tNode && tNode->sculptingEnabled && isSel) {
                    // Where the mouse points on the ground (the brush's centre)
                    glm::vec3 tPos = n->GetWorldPosition(scene);
                    {
                        ImVec2 mPos = ImGui::GetIO().MousePos;
                        float lX = mPos.x - m_viewportPos.x;
                        float lY = mPos.y - m_viewportPos.y;
                        if (lX >= 0.0f && lX <= m_viewportSize.x && lY >= 0.0f && lY <= m_viewportSize.y) {
                            glm::mat4 invVP = glm::inverse(vp);
                            glm::vec4 nearW = invVP * glm::vec4(lX / m_viewportSize.x * 2.0f - 1.0f, lY / m_viewportSize.y * 2.0f - 1.0f, 0.0f, 1.0f);
                            glm::vec4 farW = invVP * glm::vec4(lX / m_viewportSize.x * 2.0f - 1.0f, lY / m_viewportSize.y * 2.0f - 1.0f, 1.0f, 1.0f);
                            glm::vec3 hit;
                            if (nearW.w != 0.0f && farW.w != 0.0f &&
                                tNode->Raycast(glm::vec3(nearW) / nearW.w, glm::normalize(glm::vec3(farW) / farW.w - glm::vec3(nearW) / nearW.w), scene, hit)) {
                                tPos = hit;
                            }
                        }
                    }
                    const int segments = 48;
                    ImVec2 prevPt;
                    bool prevOk = false;
                    for (int s = 0; s <= segments; ++s) {
                        float angle = (float)s / (float)segments * 6.2831853f;
                        float rx = std::cos(angle) * tNode->brushRadius;
                        float rz = std::sin(angle) * tNode->brushRadius;
                        // Draped over the ground
                        glm::vec3 ptPos(tPos.x + rx, tNode->SampleHeight(tPos.x + rx, tPos.z + rz, scene) + 0.15f, tPos.z + rz);
                        ImVec2 scrPt;
                        bool ok = Project3D(ptPos, scrPt);
                        if (ok && prevOk) {
                            viewportDrawList->AddLine(prevPt, scrPt, IM_COL32(75, 230, 110, 240), 2.5f);
                        }
                        prevPt = scrPt;
                        prevOk = ok;
                    }
                }
            }

            // 3. Decal OBB Projector box wireframe
            if (n->nodeType == NodeType::Decal) {
                auto dNode = std::dynamic_pointer_cast<DecalNode>(n);
                glm::mat4 wm = n->GetWorldTransformMatrix(scene) * glm::scale(glm::mat4(1.0f), dNode ? dNode->decalSize : glm::vec3(1.0f));
                glm::vec3 corners[8] = {
                    {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},
                    {-0.5f,-0.5f, 0.5f}, {0.5f,-0.5f, 0.5f}, {0.5f,0.5f, 0.5f}, {-0.5f,0.5f, 0.5f}
                };
                ImVec2 sc[8];
                bool ok[8];
                for (int i = 0; i < 8; ++i) {
                    glm::vec3 wp = glm::vec3(wm * glm::vec4(corners[i], 1.0f));
                    ok[i] = Project3D(wp, sc[i]);
                }
                const int edges[12][2] = {
                    {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
                };
                ImU32 dCol = isSel ? IM_COL32(220, 120, 255, 240) : IM_COL32(170, 90, 210, 160);
                for (int e = 0; e < 12; ++e) {
                    if (ok[edges[e][0]] && ok[edges[e][1]]) {
                        drawList->AddLine(sc[edges[e][0]], sc[edges[e][1]], dCol, 1.8f);
                    }
                }
            }

            // 3b. Camera 3D Frustum, Up Indicator & Forward Direction Arrow
            if (n->nodeType == NodeType::Camera) {
                glm::mat4 wm = n->GetWorldTransformMatrix(scene);
                float fovRad = glm::radians(std::clamp(n->cameraFov, 25.0f, 110.0f));
                float fDist = 2.4f;
                float halfH = fDist * std::tan(fovRad * 0.5f) * 0.55f;
                float halfW = halfH * 1.6f;
                glm::vec3 localPts[8] = {
                    {0.0f, 0.0f, 0.0f},
                    {-halfW, -halfH, -fDist}, {halfW, -halfH, -fDist}, {halfW, halfH, -fDist}, {-halfW, halfH, -fDist},
                    {-halfW * 0.45f, halfH * 1.08f, -fDist}, {halfW * 0.45f, halfH * 1.08f, -fDist}, {0.0f, halfH * 1.38f, -fDist}
                };
                ImVec2 sp[8];
                bool ok[8];
                for (int i = 0; i < 8; ++i) {
                    ok[i] = Project3D(glm::vec3(wm * glm::vec4(localPts[i], 1.0f)), sp[i]);
                }
                ImU32 camCol = isSel ? IM_COL32(80, 225, 255, 250) : IM_COL32(65, 185, 230, 185);
                float th = isSel ? 2.2f : 1.5f;
                for (int c = 1; c <= 4; ++c) {
                    if (ok[0] && ok[c]) drawList->AddLine(sp[0], sp[c], camCol, th);
                    int nextC = (c % 4) + 1;
                    if (ok[c] && ok[nextC]) drawList->AddLine(sp[c], sp[nextC], camCol, th);
                }
                // Up triangle on top of frustum
                if (ok[5] && ok[6] && ok[7]) {
                    drawList->AddTriangleFilled(sp[5], sp[6], sp[7], IM_COL32(80, 225, 255, 120));
                    drawList->AddTriangle(sp[5], sp[6], sp[7], camCol, th);
                }
                // Central forward arrow
                ImVec2 arrowTip, arrowL, arrowR;
                if (ok[0] && Project3D(glm::vec3(wm * glm::vec4(0.0f, 0.0f, -1.35f, 1.0f)), arrowTip)) {
                    drawList->AddLine(sp[0], arrowTip, IM_COL32(255, 230, 80, 245), 2.5f);
                    if (Project3D(glm::vec3(wm * glm::vec4(-0.15f, 0.0f, -1.10f, 1.0f)), arrowL) &&
                        Project3D(glm::vec3(wm * glm::vec4(0.15f, 0.0f, -1.10f, 1.0f)), arrowR)) {
                        drawList->AddTriangleFilled(arrowTip, arrowL, arrowR, IM_COL32(255, 230, 80, 245));
                    }
                    drawList->AddText(ImVec2(sp[0].x + 10.0f, sp[0].y - 10.0f), IM_COL32(120, 235, 255, 245), "[Camera]");
                }
            }

            // 3c. Directional & Point Light 3D Direction Rays & Icons
            if (n->nodeType == NodeType::Light) {
                glm::mat4 wm = n->GetWorldTransformMatrix(scene);
                ImU32 lCol = IM_COL32((int)(n->lightColor.r * 255), (int)(n->lightColor.g * 255), (int)(n->lightColor.b * 255), isSel ? 245 : 195);
                ImVec2 centerSp;
                if (Project3D(worldPos, centerSp)) {
                    const char* lLabel = (n->lightType == LightType::Directional) ? "[Directional Light]" :
                                         (n->lightType == LightType::Spot) ? "[Spot Light]" : "[Point Light]";
                    drawList->AddText(ImVec2(centerSp.x + 10.0f, centerSp.y - 10.0f), lCol, lLabel);
                }
                if (n->lightType == LightType::Directional) {
                    // 5 parallel directional rays pointing along local -Z with arrowheads
                    const glm::vec3 offsets[5] = {
                        {0.0f, 0.0f, 0.0f}, {0.35f, 0.0f, 0.0f}, {-0.35f, 0.0f, 0.0f}, {0.0f, 0.35f, 0.0f}, {0.0f, -0.35f, 0.0f}
                    };
                    for (int r = 0; r < 5; ++r) {
                        ImVec2 rStart, rEnd, rWing1, rWing2;
                        glm::vec3 sLocal = offsets[r];
                        glm::vec3 eLocal = offsets[r] + glm::vec3(0.0f, 0.0f, -2.0f);
                        if (Project3D(glm::vec3(wm * glm::vec4(sLocal, 1.0f)), rStart) &&
                            Project3D(glm::vec3(wm * glm::vec4(eLocal, 1.0f)), rEnd)) {
                            drawList->AddLine(rStart, rEnd, lCol, (r == 0) ? 2.6f : 1.8f);
                            if (Project3D(glm::vec3(wm * glm::vec4(eLocal + glm::vec3(-0.12f, 0.0f, 0.25f), 1.0f)), rWing1) &&
                                Project3D(glm::vec3(wm * glm::vec4(eLocal + glm::vec3(0.12f, 0.0f, 0.25f), 1.0f)), rWing2)) {
                                drawList->AddTriangleFilled(rEnd, rWing1, rWing2, lCol);
                            }
                        }
                    }
                } else if (n->lightType == LightType::Point) {
                    // 6 starburst rays around the point light bulb
                    const glm::vec3 dirs[6] = {
                        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
                    };
                    for (int d = 0; d < 6; ++d) {
                        ImVec2 a, b;
                        if (Project3D(worldPos + dirs[d] * 0.25f, a) && Project3D(worldPos + dirs[d] * 0.65f, b)) {
                            drawList->AddLine(a, b, lCol, 2.0f);
                        }
                    }
                } else if (n->lightType == LightType::Spot) {
                    // Central beam arrow pointing down along local -Y
                    ImVec2 sStart, sEnd, sW1, sW2;
                    if (Project3D(worldPos, sStart) &&
                        Project3D(glm::vec3(wm * glm::vec4(0.0f, -2.2f, 0.0f, 1.0f)), sEnd)) {
                        drawList->AddLine(sStart, sEnd, lCol, 2.5f);
                        if (Project3D(glm::vec3(wm * glm::vec4(-0.15f, -1.85f, 0.0f, 1.0f)), sW1) &&
                            Project3D(glm::vec3(wm * glm::vec4(0.15f, -1.85f, 0.0f, 1.0f)), sW2)) {
                            drawList->AddTriangleFilled(sEnd, sW1, sW2, lCol);
                        }
                    }
                }
            }

            // 4. Spot Light cone wireframe
            if (n->nodeType == NodeType::Light && n->lightType == LightType::Spot) {
                auto dNode = std::dynamic_pointer_cast<DecalNode>(n);
                glm::mat4 wm = n->GetWorldTransformMatrix(scene) * glm::scale(glm::mat4(1.0f), dNode ? dNode->decalSize : glm::vec3(1.0f));
                glm::vec3 dir = glm::normalize(glm::vec3(wm * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
                glm::vec3 right = glm::normalize(glm::vec3(wm * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
                glm::vec3 fwd = glm::normalize(glm::vec3(wm * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
                float range = std::min(n->lightRange, 12.0f);
                float rad = range * std::tan(glm::radians(std::clamp(n->spotOuterConeAngle, 1.0f, 80.0f)));
                glm::vec3 baseCenter = worldPos + dir * range;
                ImVec2 apexScreen;
                if (Project3D(worldPos, apexScreen)) {
                    ImU32 lCol = IM_COL32((int)(n->lightColor.r * 255), (int)(n->lightColor.g * 255), (int)(n->lightColor.b * 255), isSel ? 230 : 140);
                    ImVec2 prevRing;
                    bool hasPrevRing = false;
                    for (int s = 0; s <= 16; ++s) {
                        float a = (static_cast<float>(s) / 16.0f) * 6.2831853f;
                        glm::vec3 ringPt = baseCenter + (right * std::cos(a) + fwd * std::sin(a)) * rad;
                        ImVec2 ringScreen;
                        if (Project3D(ringPt, ringScreen)) {
                            if (hasPrevRing) drawList->AddLine(prevRing, ringScreen, lCol, 1.5f);
                            if (s % 4 == 0) drawList->AddLine(apexScreen, ringScreen, lCol, 1.5f);
                            prevRing = ringScreen;
                            hasPrevRing = true;
                        } else {
                            hasPrevRing = false;
                        }
                    }
                }
            }

            // 5. Audio Source spatial range ring & label
            if (n->nodeType == NodeType::AudioSource) {
                ImVec2 centerSp;
                if (Project3D(worldPos, centerSp)) {
                    drawList->AddCircleFilled(centerSp, 5.5f, IM_COL32(70, 245, 190, 240));
                    drawList->AddText(ImVec2(centerSp.x + 8.0f, centerSp.y - 8.0f), IM_COL32(110, 255, 205, 240), "[Audio]");
                }
                auto aNode = std::dynamic_pointer_cast<AudioNode>(n);
                if (isSel && aNode && aNode->is3DSpatial) {
                    ImVec2 prevRing;
                    bool hasPrev = false;
                    for (int s = 0; s <= 24; ++s) {
                        float a = (static_cast<float>(s) / 24.0f) * 6.2831853f;
                        glm::vec3 rp = worldPos + glm::vec3(std::cos(a) * aNode->minDistance, 0.0f, std::sin(a) * aNode->minDistance);
                        ImVec2 sp;
                        if (Project3D(rp, sp)) {
                            if (hasPrev) drawList->AddLine(prevRing, sp, IM_COL32(70, 245, 190, 190), 1.5f);
                            prevRing = sp;
                            hasPrev = true;
                        } else hasPrev = false;
                    }
                }
            }
        }

        // 6. Baked NavMesh & A* Path visualization
        if (m_showNavMeshWindow && !m_navMesh.GetPolys().empty()) {
            for (const auto& tri : m_navMesh.GetPolys()) {
                if (!tri.walkable) continue;
                ImVec2 p0, p1, p2;
                if (Project3D(tri.vertices[0] + glm::vec3(0, 0.05f, 0), p0) &&
                    Project3D(tri.vertices[1] + glm::vec3(0, 0.05f, 0), p1) &&
                    Project3D(tri.vertices[2] + glm::vec3(0, 0.05f, 0), p2)) {
                    drawList->AddTriangleFilled(p0, p1, p2, IM_COL32(45, 195, 255, 45));
                    drawList->AddTriangle(p0, p1, p2, IM_COL32(60, 215, 255, 140), 1.2f);
                }
            }
            if (m_lastNavPath.size() >= 2) {
                for (size_t i = 0; i + 1 < m_lastNavPath.size(); ++i) {
                    ImVec2 a, b;
                    if (Project3D(m_lastNavPath[i] + glm::vec3(0, 0.15f, 0), a) &&
                        Project3D(m_lastNavPath[i + 1] + glm::vec3(0, 0.15f, 0), b)) {
                        drawList->AddLine(a, b, IM_COL32(80, 255, 110, 255), 3.5f);
                        drawList->AddCircleFilled(a, 4.5f, IM_COL32(255, 240, 80, 255));
                    }
                }
            }
        }
    }

    // 7. 3D Ruler & Measurement Tool Overlay
    RenderRulerOverlay(scene, camera, viewportDrawList);

    // Viewport Toolbar Overlay
    ImGui::SetCursorPos(ImVec2(minBound.x + 14.0f, minBound.y + 14.0f));
    ImGui::BeginGroup();

    // Row 0: Simulation Controls (Play, Pause, Stop)
    bool isPlay = gameLoop.IsPlaying();
    bool isPause = gameLoop.IsPaused();

    if (isPlay) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.22f, 1.0f));
    }
    if (ImGui::Button(isPlay ? TR("Game Active [Play]") : TR("Play Game [Play]"), ImVec2(110, 24))) {
        gameLoop.Play(scene);
        m_showGameWindow = true;
        m_uiRendererState.ResetPopups();
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (isPause) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.70f, 0.20f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.32f, 0.22f, 1.0f));
    }
    if (ImGui::Button(TR("Pause [||]"), ImVec2(75, 24))) {
        gameLoop.Pause();
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
    if (ImGui::Button(TR("Stop [X]"), ImVec2(75, 24))) {
        gameLoop.Stop(scene);
        m_showGameWindow = false;
        m_uiRendererState.ResetPopups();
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (isPlay) {
        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "%s", TR("| GAME RUNNING in Standalone Window"));
    } else if (isPause) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "%s", TR("| GAME PAUSED"));
    } else {
        ImGui::TextDisabled("%s", TR("| Editor Mode"));
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Row 1: Transform Controls & Selection
    bool isTranslate = (m_currentGizmoOperation == ImGuizmo::TRANSLATE);
    if (isTranslate) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.65f, 0.95f, 1.0f));
    if (ImGui::Button(TR("Translate [W]"))) m_currentGizmoOperation = ImGuizmo::TRANSLATE;
    if (isTranslate) ImGui::PopStyleColor();

    ImGui::SameLine();
    bool isRotate = (m_currentGizmoOperation == ImGuizmo::ROTATE);
    if (isRotate) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.65f, 0.95f, 1.0f));
    if (ImGui::Button(TR("Rotate [E]"))) m_currentGizmoOperation = ImGuizmo::ROTATE;
    if (isRotate) ImGui::PopStyleColor();

    ImGui::SameLine();
    bool isScale = (m_currentGizmoOperation == ImGuizmo::SCALE);
    if (isScale) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.65f, 0.95f, 1.0f));
    if (ImGui::Button(TR("Scale [R]"))) m_currentGizmoOperation = ImGuizmo::SCALE;
    if (isScale) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button(m_currentGizmoMode == ImGuizmo::LOCAL ? TR("Local") : TR("World"))) {
        m_currentGizmoMode = (m_currentGizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    }

    ImGui::SameLine();
    ImGui::Checkbox(TR("Snap"), &m_useSnap);

    // Row 2: Render & Shading Mode Buttons
    ImGui::Spacing();
    bool isSolid = (m_polygonMode == PolygonMode::Solid);
    if (isSolid) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.85f, 1.0f));
    if (ImGui::Button(TR("Solid"))) m_polygonMode = PolygonMode::Solid;
    if (isSolid) ImGui::PopStyleColor();

    ImGui::SameLine();
    bool isWire = (m_polygonMode == PolygonMode::Wireframe);
    if (isWire) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.85f, 1.0f));
    if (ImGui::Button(TR("Wireframe"))) m_polygonMode = PolygonMode::Wireframe;
    if (isWire) ImGui::PopStyleColor();

    ImGui::SameLine();
    bool isShadedWire = (m_polygonMode == PolygonMode::WireframeOnShaded);
    if (isShadedWire) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.85f, 1.0f));
    if (ImGui::Button(TR("Wire+Shaded"))) m_polygonMode = PolygonMode::WireframeOnShaded;
    if (isShadedWire) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button(m_showGrid ? TR("Grid: ON") : TR("Grid: OFF"))) {
        m_showGrid = !m_showGrid;
    }

    ImGui::SameLine();
    bool isRulerActive = m_rulerToolState.active;
    if (isRulerActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.15f, 1.0f));
    if (ImGui::Button(isRulerActive ? TR("Ruler: ON [M]") : TR("Ruler: OFF [M]"))) {
        SetRulerActive(!isRulerActive);
        m_rulerToolState.showRulerWindow = m_rulerToolState.active;
    }
    if (isRulerActive) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("3D Ruler & Measurement Tool [M]\nMeasure 3D distances, dimensions & angles in the viewport.\nClick to place Point A and Point B. Drag handles to adjust.");

    ImGui::SameLine();
    if (ImGui::Button(TR("Reset Cam"))) camera.Reset();
    ImGui::SameLine();
    if (ImGui::Button(TR("Front"))) camera.SetPresetView(0.0f, 0.0f);
    ImGui::SameLine();
    if (ImGui::Button(TR("Top"))) camera.SetPresetView(0.0f, 89.0f);
    ImGui::SameLine();
    if (ImGui::Button(TR("Bottom"))) camera.SetPresetView(0.0f, -89.0f);
    ImGui::SameLine();
    if (ImGui::Button(TR("Side"))) camera.SetPresetView(90.0f, 0.0f);
    ImGui::SameLine();
    if (ImGui::Button(TR("Iso"))) camera.SetPresetView(45.0f, 25.0f);

    // Touch & Viewport Navigation Controls
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.78f, 1.0f));
    ImGui::PushButtonRepeat(true);
    if (ImGui::Button(TR("Zoom +"), ImVec2(56, 0))) camera.Zoom(1.5f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom In (Tap or hold on touch screens)");

    ImGui::SameLine();
    if (ImGui::Button(TR("Zoom -"), ImVec2(56, 0))) camera.Zoom(-1.5f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom Out (Tap or hold on touch screens)");
    ImGui::PopButtonRepeat();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button(TR("Focus [F]"))) {
        auto sel = scene.GetSelectedNode();
        if (sel) camera.FocusOn(sel->GetWorldPosition(scene));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center view on selected node");

    ImGui::SameLine();
    ImGui::TextDisabled("| Pan:");
    ImGui::SameLine();
    ImGui::PushButtonRepeat(true);
    if (ImGui::Button("<##PanL")) camera.Pan(15.0f, 0.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan Left");
    ImGui::SameLine();
    if (ImGui::Button(">##PanR")) camera.Pan(-15.0f, 0.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan Right");
    ImGui::SameLine();
    if (ImGui::Button("^##PanU")) camera.Pan(0.0f, 15.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan Up");
    ImGui::SameLine();
    if (ImGui::Button("v##PanD")) camera.Pan(0.0f, -15.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan Down");
    ImGui::PopButtonRepeat();

    ImGui::SameLine();
    ImGui::TextDisabled("| Touch Drag:");
    ImGui::SameLine();
    bool isTSel = (m_touchNavMode == TouchNavMode::Select);
    if (isTSel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.60f, 0.35f, 1.0f));
    if (ImGui::Button("Select##TNav")) m_touchNavMode = TouchNavMode::Select;
    if (isTSel) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Touch Drag Mode: Object picking and gizmo transform");

    ImGui::SameLine();
    bool isTOrb = (m_touchNavMode == TouchNavMode::Orbit);
    if (isTOrb) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.15f, 1.0f));
    if (ImGui::Button("Orbit##TNav")) m_touchNavMode = TouchNavMode::Orbit;
    if (isTOrb) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Touch Drag Mode: 1-finger drag rotates the camera around the scene");

    ImGui::SameLine();
    bool isTPan = (m_touchNavMode == TouchNavMode::Pan);
    if (isTPan) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.30f, 0.85f, 1.0f));
    if (ImGui::Button("Pan##TNav")) m_touchNavMode = TouchNavMode::Pan;
    if (isTPan) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Touch Drag Mode: 1-finger drag pans the camera view horizontally & vertically");

    // Selected Node quick actions in viewport
    auto overlaySelectedNode = scene.GetSelectedNode();
    if (overlaySelectedNode) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button(TR("Delete Selected [Del]"))) {
            scene.DeleteSelected();
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete currently selected node from scene (Del)");
    }

    // Help Hint
    ImGui::Spacing();
    glm::vec3 pivot = camera.GetTarget();
    ImGui::TextDisabled("Orbit: Right Click Drag  |  Pan: Middle Click (or Shift+Drag)  |  Zoom: Scroll  |  Pivot: (%.1f, %.1f, %.1f)",
                        pivot.x, pivot.y, pivot.z);

    ImGui::EndGroup();

    // Ruler Active HUD Banner overlay
    if (m_rulerToolState.active) {
        ImGui::SetCursorPos(ImVec2(minBound.x + 14.0f, minBound.y + 14.0f + 112.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.13f, 0.18f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        float bannerWidth = std::min(m_viewportSize.x - 28.0f, 640.0f);
        if (bannerWidth > 200.0f && ImGui::BeginChild("##RulerViewportBanner", ImVec2(bannerWidth, 34), true, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "RULER:");
            ImGui::SameLine();
            if (!m_rulerToolState.current.hasA) {
                ImGui::Text("Click in 3D viewport to place Start Point (A)");
            } else if (!m_rulerToolState.current.hasB) {
                ImGui::Text("Click to place End Point (B)");
            } else {
                float d = glm::distance(m_rulerToolState.current.pointA, m_rulerToolState.current.pointB);
                ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "Distance: %s", m_rulerToolState.FormatDist(d).c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear [Esc]")) {
                m_rulerToolState.current.hasA = false;
                m_rulerToolState.current.hasB = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Measure Selected")) {
                auto sel = scene.GetSelectedNode();
                if (sel) {
                    if (!m_rulerToolState.current.hasA) {
                        m_rulerToolState.current.pointA = sel->GetWorldPosition(scene);
                        m_rulerToolState.current.hasA = true;
                    } else {
                        m_rulerToolState.current.pointB = sel->GetWorldPosition(scene);
                        m_rulerToolState.current.hasB = true;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Options...")) {
                m_rulerToolState.showRulerWindow = true;
            }
        }
        if (bannerWidth > 200.0f) ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ImGuizmo 3D Manipulator (Only active for 3D world nodes, not 2D UI nodes)
    auto selectedNode = scene.GetSelectedNode();
    if (selectedNode && !selectedNode->IsUINode()) {
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 proj = camera.GetProjectionMatrix();
        proj[1][1] *= -1.0f;

        glm::mat4 model = selectedNode->GetWorldTransformMatrix(scene);

        float* snap = nullptr;
        if (m_useSnap) {
            if (m_currentGizmoOperation == ImGuizmo::TRANSLATE) snap = m_snapTranslation;
            else if (m_currentGizmoOperation == ImGuizmo::ROTATE) snap = &m_snapRotation;
            else if (m_currentGizmoOperation == ImGuizmo::SCALE) snap = &m_snapScale;
        }

        ImGuizmo::OPERATION op = m_currentGizmoOperation;
        if ((selectedNode->nodeType == NodeType::Light || selectedNode->nodeType == NodeType::Camera) && op == ImGuizmo::SCALE) {
            op = ImGuizmo::TRANSLATE;
        }

        ImGuizmo::MODE actualMode = (op == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : m_currentGizmoMode;
        glm::vec3 origScale = selectedNode->scale;

        ImGuizmo::SetDrawlist();
        static bool s_wasGizmoUsing = false;
        static uint32_t s_gizmoNodeId = 0;
        static glm::vec3 s_gizmoStartPos{0.0f}, s_gizmoStartRot{0.0f}, s_gizmoStartScale{1.0f};
        if (s_wasGizmoUsing && !ImGuizmo::IsUsing()) {
            s_wasGizmoUsing = false;
            if (s_gizmoNodeId == selectedNode->id) {
                auto cmd = std::make_shared<Core::TransformCommand>(
                    selectedNode->id,
                    s_gizmoStartPos, s_gizmoStartRot, s_gizmoStartScale,
                    selectedNode->position, selectedNode->rotation, selectedNode->scale,
                    "Transform " + selectedNode->name
                );
                Core::UndoRedoManager::Get().PushCommand(cmd);
            }
        }
        if (!s_wasGizmoUsing && ImGuizmo::IsOver()) {
            s_gizmoNodeId = selectedNode->id;
            s_gizmoStartPos = selectedNode->position;
            s_gizmoStartRot = selectedNode->rotation;
            s_gizmoStartScale = selectedNode->scale;
        }
        ImGuizmo::SetRect(m_viewportPos.x, m_viewportPos.y, m_viewportSize.x, m_viewportSize.y);

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 op, actualMode,
                                 glm::value_ptr(model), nullptr, snap)) {
            selectedNode->SetWorldTransformMatrix(model, scene);
            s_wasGizmoUsing = true;
            if (selectedNode->nodeType == NodeType::Light || selectedNode->nodeType == NodeType::Camera) {
                selectedNode->scale = origScale;
            }
        }
    }

    // Allow Delete key to remove selected node while in Viewport
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
        scene.DeleteSelected();
    }

    ImGui::End();
}

void EditorUI::RenderCameraPreview(Scene& scene, Camera& camera, Graphics::IRenderTarget& previewOffscreen, Gameplay::GameLoop& gameLoop) {
    if (!m_showCameraPreviewWindow) return;
    if (!ImGui::Begin(TR("Camera Preview###CameraPreviewWindow"), &m_showCameraPreviewWindow)) {
        ImGui::End();
        return;
    }

    auto cameras = scene.GetCameras();
    auto activeCam = scene.GetActiveCamera();

    if (cameras.empty()) {
        ImGui::TextDisabled("No cameras present in the scene.");
        ImGui::Spacing();
        if (ImGui::Button("+ Add Scene Camera", ImVec2(160, 26))) {
            scene.AddCameraNode();
        }
    } else {
        ImGui::Text("Active Camera:");
        ImGui::SameLine();
        std::string currentCamName = activeCam ? activeCam->name : "None";
        if (ImGui::BeginCombo("##ActiveCamCombo", currentCamName.c_str())) {
            for (const auto& cam : cameras) {
                bool isSelected = (activeCam && activeCam->id == cam->id);
                if (ImGui::Selectable(cam->name.c_str(), isSelected)) {
                    scene.SetActiveCamera(cam->id);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Sync Editor Cam to Scene Cam")) {
            if (activeCam) {
                camera.SetTarget(activeCam->GetWorldPosition(scene) + glm::vec3(0.0f, 0.0f, -activeCam->scale.z * 5.0f));
            }
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.78f, 1.0f));
        ImGui::PushButtonRepeat(true);
        if (ImGui::Button("Zoom +##CamPrev")) {
            if (activeCam) activeCam->cameraFov = std::clamp(activeCam->cameraFov - 1.5f, 10.0f, 120.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom In (Narrow Viewfinder Field of View)");
        ImGui::SameLine();
        if (ImGui::Button("Zoom -##CamPrev")) {
            if (activeCam) activeCam->cameraFov = std::clamp(activeCam->cameraFov + 1.5f, 10.0f, 120.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom Out (Widen Viewfinder Field of View)");
        ImGui::SameLine();
        if (ImGui::Button("Dolly +##CamPrev")) {
            if (activeCam) activeCam->position += glm::vec3(activeCam->GetWorldTransformMatrix(scene) * glm::vec4(0.0f, 0.0f, -0.5f, 0.0f));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dolly Camera Forward Closer");
        ImGui::SameLine();
        if (ImGui::Button("Dolly -##CamPrev")) {
            if (activeCam) activeCam->position += glm::vec3(activeCam->GetWorldTransformMatrix(scene) * glm::vec4(0.0f, 0.0f, 0.5f, 0.0f));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dolly Camera Backward Further");
        ImGui::PopButtonRepeat();
        ImGui::SameLine();
        if (ImGui::Button("Cutscenes...##CamPrev")) {
            m_showCutsceneWindow = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open Cutscene & Cinematic Sequencer (Shift+C)");
        ImGui::PopStyleColor();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 0 && avail.y > 0) {
            float aspect = 16.0f / 9.0f;
            float w = avail.x;
            float h = w / aspect;
            if (h > avail.y) {
                h = avail.y;
                w = h * aspect;
            }

            ImTextureID previewTexture = (ImTextureID)(intptr_t)previewOffscreen.GetTextureId();
            if (previewTexture != 0) {
                float imgX = ImGui::GetCursorPosX() + (avail.x - w) * 0.5f;
                ImGui::SetCursorPosX(imgX);
                ImGui::Image(previewTexture, ImVec2(w, h));
                m_previewImageSize = glm::vec2(w, h);
                m_previewImageFrame = ImGui::GetFrameCount();
                ImVec2 imgPos = ImGui::GetItemRectMin();
                ImVec2 imgSize = ImGui::GetItemRectSize();

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                RenderUIElements(scene, drawList, imgPos, imgSize, false, &gameLoop);
            }
        }
    }

    ImGui::End();
}

void EditorUI::RenderAnimationTimeline(Scene& scene, Animation::AnimationPlayer& animPlayer) {
    if (!m_showTimelineWindow) return;
    if (!ImGui::Begin(TR("Animation Timeline###TimelineWindow"), &m_showTimelineWindow)) {
        ImGui::End();
        return;
    }

    auto selNode = scene.GetSelectedNode();
    if (selNode && selNode->hasSkeleton) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "Rigged Model: %s", selNode->name.c_str());
        ImGui::Text("Armature: %s  |  Bones: %zu", selNode->skeletonName.c_str(), selNode->boneNames.size());
        ImGui::Spacing();

        float curTime = 0.0f;
        float duration = 1.0f;
        bool isPlaying = false;
        if (m_onGetSkeletalAnimInfo) {
            m_onGetSkeletalAnimInfo(selNode->customMeshName, curTime, duration, isPlaying);
        }

        if (isPlaying) {
            if (ImGui::Button("Pause [||]", ImVec2(75, 26))) {
                if (m_onSkeletalScrub) m_onSkeletalScrub(selNode->customMeshName, selNode->currentAnimState, curTime, false);
            }
        } else {
            if (ImGui::Button("Play [>]", ImVec2(75, 26))) {
                selNode->playAnimation = true;
                if (m_onSkeletalScrub) m_onSkeletalScrub(selNode->customMeshName, selNode->currentAnimState, curTime, true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset [|<]", ImVec2(75, 26))) {
            if (m_onSkeletalScrub) m_onSkeletalScrub(selNode->customMeshName, selNode->currentAnimState, 0.0f, false);
        }

        ImGui::SameLine();
        ImGui::Text("Active Clip: %s", selNode->currentAnimState.c_str());

        char timeOverlay[64];
        snprintf(timeOverlay, sizeof(timeOverlay), "Time: %.2f s / %.2f s", curTime, duration);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##SkeletalScrubber", &curTime, 0.0f, duration, timeOverlay)) {
            if (m_onSkeletalScrub) m_onSkeletalScrub(selNode->customMeshName, selNode->currentAnimState, curTime, false);
        }

        if (!selNode->animClipNames.empty()) {
            ImGui::Spacing();
            ImGui::Text("Embedded Animation Clips (%zu):", selNode->animClipNames.size());
            for (const auto& cName : selNode->animClipNames) {
                bool isCur = (selNode->currentAnimState == cName);
                if (isCur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.35f, 1.0f));
                if (ImGui::Button(cName.c_str())) {
                    selNode->currentAnimState = cName;
                    if (m_onSkeletalScrub) m_onSkeletalScrub(selNode->customMeshName, cName, 0.0f, isPlaying);
                }
                if (isCur) ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }

        ImGui::End();
        return;
    }

    auto clip = animPlayer.GetClip();
    if (!clip) {
        ImGui::TextDisabled("No active animation clip.");
        ImGui::End();
        return;
    }

    float currentTime = animPlayer.GetPlaybackTime();
    float duration = clip->duration;

    if (animPlayer.IsPlaying()) {
        if (ImGui::Button("Pause [||]", ImVec2(75, 26))) animPlayer.Pause();
    } else {
        if (ImGui::Button("Play [>]", ImVec2(75, 26))) animPlayer.Play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop [X]", ImVec2(75, 26))) animPlayer.Stop(scene);

    ImGui::SameLine();
    ImGui::Checkbox("Loop", &clip->loop);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderFloat("Speed", &animPlayer.playbackSpeed, 0.1f, 3.0f, "%.1fx");

    ImGui::SameLine();
    selNode = scene.GetSelectedNode();
    if (selNode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("+ Keyframe Node", ImVec2(130, 26))) {
            clip->AddKeyframe(selNode->id, currentTime, selNode->position, selNode->rotation, selNode->scale);
            std::cout << "[Animation] Added keyframe for " << selNode->name << " at t=" << currentTime << "s" << std::endl;
        }
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    char timeOverlay[64];
    snprintf(timeOverlay, sizeof(timeOverlay), "Time: %.2f s / %.2f s", currentTime, duration);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##TimeScrubber", &currentTime, 0.0f, duration, timeOverlay)) {
        animPlayer.Seek(scene, currentTime);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Animated Tracks in Clip: %zu", clip->tracks.size());
    for (const auto& track : clip->tracks) {
        auto n = scene.GetNodeById(track.nodeId);
        std::string nodeName = n ? n->name : ("Node #" + std::to_string(track.nodeId));
        ImGui::BulletText("%s: %zu Pos Keys, %zu Rot Keys, %zu Scale Keys",
                          nodeName.c_str(), track.positionKeys.size(), track.rotationKeys.size(), track.scaleKeys.size());
    }

    ImGui::End();
}

void EditorUI::RenderAnimationStateMachine(Scene& scene) {
    if (!m_showAnimStateMachineWindow) return;

    ImGui::SetNextWindowSize(ImVec2(980, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(TR("Animation State Machine Graph###AnimStateMachineWindow"), &m_showAnimStateMachineWindow)) {
        ImGui::End();
        return;
    }

    // 1. Locate selected rigged model or offer selector
    std::shared_ptr<Node> targetNode = scene.GetSelectedNode();
    if (!targetNode || !targetNode->hasSkeleton) {
        std::vector<std::shared_ptr<Node>> riggedNodes;
        for (const auto& n : scene.GetNodes()) {
            if (n && n->hasSkeleton) riggedNodes.push_back(n);
        }

        if (riggedNodes.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "No rigged animated models in the scene.");
            ImGui::TextDisabled("Import an FBX character with an armature or skeleton to view its animation state machine graph.");
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Select Rigged Model to Inspect:");
        for (const auto& rn : riggedNodes) {
            std::string label = "> " + rn->name + " (" + rn->customMeshName + ")";
            if (ImGui::Button(label.c_str(), ImVec2(-1, 28))) {
                scene.SelectNode(rn->id);
            }
        }
        ImGui::End();
        return;
    }

    // 2. Extract states from model's animation clips
    std::vector<std::string> states = targetNode->animClipNames;
    if (states.empty()) {
        states = { "Idle", "Walk", "Run", "Attack" };
    }
    if (std::find(states.begin(), states.end(), targetNode->currentAnimState) == states.end()) {
        states.insert(states.begin(), targetNode->currentAnimState);
    }

    // 3. Auto-populate layout & default transitions if first time or model changed
    if (m_lastRiggedModelName != targetNode->customMeshName || m_animNodeCanvasPositions.empty()) {
        m_lastRiggedModelName = targetNode->customMeshName;
        m_animNodeCanvasPositions.clear();
        m_uiAnimTransitions.clear();

        float startX = 60.0f;
        float startY = 80.0f;
        for (size_t i = 0; i < states.size(); ++i) {
            const auto& st = states[i];
            if (st == "Idle") {
                m_animNodeCanvasPositions[st] = ImVec2(startX, startY + 90.0f);
            } else if (st == "Walk") {
                m_animNodeCanvasPositions[st] = ImVec2(startX + 280.0f, startY);
            } else if (st == "Run") {
                m_animNodeCanvasPositions[st] = ImVec2(startX + 560.0f, startY);
            } else if (st == "Attack") {
                m_animNodeCanvasPositions[st] = ImVec2(startX + 280.0f, startY + 200.0f);
            } else {
                float col = (float)(i % 3);
                float row = (float)(i / 3);
                m_animNodeCanvasPositions[st] = ImVec2(startX + col * 280.0f, startY + row * 180.0f);
            }
        }

        auto hasState = [&](const std::string& s) {
            return std::find(states.begin(), states.end(), s) != states.end();
        };

        if (hasState("Idle") && hasState("Walk")) {
            m_uiAnimTransitions.push_back({ "Idle", "Walk", 0.20f });
            m_uiAnimTransitions.push_back({ "Walk", "Idle", 0.20f });
        }
        if (hasState("Walk") && hasState("Run")) {
            m_uiAnimTransitions.push_back({ "Walk", "Run", 0.20f });
            m_uiAnimTransitions.push_back({ "Run", "Walk", 0.20f });
        }
        if (hasState("Attack")) {
            if (hasState("Idle")) {
                m_uiAnimTransitions.push_back({ "Idle", "Attack", 0.15f });
                m_uiAnimTransitions.push_back({ "Attack", "Idle", 0.20f });
            }
            if (hasState("Walk")) {
                m_uiAnimTransitions.push_back({ "Walk", "Attack", 0.15f });
            }
        }

        if (m_uiAnimTransitions.empty() && states.size() > 1) {
            for (size_t i = 0; i < states.size(); ++i) {
                size_t next = (i + 1) % states.size();
                m_uiAnimTransitions.push_back({ states[i], states[next], 0.20f });
            }
        }
    }

    // 4. Header Bar
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "Rigged Model: %s", targetNode->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| Armature: %s (%zu bones)", targetNode->skeletonName.c_str(), targetNode->boneNames.size());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.45f, 1.0f), "| Active State: [%s]", targetNode->currentAnimState.c_str());

    ImGui::SameLine();
    if (ImGui::SmallButton("Auto-Layout")) {
        float startX = 60.0f;
        float startY = 80.0f;
        for (size_t i = 0; i < states.size(); ++i) {
            const auto& st = states[i];
            if (st == "Idle") m_animNodeCanvasPositions[st] = ImVec2(startX, startY + 90.0f);
            else if (st == "Walk") m_animNodeCanvasPositions[st] = ImVec2(startX + 280.0f, startY);
            else if (st == "Run") m_animNodeCanvasPositions[st] = ImVec2(startX + 560.0f, startY);
            else if (st == "Attack") m_animNodeCanvasPositions[st] = ImVec2(startX + 280.0f, startY + 200.0f);
            else {
                float col = (float)(i % 3);
                float row = (float)(i / 3);
                m_animNodeCanvasPositions[st] = ImVec2(startX + col * 280.0f, startY + row * 180.0f);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset View")) {
        m_animGraphScroll = ImVec2(60.0f, 60.0f);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add Transition")) {
        m_showAddTransitionModal = true;
    }

    ImGui::Separator();

    // 5. Split: Left Canvas + Right Inspector Panel
    float availW = ImGui::GetContentRegionAvail().x;
    float panelW = 260.0f;
    float canvasW = availW - panelW - 8.0f;
    if (canvasW < 300.0f) {
        canvasW = availW;
        panelW = 0.0f;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Canvas Child
    ImGui::BeginChild("SM_Canvas", ImVec2(canvasW, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    ImVec2 canvasSz = ImGui::GetContentRegionAvail();
    ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSz.x, canvasP0.y + canvasSz.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(canvasP0, canvasP1, 0xFF16161B);

    bool isCanvasHovered = ImGui::IsWindowHovered();
    if (isCanvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        m_animGraphScroll.x += io.MouseDelta.x;
        m_animGraphScroll.y += io.MouseDelta.y;
    }

    float gridStep = 32.0f;
    float majorGridStep = 128.0f;
    for (float x = fmodf(m_animGraphScroll.x, gridStep); x < canvasSz.x; x += gridStep) {
        bool isMajor = fmodf(x - m_animGraphScroll.x, majorGridStep) < gridStep * 0.5f;
        drawList->AddLine(ImVec2(canvasP0.x + x, canvasP0.y), ImVec2(canvasP0.x + x, canvasP1.y), isMajor ? 0x22FFFFFF : 0x0EFFFFFF);
    }
    for (float y = fmodf(m_animGraphScroll.y, gridStep); y < canvasSz.y; y += gridStep) {
        bool isMajor = fmodf(y - m_animGraphScroll.y, majorGridStep) < gridStep * 0.5f;
        drawList->AddLine(ImVec2(canvasP0.x, canvasP0.y + y), ImVec2(canvasP1.x, canvasP0.y + y), isMajor ? 0x22FFFFFF : 0x0EFFFFFF);
    }

    const ImVec2 cardSize(200.0f, 90.0f);

    // 6. Draw Transitions
    for (size_t ti = 0; ti < m_uiAnimTransitions.size(); ++ti) {
        const auto& trans = m_uiAnimTransitions[ti];
        auto itFrom = m_animNodeCanvasPositions.find(trans.fromState);
        auto itTo = m_animNodeCanvasPositions.find(trans.toState);
        if (itFrom == m_animNodeCanvasPositions.end() || itTo == m_animNodeCanvasPositions.end()) continue;

        ImVec2 c1(canvasP0.x + m_animGraphScroll.x + itFrom->second.x + cardSize.x * 0.5f,
                  canvasP0.y + m_animGraphScroll.y + itFrom->second.y + cardSize.y * 0.5f);
        ImVec2 c2(canvasP0.x + m_animGraphScroll.x + itTo->second.x + cardSize.x * 0.5f,
                  canvasP0.y + m_animGraphScroll.y + itTo->second.y + cardSize.y * 0.5f);

        ImVec2 p1, p2;
        float dx = c2.x - c1.x;
        float dy = c2.y - c1.y;

        if (fabsf(dx) >= fabsf(dy)) {
            if (dx >= 0.0f) {
                p1 = ImVec2(c1.x + cardSize.x * 0.5f, c1.y);
                p2 = ImVec2(c2.x - cardSize.x * 0.5f, c2.y);
            } else {
                p1 = ImVec2(c1.x - cardSize.x * 0.5f, c1.y);
                p2 = ImVec2(c2.x + cardSize.x * 0.5f, c2.y);
            }
        } else {
            if (dy >= 0.0f) {
                p1 = ImVec2(c1.x, c1.y + cardSize.y * 0.5f);
                p2 = ImVec2(c2.x, c2.y - cardSize.y * 0.5f);
            } else {
                p1 = ImVec2(c1.x, c1.y - cardSize.y * 0.5f);
                p2 = ImVec2(c2.x, c2.y + cardSize.y * 0.5f);
            }
        }

        float offsetMult = 0.0f;
        for (size_t prev = 0; prev < ti; ++prev) {
            if (m_uiAnimTransitions[prev].fromState == trans.toState && m_uiAnimTransitions[prev].toState == trans.fromState) {
                offsetMult = 14.0f;
                break;
            }
        }

        float curveDist = fminf(fmaxf(fabsf(p2.x - p1.x) * 0.5f, fabsf(p2.y - p1.y) * 0.5f), 120.0f);
        ImVec2 cp1 = (fabsf(dx) >= fabsf(dy)) ? ImVec2(p1.x + (dx >= 0 ? curveDist : -curveDist), p1.y + offsetMult)
                                              : ImVec2(p1.x + offsetMult, p1.y + (dy >= 0 ? curveDist : -curveDist));
        ImVec2 cp2 = (fabsf(dx) >= fabsf(dy)) ? ImVec2(p2.x - (dx >= 0 ? curveDist : -curveDist), p2.y + offsetMult)
                                              : ImVec2(p2.x + offsetMult, p2.y - (dy >= 0 ? curveDist : -curveDist));

        bool isCurrentTransition = (targetNode->currentAnimState == trans.toState);
        bool isSelectedTransition = (m_selectedTransitionIdx == (int)ti);

        ImU32 lineColor = isSelectedTransition ? 0xFFFFAA33 : (isCurrentTransition ? 0xFF44DD66 : 0xAA778899);
        float lineThick = isSelectedTransition ? 3.5f : (isCurrentTransition ? 3.0f : 2.0f);

        drawList->AddBezierCubic(p1, cp1, cp2, p2, lineColor, lineThick);

        ImVec2 arrowDir(p2.x - cp2.x, p2.y - cp2.y);
        float dLen = sqrtf(arrowDir.x * arrowDir.x + arrowDir.y * arrowDir.y);
        if (dLen > 0.001f) {
            arrowDir.x /= dLen;
            arrowDir.y /= dLen;
            ImVec2 arrowPerp(-arrowDir.y, arrowDir.x);
            float aSize = isSelectedTransition ? 14.0f : 11.0f;
            float aWidth = isSelectedTransition ? 7.0f : 5.5f;

            ImVec2 tip = p2;
            ImVec2 left = ImVec2(p2.x - arrowDir.x * aSize + arrowPerp.x * aWidth, p2.y - arrowDir.y * aSize + arrowPerp.y * aWidth);
            ImVec2 right = ImVec2(p2.x - arrowDir.x * aSize - arrowPerp.x * aWidth, p2.y - arrowDir.y * aSize - arrowPerp.y * aWidth);
            drawList->AddTriangleFilled(tip, left, right, lineColor);
        }

        ImVec2 mid(0.125f * p1.x + 0.375f * cp1.x + 0.375f * cp2.x + 0.125f * p2.x,
                   0.125f * p1.y + 0.375f * cp1.y + 0.375f * cp2.y + 0.125f * p2.y);

        char pillTxt[32];
        snprintf(pillTxt, sizeof(pillTxt), "%.2fs", trans.blendDuration);
        ImVec2 txtSz = ImGui::CalcTextSize(pillTxt);
        ImVec2 pillMin(mid.x - txtSz.x * 0.5f - 6.0f, mid.y - txtSz.y * 0.5f - 3.0f);
        ImVec2 pillMax(mid.x + txtSz.x * 0.5f + 6.0f, mid.y + txtSz.y * 0.5f + 3.0f);

        bool pillHovered = io.MousePos.x >= pillMin.x && io.MousePos.x <= pillMax.x &&
                           io.MousePos.y >= pillMin.y && io.MousePos.y <= pillMax.y;

        drawList->AddRectFilled(pillMin, pillMax, isSelectedTransition ? 0xEE443311 : (pillHovered ? 0xEE2A3545 : 0xEE1E2430), 4.0f);
        drawList->AddRect(pillMin, pillMax, isSelectedTransition ? 0xFFFFAA33 : (pillHovered ? 0xFF66B0FF : lineColor), 4.0f);
        drawList->AddText(ImVec2(pillMin.x + 6.0f, pillMin.y + 3.0f), 0xFFEEEEEE, pillTxt);

        if (pillHovered) {
            ImGui::SetTooltip("Transition: %s -> %s (Crossfade: %.2fs)\nClick to edit duration", trans.fromState.c_str(), trans.toState.c_str(), trans.blendDuration);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_selectedTransitionIdx = (int)ti;
                m_selectedGraphState = "";
            }
        }
    }

    // 7. Draw State Cards
    for (size_t si = 0; si < states.size(); ++si) {
        const auto& stateName = states[si];
        ImVec2& nodePos = m_animNodeCanvasPositions[stateName];
        ImVec2 cardMin(canvasP0.x + m_animGraphScroll.x + nodePos.x, canvasP0.y + m_animGraphScroll.y + nodePos.y);
        ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);

        bool isActive = (targetNode->currentAnimState == stateName);
        bool isSelected = (m_selectedGraphState == stateName);

        ImVec2 headerMax(cardMax.x, cardMin.y + 26.0f);
        bool headerHovered = io.MousePos.x >= cardMin.x && io.MousePos.x <= headerMax.x &&
                             io.MousePos.y >= cardMin.y && io.MousePos.y <= headerMax.y;

        ImGui::PushID(stateName.c_str());
        if (headerHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_selectedGraphState = stateName;
            m_selectedTransitionIdx = -1;
        }

        if (headerHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            nodePos.x += io.MouseDelta.x;
            nodePos.y += io.MouseDelta.y;
        }

        drawList->AddRectFilled(ImVec2(cardMin.x + 3, cardMin.y + 3), ImVec2(cardMax.x + 3, cardMax.y + 3), 0x55000000, 8.0f);

        ImU32 cardBg = isActive ? 0xF21C2E22 : (isSelected ? 0xF22A2E3D : 0xF220222B);
        ImU32 borderCol = isActive ? 0xFF44DD66 : (isSelected ? 0xFF66AAFF : 0xFF3F4452);
        float borderThick = (isActive || isSelected) ? 2.5f : 1.0f;
        drawList->AddRectFilled(cardMin, cardMax, cardBg, 8.0f);
        drawList->AddRect(cardMin, cardMax, borderCol, 8.0f, 0, borderThick);

        ImU32 headerBg = isActive ? 0xFF2A7A40 : (isSelected ? 0xFF2C4A70 : 0xFF2C303E);
        drawList->AddRectFilled(cardMin, headerMax, headerBg, 8.0f, ImDrawFlags_RoundCornersTop);
        drawList->AddLine(ImVec2(cardMin.x, headerMax.y), headerMax, borderCol, 1.0f);

        std::string title = stateName;
        if (title.size() > 14) title = title.substr(0, 12) + "..";
        drawList->AddText(ImVec2(cardMin.x + 10.0f, cardMin.y + 5.0f), 0xFFFFFFFF, title.c_str());

        if (isActive) {
            drawList->AddCircleFilled(ImVec2(cardMax.x - 14.0f, cardMin.y + 13.0f), 5.0f, 0xFF44FF66);
            drawList->AddText(ImVec2(cardMax.x - 68.0f, cardMin.y + 5.0f), 0xFF66FF88, "ACTIVE");
        }

        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 10.0f, cardMin.y + 32.0f));
        ImGui::TextDisabled("Clip: %s", stateName.c_str());

        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 10.0f, cardMin.y + 54.0f));
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.35f, 1.0f));
            ImGui::Button("[Active] Playing State", ImVec2(cardSize.x - 20.0f, 24.0f));
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.32f, 0.44f, 1.0f));
            if (ImGui::Button("> Transition To", ImVec2(cardSize.x - 20.0f, 24.0f))) {
                targetNode->currentAnimState = stateName;
                if (m_onSkeletalScrub) {
                    m_onSkeletalScrub(targetNode->customMeshName, stateName, 0.0f, true);
                }
            }
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    // 8. Right-Side Inspector Panel
    if (panelW > 10.0f) {
        ImGui::SameLine();
        ImGui::BeginChild("SM_Inspector", ImVec2(panelW, 0), true);

        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "State Machine Details");
        ImGui::Separator();
        ImGui::Spacing();

        if (m_selectedTransitionIdx >= 0 && m_selectedTransitionIdx < (int)m_uiAnimTransitions.size()) {
            auto& selTrans = m_uiAnimTransitions[m_selectedTransitionIdx];
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Transition Selected:");
            ImGui::Text("%s  ->  %s", selTrans.fromState.c_str(), selTrans.toState.c_str());
            ImGui::Spacing();
            ImGui::SliderFloat("Blend Time (s)", &selTrans.blendDuration, 0.01f, 1.5f, "%.2f s");

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.65f, 1.0f));
            if (ImGui::Button("> Test Transition Live", ImVec2(-1, 26))) {
                targetNode->currentAnimState = selTrans.toState;
                if (m_onSkeletalScrub) {
                    m_onSkeletalScrub(targetNode->customMeshName, selTrans.toState, 0.0f, true);
                }
            }
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
            if (ImGui::Button("Delete Transition", ImVec2(-1, 24))) {
                m_uiAnimTransitions.erase(m_uiAnimTransitions.begin() + m_selectedTransitionIdx);
                m_selectedTransitionIdx = -1;
            }
            ImGui::PopStyleColor();

        } else if (!m_selectedGraphState.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Selected State: %s", m_selectedGraphState.c_str());
            bool isCurrent = (targetNode->currentAnimState == m_selectedGraphState);
            ImGui::Text("Status: %s", isCurrent ? "Active (Playing)" : "Standby");

            ImGui::Spacing();
            if (!isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.35f, 1.0f));
                if (ImGui::Button("Switch To This State", ImVec2(-1, 26))) {
                    targetNode->currentAnimState = m_selectedGraphState;
                    if (m_onSkeletalScrub) {
                        m_onSkeletalScrub(targetNode->customMeshName, m_selectedGraphState, 0.0f, true);
                    }
                }
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Outgoing Transitions:");

            int transToDelete = -1;
            for (size_t ti = 0; ti < m_uiAnimTransitions.size(); ++ti) {
                auto& tr = m_uiAnimTransitions[ti];
                if (tr.fromState == m_selectedGraphState) {
                    ImGui::PushID((int)ti);
                    ImGui::BulletText("-> %s", tr.toState.c_str());
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0f);
                    ImGui::DragFloat("##Duration", &tr.blendDuration, 0.02f, 0.01f, 2.0f, "%.2fs");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        transToDelete = (int)ti;
                    }
                    ImGui::PopID();
                }
            }
            if (transToDelete >= 0) {
                m_uiAnimTransitions.erase(m_uiAnimTransitions.begin() + transToDelete);
            }

            ImGui::Spacing();
            if (ImGui::Button("+ Add Outgoing Transition", ImVec2(-1, 24))) {
                m_showAddTransitionModal = true;
                for (size_t si = 0; si < states.size(); ++si) {
                    if (states[si] == m_selectedGraphState) {
                        m_newTransitionFromIdx = (int)si;
                        break;
                    }
                }
            }

        } else {
            ImGui::TextDisabled("Select a State card or Transition pill in the graph to view and edit properties.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Graph Summary:");
            ImGui::BulletText("Total States: %zu", states.size());
            ImGui::BulletText("Total Transitions: %zu", m_uiAnimTransitions.size());
            ImGui::BulletText("Active State: %s", targetNode->currentAnimState.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Tips:");
            ImGui::BulletText("Drag card headers to arrange.");
            ImGui::BulletText("Right-click drag to pan.");
            ImGui::BulletText("Click 'Transition To' to preview.");
        }

        ImGui::EndChild();
    }

    // 9. Add Transition Modal Dialog
    if (m_showAddTransitionModal) {
        ImGui::OpenPopup("Add State Transition");
    }

    if (ImGui::BeginPopupModal("Add State Transition", &m_showAddTransitionModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Create a new transition between animation states:");
        ImGui::Spacing();

        if (m_newTransitionFromIdx >= (int)states.size()) m_newTransitionFromIdx = 0;
        if (m_newTransitionToIdx >= (int)states.size()) m_newTransitionToIdx = (states.size() > 1) ? 1 : 0;

        if (ImGui::BeginCombo("From State", states[m_newTransitionFromIdx].c_str())) {
            for (int i = 0; i < (int)states.size(); ++i) {
                bool isSel = (m_newTransitionFromIdx == i);
                if (ImGui::Selectable(states[i].c_str(), isSel)) m_newTransitionFromIdx = i;
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("To State", states[m_newTransitionToIdx].c_str())) {
            for (int i = 0; i < (int)states.size(); ++i) {
                bool isSel = (m_newTransitionToIdx == i);
                if (ImGui::Selectable(states[i].c_str(), isSel)) m_newTransitionToIdx = i;
                if (isSel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SliderFloat("Blend Duration (s)", &m_newTransitionBlend, 0.01f, 1.0f, "%.2f s");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Add Transition", ImVec2(120, 26))) {
            std::string from = states[m_newTransitionFromIdx];
            std::string to = states[m_newTransitionToIdx];
            if (from != to) {
                bool exists = false;
                for (auto& tr : m_uiAnimTransitions) {
                    if (tr.fromState == from && tr.toState == to) {
                        tr.blendDuration = m_newTransitionBlend;
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    m_uiAnimTransitions.push_back({ from, to, m_newTransitionBlend });
                }
            }
            m_showAddTransitionModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 26))) {
            m_showAddTransitionModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}

void EditorUI::RenderGameWindow(Scene& scene, Graphics::IRenderTarget& gameOffscreen, Gameplay::GameLoop& gameLoop) {
    if (!m_showGameWindow && !gameLoop.IsPlaying() && !gameLoop.IsPaused()) {
        m_isGameWindowFocused = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(1060, 640), ImGuiCond_FirstUseEver);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool isOpen = true;
    ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin(TR("Game [Standalone Window]###GameStandaloneWindow"), &isOpen, winFlags)) {
        m_isGameWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool isPlay = gameLoop.IsPlaying();
        bool isPause = gameLoop.IsPaused();

        if (isPlay) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
            if (ImGui::Button("Pause [||]", ImVec2(80, 24))) gameLoop.Pause();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.22f, 1.0f));
            if (ImGui::Button("Resume [>]", ImVec2(80, 24))) gameLoop.Resume();
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("Stop [X]", ImVec2(80, 24))) {
            gameLoop.Stop(scene);
            m_showGameWindow = false;
            isOpen = false;
            m_uiRendererState.ResetPopups();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.35f, 0.45f, 1.0f));
        if (ImGui::Button("Key Bindings", ImVec2(110, 24))) {
            m_showInputSettingsModal = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (gameLoop.IsPointerLocked()) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "[Pointer Locked - Move Mouse to Look | Esc to Release]");
        } else {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "| WASD Move, Shift Run, Space Jump | Click Canvas or Hold Right-Click to Look");
        }

        ImGui::Separator();

        // Render Game Texture (from Active Scene Camera)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 10.0f && avail.y > 10.0f) {
            float aspect = 16.0f / 9.0f;
            float w = avail.x;
            float h = w / aspect;
            if (h > avail.y) {
                h = avail.y;
                w = h * aspect;
            }

            ImTextureID tex = (ImTextureID)(intptr_t)gameOffscreen.GetTextureId();
            if (tex) {
                float offsetX = (avail.x - w) * 0.5f;
                float offsetY = (avail.y - h) * 0.5f;

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
                ImGui::Image(tex, ImVec2(w, h));
                m_gameImageSize = glm::vec2(w, h);
                m_gameImageFrame = ImGui::GetFrameCount();
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    gameLoop.SetPointerLocked(true);
                }
                ImVec2 imgPos = ImGui::GetItemRectMin();
                ImVec2 imgSize = ImGui::GetItemRectSize();

                // Render 2D Scene UI Nodes strictly inside Game Screen
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                Scene& gameScene = gameLoop.GetActivePlayScene(scene);
                RenderUIElements(gameScene, drawList, imgPos, imgSize, false, &gameLoop);


            }
        }
    }
    ImGui::End();

    if (!isOpen) {
        m_isGameWindowFocused = false;
        m_showGameWindow = false;
        gameLoop.SetPointerLocked(false);
        if (gameLoop.IsPlaying() || gameLoop.IsPaused()) {
            gameLoop.Stop(scene);
        }
    }
}

void EditorUI::LoadScriptFile(const std::string& path) {
    if (m_scriptTextBuffer.empty()) {
        m_scriptTextBuffer.resize(65536, '\0');
    }
    std::fill(m_scriptTextBuffer.begin(), m_scriptTextBuffer.end(), '\0');

    std::ifstream file(path);
    if (file.is_open()) {
        std::string sContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        if (sContent.size() >= m_scriptTextBuffer.size()) {
            m_scriptTextBuffer.resize(sContent.size() + 2048, '\0');
        }
        memcpy(m_scriptTextBuffer.data(), sContent.data(), sContent.size());
        m_scriptTextBuffer[sContent.size()] = '\0';
    }
    m_scriptModified = false;
}

void EditorUI::SaveScriptFile(const std::string& path) {
    if (path.empty()) return;
    std::ofstream file(path);
    if (file.is_open()) {
        file << m_scriptTextBuffer.data();
        file.close();
        m_scriptModified = false;
        std::cout << "[ScriptEditor] Successfully saved script: " << path << std::endl;
    }
}

void EditorUI::InsertScriptTemplate(int index) {
    std::string templateCode;
    if (index == 0) {
        templateCode =
            "-- NodeRotator.lua\n"
            "-- Smoothly rotates an object around its Y axis\n\n"
            "local speed = 45.0 -- degrees per second\n\n"
            "function OnInit(self)\n"
            "    print(\"Initialized NodeRotator on node: \" .. self.name)\n"
            "end\n\n"
            "function OnUpdate(self, dt)\n"
            "    -- Rotate node around Y axis\n"
            "    NodeAPI.Rotate(self.id, 0.0, speed * dt, 0.0)\n"
            "end\n";
    } else if (index == 1) {
        templateCode =
            "-- FloatingPlatform.lua\n"
            "-- Moves a node smoothly up and down\n\n"
            "local time = 0.0\n"
            "local amplitude = 2.0\n"
            "local frequency = 1.5\n"
            "local startY = 0.0\n\n"
            "function OnInit(self)\n"
            "    startY = self.position.y\n"
            "    print(\"Floating platform initialized at Y = \" .. startY)\n"
            "end\n\n"
            "function OnUpdate(self, dt)\n"
            "    time = time + dt\n"
            "    local newY = startY + math.sin(time * frequency) * amplitude\n"
            "    NodeAPI.SetPosition(self.id, self.position.x, newY, self.position.z)\n"
            "end\n";
    } else if (index == 2) {
        templateCode =
            "-- CustomInputController.lua\n"
            "-- Move any node using configured action inputs\n\n"
            "local moveSpeed = 5.0\n\n"
            "function OnInit(self)\n"
            "    print(\"Custom controller ready on \" .. self.name)\n"
            "end\n\n"
            "function OnUpdate(self, dt)\n"
            "    local dx = 0.0\n"
            "    local dz = 0.0\n\n"
            "    if Input.IsDown(\"MoveForward\") then dz = dz - 1.0 end\n"
            "    if Input.IsDown(\"MoveBackward\") then dz = dz + 1.0 end\n"
            "    if Input.IsDown(\"MoveLeft\") then dx = dx - 1.0 end\n"
            "    if Input.IsDown(\"MoveRight\") then dx = dx + 1.0 end\n\n"
            "    if dx ~= 0.0 or dz ~= 0.0 then\n"
            "        NodeAPI.Translate(self.id, dx * moveSpeed * dt, 0.0, dz * moveSpeed * dt)\n"
            "    end\n"
            "end\n";
    } else {
        templateCode =
            "-- UIController.lua\n"
            "-- Updates 2D UI elements like progress bars or text\n\n"
            "local counter = 0.0\n\n"
            "function OnInit(self)\n"
            "    print(\"UI Controller initialized on \" .. self.name)\n"
            "end\n\n"
            "function OnUpdate(self, dt)\n"
            "    counter = (counter + dt * 0.2) % 1.0\n"
            "    NodeAPI.SetUIProgress(self.id, counter)\n"
            "    NodeAPI.SetUIText(self.id, string.format(\"Progress: %d%%\", math.floor(counter * 100)))\n"
            "end\n";
    }

    if (m_scriptTextBuffer.size() < templateCode.size() + 1024) {
        m_scriptTextBuffer.resize(templateCode.size() + 4096, '\0');
    }
    std::fill(m_scriptTextBuffer.begin(), m_scriptTextBuffer.end(), '\0');
    memcpy(m_scriptTextBuffer.data(), templateCode.data(), templateCode.size());
    m_scriptModified = true;
}

void EditorUI::RenderScriptEditor() {
    if (!m_showScriptEditor) return;
    if (!ImGui::Begin(TR("Script Editor###ScriptEditorWindow"), &m_showScriptEditor)) {
        ImGui::End();
        return;
    }

    if (m_projectRoot.empty()) {
        ImGui::TextDisabled("No project loaded. Open or create a project to edit scripts.");
        ImGui::End();
        return;
    }

    std::filesystem::path scriptsDir = std::filesystem::path(m_projectRoot) / "scripts";
    if (!std::filesystem::exists(scriptsDir)) {
        try { std::filesystem::create_directories(scriptsDir); } catch (...) {}
    }

    std::vector<std::string> scriptFiles;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(scriptsDir)) {
            if (entry.is_regular_file()) {
                scriptFiles.push_back(entry.path().filename().string());
            }
        }
    } catch (...) {}
    std::sort(scriptFiles.begin(), scriptFiles.end());

    if (scriptFiles.empty()) {
        std::string starterPath = (scriptsDir / "PlayerController.lua").string();
        std::ofstream f(starterPath);
        if (f.is_open()) {
            f << "-- PlayerController.lua\n"
              << "-- Controls player movement, jump mechanics and state\n\n"
              << "function OnInit()\n"
              << "    print(\"[Lua] PlayerController initialized\")\n"
              << "    speed = 5.0\n"
              << "    jumpForce = 7.0\n"
              << "    gravity = -18.0\n"
              << "end\n\n"
              << "function OnUpdate(dt)\n"
              << "    -- Input handled by engine CharacterController\n"
              << "end\n\n"
              << "function OnInteract(item)\n"
              << "    print(\"[Lua] Interacted with: \" .. item.name)\n"
              << "    if item.type == \"Consumable\" then\n"
              << "        player:AddHealth(25)\n"
              << "    end\n"
              << "end\n";
            f.close();
            scriptFiles.push_back("PlayerController.lua");
        }
    }

    if (m_activeScriptFile.empty() && !scriptFiles.empty()) {
        m_activeScriptFile = scriptFiles[0];
        LoadScriptFile((scriptsDir / m_activeScriptFile).string());
    }

    ImGui::Text("Script:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##ScriptSelectCombo", m_activeScriptFile.c_str())) {
        for (const auto& s : scriptFiles) {
            bool isSelected = (s == m_activeScriptFile);
            if (ImGui::Selectable(s.c_str(), isSelected)) {
                if (m_scriptModified) {
                    SaveScriptFile((scriptsDir / m_activeScriptFile).string());
                }
                m_activeScriptFile = s;
                LoadScriptFile((scriptsDir / m_activeScriptFile).string());
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("+ New Script", ImVec2(95, 24))) {
        ImGui::OpenPopup("CreateNewScriptPopup");
    }

    if (ImGui::BeginPopup("CreateNewScriptPopup")) {
        ImGui::Text("Enter script name (.lua):");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##NewScriptName", m_newScriptNameBuffer, sizeof(m_newScriptNameBuffer));
        if (ImGui::Button("Create", ImVec2(90, 24))) {
            std::string sName = m_newScriptNameBuffer;
            if (sName.find('.') == std::string::npos) sName += ".lua";
            std::string fullNew = (scriptsDir / sName).string();
            std::ofstream nf(fullNew);
            if (nf.is_open()) {
                nf << "-- " << sName << "\n\nfunction OnInit()\nend\n\nfunction OnUpdate(dt)\nend\n";
                nf.close();
            }
            m_activeScriptFile = sName;
            LoadScriptFile(fullNew);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(70, 24))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, m_scriptModified ? ImVec4(0.85f, 0.45f, 0.15f, 1.0f) : ImVec4(0.22f, 0.45f, 0.28f, 1.0f));
    if (ImGui::Button(m_scriptModified ? "Save Script * [Ctrl+S]" : "Save Script [Ctrl+S]", ImVec2(160, 24))) {
        SaveScriptFile((scriptsDir / m_activeScriptFile).string());
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("Templates [v]", ImVec2(95, 24))) {
        ImGui::OpenPopup("TemplatesPopup");
    }

    if (ImGui::BeginPopup("TemplatesPopup")) {
        if (ImGui::MenuItem("Character Controller & Movement")) {
            InsertScriptTemplate(0);
        }
        if (ImGui::MenuItem("Item Pickup & Interaction")) {
            InsertScriptTemplate(1);
        }
        if (ImGui::MenuItem("Enemy AI Patrol")) {
            InsertScriptTemplate(2);
        }
        if (ImGui::MenuItem("Quest & Level Trigger Volume")) {
            InsertScriptTemplate(3);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (m_scriptModified) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "[Unsaved Changes]");
    } else {
        ImGui::TextDisabled("[Saved]");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_scriptTextBuffer.empty()) {
        m_scriptTextBuffer.resize(65536, '\0');
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y = std::max(avail.y - 4.0f, 100.0f);

    ImGuiInputTextFlags textFlags = ImGuiInputTextFlags_AllowTabInput;
    if (ImGui::InputTextMultiline("##ScriptCodeEditor", m_scriptTextBuffer.data(), m_scriptTextBuffer.size(), avail, textFlags)) {
        m_scriptModified = true;
    }

    if (ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        SaveScriptFile((scriptsDir / m_activeScriptFile).string());
    }

    ImGui::End();
}

void EditorUI::RenderInputSettingsModal() {
    if (!m_showInputSettingsModal) return;

    ImGui::OpenPopup("Project Input & Key Bindings");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal("Project Input & Key Bindings", &m_showInputSettingsModal)) {
        auto& input = Core::InputManager::Get();

        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.45f, 1.0f), "Customize Controls (Keyboard Keys & Mouse Buttons)");
        ImGui::TextDisabled("Click any binding button to rebind. Press ESC to cancel, or Backspace/Delete to clear.");
        ImGui::Spacing();

        // Handle Active Rebinding Mode
        if (!m_rebindingActionId.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.28f, 0.20f, 0.08f, 0.95f));
            ImGui::BeginChild("RebindingBanner", ImVec2(0, 52), true);
            auto* act = input.FindAction(m_rebindingActionId);
            std::string actionName = act ? act->displayName : m_rebindingActionId;
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "LISTENING FOR INPUT: %s (%s Binding)",
                               actionName.c_str(), m_rebindingIsPrimary ? "Primary" : "Secondary");
            ImGui::Text("Press any keyboard key (e.g. W, R, Space, Up Arrow) or click any mouse button (Left/Right/Middle)...");
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Wait 1 frame after click before polling to avoid registering the button click itself
            if (ImGui::GetFrameCount() > m_rebindStartFrame + 1) {
                Core::InputButton captured;
                if (Core::InputManager::PollAnyInput(captured, true)) {
                    if (m_rebindingIsPrimary) {
                        input.SetPrimaryBinding(m_rebindingActionId, captured);
                    } else {
                        input.SetSecondaryBinding(m_rebindingActionId, captured);
                    }
                    m_rebindingActionId.clear();
                } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    m_rebindingActionId.clear();
                }
            }
        }

        // Action Bindings Table
        if (ImGui::BeginTable("KeyBindingsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 320))) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableSetupColumn("Primary Binding", ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("Secondary Binding", ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("Clear", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            auto& actions = input.GetActions();
            for (size_t i = 0; i < actions.size(); ++i) {
                auto& action = actions[i];
                ImGui::TableNextRow();

                // Col 0: Action Name
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", action.displayName.c_str());
                if (action.actionId == "Run") {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Sprint)");
                }

                // Col 1: Primary Binding
                ImGui::TableSetColumnIndex(1);
                bool isRebindingThisPrimary = (!m_rebindingActionId.empty() && m_rebindingActionId == action.actionId && m_rebindingIsPrimary);
                if (isRebindingThisPrimary) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.60f, 0.15f, 1.0f));
                    ImGui::Button(("< Press Input... >##Prim_" + action.actionId).c_str(), ImVec2(-1, 26));
                    ImGui::PopStyleColor();
                } else {
                    std::string label = action.primary.GetName() + "##Prim_" + action.actionId;
                    if (ImGui::Button(label.c_str(), ImVec2(-1, 26))) {
                        m_rebindingActionId = action.actionId;
                        m_rebindingIsPrimary = true;
                        m_rebindStartFrame = ImGui::GetFrameCount();
                    }
                }

                // Col 2: Secondary Binding
                ImGui::TableSetColumnIndex(2);
                bool isRebindingThisSec = (!m_rebindingActionId.empty() && m_rebindingActionId == action.actionId && !m_rebindingIsPrimary);
                if (isRebindingThisSec) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.60f, 0.15f, 1.0f));
                    ImGui::Button(("< Press Input... >##Sec_" + action.actionId).c_str(), ImVec2(-1, 26));
                    ImGui::PopStyleColor();
                } else {
                    std::string label = action.secondary.GetName() + "##Sec_" + action.actionId;
                    if (ImGui::Button(label.c_str(), ImVec2(-1, 26))) {
                        m_rebindingActionId = action.actionId;
                        m_rebindingIsPrimary = false;
                        m_rebindStartFrame = ImGui::GetFrameCount();
                    }
                }

                // Col 3: Clear
                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button(("Clear##" + action.actionId).c_str(), ImVec2(-1, 26))) {
                    action.primary.Clear();
                    action.secondary.Clear();
                }
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Add Custom Action section
        ImGui::Text("Add Custom Action:");
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##NewActionId", "Action ID (e.g. Crouch)", m_newCustomActionId, sizeof(m_newCustomActionId));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##NewActionName", "Display Name", m_newCustomActionName, sizeof(m_newCustomActionName));
        ImGui::SameLine();
        if (ImGui::Button("+ Add Action", ImVec2(110, 26))) {
            if (strlen(m_newCustomActionId) > 0) {
                std::string dName = strlen(m_newCustomActionName) > 0 ? m_newCustomActionName : m_newCustomActionId;
                input.RegisterAction(m_newCustomActionId, dName,
                                    Core::InputButton{Core::InputDevice::None, 0});
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Footer buttons
        if (ImGui::Button("Reset All to Defaults", ImVec2(170, 28))) {
            input.ResetToDefaults();
            m_rebindingActionId.clear();
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 180.0f);
        if (ImGui::Button("Save & Close", ImVec2(160, 28))) {
            m_rebindingActionId.clear();
            m_showInputSettingsModal = false;
            ImGui::CloseCurrentPopup();
            if (m_onSaveProject) m_onSaveProject();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderExportGameModal(Scene& scene) {
    if (!m_showExportGameModal) return;

    ImGui::OpenPopup("Export Standalone Game");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720, 580), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal("Export Standalone Game", &m_showExportGameModal)) {
        // Target Platform Selector
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Target Platform:");
        const char* platforms[] = { "Windows Standalone (.exe)", "Web / HTML5 (WebGL 2.0)", "Linux Standalone (x86_64 / SteamOS)" };
        ImGui::SetNextItemWidth(320.0f);
        ImGui::Combo("##ExportPlatformCombo", &m_exportPlatformIndex, platforms, IM_ARRAYSIZE(platforms));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (m_exportPlatformIndex == 0) {
            // ================== WINDOWS STANDALONE ==================
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.45f, 1.0f), "Package Standalone Windows Executable (.exe)");
            ImGui::TextDisabled("Export a self-contained game build ready to distribute. No Vulkan SDK or editor required.");
            ImGui::Spacing();

            // 1. Game Executable Name
            ImGui::Text("Game Executable Name:");
            ImGui::SetNextItemWidth(380.0f);
            ImGui::InputText("##ExportExeName", m_exportExeNameBuffer, sizeof(m_exportExeNameBuffer));
            ImGui::SameLine();
            ImGui::TextDisabled(".exe");

            ImGui::Spacing();

            // 2. Export Destination Folder
            ImGui::Text("%s", TR("Destination Directory:"));
            ImGui::SetNextItemWidth(520.0f);
            ImGui::InputText("##ExportDir", m_exportDirBuffer, sizeof(m_exportDirBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##ExportDirBrowse", ImVec2(100, 24))) {
#ifdef _WIN32
                char folder[MAX_PATH] = { 0 };
                BROWSEINFOA bi = { 0 };
                bi.lpszTitle = "Select Game Export Destination Folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
                if (pidl != 0) {
                    if (SHGetPathFromIDListA(pidl, folder)) {
                        strncpy(m_exportDirBuffer, folder, sizeof(m_exportDirBuffer) - 1);
                    }
                    CoTaskMemFree(pidl);
                }
#endif
            }

            ImGui::Spacing();

            // 3. Starting Scene selection
            ImGui::Text("Starting Scene:");
            std::vector<std::string> scenes;
            if (m_onGetScenes) scenes = m_onGetScenes();
            if (scenes.empty()) {
                scenes.push_back("main.scene");
            }
            if (ImGui::BeginCombo("##StartSceneCombo", m_exportSelectedScene.c_str())) {
                for (const auto& sc : scenes) {
                    bool isSelected = (m_exportSelectedScene == sc);
                    if (ImGui::Selectable(sc.c_str(), isSelected)) {
                        m_exportSelectedScene = sc;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();

            // 4. Window & Resolution Settings
            ImGui::Text("Display Mode:");
            ImGui::Checkbox("Fullscreen Mode", &m_exportFullscreen);
            if (!m_exportFullscreen) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(260.0f);
                const char* resOptions[] = { "1280 x 720 (720p)", "1600 x 900 (Default)", "1920 x 1080 (1080p Full HD)", "2560 x 1440 (2K QHD)" };
                ImGui::Combo("Window Resolution", &m_exportResolutionPreset, resOptions, IM_ARRAYSIZE(resOptions));
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 5. Standalone Packaging Options
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "Packaging Format:");
            ImGui::Checkbox("Embed all files into single standalone executable (.exe)", &m_exportEmbedInExe);
            if (m_exportEmbedInExe) {
                ImGui::TextDisabled("Single-file bundle: assets, shaders, scenes, configs and DLLs are packaged inside the .exe.\nZero loose files, zero installation, runs anywhere on Windows.");
            } else {
                ImGui::TextDisabled("Folder distribution: writes .exe with loose shaders/, scenes/, assets/ folders and DLLs.");
            }
        } else if (m_exportPlatformIndex == 1) {
            // ================== WEB / HTML5 (WebGL2) ==================
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.45f, 1.0f), "Package Web / HTML5 Application (WebGL 2.0)");
            ImGui::TextDisabled("Export a self-contained 3D WebGL2 web game playable in any modern browser (Chrome, Edge, Firefox, Safari).\nIncludes desktop keyboard/mouse navigation and mobile virtual touch controls.");
            ImGui::Spacing();

            // 1. Web Game Title
            ImGui::Text("Web Page & Game Title:");
            ImGui::SetNextItemWidth(380.0f);
            ImGui::InputText("##ExportWebTitle", m_exportWebTitle, sizeof(m_exportWebTitle));

            ImGui::Spacing();

            // 2. Destination Directory
            ImGui::Text("%s", TR("Destination Directory:"));
            ImGui::SetNextItemWidth(520.0f);
            ImGui::InputText("##ExportWebDir", m_exportWebDirBuffer, sizeof(m_exportWebDirBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##ExportWebDirBrowse", ImVec2(100, 24))) {
#ifdef _WIN32
                char folder[MAX_PATH] = { 0 };
                BROWSEINFOA bi = { 0 };
                bi.lpszTitle = "Select Web Export Destination Folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
                if (pidl != 0) {
                    if (SHGetPathFromIDListA(pidl, folder)) {
                        strncpy(m_exportWebDirBuffer, folder, sizeof(m_exportWebDirBuffer) - 1);
                    }
                    CoTaskMemFree(pidl);
                }
#endif
            }

            ImGui::Spacing();

            // 3. Starting Scene selection
            ImGui::Text("Starting Scene:");
            std::vector<std::string> scenes;
            if (m_onGetScenes) scenes = m_onGetScenes();
            if (scenes.empty()) {
                scenes.push_back("main.scene");
            }
            if (ImGui::BeginCombo("##StartSceneComboWeb", m_exportSelectedScene.c_str())) {
                for (const auto& sc : scenes) {
                    bool isSelected = (m_exportSelectedScene == sc);
                    if (ImGui::Selectable(sc.c_str(), isSelected)) {
                        m_exportSelectedScene = sc;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();

            // 4. Canvas Display Mode
            ImGui::Text("Canvas Display Mode:");
            const char* canvasModes[] = {
                "Responsive Aspect Ratio (Auto-scaling with letterbox)",
                "Fixed Resolution (1280 x 720 HD)",
                "Fill Entire Browser Window (100vw x 100vh)"
            };
            ImGui::SetNextItemWidth(420.0f);
            ImGui::Combo("##WebCanvasMode", &m_exportWebCanvasMode, canvasModes, IM_ARRAYSIZE(canvasModes));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 5. Web Distribution & Packaging Options
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "Web Distribution & Offline Options:");
            ImGui::Checkbox("Embed Starting Scene into HTML (Loads without CORS errors when opened directly via file://)", &m_exportWebEmbedScene);
            ImGui::Checkbox("Include 1-Click Local Web Server Helper (serve.py & start_server.bat on port 8080)", &m_exportWebIncludeServer);
            ImGui::Checkbox("Enable Mobile Virtual Touch Controls (On-screen touch joystick & jump button for mobile/tablets)", &m_exportWebTouchControls);
        } else {
            // ================== LINUX STANDALONE (x86_64 / SteamOS) ==================
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.45f, 1.0f), "Package Linux Standalone Executable (x86_64 / SteamOS)");
            ImGui::TextDisabled("Export a self-contained game build ready for Linux distributions and Valve Steam Deck.\nIncludes native ELF execution, Vulkan 1.2 runtime launcher, SteamOS / MangoHud flags, and Proton fallback.");
            ImGui::Spacing();

            // 1. Linux Executable / Game Title
            ImGui::Text("Game Title / Binary Name:");
            ImGui::SetNextItemWidth(380.0f);
            ImGui::InputText("##ExportLinuxName", m_exportLinuxNameBuffer, sizeof(m_exportLinuxNameBuffer));
            ImGui::SameLine();
            ImGui::TextDisabled(".x86_64 / launch.sh");

            ImGui::Spacing();

            // 2. Export Destination Folder
            ImGui::Text("%s", TR("Destination Directory:"));
            ImGui::SetNextItemWidth(520.0f);
            ImGui::InputText("##ExportLinuxDir", m_exportLinuxDirBuffer, sizeof(m_exportLinuxDirBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##ExportLinuxDirBrowse", ImVec2(100, 24))) {
#ifdef _WIN32
                char folder[MAX_PATH] = { 0 };
                BROWSEINFOA bi = { 0 };
                bi.lpszTitle = "Select Linux Export Destination Folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
                if (pidl != 0) {
                    if (SHGetPathFromIDListA(pidl, folder)) {
                        strncpy(m_exportLinuxDirBuffer, folder, sizeof(m_exportLinuxDirBuffer) - 1);
                    }
                    CoTaskMemFree(pidl);
                }
#endif
            }

            ImGui::Spacing();

            // 3. Starting Scene selection
            ImGui::Text("Starting Scene:");
            std::vector<std::string> scenes;
            if (m_onGetScenes) scenes = m_onGetScenes();
            if (scenes.empty()) {
                scenes.push_back("main.scene");
            }
            if (ImGui::BeginCombo("##StartSceneComboLinux", m_exportSelectedScene.c_str())) {
                for (const auto& sc : scenes) {
                    bool isSelected = (m_exportSelectedScene == sc);
                    if (ImGui::Selectable(sc.c_str(), isSelected)) {
                        m_exportSelectedScene = sc;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();

            // 4. Window & Resolution Settings
            ImGui::Text("Display Mode:");
            ImGui::Checkbox("Fullscreen Mode##Linux", &m_exportFullscreen);
            if (!m_exportFullscreen) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(260.0f);
                const char* resOptions[] = { "1280 x 720 (720p)", "1600 x 900 (Default)", "1920 x 1080 (1080p Full HD)", "2560 x 1440 (2K QHD)" };
                ImGui::Combo("Window Resolution##Linux", &m_exportResolutionPreset, resOptions, IM_ARRAYSIZE(resOptions));
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 5. Linux / Steam Deck Packaging Options
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "Linux & Steam Deck Compatibility:");
            ImGui::Checkbox("Generate Steam Deck & Desktop App Launcher (.desktop & launch.sh)", &m_exportLinuxSteamDeckSupport);
            ImGui::Checkbox("Include Dual Proton / Wine Compatibility Fallback Runner", &m_exportLinuxDualProton);
            ImGui::Checkbox("Generate tar.gz Distribution Archive", &m_exportLinuxTarArchive);
            if (m_exportLinuxSteamDeckSupport) {
                ImGui::TextDisabled("Includes turnkey launch.sh with LD_LIBRARY_PATH, gamescope, and MangoHud support.\nAllows 1-click 'Add a Non-Steam Game' on Valve Steam Deck.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Status or Error banner
        if (m_exportDone) {
            if (m_exportSuccess) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.35f, 0.18f, 0.95f));
                ImGui::BeginChild("ExportSuccessBanner", ImVec2(0, 76), true);
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "SUCCESS: %s", m_exportStatusMessage.c_str());
                ImGui::Spacing();

                const char* folderToOpen = (m_exportPlatformIndex == 1) ? m_exportWebDirBuffer : ((m_exportPlatformIndex == 2) ? m_exportLinuxDirBuffer : m_exportDirBuffer);
                if (ImGui::Button("Open Export Folder", ImVec2(160, 26))) {
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", folderToOpen, NULL, NULL, SW_SHOW);
#elif defined(__linux__)
                    std::string openCmd = std::string("xdg-open \"") + folderToOpen + "\" &";
                    (void)system(openCmd.c_str());
#endif
                }
                ImGui::SameLine();
                if (m_exportPlatformIndex == 1) {
                    if (ImGui::Button("Launch in Browser (Start Web Server)", ImVec2(280, 26))) {
                        Export::WebExporter::LaunchInBrowser(m_exportWebDirBuffer, 8080);
                    }
                } else if (m_exportPlatformIndex == 0) {
                    if (ImGui::Button("> Test Launch Exported Game", ImVec2(220, 26))) {
#ifdef _WIN32
                        std::string targetExe = (std::filesystem::path(m_exportDirBuffer) / (std::string(m_exportExeNameBuffer) + ".exe")).string();
                        ShellExecuteA(NULL, "open", targetExe.c_str(), NULL, m_exportDirBuffer, SW_SHOW);
#endif
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.15f, 0.15f, 0.95f));
                ImGui::BeginChild("ExportFailBanner", ImVec2(0, 52), true);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "ERROR: %s", m_exportStatusMessage.c_str());
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        }

        // Export Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.78f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.55f, 0.28f, 1.0f));

        if (m_exportPlatformIndex == 1) {
            if (ImGui::Button("Package & Export Web Game (HTML5)", ImVec2(280, 32))) {
                m_exportDone = true;
                m_exportSuccess = ExportWebGame(scene, m_exportWebDirBuffer, m_exportWebTitle, m_exportSelectedScene);
            }
        } else if (m_exportPlatformIndex == 2) {
            if (ImGui::Button("Package & Export Linux Game (SteamOS)", ImVec2(280, 32))) {
                uint32_t w = 1600, h = 900;
                if (m_exportResolutionPreset == 0) { w = 1280; h = 720; }
                else if (m_exportResolutionPreset == 1) { w = 1600; h = 900; }
                else if (m_exportResolutionPreset == 2) { w = 1920; h = 1080; }
                else if (m_exportResolutionPreset == 3) { w = 2560; h = 1440; }

                m_exportDone = true;
                m_exportSuccess = ExportLinuxGame(scene, m_exportLinuxDirBuffer, m_exportLinuxNameBuffer, m_exportSelectedScene, m_exportFullscreen, w, h, m_exportLinuxSteamDeckSupport);
            }
        } else {
            if (ImGui::Button("Package & Export Windows Game", ImVec2(250, 32))) {
                uint32_t w = 1600, h = 900;
                if (m_exportResolutionPreset == 0) { w = 1280; h = 720; }
                else if (m_exportResolutionPreset == 1) { w = 1600; h = 900; }
                else if (m_exportResolutionPreset == 2) { w = 1920; h = 1080; }
                else if (m_exportResolutionPreset == 3) { w = 2560; h = 1440; }

                m_exportDone = true;
                m_exportSuccess = ExportGame(scene, m_exportDirBuffer, m_exportExeNameBuffer, m_exportSelectedScene, m_exportFullscreen, w, h, m_exportEmbedInExe);
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
        if (ImGui::Button("Close", ImVec2(100, 32))) {
            m_showExportGameModal = false;
            m_exportDone = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool EditorUI::ExportWebGame(Scene& scene, const std::string& destinationDir, const std::string& gameTitle, const std::string& startScene) {
    Export::WebExportOptions opts;
    opts.destinationDir = destinationDir.empty() ? "exports/MyGame_Web" : destinationDir;
    opts.gameTitle = gameTitle.empty() ? "Tessera Web Game" : gameTitle;
    opts.startScene = startScene.empty() ? "main.scene" : startScene;
    opts.projectRoot = m_projectRoot;
    opts.canvasMode = static_cast<Export::WebCanvasMode>(m_exportWebCanvasMode);
    opts.includeLocalServerHelper = m_exportWebIncludeServer;
    opts.embedSceneInHTML = m_exportWebEmbedScene;
    opts.enableTouchControls = m_exportWebTouchControls;

    bool ok = Export::WebExporter::Export(scene, opts, m_exportStatusMessage);
    if (ok) {
        m_lastExportedPath = (std::filesystem::path(opts.destinationDir) / "index.html").string();
    }
    return ok;
}

bool EditorUI::ExportLinuxGame(Scene& scene, const std::string& destinationDir, const std::string& gameTitle, const std::string& startScene, bool fullscreen, int width, int height, bool steamDeckSupport) {
    Export::LinuxExportOptions opts;
    opts.destinationDir = destinationDir.empty() ? "exports/MyGame_Linux" : destinationDir;
    opts.gameTitle = gameTitle.empty() ? "MyGame" : gameTitle;
    opts.startScene = startScene.empty() ? "scenes/main.scene" : startScene;
    opts.projectRoot = m_projectRoot;
    opts.fullscreen = fullscreen;
    opts.windowWidth = width;
    opts.windowHeight = height;
    opts.includeSteamDeckLauncher = steamDeckSupport;
    opts.createTarGzArchive = m_exportLinuxTarArchive;
    opts.includeDualProtonRunner = m_exportLinuxDualProton;

    bool ok = Export::LinuxExporter::Export(scene, opts, m_exportStatusMessage);
    if (ok) {
        m_lastExportedPath = (std::filesystem::path(opts.destinationDir) / "launch.sh").string();
    }
    return ok;
}

bool EditorUI::ExportGame(Scene& scene, const std::string& destinationDir, const std::string& gameTitle, const std::string& startScene, bool fullscreen, int width, int height, bool embedInExe) {
    namespace fs = std::filesystem;
    try {
        fs::path dest(destinationDir);
        fs::create_directories(dest);

        // 1. Get current executable path and directory
#ifdef _WIN32
        char curExePath[MAX_PATH];
        GetModuleFileNameA(NULL, curExePath, MAX_PATH);
        fs::path exePath(curExePath);
#else
        char curExePath[1024] = {0};
        ssize_t len = readlink("/proc/self/exe", curExePath, sizeof(curExePath) - 1);
        fs::path exePath(len > 0 ? std::string(curExePath, len) : "TesseraGameEngine");
#endif
        fs::path exeDir = exePath.parent_path();

        // Resolve Windows GameRunner executable (from templates/windows, local build, or current exe)
        fs::path winRunner = "templates/windows/TesseraGameRunner.exe";
        if (!fs::exists(winRunner)) winRunner = "templates/windows/" + gameTitle + ".exe";
        if (!fs::exists(winRunner)) winRunner = "templates/windows/Tessera.exe";
        if (!fs::exists(winRunner) && exePath.extension() == ".exe") winRunner = exePath;
        if (!fs::exists(winRunner)) winRunner = exeDir / "Tessera.exe";
        if (!fs::exists(winRunner)) winRunner = "build/Release/Tessera.exe";
        if (!fs::exists(winRunner)) winRunner = "build/Debug/Tessera.exe";

        if (!fs::exists(winRunner)) {
            m_exportStatusMessage = "Export failed: Windows export template (templates/windows/TesseraGameRunner.exe) not found.";
            std::cerr << "[Exporter] " << m_exportStatusMessage << std::endl;
            return false;
        }

        if (embedInExe) {
            // Find StandaloneBootstrapper.exe
            fs::path bootstrapper = "templates/windows/StandaloneBootstrapper.exe";
            if (!fs::exists(bootstrapper)) bootstrapper = exeDir / "StandaloneBootstrapper.exe";
            if (!fs::exists(bootstrapper)) bootstrapper = "build/Release/StandaloneBootstrapper.exe";
            if (!fs::exists(bootstrapper)) bootstrapper = "build/Debug/StandaloneBootstrapper.exe";
            if (!fs::exists(bootstrapper)) {
                m_exportStatusMessage = "Export failed: StandaloneBootstrapper.exe not found (checked templates/windows and build).";
                std::cerr << "[Exporter] " << m_exportStatusMessage << std::endl;
                return false;
            }

            // Create temporary staging directory
            fs::path stagingDir = fs::temp_directory_path() / ("ag_stage_" + gameTitle);
            std::error_code ec;
            fs::remove_all(stagingDir, ec);
            fs::create_directories(stagingDir);

            // A. Copy engine executable as GameRunner.exe
            fs::copy_file(winRunner, stagingDir / "GameRunner.exe", fs::copy_options::overwrite_existing);

            // B. Copy all runtime DLLs (deflate.dll, glfw3.dll, vulkan-1.dll, etc.)
            auto copyDlls = [&](const fs::path& dir) {
                if (fs::exists(dir) && fs::is_directory(dir)) {
                    for (const auto& entry : fs::directory_iterator(dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                            fs::copy_file(entry.path(), stagingDir / entry.path().filename(), fs::copy_options::overwrite_existing);
                            std::cout << "[Exporter] Staged runtime DLL: " << entry.path().filename().string() << std::endl;
                        }
                    }
                }
            };
            copyDlls("templates/windows");
            copyDlls("thirdparty/bin");
            copyDlls(exeDir);
            if (fs::exists("build/Release")) copyDlls("build/Release");
            if (fs::exists("build/Debug")) copyDlls("build/Debug");

            // C. Copy Shaders
            fs::create_directories(stagingDir / "shaders");
            fs::path shadersSrc = "shaders";
            if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = exeDir / "shaders";
            if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = "build/shaders";
            if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = exeDir.parent_path() / "shaders";
            if (fs::exists(shadersSrc)) {
                fs::copy(shadersSrc, stagingDir / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }

            // D. Copy Scenes
            fs::create_directories(stagingDir / "scenes");
            fs::path scenesSrc = "scenes";
            if (!m_projectRoot.empty() && fs::exists(fs::path(m_projectRoot) / "scenes")) {
                scenesSrc = fs::path(m_projectRoot) / "scenes";
            }
            if (fs::exists(scenesSrc)) {
                fs::copy(scenesSrc, stagingDir / "scenes", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }
            std::string targetSceneName = startScene.empty() ? "main.scene" : startScene;
            if (targetSceneName.length() < 6 || targetSceneName.substr(targetSceneName.length() - 6) != ".scene") {
                targetSceneName += ".scene";
            }
            fs::path outSceneFile = stagingDir / "scenes" / targetSceneName;
            scene.SaveToFile(outSceneFile.string());
            if (fs::exists("shaders")) fs::copy("shaders", stagingDir / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            if (fs::exists("build/shaders")) fs::copy("build/shaders", stagingDir / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            if (!fs::exists(outSceneFile)) {
                scene.SaveToFile(outSceneFile.string());
            }

            // E. Copy Assets
            fs::path assetsSrc = "assets";
            if (!m_projectRoot.empty() && fs::exists(fs::path(m_projectRoot) / "assets")) {
                assetsSrc = fs::path(m_projectRoot) / "assets";
            }
            if (fs::exists(assetsSrc)) {
                fs::create_directories(stagingDir / "assets");
                fs::copy(assetsSrc, stagingDir / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }

            // E2. Copy Scripts (.lua)
            fs::path scriptsSrc = "scripts";
            if (!m_projectRoot.empty() && fs::exists(fs::path(m_projectRoot) / "scripts")) {
                scriptsSrc = fs::path(m_projectRoot) / "scripts";
            }
            if (fs::exists(scriptsSrc)) {
                fs::create_directories(stagingDir / "scripts");
                fs::copy(scriptsSrc, stagingDir / "scripts", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }

            // F. Export input_bindings.json
            Core::InputManager::Get().SaveToFile((stagingDir / "input_bindings.json").string());

            // G. Write game.agconfig manifest
            JsonValue config = JsonValue::Object();
            config["gameTitle"] = gameTitle;
            config["startingScene"] = startScene.empty() ? "scenes/main.scene" : ("scenes/" + startScene);
            config["fullscreen"] = fullscreen;
            config["windowWidth"] = width;
            config["windowHeight"] = height;

            std::ofstream cfgFile(stagingDir / "game.agconfig");
            if (cfgFile.is_open()) {
                cfgFile << config.Serialize(2);
                cfgFile.close();
            }

            // Collect all files in stagingDir
            std::vector<fs::path> stagedFiles;
            for (const auto& entry : fs::recursive_directory_iterator(stagingDir)) {
                if (entry.is_regular_file()) {
                    stagedFiles.push_back(entry.path());
                }
            }

            // Build single output .exe
            fs::path targetExe = dest / (gameTitle + ".exe");
            std::ofstream outExe(targetExe, std::ios::binary);
            if (!outExe.is_open()) {
                throw std::runtime_error("Cannot create output executable: " + targetExe.string());
            }

            // 1) Write bootstrapper binary
            std::ifstream bootIn(bootstrapper, std::ios::binary);
            if (!bootIn.is_open()) {
                throw std::runtime_error("Cannot read bootstrapper: " + bootstrapper.string());
            }
            outExe << bootIn.rdbuf();
            bootIn.close();

            // 2) Write payload header
            uint64_t payloadOffset = static_cast<uint64_t>(outExe.tellp());
            BundleHeader header;
            memcpy(header.magic, "AGYPACK1", 8);
            header.fileCount = static_cast<uint32_t>(stagedFiles.size());
            header.flags = 0;
            strncpy(header.mainExeName, "GameRunner.exe", sizeof(header.mainExeName) - 1);
            outExe.write(reinterpret_cast<const char*>(&header), sizeof(header));

            // 3) Write each file
            std::vector<char> chunkBuf(64 * 1024);
            for (const auto& fpath : stagedFiles) {
                std::string rel = fs::relative(fpath, stagingDir).generic_string();
                uint32_t pathLen = static_cast<uint32_t>(rel.size());
                outExe.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
                outExe.write(rel.data(), pathLen);

                uint64_t fsize = fs::file_size(fpath);
                outExe.write(reinterpret_cast<const char*>(&fsize), sizeof(fsize));

                std::ifstream fIn(fpath, std::ios::binary);
                uint64_t remaining = fsize;
                while (remaining > 0) {
                    size_t toRead = static_cast<size_t>((std::min)(remaining, static_cast<uint64_t>(chunkBuf.size())));
                    fIn.read(chunkBuf.data(), toRead);
                    outExe.write(chunkBuf.data(), toRead);
                    remaining -= toRead;
                }
            }

            // 4) Write trailer
            uint64_t payloadEnd = static_cast<uint64_t>(outExe.tellp());
            uint64_t payloadSize = payloadEnd - payloadOffset;
            BundleTrailer trailer;
            trailer.payloadOffset = payloadOffset;
            trailer.payloadSize = payloadSize;
            memcpy(trailer.magic, "AGYPACK1", 8);
            outExe.write(reinterpret_cast<const char*>(&trailer), sizeof(trailer));

            outExe.close();

            // Cleanup staging
            fs::remove_all(stagingDir, ec);

            m_exportStatusMessage = "Single-file game packaged: " + targetExe.string();
            m_lastExportedPath = targetExe.string();
            std::cout << "[Exporter] Successfully packaged single-file game executable (" << (payloadEnd / (1024 * 1024)) << " MB): " << targetExe.string() << std::endl;
            return true;
        } else {
            // Folder export mode (loose files)
            fs::path targetExe = dest / (gameTitle + ".exe");
            fs::copy_file(winRunner, targetExe, fs::copy_options::overwrite_existing);

            auto copyDllsFrom = [&](const fs::path& dir) {
                if (fs::exists(dir) && fs::is_directory(dir)) {
                    for (const auto& entry : fs::directory_iterator(dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                            fs::copy_file(entry.path(), dest / entry.path().filename(), fs::copy_options::overwrite_existing);
                            std::cout << "[Exporter] Bundled runtime DLL: " << entry.path().filename().string() << std::endl;
                        }
                    }
                }
            };
            copyDllsFrom("templates/windows");
            copyDllsFrom("thirdparty/bin");
            copyDllsFrom(exeDir);
            if (fs::exists("build/Release")) copyDllsFrom("build/Release");
            if (fs::exists("build/Debug")) copyDllsFrom("build/Debug");

            // Copy Shaders
            fs::create_directories(dest / "shaders");
            fs::path shadersSrc = "shaders";
            if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = exeDir / "shaders";
            if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = "build/shaders";
            if (!fs::exists(shadersSrc / "vert.spv")) shadersSrc = exeDir.parent_path() / "shaders";
            if (fs::exists(shadersSrc)) {
                fs::copy(shadersSrc, dest / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }

            // Copy Scenes
            fs::create_directories(dest / "scenes");
            fs::path scenesSrc = "scenes";
            if (!m_projectRoot.empty() && fs::exists(fs::path(m_projectRoot) / "scenes")) {
                scenesSrc = fs::path(m_projectRoot) / "scenes";
            }
            if (fs::exists(scenesSrc)) {
                fs::copy(scenesSrc, dest / "scenes", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }
            std::string targetSceneName = startScene.empty() ? "main.scene" : startScene;
            if (targetSceneName.length() < 6 || targetSceneName.substr(targetSceneName.length() - 6) != ".scene") {
                targetSceneName += ".scene";
            }
            fs::path outSceneFile = dest / "scenes" / targetSceneName;
            scene.SaveToFile(outSceneFile.string());
            if (fs::exists("shaders")) fs::copy("shaders", dest / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            if (fs::exists("build/shaders")) fs::copy("build/shaders", dest / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            if (!fs::exists(outSceneFile)) {
                scene.SaveToFile(outSceneFile.string());
            }

            // Copy Assets
            fs::path assetsSrc = "assets";
            if (!m_projectRoot.empty() && fs::exists(fs::path(m_projectRoot) / "assets")) {
                assetsSrc = fs::path(m_projectRoot) / "assets";
            }
            if (fs::exists(assetsSrc)) {
                fs::create_directories(dest / "assets");
                fs::copy(assetsSrc, dest / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }

            // Copy Scripts (.lua)
            fs::path scriptsSrc = "scripts";
            if (!m_projectRoot.empty() && fs::exists(fs::path(m_projectRoot) / "scripts")) {
                scriptsSrc = fs::path(m_projectRoot) / "scripts";
            }
            if (fs::exists(scriptsSrc)) {
                fs::create_directories(dest / "scripts");
                fs::copy(scriptsSrc, dest / "scripts", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }

            // Export input_bindings.json
            Core::InputManager::Get().SaveToFile((dest / "input_bindings.json").string());

            // Write game.agconfig manifest
            JsonValue config = JsonValue::Object();
            config["gameTitle"] = gameTitle;
            config["startingScene"] = startScene.empty() ? "scenes/main.scene" : ("scenes/" + startScene);
            config["fullscreen"] = fullscreen;
            config["windowWidth"] = width;
            config["windowHeight"] = height;

            std::ofstream cfgFile(dest / "game.agconfig");
            if (cfgFile.is_open()) {
                cfgFile << config.Serialize(2);
                cfgFile.close();
            }

            m_exportStatusMessage = "Game exported to " + dest.string();
            m_lastExportedPath = targetExe.string();
            std::cout << "[Exporter] Successfully packaged standalone game folder: " << targetExe.string() << std::endl;
            return true;
        }
    } catch (const std::exception& e) {
        m_exportStatusMessage = std::string("Export failed: ") + e.what();
        std::cerr << "[Exporter] " << m_exportStatusMessage << std::endl;
        return false;
    }
}


void EditorUI::RenderEnvironmentAndPostProcessWindow(Scene& scene) {
    if (!m_showEnvironmentWindow) return;
    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR("Rendering, GI & Post-Processing###EnvironmentWindow"), &m_showEnvironmentWindow)) {
        auto& env = scene.GetEnvironment();
        if (ImGui::CollapsingHeader("HDR & Tonemapping (ACES Filmic)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable ACES Filmic Tonemapping", &env.enableACES);
            ImGui::SliderFloat("HDR Exposure", &env.hdrExposure, 0.1f, 5.0f, "%.2f");
            const char* aaModes[] = { "Off", "FXAA", "TAA (Temporal)" };
            ImGui::Combo("Anti-Aliasing", &env.antiAliasing, aaModes, IM_ARRAYSIZE(aaModes));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("FXAA: fast edge smoothing on the final image.\nTAA: blends several jittered frames; smoother edges and less shimmer,\nbut very fast motion can soften slightly.");
        }
        if (ImGui::CollapsingHeader("Realtime Shadows & Cascaded Shadow Maps (CSM)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Realtime Shadows & CSM", &env.enableShadows);
            ImGui::SliderFloat("Shadow Distance (m)", &env.shadowDistance, 5.0f, 300.0f, "%.0f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far in front of the camera the sun's shadows reach.\nSmaller = sharper shadows, larger = shadows further away.");
            ImGui::TextDisabled("The first shadow-casting directional light gets 4 cascades; up to 4 more\nshadow-casting point / spot lights (nearest the camera) get shadow maps too.");
        }
        if (ImGui::CollapsingHeader("Hardware Ray Tracing (Vulkan / Metal)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& rt = Graphics::RayTracing::RayTracingManager::Get();
            rt.SyncScene(scene);
            auto& rtSettings = rt.GetSettings();
            auto rtStats = rt.GetStats();

            if (ImGui::Checkbox("Enable Hardware Ray Tracing", &rtSettings.enableHardwareRT)) {
                if (rtSettings.enableHardwareRT) {
                    rtSettings.enableRTGI = true;
                    rtSettings.enableRTAO = true;
                    rtSettings.enableRTR = true;
                    auto& env = scene.GetEnvironment();
                    env.vxgiEnabled = true;
                }
            }
            ImGui::SameLine();
            if (rtStats.supported) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: %s]", rt.GetBackend()->GetName());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[%s]", rtStats.deviceName.c_str());
            }

            if (rtSettings.enableHardwareRT) {
                ImGui::Indent(10.0f);
                ImGui::Checkbox("Ray-Traced Ambient Occlusion (RTAO)", &rtSettings.enableRTAO);
                if (rtSettings.enableRTAO) {
                    ImGui::SliderFloat("RTAO Radius", &rtSettings.rtaoRadius, 0.5f, 10.0f, "%.1f m");
                    ImGui::SliderInt("RTAO Samples", &rtSettings.rtaoSamples, 1, 16);
                }

                ImGui::Checkbox("Ray-Traced Reflections (RTR)", &rtSettings.enableRTR);
                if (rtSettings.enableRTR) {
                    ImGui::SliderInt("RTR Max Bounces", &rtSettings.rtrMaxBounces, 1, 4);
                    ImGui::SliderFloat("Roughness Cutoff", &rtSettings.rtrRoughnessThreshold, 0.05f, 1.0f, "%.2f");
                }

                ImGui::Checkbox("Ray-Traced Shadows (RTS)", &rtSettings.enableRTS);
                if (rtSettings.enableRTS) {
                    ImGui::SliderInt("RTS Light Samples", &rtSettings.rtsLightSamples, 1, 8);
                    ImGui::SliderFloat("Penumbra Softness", &rtSettings.rtsPenumbraScale, 0.1f, 5.0f, "%.1f");
                }

                if (ImGui::Checkbox("Ray-Traced Global Illumination (RTGI)", &rtSettings.enableRTGI)) {
                    if (rtSettings.enableRTGI) {
                        scene.GetEnvironment().vxgiEnabled = true;
                    }
                }
                if (rtSettings.enableRTGI) {
                    ImGui::SliderInt("RTGI Bounces", &rtSettings.rtgiBounces, 1, 4);
                    ImGui::SliderFloat("RTGI Radiance Scale", &rtSettings.rtgiIntensity, 0.1f, 10.0f, "%.1f");
                }

                ImGui::Separator();
                if (rtStats.bvhMemoryBytes < 1024ULL * 1024ULL) {
                    float kb = static_cast<float>(rtStats.bvhMemoryBytes) / 1024.0f;
                    ImGui::TextDisabled("BVH Acceleration Structures: %u BLAS, %u TLAS Instances (%.1f KB VRAM)",
                        rtStats.totalBLASCount, rtStats.activeTLASInstances, kb);
                } else {
                    float mb = static_cast<float>(rtStats.bvhMemoryBytes) / (1024.0f * 1024.0f);
                    ImGui::TextDisabled("BVH Acceleration Structures: %u BLAS, %u TLAS Instances (%.2f MB VRAM)",
                        rtStats.totalBLASCount, rtStats.activeTLASInstances, mb);
                }
                ImGui::Unindent(10.0f);
            }
        }

        if (ImGui::CollapsingHeader("Meshlet GPU Virtualization (Nanite-Class Geometry)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& meshletMgr = Graphics::Meshlet::MeshletManager::Get();
            meshletMgr.SyncScene(scene);
            auto& meshletSettings = meshletMgr.GetSettings();
            const auto& meshletStats = meshletMgr.GetStats();

            ImGui::Checkbox("Enable Meshlet GPU Pipeline", &meshletSettings.enableMeshletPipeline);
            ImGui::SameLine();
            if (meshletStats.extensionSupported) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: VK_EXT_mesh_shader]");
            } else {
                ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f), "[%s]", meshletStats.deviceName.c_str());
            }

            ImGui::Indent(10.0f);
            ImGui::Checkbox("Cluster Frustum Culling", &meshletSettings.enableFrustumCulling);
            ImGui::SameLine();
            ImGui::Checkbox("Cluster Backface Cone Culling", &meshletSettings.enableConeCulling);

            float kb = static_cast<float>(meshletStats.gpuMemoryBytes) / 1024.0f;
            ImGui::TextDisabled("Meshlet Clusters: %u Active, %u Visible, %u Culled (%.1f%% Culling Efficiency)",
                meshletStats.totalMeshletsActive,
                meshletStats.visibleMeshletsLastFrame,
                meshletStats.culledMeshletsLastFrame,
                meshletStats.cullingEfficiencyPercent);
            ImGui::TextDisabled("Cluster Memory: %.1f KB VRAM | Limits: <= %u Vertices, <= %u Triangles / Meshlet",
                kb, meshletStats.maxPreferredMeshletVertices, meshletStats.maxPreferredMeshletTriangles);
            ImGui::Unindent(10.0f);
        }

        if (ImGui::CollapsingHeader("Temporal Upscaling (DLSS 3.5 / FSR 3 / XeSS)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& upscaler = Graphics::Upscaling::TemporalUpscaler::Get();
            auto& upSettings = upscaler.GetSettings();
            const auto& upStats = upscaler.GetStats();

            ImGui::Checkbox("Enable Temporal Upscaling", &upSettings.enableUpscaling);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[%s]", upscaler.GetModeName(upSettings.mode));

            if (upSettings.enableUpscaling) {
                ImGui::Indent(10.0f);
                const char* modes[] = { "Native TAA", "AMD FSR 3.1", "NVIDIA DLSS 3.5", "Intel XeSS" };
                int currentMode = static_cast<int>(upSettings.mode);
                if (ImGui::Combo("Upscaler Engine", &currentMode, modes, IM_ARRAYSIZE(modes))) {
                    upSettings.mode = static_cast<Graphics::Upscaling::UpscalerMode>(currentMode);
                }

                const char* presets[] = { "Native (1.0x)", "Quality (1.5x)", "Balanced (1.7x)", "Performance (2.0x)", "Ultra Performance (3.0x)" };
                int currentPreset = static_cast<int>(upSettings.preset);
                if (ImGui::Combo("Quality Preset", &currentPreset, presets, IM_ARRAYSIZE(presets))) {
                    upSettings.preset = static_cast<Graphics::Upscaling::UpscalerPreset>(currentPreset);
                }

                ImGui::SliderFloat("RCAS Sharpness", &upSettings.sharpness, 0.0f, 1.0f, "%.2f");
                ImGui::Checkbox("Reactive Mask Generator", &upSettings.enableReactiveMask);
                ImGui::SameLine();
                ImGui::Checkbox("Jitter Motion Cancellation", &upSettings.enableMotionVectorJitterCancellation);

                ImGui::TextDisabled("Render Resolution: %ux%u -> Target: %ux%u (%.1f%% Scale)",
                    upStats.renderResolution.x, upStats.renderResolution.y,
                    upStats.displayResolution.x, upStats.displayResolution.y,
                    upStats.renderScaleFactor * 100.0f);
                if (upStats.estimatedGpuSpeedupPercent > 0.0f) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "Estimated GPU Fillrate Boost: +%.1f%%", upStats.estimatedGpuSpeedupPercent);
                }
                ImGui::Unindent(10.0f);
            }
        }

        if (ImGui::CollapsingHeader("Pipeline Cache & Zero Shader Stutter (PSO)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& psoCache = Graphics::Pipeline::VulkanPipelineCache::Get();
            const auto& psoStats = psoCache.GetStats();

            if (psoStats.cacheLoaded) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: Pre-Baked Disk Cache Loaded]");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[On-Demand Compilation]");
            }

            ImGui::Indent(10.0f);
            float kb = static_cast<float>(psoStats.cacheSizeBytes) / 1024.0f;
            ImGui::TextDisabled("Disk Cache: %.1f KB (%s)", kb, psoStats.cacheFilePath.c_str());
            ImGui::TextDisabled("Pipelines: %u Compiled | Cache Hits: %u | Misses: %u",
                psoStats.totalPipelinesCompiled, psoStats.cacheHits, psoStats.cacheMisses);

            if (ImGui::Button("Save Pipeline Cache to Disk")) {
                psoCache.SaveToDisk();
            }
            ImGui::Unindent(10.0f);
        }

        if (ImGui::CollapsingHeader("Large World Coordinates & Camera-Relative Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& lwc = Core::LWC::CameraRelativeCoordinator::Get();
            bool lwcEnabled = lwc.IsEnabled();
            if (ImGui::Checkbox("Enable 64-Bit Camera-Relative Coordinates", &lwcEnabled)) {
                lwc.SetEnabled(lwcEnabled);
            }
            ImGui::Indent(10.0f);
            const auto& cpos = lwc.GetCameraWorldPosition();
            ImGui::TextDisabled("Camera 64-bit Position: (%.2f, %.2f, %.2f) m", cpos.x, cpos.y, cpos.z);
            double precStep = Core::LWC::CameraRelativeCoordinator::EstimateFloatingPointStepAtDistance(glm::length(cpos));
            ImGui::TextDisabled("32-bit Float Jitter Delta: %.6f mm (Eliminated by LWC)", precStep * 1000.0);
            ImGui::Unindent(10.0f);
        }

        if (ImGui::CollapsingHeader("Asynchronous World Partition & DirectStorage Streaming", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& wpMgr = Streaming::WorldPartitionManager::GetInstance();
            auto& wpSettings = wpMgr.GetSettingsMutable();
            const auto wpStats = wpMgr.GetStats();

            ImGui::Checkbox("Enable World Partitioning", &wpSettings.enableStreaming);
            ImGui::SameLine();
            if (wpSettings.enableDirectStorageFastPath) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[DirectStorage FastPath: ACTIVE]");
            }

            ImGui::Indent(10.0f);
            ImGui::SliderFloat("Cell Sector Size (m)", &wpSettings.cellSize, 16.0f, 256.0f, "%.0f m");
            ImGui::SliderFloat("Active Radius (m)", &wpSettings.activeRadius, 64.0f, 512.0f, "%.0f m");
            ImGui::SliderFloat("Loading Radius (m)", &wpSettings.loadingRadius, 128.0f, 1024.0f, "%.0f m");
            ImGui::SliderFloat("Unload Radius (m)", &wpSettings.unloadRadius, 256.0f, 2048.0f, "%.0f m");
            ImGui::Checkbox("Camera Frustum Priority Boost", &wpSettings.enableFrustumPriority);

            float memMB = static_cast<float>(wpStats.currentMemoryUsageBytes) / (1024.0f * 1024.0f);
            ImGui::TextDisabled("World Cells: %u Registered | %u Active | %u Loaded in RAM",
                wpStats.totalRegisteredCells, wpStats.activeCellsCount, wpStats.loadedCellsCount);
            ImGui::TextDisabled("I/O Throughput: %.2f MB/s | Queued DirectStorage Requests: %u",
                wpStats.readThroughputMBs, wpStats.queuedIORequests);
            ImGui::TextDisabled("Streamed Memory: %.2f MB / %zu MB Budget | Active Nodes: %u",
                memMB, wpSettings.maxMemoryBudgetMB, wpStats.activeStreamedNodes);

            if (ImGui::Button("Partition Current Scene")) {
                wpMgr.PartitionScene(scene);
            }
            ImGui::SameLine();
            if (ImGui::Button("Force Load All")) {
                wpMgr.ForceLoadAll(scene);
            }
            ImGui::Unindent(10.0f);
        }

        if (ImGui::CollapsingHeader("Motion Matching & Procedural Full-Body IK", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& mm = Animation::MotionMatcher::Get();
            auto db = mm.GetDatabase();
            size_t frameCount = db ? db->GetFrameCount() : 0;
            const auto& inertializer = mm.GetInertializer();

            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: Continuous Motion Trajectory Search]");
            ImGui::Indent(10.0f);
            ImGui::TextDisabled("Motion Database: %zu Poses Indexed (Idle, Walk, Run, 90deg Plant)", frameCount);
            ImGui::TextDisabled("Inertialization Filter: %s (Duration: %.2f s)",
                inertializer.active ? "Decaying Blended Offset" : "Steady-State Trajectory",
                inertializer.transitionDuration);
            ImGui::TextDisabled("Full-Body IK: Pelvis Adaptation + 2-Leg Ground Alignment + Look-At");
            ImGui::Unindent(10.0f);
        }

        if (ImGui::CollapsingHeader("Character Physics: GPU Cloth (XPBD) & Dynamic Hair", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& clothSim = Physics::XPBDClothSimulator::Get();
            auto& hairSim = Physics::DynamicHairSimulator::Get();

            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: Extended Position-Based Dynamics]");
            ImGui::Indent(10.0f);
            ImGui::TextDisabled("XPBD Cloth Particles: %zu | Constraints: %zu (Compliance alpha=1e-5)",
                clothSim.GetParticleCount(), clothSim.GetConstraintCount());
            ImGui::TextDisabled("Dynamic Hair Strands: %zu | Particles: %zu (Bending Rod Constraints)",
                hairSim.GetStrandCount(), hairSim.GetTotalParticles());
            if (ImGui::Button("Reset Cloth Simulation")) {
                clothSim.InitializeGridCloth(1.0f, 1.0f, 10, 10);
            }
            ImGui::Unindent(10.0f);
        }

        if (ImGui::CollapsingHeader("Acoustic Ray Tracing & Door Portaling", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& audioRT = Audio::AcousticRayTracer::Get();
            const auto& obstacles = audioRT.GetObstacles();
            const auto& portals = audioRT.GetPortals();

            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: Ray-Traced Diffraction & Portaling]");
            ImGui::Indent(10.0f);
            ImGui::TextDisabled("Acoustic Geometry: %zu Obstacles (Concrete/Wood/Glass) | %zu Portals",
                obstacles.size(), portals.size());
            for (size_t i = 0; i < portals.size(); ++i) {
                float aperture = portals[i].openFraction;
                std::string label = "Portal [" + portals[i].name + "] Aperture";
                if (ImGui::SliderFloat(label.c_str(), &aperture, 0.0f, 1.0f, "%.2f")) {
                    audioRT.SetPortalOpenFraction(portals[i].portalId, aperture);
                }
            }
            ImGui::Unindent(10.0f);
        }

                if (ImGui::CollapsingHeader("Baked & Dynamic Lighting (GI)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Voxel Global Illumination (VXGI)##Top", &env.vxgiEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Real-time 64^3 3D voxel cone tracing (VXGI): dynamically voxelizes scene meshes\nand terrain for real-time diffuse bounce light, voxel AO and glossy reflections.");
            if (env.vxgiEnabled) {
                ImGui::SliderFloat("VXGI Intensity##Top", &env.vxgiIntensity, 0.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("VXGI Range (m)##Top", &env.vxgiRange, 8.0f, 80.0f, "%.1f");
                ImGui::SliderFloat("VXGI Multi-Bounce##Top", &env.vxgiBounce, 0.0f, 1.5f, "%.2f");
            }
            ImGui::Separator();
            ImGui::TextWrapped("Bakes bounce light into a grid of light probes: light reflected off walls and floors, "
                               "coloured by them, and darker ambient indoors. Works on every object, moving ones too. "
                               "Covers the GI Volume node's box if there is one, otherwise the whole scene.");
            ImGui::SliderFloat("Probe Spacing (m)", &env.bakedGISpacing, 0.25f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Bounces", &env.bakedGIBounces, 1, 4);
            ImGui::Checkbox("Bake Lightmaps", &env.bakeLightmaps);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also bakes lightmaps for meshes marked Lightmap Static: per-texel bounce\n"
                                                          "light and sky shadowing, much more detailed than the probe grid.\n"
                                                          "Direct light and shadows stay real time.");
            if (env.bakeLightmaps) {
                ImGui::SliderFloat("Lightmap Density (texels/m)", &env.lightmapDensity, 1.0f, 32.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
                const int sampleOptions[] = { 32, 64, 128, 256, 512 };
                const char* sampleLabels[] = { "Draft (32 rays)", "Low (64 rays)", "Medium (128 rays)", "High (256 rays)", "Very High (512 rays)" };
                int sampleIndex = 2;
                for (int i = 0; i < 5; ++i) if (env.lightmapSamples == sampleOptions[i]) sampleIndex = i;
                if (ImGui::Combo("Lightmap Quality", &sampleIndex, sampleLabels, 5)) env.lightmapSamples = sampleOptions[sampleIndex];
            }
            ImGui::BeginDisabled(m_giBaking);
            if (ImGui::Button(m_giBaking ? "Baking..." : "Bake Lighting", ImVec2(160, 0))) m_giBakeRequested = true;
            ImGui::EndDisabled();
            if (!env.bakedGIPath.empty() || !env.lightmapPath.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear Bake")) {
                    env.bakedGIPath.clear();
                    env.lightmapPath.clear();
                }
                ImGui::Checkbox("Use Baked Lighting", &env.bakedGIEnabled);
                ImGui::SliderFloat("Baked Intensity", &env.bakedGIIntensity, 0.0f, 4.0f, "%.2f");
                if (!env.bakedGIPath.empty()) ImGui::TextDisabled("Probes: %s", env.bakedGIPath.c_str());
                if (!env.lightmapPath.empty()) ImGui::TextDisabled("Lightmaps: %s", env.lightmapPath.c_str());
            }
            if (!m_giBakeStatus.empty()) ImGui::TextWrapped("%s", m_giBakeStatus.c_str());
            ImGui::TextDisabled("Re-bake after moving walls, lights or large objects. Save the scene to keep the bake.");
        }
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Occlusion Culling", &env.occlusionCulling);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skips objects completely hidden behind others (walls, buildings).\nHelps scenes with many objects out of sight. Results are two frames old,\nso an object can appear a frame or two late when uncovered by a slow\ncamera move; culling pauses during fast moves and turns.");
            ImGui::TextDisabled("Repeated objects are drawn instanced and textures are block-compressed automatically.");
        }
        if (ImGui::CollapsingHeader("Procedural Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Procedural Sky", &env.enableSky);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws a sky with a sun, sunsets and stars, and lights the scene from it\n(ambient light and reflections). The sun follows the first directional light.");
            if (env.enableSky) {
                ImGui::SliderFloat("Sky Intensity", &env.skyIntensity, 0.05f, 4.0f, "%.2f");
                ImGui::Checkbox("Physical Atmosphere", &env.physicalAtmosphere);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sunlight scattered through a real atmosphere (air, haze and ozone) instead of\nthe stylised colours: deep blue overhead, pale at the horizon, true sunset colours,\nand the sun's light reddening through the air. Changes with the camera's height.");
                if (!env.timeOfDayEnabled) ImGui::TextDisabled("Rotate the directional light to move the sun, or turn on the clock below.");
                ImGui::TextDisabled("While the sky is on, 'Sky Hemisphere Color' is not used.");
            }

            ImGui::SeparatorText("Time of Day");
            ImGui::Checkbox("Use Clock", &env.timeOfDayEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The clock places the sun and the moon, and points the first directional light\nat the sun by day and the moon by night (its Intensity is the noon sun).\nThe stars turn with the time.");
            if (env.timeOfDayEnabled) {
                const int minutes = static_cast<int>(env.timeOfDay * 60.0f + 0.5f) % (24 * 60);
                char label[32];
                snprintf(label, sizeof(label), "%02d:%02d", minutes / 60, minutes % 60);
                ImGui::SliderFloat("Time", &env.timeOfDay, 0.0f, 24.0f, label);
                ImGui::SliderFloat("Day Length (min)", &env.dayLengthMinutes, 0.0f, 120.0f, env.dayLengthMinutes <= 0.0f ? "stopped" : "%.1f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Real minutes for a whole day while the clock runs (0 = the time stays put).\nThe clock runs in Play; scripts can also set it (Sky.SetTime).");
                ImGui::Checkbox("Run While Editing", &env.clockRunsInEditor);
                ImGui::SliderFloat("Latitude", &env.latitude, -80.0f, 80.0f, "%.0f deg");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("How high the sun climbs: 0 = straight overhead at noon, higher = lower sun.");
                ImGui::SliderFloat("North", &env.northAngle, -180.0f, 180.0f, "%.0f deg");
                {
                    static const char* kMonths[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
                    static const int kMonthStart[13] = { 1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 };
                    const int day = std::clamp(static_cast<int>(env.dayOfYear), 1, 365);
                    int month = 0;
                    while (month < 11 && day >= kMonthStart[month + 1]) ++month;
                    char dateLabel[48];
                    snprintf(dateLabel, sizeof(dateLabel), "%d %s (%.1f h of daylight)", day - kMonthStart[month] + 1, kMonths[month],
                             SkyClock::DayLengthHours(env.latitude, env.dayOfYear));
                    ImGui::SliderFloat("Date", &env.dayOfYear, 1.0f, 365.0f, dateLabel);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The season: how high the sun climbs and how long the days are\n(longest around 21 Jun in the north, shortest around 21 Dec).");
                    ImGui::Checkbox("Advance Calendar", &env.advanceCalendar);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Past midnight the date moves on a day and the moon a day through its phases.");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns the sun's path around the vertical axis (the sun rises in the east, +X, at 0).");
                ImGui::Checkbox("Moon", &env.moonEnabled);
                if (env.moonEnabled) {
                    ImGui::SliderFloat("Moon Phase", &env.moonPhase, 0.0f, 1.0f, env.moonPhase < 0.06f || env.moonPhase > 0.94f ? "new" :
                                       std::abs(env.moonPhase - 0.5f) < 0.06f ? "full" : env.moonPhase < 0.5f ? "%.2f waxing" : "%.2f waning");
                    ImGui::SliderFloat("Moonlight", &env.moonBrightness, 0.0f, 4.0f, "%.2f");
                }
            }

            ImGui::SeparatorText("Clouds");
            ImGui::Checkbox("Clouds", &env.cloudsEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clouds over the procedural sky or an HDRI, moved by the wind, with shadows on the ground.");
            if (env.cloudsEnabled) {
                ImGui::Checkbox("Volumetric Clouds", &env.volumetricClouds);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ray-marched 3D clouds with real depth: lit through their thickness, in front\nof distant mountains, and the camera can fly into, through and above them.\nReflections and probes still see the flat layer of the same shapes.");
                if (env.volumetricClouds) {
                    ImGui::SliderFloat("Cloud Thickness (m)", &env.cloudThickness, 100.0f, 5000.0f, "%.0f");
                    ImGui::SliderFloat("Cloud Detail", &env.cloudDetail, 0.0f, 1.5f, "%.2f");
                }
                ImGui::SliderFloat("Coverage", &env.cloudCoverage, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Opacity", &env.cloudOpacity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Cloud Size", &env.cloudScale, 0.2f, 4.0f, "%.2f");
                ImGui::SliderFloat("Cloud Height (m)", &env.cloudHeight, 200.0f, 5000.0f, "%.0f");
                ImGui::SliderFloat("Wind Speed (m/s)", &env.windSpeed, 0.0f, 60.0f, "%.1f");
                ImGui::SliderFloat("Wind Direction", &env.windDirection, 0.0f, 360.0f, "%.0f deg");
                ImGui::SliderFloat("Cloud Shadows", &env.cloudShadows, 0.0f, 1.0f, "%.2f");
            }
        }
        if (ImGui::CollapsingHeader("Weather", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Use Weather Presets", &env.weatherEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A preset sets the clouds, wind, fog, rain, snow and lightning, blending\nover the transition time. Rain wets the ground and fills puddles over time;\nsnow settles on surfaces facing up. Scripts use the Weather table.");
            if (env.weatherEnabled) {
                const char* names[Weather::PresetCount];
                for (int p = 0; p < Weather::PresetCount; ++p) names[p] = Weather::PresetName(p);
                ImGui::Combo("Weather", &env.weatherPreset, names, Weather::PresetCount);
                ImGui::SliderFloat("Transition (s)", &env.weatherTransition, 0.0f, 120.0f, "%.0f");
                ImGui::TextDisabled("The preset drives the clouds, wind and fog settings.");
            }
            // With presets on these show the current blend; with them off they are set directly
            ImGui::BeginDisabled(env.weatherEnabled);
            ImGui::SliderFloat("Rain", &env.rainAmount, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Snow", &env.snowAmount, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Wetness", &env.wetness, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Snow Cover", &env.snowCover, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Lightning (per min)", &env.lightningRate, 0.0f, 30.0f, "%.1f");
            ImGui::EndDisabled();
            if (ImGui::Button("Lightning Strike")) Weather::Strike(env);
            ImGui::SliderFloat("Snow Depth (m)", &env.snowDepth, 0.0f, 1.5f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How deep the snow lies at full cover: it banks up against walls and\nobjects and lies as a layer over their top edges.");
            ImGui::SliderFloat("Thunder Volume", &env.thunderVolume, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Thunder follows each strike after the time its sound takes to arrive\n(near strikes crack, far ones rumble). Plays in Play mode.");
            ImGui::TextDisabled("Rain, snow and wetness stay out from under roofs.");
        }
        if (ImGui::CollapsingHeader("HDRI Environment & Reflection Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Use HDRI Environment", &env.useHDRI);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Uses a 360-degree .hdr photo as the background and lights the scene\nfrom it (ambient light and reflections). Replaces the procedural sky.");
            if (env.useHDRI) {
                char buf[512];
                strncpy(buf, env.hdriPath.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                ImGui::SetNextItemWidth(-80.0f);
                if (ImGui::InputText("##HDRIPath", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    env.hdriPath = buf;
                }
                ImGui::SameLine();
                if (ImGui::Button("Browse##HDRI", ImVec2(-1, 0))) {
                    std::string picked;
                    if (OpenHDRFileDialog(picked, "Select HDRI (.hdr)")) {
                        env.hdriPath = m_onImportTexture ? m_onImportTexture(picked) : picked;
                    }
                }
                if (env.hdriPath.empty()) ImGui::TextDisabled("Pick an equirectangular (2:1) .hdr image.");
                ImGui::SliderFloat("HDRI Intensity", &env.hdriIntensity, 0.05f, 8.0f, "%.2f");
                ImGui::SliderFloat("HDRI Rotation", &env.hdriRotation, -180.0f, 180.0f, "%.0f deg");
            }
            if (ImGui::Button("Recapture Probes")) {
                m_probeRecaptureRequests++;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-renders every reflection probe (after moving lights or objects).\nProbes recapture by themselves when a probe is added, moved or resized.");
        }
        if (ImGui::CollapsingHeader("Camera: Exposure, Grading & Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto Exposure (Eye Adaptation)", &env.autoExposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Brightens dark scenes and darkens bright ones over time, like an eye.\n'HDR Exposure' above then acts as compensation.");
            if (env.autoExposure) {
                ImGui::DragFloatRange2("Exposure Range", &env.autoExposureMin, &env.autoExposureMax, 0.01f, 0.01f, 32.0f, "Min %.2f", "Max %.2f");
                ImGui::SliderFloat("Adaptation Speed", &env.autoExposureSpeed, 0.1f, 10.0f, "%.1f");
            }
            ImGui::SliderFloat("Contrast", &env.contrast, 0.5f, 2.0f, "%.2f");
            ImGui::SliderFloat("Saturation", &env.saturation, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Temperature", &env.temperature, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Negative = cooler (blue), positive = warmer (orange).");
            ImGui::SliderFloat("Tint", &env.tint, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Negative = green, positive = magenta.");
            ImGui::SliderFloat("Vignette", &env.vignette, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Film Grain", &env.filmGrain, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Sharpen (TAA)", &env.sharpen, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Lens Flare", &env.lensFlare, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ghosts and a halo from very bright spots (the sun, lamps).");
            ImGui::SliderFloat("Chromatic Aberration", &env.chromaticAberration, 0.0f, 1.0f, env.chromaticAberration > 0.0f ? "%.2f" : "Off");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Red and blue fringes toward the screen edges, like a real lens.");
            {
                char lutBuf[512];
                strncpy(lutBuf, env.colorLutPath.c_str(), sizeof(lutBuf) - 1);
                lutBuf[sizeof(lutBuf) - 1] = '\0';
                ImGui::TextUnformatted("Colour LUT");
                ImGui::SetNextItemWidth(-150.0f);
                if (ImGui::InputText("##LUTPath", lutBuf, sizeof(lutBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    env.colorLutPath = lutBuf;
                }
                ImGui::SameLine();
                if (ImGui::Button("Browse##LUT")) {
                    std::string picked;
                    if (OpenLUTFileDialog(picked, "Select Colour LUT (.cube or strip image)")) {
                        env.colorLutPath = m_onImportTexture ? m_onImportTexture(picked) : picked;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear##LUT")) env.colorLutPath.clear();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A colour look from Photoshop, DaVinci Resolve, etc.:\na .cube file, or an image strip N*N wide and N high (e.g. 256x16).");
                if (!env.colorLutPath.empty()) {
                    ImGui::SliderFloat("LUT Strength", &env.lutStrength, 0.0f, 1.0f, "%.2f");
                }
            }
            ImGui::Separator();
            ImGui::Checkbox("Depth of Field", &env.depthOfField);
            if (env.depthOfField) {
                ImGui::SliderFloat("Focus Distance (m)", &env.focusDistance, 0.1f, 200.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Aperture (Blur Strength)", &env.aperture, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Max Blur (px)", &env.dofMaxBlur, 2.0f, 32.0f, "%.0f");
            }
            ImGui::SliderFloat("Motion Blur", &env.motionBlur, 0.0f, 1.0f, env.motionBlur > 0.0f ? "%.2f" : "Off");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blurs along camera and object motion. 0.5 is a typical film shutter.");
        }
        if (ImGui::CollapsingHeader("Global Illumination (SH Light Probes & Sky GI)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Spherical Harmonics GI", &env.enableGI);
            ImGui::ColorEdit3("Sky Hemisphere Color", glm::value_ptr(env.giSkyColor));
            ImGui::SliderFloat("GI Bounce Intensity", &env.giIntensity, 0.0f, 5.0f, "%.2f");
            ImGui::Separator();
            ImGui::Checkbox("Enable Voxel Global Illumination (VXGI)", &env.vxgiEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Real-time 3D voxel cone tracing (VXGI): dynamically voxelizes scene meshes and\nterrain into a mipmapped 3D radiance + opacity grid for dynamic diffuse colour\nbleeding, voxel ambient occlusion and glossy indirect reflections.");
            if (env.vxgiEnabled) {
                ImGui::SliderFloat("VXGI Bounce Intensity", &env.vxgiIntensity, 0.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("VXGI Volume Range (m)", &env.vxgiRange, 8.0f, 80.0f, "%.1f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("World-space extent (metres) of the 32^3 dynamic voxel radiance volume.");
                ImGui::SliderFloat("VXGI Multi-Bounce Feedback", &env.vxgiBounce, 0.0f, 1.5f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Feeds previous voxel irradiance back into surface voxels for multi-bounce colour bleeding.");
            }
        }
        if (ImGui::CollapsingHeader("Post-Processing Stack (Bloom, SSAO, SSR)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("HDR Bloom Intensity", &env.bloomIntensity, 0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Screen-Space Ambient Occlusion (SSAO)", &env.ssaoIntensity, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Darkens ambient light in corners, creases and where objects meet. 0 = off.");
            ImGui::SliderFloat("SSAO Radius (m)", &env.ssaoRadius, 0.05f, 3.0f, "%.2f");
            ImGui::SliderFloat("Screen-Space Reflections (SSR)", &env.ssrIntensity, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smooth surfaces reflect what's visible on screen (floors, water, metal).\nAnything off-screen falls back to the sky, HDRI or reflection probes.");
            if (env.ssrIntensity > 0.0f) {
                ImGui::SliderFloat("SSR Max Distance (m)", &env.ssrMaxDistance, 2.0f, 100.0f, "%.1f");
                ImGui::SliderFloat("SSR Thickness (m)", &env.ssrThickness, 0.05f, 2.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("How thick objects are assumed to be. Too small: gaps in reflections.\nToo large: reflections smear behind thin objects.");
            }
        }
        if (ImGui::CollapsingHeader("Exponential Height Fog & Volumetric Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Atmosphere Fog Color", glm::value_ptr(env.fogColor));
            ImGui::SliderFloat("Exponential Fog Density", &env.fogDensity, 0.0f, 0.15f, "%.4f");
            ImGui::SliderFloat("Volumetric Light Scattering (God Rays)", &env.volumetricScattering, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ray-marched volumetric fog: light shafts from the sun and from\npoint/spot lights, blocked by shadow-casting objects. 0 = off.");
            if (env.volumetricScattering > 0.0f) {
                ImGui::SliderFloat("Volumetric Density", &env.volumetricFogDensity, 0.001f, 0.3f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Height Falloff", &env.volumetricHeightFalloff, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Fog Base Height (m)", &env.volumetricBaseHeight, -50.0f, 200.0f, "%.1f");
                ImGui::SliderFloat("Anisotropy", &env.volumetricAnisotropy, -0.9f, 0.95f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher = brighter glow when looking toward a light (strong shafts).\n0 = light scatters evenly in all directions.");
                ImGui::TextDisabled("The fog colour sets the ambient tint inside the fog.");
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset Environment Defaults", ImVec2(-1, 28))) {
            env = SceneEnvironment{};
        }
    }
    if (false) {
        if (ImGui::CollapsingHeader("HDR & Tonemapping (ACES Filmic)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable ACES Filmic Tonemapping", &m_enableACES);
            ImGui::SliderFloat("HDR Exposure", &m_hdrExposure, 0.1f, 5.0f, "%.2f");
        }

        if (ImGui::CollapsingHeader("Realtime Shadows & Cascaded Shadow Maps (CSM)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Realtime Shadows & CSM", &m_enableShadows);
            ImGui::TextDisabled("Active scene lights with 'Cast Realtime Shadows' contribute to directional & point/spot attenuation.");
        }

        if (ImGui::CollapsingHeader("Global Illumination (SH Light Probes & Sky GI)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Spherical Harmonics GI", &m_enableGI);
            ImGui::ColorEdit3("Sky Hemisphere Color", glm::value_ptr(m_giSkyColor));
            ImGui::SliderFloat("GI Bounce Intensity", &m_giIntensity, 0.0f, 5.0f, "%.2f");
        }

        if (ImGui::CollapsingHeader("Post-Processing Stack (Bloom, SSAO, SSR)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("HDR Bloom Intensity", &m_bloomIntensity, 0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Screen-Space Ambient Occlusion (SSAO)", &m_ssaoIntensity, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Screen-Space Reflections (SSR)", &m_ssrIntensity, 0.0f, 2.0f, "%.2f");
        }

        if (ImGui::CollapsingHeader("Exponential Height Fog & Volumetric Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Atmosphere Fog Color", glm::value_ptr(m_fogColor));
            ImGui::SliderFloat("Exponential Fog Density", &m_fogDensity, 0.0f, 0.15f, "%.4f");
            ImGui::SliderFloat("Volumetric Light Scattering (God Rays)", &m_volumetricScattering, 0.0f, 2.0f, "%.2f");
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset Environment Defaults", ImVec2(-1, 28))) {
            m_hdrExposure = 1.0f;
            m_enableACES = true;
            m_enableShadows = true;
            m_enableGI = true;
            m_giSkyColor = glm::vec3(0.35f, 0.48f, 0.68f);
            m_giIntensity = 1.0f;
            m_fogColor = glm::vec3(0.55f, 0.68f, 0.85f);
            m_fogDensity = 0.0f;
            m_bloomIntensity = 0.35f;
            m_ssaoIntensity = 1.0f;
            m_ssrIntensity = 0.35f;
            m_volumetricScattering = 0.0f;
        }
    }
    ImGui::End();
}

void EditorUI::RenderShaderGraphWindow(Scene& scene) {
    if (!m_showShaderGraphWindow) return;
    ImGui::SetNextWindowSize(ImVec2(680, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR("Visual Shader Graph Editor###ShaderGraphWindow"), &m_showShaderGraphWindow)) {
        if (!m_shaderGraphInitialized) {
            m_shaderGraphInitialized = true;
            uint32_t outId = m_shaderGraph.GetMasterNodeId();
            uint32_t colId = m_shaderGraph.AddNode(ShaderNodeType::ConstantColor, "Base Color", glm::vec2(60, 60));
            uint32_t fresnelId = m_shaderGraph.AddNode(ShaderNodeType::Fresnel, "Rim Fresnel", glm::vec2(60, 200));
            if (auto* cn = m_shaderGraph.GetNode(colId)) cn->colorVal = glm::vec4(0.25f, 0.65f, 0.95f, 1.0f);
            if (auto* fn = m_shaderGraph.GetNode(fresnelId)) fn->floatVal = 3.0f;
            m_shaderGraph.AddLink(colId, "Out", outId, "Albedo");
            m_shaderGraph.AddLink(fresnelId, "Out", outId, "Emissive");
            m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
        }

        // Toolbar to add shader graph nodes
        if (ImGui::Button("+ Color Node")) {
            m_shaderGraph.AddNode(ShaderNodeType::ConstantColor, "Color Constant", glm::vec2(80, 80));
            m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Float Node")) {
            m_shaderGraph.AddNode(ShaderNodeType::ConstantFloat, "Scalar Float", glm::vec2(80, 140));
            m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Texture2D")) {
            m_shaderGraph.AddNode(ShaderNodeType::Texture2D, "Texture Sample", glm::vec2(80, 200));
            m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Fresnel")) {
            m_shaderGraph.AddNode(ShaderNodeType::Fresnel, "Fresnel Effect", glm::vec2(80, 260));
            m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Multiply")) {
            m_shaderGraph.AddNode(ShaderNodeType::Multiply, "Multiply", glm::vec2(240, 140));
            m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
        }
        ImGui::SameLine();
        if (ImGui::Button("Compile to GLSL")) {
            m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
        }
        ImGui::SameLine();
        // Graphs are files: save one, then apply it to objects (Inspector > Shader Graph Material)
        auto saveGraph = [&](bool askForPath) {
            std::string path = m_shaderGraphPath;
            if (askForPath || path.empty()) {
                if (path.empty()) {
                    const std::string folder = m_projectRoot.empty() ? std::string("shadergraphs") : m_projectRoot + "/assets/materials";
                    std::error_code ec;
                    std::filesystem::create_directories(folder, ec);
                    path = folder + "/" + (m_shaderGraph.GetName().empty() ? std::string("Material") : m_shaderGraph.GetName()) + ".shadergraph";
                }
                if (!ShaderGraphFileDialog(path, "Save Shader Graph", true)) return false;
            }
            if (!m_shaderGraph.SaveToFile(path)) return false;
            m_shaderGraphPath = path;
            return true;
        };
        if (ImGui::Button("Save")) saveGraph(false);
        ImGui::SameLine();
        if (ImGui::Button("Save As...")) saveGraph(true);
        ImGui::SameLine();
        if (ImGui::Button("Load...")) {
            std::string picked;
            if (ShaderGraphFileDialog(picked, "Open Shader Graph", false) && m_shaderGraph.LoadFromFile(picked)) {
                m_shaderGraphPath = picked;
                m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
            }
        }
        ImGui::SameLine();
        auto selectedForGraph = scene.GetSelectedNode();
        ImGui::BeginDisabled(!selectedForGraph);
        if (ImGui::Button("Apply to Selected Object")) {
            if (saveGraph(false) && selectedForGraph) selectedForGraph->shaderGraphPath = m_shaderGraphPath;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Saves the graph and makes it the selected object's material.\nIt is compiled the first time it's drawn (and again whenever it's saved).");
        if (!m_shaderGraphPath.empty()) ImGui::TextDisabled("File: %s", m_shaderGraphPath.c_str());

        ImGui::Separator();
        ImGui::Columns(2, "ShaderGraphCols", true);
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Shader Graph Nodes (%zu)", m_shaderGraph.GetNodes().size());
        ImGui::BeginChild("SGNodeList", ImVec2(0, 320), true);
        for (auto& sn : m_shaderGraph.GetNodesMutable()) {
            ImGui::PushID((int)sn.id);
            if (ImGui::TreeNodeEx(sn.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "[#%u] %s", sn.id, sn.name.c_str())) {
                glm::vec4 p = sn.colorVal;
                if (sn.type == ShaderNodeType::ConstantColor) {
                    if (ImGui::ColorEdit4("RGBA", glm::value_ptr(p))) {
                        sn.colorVal = p; sn.floatVal = p.x;
                        m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
                    }
                } else if (sn.type != ShaderNodeType::PBRMaster) {
                    if (ImGui::DragFloat4("Params", glm::value_ptr(p), 0.05f)) {
                        sn.colorVal = p; sn.floatVal = p.x;
                        m_compiledGLSLPreview = m_shaderGraph.CompileToGLSL();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (auto sel = scene.GetSelectedNode()) {
            if (ImGui::Button(("Apply Shader Graph Material to '" + sel->name + "'").c_str(), ImVec2(-1, 28))) {
                for (auto& sn : m_shaderGraph.GetNodesMutable()) {
                    if (sn.type == ShaderNodeType::ConstantColor) {
                        sel->color = sn.colorVal;
                    } else if (sn.type == ShaderNodeType::Fresnel) {
                        sel->emissiveColor = glm::vec3(0.25f, 0.65f, 1.0f);
                        sel->emissiveIntensity = std::max(0.5f, sn.floatVal * 0.35f);
                    }
                }
            }
        }

        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Generated GLSL Fragment Shader Code:");
        ImGui::BeginChild("GLSLCodePreview", ImVec2(0, 360), true);
        ImGui::TextUnformatted(m_compiledGLSLPreview.c_str());
        ImGui::EndChild();
        ImGui::Columns(1);
    }
    ImGui::End();
}

void EditorUI::RenderAudioMixerWindow(Scene& scene) {
    if (!m_showAudioMixerWindow) return;
    ImGui::SetNextWindowSize(ImVec2(480, 460), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR("Audio Mixer & Reverb Zones###AudioMixerWindow"), &m_showAudioMixerWindow)) {
        auto& audio = Audio::AudioEngine::Get();
        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.75f, 1.0f), "Master & Sub-Bus Volume Mixer:");
        const char* buses[] = { "Master", "Music", "SFX", "Voice", "Ambient" };
        for (const char* b : buses) {
            float vol = audio.GetBusVolume(b);
            if (ImGui::SliderFloat((std::string(b) + " Bus").c_str(), &vol, 0.0f, 1.5f, "%.2f")) {
                audio.SetBusVolume(b, vol);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Hardware Speaker Test (Windows Waveform Audio):");
        if (ImGui::Button("> Play 440Hz A4 Synth", ImVec2(145, 28))) {
            audio.TriggerHardwarePlayback("Sine_440Hz", "SFX", 0.8f, 0.8f, 1.0f, false, 0.1f);
        }
        ImGui::SameLine();
        if (ImGui::Button("> Play 880Hz Chime", ImVec2(145, 28))) {
            audio.TriggerHardwarePlayback("Chime_880Hz", "SFX", 0.8f, 0.8f, 1.0f, false, 0.25f);
        }
        ImGui::SameLine();
        if (ImGui::Button("⏹ Stop", ImVec2(80, 28))) {
            audio.StopHardwarePlayback();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "3D Acoustic Reverb Zone Nodes:");
        static char s_zoneName[64] = "Cathedral Hall";
        static glm::vec3 s_zoneCenter(0.0f, 2.0f, 0.0f);
        static float s_zoneRadius = 15.0f;
        static float s_zoneWet = 0.65f;
        ImGui::InputText("Zone Name", s_zoneName, sizeof(s_zoneName));
        ImGui::DragFloat3("Zone Center", glm::value_ptr(s_zoneCenter), 0.25f);
        ImGui::SliderFloat("Zone Radius", &s_zoneRadius, 1.0f, 100.0f);
        ImGui::SliderFloat("Wet Reverb Mix", &s_zoneWet, 0.0f, 1.0f);
        if (ImGui::Button("+ Add Reverb Zone", ImVec2(210, 26))) {
            auto rNode = scene.AddAudioNode(s_zoneName);
            if (auto arNode = std::dynamic_pointer_cast<AudioNode>(rNode)) { arNode->isReverbZone = true; arNode->position = s_zoneCenter; arNode->reverbOuterRadius = s_zoneRadius; arNode->reverbWetMix = s_zoneWet; scene.SelectNode(arNode->id); }
            (void)0;
            (void)0;
            (void)0;
            (void)0;
            (void)0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Zones", ImVec2(120, 26))) {
            audio.StopAll();
        }
    }
    ImGui::End();
}

// Measured GPU time per pass, nested as the passes are (VulkanGpuProfiler)
void EditorUI::DrawGpuTimings() {
    VulkanGpuProfiler* profiler = VulkanGpuProfiler::Active();
    if (!profiler || !profiler->Supported()) {
        ImGui::TextDisabled("This GPU doesn't report timestamps: no GPU timings.");
        return;
    }
    bool enabled = profiler->Enabled();
    if (ImGui::Checkbox("Measure", &enabled)) profiler->SetEnabled(enabled);
    ImGui::SameLine();
    const float frameMs = profiler->AverageFrameMs();
    ImGui::Text("GPU frame: %.2f ms (%.0f fps if GPU-bound)", frameMs, frameMs > 0.0f ? 1000.0f / frameMs : 0.0f);
    const auto& history = profiler->FrameHistory();
    if (!history.empty()) {
        float peak = 0.0f;
        for (float v : history) peak = std::max(peak, v);
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "last %zu frames, peak %.2f ms", history.size(), peak);
        ImGui::PlotLines("##GpuFrameHistory", history.data(), static_cast<int>(history.size()), 0, overlay, 0.0f,
                         std::max(peak * 1.15f, 1.0f), ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));
    }
    const auto& timings = profiler->Timings();
    if (ImGui::BeginTable("GpuTimings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 230.0f);
        ImGui::TableSetupColumn("ms (smoothed)", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Share of the GPU frame", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (const auto& t : timings) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Indent(t.depth * 14.0f);
            if (t.depth == 0) ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "%s", t.name.c_str());
            else ImGui::TextUnformatted(t.name.c_str());
            ImGui::Unindent(t.depth * 14.0f);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%6.3f", t.averageMs);
            ImGui::TableSetColumnIndex(2);
            const float share = frameMs > 0.0f ? std::clamp(t.averageMs / frameMs, 0.0f, 1.0f) : 0.0f;
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const ImU32 color = t.depth == 0 ? IM_COL32(70, 150, 230, 255) : (t.depth == 1 ? IM_COL32(90, 200, 120, 255) : IM_COL32(220, 170, 70, 255));
            drawList->AddRectFilled(p0, ImVec2(p0.x + width * share, p0.y + ImGui::GetTextLineHeight()), color, 2.0f);
            ImGui::Text("  %4.1f %%", share * 100.0f);
        }
        ImGui::EndTable();
    }
    if (timings.empty()) ImGui::TextDisabled("Waiting for the first timings...");
}

void EditorUI::DrawMaterialPreview(const std::string& key, const Node& material, float size, int shape) {
    void* texture = m_onMaterialPreview ? m_onMaterialPreview(key + "#" + std::to_string(shape), material, shape) : nullptr;
    if (texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(size, size));
    } else {
        // Being rendered (next frame)
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(40, 44, 52, 255), 4.0f);
        ImGui::Dummy(ImVec2(size, size));
    }
}

void EditorUI::RenderGpuProfilerWindow() {
    if (!m_showGpuProfilerWindow) return;
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR("GPU Profiler###GpuProfilerWindow"), &m_showGpuProfilerWindow)) DrawGpuTimings();
    ImGui::End();
}

void EditorUI::RenderNavMeshWindow(Scene& scene) {
    if (!m_showNavMeshWindow) return;
    ImGui::SetNextWindowSize(ImVec2(460, 440), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR("Navigation & NavMesh Baker###NavMeshWindow"), &m_showNavMeshWindow)) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Walkable NavMesh Surface Generator:");
        ImGui::DragFloat3("World Bounds Min", glm::value_ptr(m_navBoundsMin), 0.5f);
        ImGui::DragFloat3("World Bounds Max", glm::value_ptr(m_navBoundsMax), 0.5f);
        ImGui::SliderFloat("Grid Cell Size (m)", &m_navCellSize, 0.5f, 5.0f, "%.2f m");
        ImGui::SliderFloat("Max Walkable Slope", &m_navMaxSlope, 5.0f, 80.0f, "%.1f deg");

        if (ImGui::Button("Bake Walkable NavMesh (Show in Viewport)", ImVec2(-1, 30))) {
            m_navMesh.BuildFromScene(scene, m_navBoundsMin, m_navBoundsMax, m_navCellSize, m_navMaxSlope);
            m_lastNavPath = m_navMesh.FindPath(m_navStartPos, m_navGoalPos);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Baked Walkable Triangles: %zu", m_navMesh.GetWalkablePolyCount());

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "A* Pathfinding Query & Crowd Steering:");
        ImGui::DragFloat3("Start Position", glm::value_ptr(m_navStartPos), 0.25f);
        ImGui::DragFloat3("Goal Position", glm::value_ptr(m_navGoalPos), 0.25f);
        if (ImGui::Button("Compute A* Path (Draws Green Path in 3D Viewport)", ImVec2(-1, 28))) {
            if (m_navMesh.GetPolys().empty()) {
                m_navMesh.BuildFromScene(scene, m_navBoundsMin, m_navBoundsMax, m_navCellSize, m_navMaxSlope);
            }
            m_lastNavPath = m_navMesh.FindPath(m_navStartPos, m_navGoalPos);
        }
        if (!m_lastNavPath.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Path Solved: %zu waypoints", m_lastNavPath.size());
        }
    }
    ImGui::End();
}

void EditorUI::RenderNetworkingWindow(Scene& scene, Gameplay::GameLoop* gameLoop) {
    if (!m_showNetworkingWindow) return;
    Scene& activeScene = (gameLoop && (gameLoop->IsPlaying() || gameLoop->IsPaused())) ? gameLoop->GetActivePlayScene(scene) : scene;
    if (!m_showNetworkingWindow) return;
    ImGui::SetNextWindowSize(ImVec2(480, 440), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR("Multiplayer & Networking###NetworkingWindow"), &m_showNetworkingWindow)) {
        bool isGameRunning = (gameLoop && (gameLoop->IsPlaying() || gameLoop->IsPaused()));
        if (!isGameRunning) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.7f, 0.1f, 1.0f));
            ImGui::BeginChild("##NetEditorNotice", ImVec2(-1, 46), true);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Editor Mode (Replication Dormant)");
            ImGui::TextDisabled("Scene replication is restricted to Play Mode. Click 'Play Game [Play]' to start.");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.8f, 0.3f, 1.0f));
            ImGui::BeginChild("##NetPlayNotice", ImVec2(-1, 46), true);
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "Standalone Game Active (Replicating)");
            ImGui::TextDisabled("Live state synchronizing strictly with standalone game scene.");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Session Role & UDP Transport:");
        ImGui::InputText("Server Address", m_netAddress, sizeof(m_netAddress));
        ImGui::InputInt("UDP Port", &m_netPort);

        if (!m_networkManager.IsConnected()) {
            if (ImGui::Button("Start Host / Dedicated Server", ImVec2(220, 28))) {
                m_networkManager.StartServer(static_cast<uint16_t>(m_netPort));
            }
            ImGui::SameLine();
            if (ImGui::Button("Connect as Client", ImVec2(180, 28))) {
                m_networkManager.ConnectClient(m_netAddress, static_cast<uint16_t>(m_netPort));
            }
        } else {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "Status: %s Active on UDP Port %d | Local Peer ID: %u",
                m_networkManager.IsServer() ? "Server / Host" : "Client",
                m_netPort,
                m_networkManager.GetLocalPeerId());
            if (ImGui::Button("Disconnect Session", ImVec2(-1, 26))) {
                m_networkManager.Disconnect();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Live Replication & Multiplayer Synchronization:");

        bool cont = m_networkManager.IsContinuousReplicationEnabled();
        if (ImGui::Checkbox("Live Continuous Replication (30 Hz)", &cont)) {
            m_networkManager.SetContinuousReplication(cont);
        }
        ImGui::SameLine();
        bool repAll = m_networkManager.GetReplicateAllNodes();
        if (ImGui::Checkbox("Auto-Replicate All Dynamic & Scene Entities", &repAll)) {
            m_networkManager.SetReplicateAllNodes(repAll);
        }

        if (ImGui::Button("Mark All Scene Nodes for Network Replication", ImVec2(-1, 24))) {
            for (const auto& n : scene.GetNodes()) {
                if (n) n->replicateTransform = true;
            }
            for (const auto& n : activeScene.GetNodes()) {
                if (n) n->replicateTransform = true;
            }
            m_networkManager.AddLog("All scene nodes marked with replicateTransform = true.");
        }

        if (ImGui::Button("Force Replicate Now (Immediate Tick)", ImVec2(-1, 28))) {
            if (!isGameRunning) {
                m_networkManager.AddLog("Replication skipped: Game is in Editor Mode. Click 'Play Game [Play]' to start.");
            } else {
                if (!m_networkManager.IsConnected() && m_networkManager.IsServer()) {
                    m_networkManager.StartServer(static_cast<uint16_t>(m_netPort));
                }
                m_networkManager.ReplicateSceneState(activeScene, true);
            }
        }

        ImGui::Text("Socket Status: %s", m_networkManager.IsConnecting() ? "Connecting..." : (m_networkManager.IsConnected() ? "Connected" : "Disconnected / Idle"));

        if (m_networkManager.IsConnected()) {
            if (m_networkManager.IsServer()) {
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.65f, 1.0f),
                    "Replication Status (Host): Tick #%u | Sent: %u packets (30 Hz live)",
                    m_networkManager.GetLastReplicatedTick(),
                    m_networkManager.GetReplicatedPacketsSentCount());
            } else {
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.65f, 1.0f),
                    "Replication Status (Client): Tick #%u | Received: %u packets (live)",
                    m_networkManager.GetLastReplicatedTick(),
                    m_networkManager.GetReplicatedPacketsRecvCount());
            }
        }
        if (m_networkManager.IsServer() && m_networkManager.IsConnected()) {
            const auto& clients = m_networkManager.GetConnectedClients();
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.65f, 1.0f), "Connected Clients (%zu):", clients.size());
            for (const auto& c : clients) {
                ImGui::BulletText("%s:%u (last seen %.1fs ago)", c.address.c_str(), c.port, 0.0f);
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Network Activity & Connection Log:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Log")) {
            m_networkManager.ClearNetworkLog();
        }
        ImGui::BeginChild("NetEventLogChild", ImVec2(0, 160), true);
        for (const auto& logLine : m_networkManager.GetEventLog()) {
            ImVec4 col(0.85f, 0.88f, 0.92f, 1.0f);
            if (logLine.find("Connected") != std::string::npos || logLine.find("Client connected") != std::string::npos) {
                col = ImVec4(0.35f, 0.95f, 0.55f, 1.0f);
            } else if (logLine.find("Disconnected") != std::string::npos || logLine.find("timed out") != std::string::npos || logLine.find("ERROR") != std::string::npos || logLine.find("Stopped") != std::string::npos) {
                col = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
            }
            ImGui::TextColored(col, "%s", logLine.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorUI::RenderEngineEmblem(ImDrawList* drawList, ImVec2 center, float radius, float alpha) {
    if (!drawList || radius <= 1.0f) return;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    // 1. Dark Slate Shield Background
    drawList->AddCircleFilled(center, radius, IM_COL32(14, 20, 32, (int)(250 * alpha)));

    // 2. Outer Electric Neon Rim
    float rimThickness = std::max(1.0f, radius * 0.08f);
    drawList->AddCircle(center, radius, IM_COL32(35, 140, 245, (int)(220 * alpha)), 0, rimThickness);

    // 3. Inner Glowing Orbital Ring
    drawList->AddCircle(center, radius * 0.85f, IM_COL32(0, 220, 255, (int)(120 * alpha)), 0, 1.0f);

    // 4. Isometric 3D Tessera Faceted Mosaic Cube
    float r = radius * 0.65f;
    ImVec2 vTop(center.x, center.y - r * 0.95f);
    ImVec2 vTR(center.x + r * 0.85f, center.y - r * 0.45f);
    ImVec2 vBR(center.x + r * 0.85f, center.y + r * 0.50f);
    ImVec2 vBot(center.x, center.y + r * 0.98f);
    ImVec2 vBL(center.x - r * 0.85f, center.y + r * 0.50f);
    ImVec2 vTL(center.x - r * 0.85f, center.y - r * 0.45f);
    ImVec2 vCenter(center.x, center.y + r * 0.04f);

    // 3 Facets (Quads)
    // Top Face (Cyan Diamond)
    drawList->AddQuadFilled(vCenter, vTL, vTop, vTR, IM_COL32(75, 215, 255, (int)(240 * alpha)));
    // Left Face (Electric Royal Cobalt)
    drawList->AddQuadFilled(vCenter, vTL, vBL, vBot, IM_COL32(28, 120, 235, (int)(230 * alpha)));
    // Right Face (Deep Sapphire Teal)
    drawList->AddQuadFilled(vCenter, vTR, vBR, vBot, IM_COL32(18, 80, 180, (int)(240 * alpha)));

    // Facet Borders & Internal Edges
    float wireThick = std::max(1.0f, radius * 0.06f);
    ImU32 wireCol = IM_COL32(200, 245, 255, (int)(255 * alpha));
    drawList->AddLine(vCenter, vTop, wireCol, wireThick);
    drawList->AddLine(vCenter, vBL, wireCol, wireThick);
    drawList->AddLine(vCenter, vBR, wireCol, wireThick);
    drawList->AddLine(vTop, vTR, wireCol, wireThick);
    drawList->AddLine(vTR, vBR, wireCol, wireThick);
    drawList->AddLine(vBR, vBot, wireCol, wireThick);
    drawList->AddLine(vBot, vBL, wireCol, wireThick);
    drawList->AddLine(vBL, vTL, wireCol, wireThick);
    drawList->AddLine(vTL, vTop, wireCol, wireThick);

    // Central Radiant Nucleus Spark
    float coreRadius = std::max(1.5f, radius * 0.14f);
    drawList->AddCircleFilled(vCenter, coreRadius, IM_COL32(255, 210, 60, (int)(250 * alpha)));
    drawList->AddCircleFilled(vCenter, std::max(1.0f, coreRadius * 0.45f), IM_COL32(255, 255, 255, (int)(255 * alpha)));
}

void EditorUI::RenderAboutModal() {
    if (!m_showAboutModal) return;

    ImGui::OpenPopup(TR("About Tessera Game Engine"));
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 440), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TR("About Tessera Game Engine"), &m_showAboutModal, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        float winW = ImGui::GetWindowWidth();

        // Engine Emblem
        ImVec2 emblemCenter(winPos.x + winW * 0.5f, winPos.y + 60.0f);
        RenderEngineEmblem(drawList, emblemCenter, 34.0f, 1.0f);

        ImGui::Dummy(ImVec2(0, 85.0f));

        // Title & Version
        ImVec2 tSize = ImGui::CalcTextSize("TESSERA GAME ENGINE");
        ImGui::SetCursorPosX((winW - tSize.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "TESSERA GAME ENGINE");

        const char* verStr = "Version 1.0.0 (Vulkan 1.3 / C++20 / Sol2 Lua 5.4)";
        ImVec2 vSize = ImGui::CalcTextSize(verStr);
        ImGui::SetCursorPosX((winW - vSize.x) * 0.5f);
        ImGui::TextDisabled("%s", verStr);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("Tessera is a high-performance 3D game engine featuring modern Vulkan rendering, Jolt physics, Sol2 Lua scripting, procedural landscapes, and dockable Dear ImGui editor panels.");

        ImGui::Spacing();
        ImGui::BulletText("Vulkan 1.3 Renderer: ACES Tonemapping, Realtime Shadows, GI");
        ImGui::BulletText("Jolt Physics: Rigid bodies, character ground solid-contact floor");
        ImGui::BulletText("Scripting: Lua 5.4 with Sol2 C++ bindings & live hot-reload");
        ImGui::BulletText("3D Asset Formats: FBX, OBJ, glTF 2.0 / GLB, TTF Fonts");
        ImGui::BulletText("AI & Navigation: NavMesh generation & real-time A* pathfinding");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetCursorPosX(winW - 110.0f);
        if (ImGui::Button(TR("Close"), ImVec2(95, 26))) {
            m_showAboutModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderProjectFilesWindow(Scene& scene) {
    if (!m_showProjectFilesWindow) return;

    ImGui::SetNextWindowSize(ImVec2(750, 340), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(TR("Project Files###ProjectFilesWindow"), &m_showProjectFilesWindow)) {
        ImGui::End();
        return;
    }

    // Determine current root directory if not initialized or invalid (ALWAYS normalize to absolute path)
    if (m_fileViewerCurrentDir.empty() || !std::filesystem::exists(m_fileViewerCurrentDir)) {
        if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot)) {
            m_fileViewerCurrentDir = std::filesystem::absolute(m_projectRoot).string();
        } else if (std::filesystem::exists("projects")) {
            m_fileViewerCurrentDir = std::filesystem::absolute("projects").string();
        } else {
            m_fileViewerCurrentDir = std::filesystem::absolute(std::filesystem::current_path()).string();
        }
        std::replace(m_fileViewerCurrentDir.begin(), m_fileViewerCurrentDir.end(), '\\', '/');
    }

    std::filesystem::path currentPath = std::filesystem::absolute(std::filesystem::path(m_fileViewerCurrentDir));

    // --- TOP TOOLBAR ---
    // 1. Up folder button (can navigate all the way up to system root/drives)
    bool hasParent = currentPath.has_parent_path() && currentPath != currentPath.root_path();
    ImGui::BeginDisabled(!hasParent);
    if (ImGui::Button(TR("[..] Up"), ImVec2(55, 24))) {
        m_fileViewerCurrentDir = currentPath.parent_path().string();
        std::replace(m_fileViewerCurrentDir.begin(), m_fileViewerCurrentDir.end(), '\\', '/');
    }
    ImGui::EndDisabled();

    // Quick Jump Buttons
    ImGui::SameLine();
    if (ImGui::Button(TR("Project"))) {
        if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot)) {
            m_fileViewerCurrentDir = std::filesystem::absolute(m_projectRoot).string();
            std::replace(m_fileViewerCurrentDir.begin(), m_fileViewerCurrentDir.end(), '\\', '/');
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to Active Project Directory");

    ImGui::SameLine();
    if (ImGui::Button(TR("Desktop"))) {
        std::string d = GetDesktopDirectoryPath();
        if (!d.empty() && std::filesystem::exists(d)) {
            m_fileViewerCurrentDir = d;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to Desktop");

    ImGui::SameLine();
    if (ImGui::Button(TR("Browse...##FVBrowse"))) {
        std::string chosen;
        if (BrowseForFolderDialog(chosen, "Select Folder to View in Project Files")) {
            m_fileViewerCurrentDir = chosen;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse for any directory on disk");

    // 2. Breadcrumbs navigation
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Path:"));
    ImGui::SameLine();

    std::string pathStr = m_fileViewerCurrentDir;
    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
    std::stringstream ss(pathStr);
    std::string segment;
    std::string accumulated = "";

    int segIndex = 0;
    while (std::getline(ss, segment, '/')) {
        if (segment.empty()) continue;
        if (!accumulated.empty()) accumulated += "/";
        accumulated += segment;

        if (segIndex > 0) {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0, 2);
        }

        std::string btnLabel = segment + "##Seg" + std::to_string(segIndex);
        if (ImGui::SmallButton(btnLabel.c_str())) {
            m_fileViewerCurrentDir = accumulated;
        }
        segIndex++;
    }

    // Right-aligned actions
    float availRight = ImGui::GetContentRegionAvail().x;
    float searchWidth = 140.0f;
    if (availRight > 380.0f) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 370.0f);
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##FileSearch", "Search files...", m_fileViewerSearchFilter, sizeof(m_fileViewerSearchFilter));

        ImGui::SameLine();
        if (ImGui::Button("+ Script")) {
            m_showCreateScriptPopup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Folder")) {
            m_showCreateFolderPopup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Explorer")) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", m_fileViewerCurrentDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
        }
    }

    ImGui::Separator();

    // --- POPUPS FOR CREATING FOLDER / SCRIPT ---
    if (m_showCreateFolderPopup) {
        ImGui::OpenPopup(TR("Create New Folder##Popup"));
        m_showCreateFolderPopup = false;
    }
    if (ImGui::BeginPopupModal(TR("Create New Folder##Popup"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", TR("Enter folder name:"));
        ImGui::InputText("##FolderName", m_newFolderName, sizeof(m_newFolderName));
        ImGui::Spacing();
        if (ImGui::Button(TR("Create"), ImVec2(100, 24))) {
            std::filesystem::path newP = std::filesystem::path(m_fileViewerCurrentDir) / m_newFolderName;
            std::error_code ec;
            std::filesystem::create_directories(newP, ec);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(100, 24))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_showCreateScriptPopup) {
        ImGui::OpenPopup(TR("Create Lua Script##Popup"));
        m_showCreateScriptPopup = false;
    }
    if (ImGui::BeginPopupModal(TR("Create Lua Script##Popup"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", TR("Enter script filename (with .lua):"));
        ImGui::InputText("##ScriptName", m_newScriptName, sizeof(m_newScriptName));
        ImGui::Spacing();
        if (ImGui::Button(TR("Create"), ImVec2(100, 24))) {
            std::string sName = m_newScriptName;
            if (sName.find(".lua") == std::string::npos) sName += ".lua";
            std::filesystem::path newP = std::filesystem::path(m_fileViewerCurrentDir) / sName;
            std::ofstream out(newP);
            if (out.is_open()) {
                out << "-- " << sName << "\n"
                    << "-- Tessera Game Engine Lua Script\n\n"
                    << "function on_init()\n"
                    << "    print(\"Script initialized: \" .. node.name)\n"
                    << "end\n\n"
                    << "function on_update(dt)\n"
                    << "    -- Custom per-frame logic\n"
                    << "end\n";
                out.close();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(100, 24))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- TWO-PANE LAYOUT: Folder Tree on Left, File Content on Right ---
    float treeWidth = 175.0f;
    float contentWidth = ImGui::GetContentRegionAvail().x - treeWidth - 8.0f;
    if (contentWidth < 150.0f) {
        treeWidth = 120.0f;
        contentWidth = ImGui::GetContentRegionAvail().x - treeWidth - 8.0f;
    }

    // LEFT: Folder Quick Navigation Tree
    ImGui::BeginChild("FolderTreePane", ImVec2(treeWidth, 0), true);
    ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Quick Locations");
    ImGui::Separator();

    // 1. Project Root
    if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot)) {
        std::string pRoot = std::filesystem::absolute(m_projectRoot).string();
        std::replace(pRoot.begin(), pRoot.end(), '\\', '/');
        bool isProjActive = (m_fileViewerCurrentDir == pRoot);
        std::string label = "[PROJ] " + (!m_currentProjectName.empty() ? m_currentProjectName : "Project");
        if (ImGui::Selectable(label.c_str(), isProjActive)) {
            m_fileViewerCurrentDir = pRoot;
        }
    }

    // 2. Desktop
    std::string deskP = GetDesktopDirectoryPath();
    if (!deskP.empty() && std::filesystem::exists(deskP)) {
        bool isDeskActive = (m_fileViewerCurrentDir == deskP);
        if (ImGui::Selectable("[DIR] Desktop", isDeskActive)) {
            m_fileViewerCurrentDir = deskP;
        }
    }

    // 3. Documents
    std::string docP = GetDocumentsDirectoryPath();
    if (!docP.empty() && std::filesystem::exists(docP)) {
        bool isDocActive = (m_fileViewerCurrentDir == docP);
        if (ImGui::Selectable("[DIR] Documents", isDocActive)) {
            m_fileViewerCurrentDir = docP;
        }
    }

    // 4. Drive C:
    if (std::filesystem::exists("C:/")) {
        bool isCActive = (m_fileViewerCurrentDir == "C:/" || m_fileViewerCurrentDir == "C:");
        if (ImGui::Selectable("[DRV] Local Disk (C:)", isCActive)) {
            m_fileViewerCurrentDir = "C:/";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.70f, 0.85f, 0.95f, 1.0f), "Current Subfolders");
    ImGui::Separator();

    std::error_code ecTree;
    for (const auto& entry : std::filesystem::directory_iterator(m_fileViewerCurrentDir, std::filesystem::directory_options::skip_permission_denied, ecTree)) {
        if (entry.is_directory(ecTree)) {
            std::string subStr = entry.path().string();
            std::replace(subStr.begin(), subStr.end(), '\\', '/');
            std::string dirName = entry.path().filename().string();
            if (dirName.rfind(".", 0) == 0 || dirName == "$RECYCLE.BIN" || dirName == "System Volume Information") continue;

            bool isSubActive = (m_fileViewerCurrentDir == subStr);
            if (ImGui::Selectable(("  [DIR] " + dirName).c_str(), isSubActive)) {
                m_fileViewerCurrentDir = subStr;
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // RIGHT: File Content Area
    ImGui::BeginChild("FileContentPane", ImVec2(0, 0), true);

    std::vector<std::filesystem::directory_entry> subDirs;
    std::vector<std::filesystem::directory_entry> files;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(currentPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
        std::string filename = entry.path().filename().string();
        if (filename.rfind(".", 0) == 0) continue; // Skip hidden/dot files

        // Search filter matching
        if (strlen(m_fileViewerSearchFilter) > 0) {
            std::string lowerName = filename;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            std::string lowerFilter = m_fileViewerSearchFilter;
            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
            if (lowerName.find(lowerFilter) == std::string::npos) continue;
        }

        if (entry.is_directory(ec)) {
            subDirs.push_back(entry);
        } else {
            files.push_back(entry);
        }
    }

    std::sort(subDirs.begin(), subDirs.end(), [](const auto& a, const auto& b) {
        return a.path().filename().string() < b.path().filename().string();
    });
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path().filename().string() < b.path().filename().string();
    });

    // Render Subdirectories first
    for (const auto& d : subDirs) {
        std::string dirName = d.path().filename().string();
        std::string fullDir = d.path().string();
        std::replace(fullDir.begin(), fullDir.end(), '\\', '/');

        ImGui::PushID(fullDir.c_str());
        ImGui::TextColored(ImVec4(0.40f, 0.78f, 1.0f, 1.0f), "[DIR]");
        ImGui::SameLine();
        bool isSel = (m_fileViewerSelectedPath == fullDir);
        if (ImGui::Selectable((dirName + "/").c_str(), isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
            m_fileViewerSelectedPath = fullDir;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_fileViewerCurrentDir = fullDir;
            }
        }
        if (ImGui::BeginPopupContextItem("DirContextMenu")) {
            if (ImGui::MenuItem("Enter Directory")) {
                m_fileViewerCurrentDir = fullDir;
            }
            if (ImGui::MenuItem("Open in Explorer")) {
#ifdef _WIN32
                ShellExecuteA(NULL, "open", fullDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // Render Files
    for (const auto& f : files) {
        std::string filename = f.path().filename().string();
        std::string fullPath = f.path().string();
        std::replace(fullPath.begin(), fullPath.end(), '\\', '/');

        std::string ext = f.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        const char* badge = "[FILE]";
        ImVec4 badgeColor(0.70f, 0.75f, 0.85f, 1.0f);
        bool isModel = (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae");
        bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr");
        bool isScript = (ext == ".lua");
        bool isProject = (ext == ".tess");
        bool isScene = (ext == ".scene" || ext == ".json");
        bool isAudio = (ext == ".wav" || ext == ".ogg" || ext == ".mp3");
        bool isFont = (ext == ".ttf" || ext == ".otf");
        bool isShader = (ext == ".vert" || ext == ".frag" || ext == ".spv" || ext == ".glsl");
        bool isShaderGraph = (ext == ".shadergraph");

        if (isProject) { badge = "[PROJ]"; badgeColor = ImVec4(0.95f, 0.75f, 0.20f, 1.0f); }
        else if (isModel) { badge = "[3D]"; badgeColor = ImVec4(0.20f, 0.90f, 0.95f, 1.0f); }
        else if (isTexture) { badge = "[IMG]"; badgeColor = ImVec4(1.0f, 0.65f, 0.25f, 1.0f); }
        else if (isScript) { badge = "[LUA]"; badgeColor = ImVec4(0.55f, 0.95f, 0.35f, 1.0f); }
        else if (isScene) { badge = "[SCENE]"; badgeColor = ImVec4(0.35f, 0.95f, 0.65f, 1.0f); }
        else if (isAudio) { badge = "[AUDIO]"; badgeColor = ImVec4(0.85f, 0.45f, 1.0f, 1.0f); }
        else if (isFont) { badge = "[FONT]"; badgeColor = ImVec4(1.0f, 0.90f, 0.35f, 1.0f); }
        else if (isShader) { badge = "[SHDR]"; badgeColor = ImVec4(1.0f, 0.40f, 0.60f, 1.0f); }
        else if (isShaderGraph) { badge = "[MAT]"; badgeColor = ImVec4(0.95f, 0.55f, 0.85f, 1.0f); }

        ImGui::PushID(fullPath.c_str());
        ImGui::TextColored(badgeColor, "%s", badge);
        ImGui::SameLine();

        bool isSel = (m_fileViewerSelectedPath == fullPath);
        if (ImGui::Selectable(filename.c_str(), isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
            m_fileViewerSelectedPath = fullPath;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (isScript) {
                    m_showScriptEditor = true;
                    LoadScriptFile(fullPath);
                } else if (isModel) {
                    if (m_onImportObj) m_onImportObj(fullPath);
                } else if (isTexture) {
                    auto sel = scene.GetSelectedNode();
                    if (sel) sel->texturePath = fullPath;
                }
            }
        }

        // Hovering a texture or a shader graph shows it as a material on a sphere
        if ((isTexture || isShaderGraph) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            static Node previewMaterial;
            previewMaterial = Node{};
            previewMaterial.color = glm::vec4(1.0f);
            previewMaterial.roughness = 0.6f;
            if (isTexture) previewMaterial.texturePath = fullPath;
            else previewMaterial.shaderGraphPath = fullPath;
            ImGui::BeginTooltip();
            DrawMaterialPreview("file:" + fullPath, previewMaterial, 160.0f, 0);
            ImGui::TextDisabled("%s", filename.c_str());
            ImGui::EndTooltip();
        }

        // Context Menu
        if (ImGui::BeginPopupContextItem("FileContextMenu")) {
            if (isProject) {
                if (ImGui::MenuItem("Open Project")) {
                    if (m_onOpenProject) m_onOpenProject(fullPath);
                }
                ImGui::Separator();
            }
            if (isScene) {
                if (ImGui::MenuItem("Instantiate into Active Scene (as Prefab)")) {
                    scene.InstantiateScene(fullPath, 0);
                }
                if (ImGui::MenuItem("Instantiate as Child of Selected Node")) {
                    uint32_t targetParent = scene.GetSelectedId() > 0 ? static_cast<uint32_t>(scene.GetSelectedId()) : 0;
                    scene.InstantiateScene(fullPath, targetParent);
                }
                if (ImGui::MenuItem("Open Scene as Main Scene")) {
                    if (m_onOpenScene) m_onOpenScene(fullPath);
                }
                ImGui::Separator();
            }
            if (isScript) {
                if (ImGui::MenuItem("Open in Script Editor")) {
                    m_showScriptEditor = true;
                    LoadScriptFile(fullPath);
                }
            }
            if (isModel) {
                if (ImGui::MenuItem("Import into Scene")) {
                    if (m_onImportObj) m_onImportObj(fullPath);
                }
            }
            if (isTexture) {
                if (ImGui::MenuItem("Assign to Selected Node (Texture)")) {
                    auto sel = scene.GetSelectedNode();
                    if (sel) sel->texturePath = fullPath;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open with Default Application")) {
#ifdef _WIN32
                ShellExecuteA(NULL, "open", fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
            }
            if (ImGui::MenuItem("Show in Explorer")) {
#ifdef _WIN32
                std::string selectCmd = "/select,\"" + fullPath + "\"";
                std::replace(selectCmd.begin(), selectCmd.end(), '/', '\\');
                ShellExecuteA(NULL, "open", "explorer.exe", selectCmd.c_str(), NULL, SW_SHOWNORMAL);
#endif
            }
            if (ImGui::MenuItem("Copy Path")) {
                ImGui::SetClipboardText(fullPath.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete File")) {
                std::error_code ecDel;
                std::filesystem::remove(fullPath, ecDel);
            }
            ImGui::EndPopup();
        }

        // Drag & Drop Source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("PROJECT_FILE_PATH", fullPath.c_str(), fullPath.size() + 1);
            if (isModel) ImGui::SetDragDropPayload("DND_MODEL_PATH", fullPath.c_str(), fullPath.size() + 1);
            if (isTexture) ImGui::SetDragDropPayload("DND_TEXTURE_PATH", fullPath.c_str(), fullPath.size() + 1);
            if (isScript) ImGui::SetDragDropPayload("DND_SCRIPT_PATH", fullPath.c_str(), fullPath.size() + 1);
            ImGui::Text("%s %s", badge, filename.c_str());
            ImGui::EndDragDropSource();
        }

        // File Size & Details on right
        uintmax_t fSize = 0;
        std::error_code ecSize;
        fSize = f.file_size(ecSize);
        std::string sizeText;
        if (fSize >= 1024 * 1024) {
            sizeText = std::to_string(fSize / (1024 * 1024)) + " MB";
        } else if (fSize >= 1024) {
            sizeText = std::to_string(fSize / 1024) + " KB";
        } else {
            sizeText = std::to_string(fSize) + " B";
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
        ImGui::TextDisabled("%s", sizeText.c_str());
        ImGui::PopID();
    }

    if (subDirs.empty() && files.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("This folder is empty.");
        ImGui::TextDisabled("Click '+ Script' or '+ Folder' above to create files here.");
    }

    ImGui::EndChild();

    ImGui::End();
}

void EditorUI::RenderSplashScreen(float dt) {
    m_splashTimer += dt;
    float progress = std::clamp(m_splashTimer / m_splashDuration, 0.0f, 1.0f);

    // Skip splash on mouse click or Space/Enter/Escape
    ImGuiIO& io = ImGui::GetIO();
    if (m_splashTimer > 0.25f && (io.MouseClicked[0] || io.MouseClicked[1] ||
        ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        progress = 1.0f;
        m_splashTimer = m_splashDuration;
    }

    if (m_splashTimer >= m_splashDuration) {
        m_startupState = StartupState::ProjectLauncher;
        return;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + 32.0f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - 32.0f));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.07f, 0.10f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##SplashScreenWindow", nullptr, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 center = ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + 32.0f + (vp->Size.y - 32.0f) * 0.5f);

    // Dark backdrop fill
    drawList->AddRectFilled(ImVec2(vp->Pos.x, vp->Pos.y + 32.0f), ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                            IM_COL32(10, 13, 18, 255));

    // Centered Card
    float cardW = 560.0f;
    float cardH = 340.0f;
    ImVec2 cardMin = ImVec2(center.x - cardW * 0.5f, center.y - cardH * 0.5f);
    ImVec2 cardMax = ImVec2(center.x + cardW * 0.5f, center.y + cardH * 0.5f);

    // Card background with rounded corners & glowing edge
    drawList->AddRectFilled(cardMin, cardMax, IM_COL32(18, 24, 36, 250), 12.0f);
    drawList->AddRect(cardMin, cardMax, IM_COL32(35, 95, 170, 200), 12.0f, 0, 1.5f);
    drawList->AddLine(ImVec2(cardMin.x + 24.0f, cardMin.y), ImVec2(cardMax.x - 24.0f, cardMin.y),
                      IM_COL32(65, 175, 255, 240), 2.0f);

    float curY = cardMin.y + 32.0f;

    // Logo Icon: High-Resolution Glowing Tessera Emblem with Shimmer Pulse
    float pulse = 1.0f + 0.04f * std::sin(m_splashTimer * 4.5f);
    RenderEngineEmblem(drawList, ImVec2(center.x, curY + 28.0f), 32.0f * pulse, 1.0f);
    curY += 70.0f;

    // Title
    const char* title = "T E S S E R A";
    const char* titleSub = "3D GAME ENGINE";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    float titleScale = 24.0f / ImGui::GetFontSize();
    float titleW = titleSize.x * titleScale;
    drawList->AddText(nullptr, 24.0f, ImVec2(center.x - titleW * 0.5f, curY),
                      IM_COL32(235, 245, 255, 255), title);
    curY += 34.0f;

    ImVec2 subSize = ImGui::CalcTextSize(titleSub);
    drawList->AddText(ImVec2(center.x - subSize.x * 0.5f, curY),
                      IM_COL32(75, 180, 255, 230), titleSub);
    curY += 22.0f;

    const char* verStr = "Version 2.0 (x64 Windows) \xe2\x80\xa2 Sol2 Lua 5.4 \xe2\x80\xa2 JoltPhysics \xe2\x80\xa2 PBR";
    ImVec2 verSize = ImGui::CalcTextSize(verStr);
    drawList->AddText(ImVec2(center.x - verSize.x * 0.5f, curY),
                      IM_COL32(130, 155, 180, 200), verStr);
    curY += 40.0f;

    // Progress Bar
    float barW = cardW - 80.0f;
    float barH = 8.0f;
    ImVec2 barMin = ImVec2(center.x - barW * 0.5f, curY);
    ImVec2 barMax = ImVec2(center.x + barW * 0.5f, curY + barH);
    drawList->AddRectFilled(barMin, barMax, IM_COL32(25, 35, 52, 255), 4.0f);
    ImVec2 fillMax = ImVec2(barMin.x + barW * progress, barMax.y);
    if (progress > 0.01f) {
        drawList->AddRectFilled(barMin, fillMax, IM_COL32(40, 150, 245, 255), 4.0f);
        drawList->AddCircleFilled(ImVec2(fillMax.x, barMin.y + barH * 0.5f), 4.5f, IM_COL32(110, 210, 255, 255));
    }
    curY += 18.0f;

    // Dynamic loading status messages
    const char* stageText = "Loading Core Systems...";
    if (progress < 0.25f) stageText = "[1/4] Initializing Vulkan 1.3 Physical Device & Swapchain...";
    else if (progress < 0.50f) stageText = "[2/4] Compiling Graphics Pipelines, Shaders & Post-Process Stack...";
    else if (progress < 0.75f) stageText = "[3/4] Loading Sol2 Lua 5.4 Script Runtime & Jolt Physics World...";
    else if (progress < 0.95f) stageText = "[4/4] Mounting Project Filesystem & Spatial Audio Engine...";
    else stageText = "Ready! Opening Project Hub...";

    ImVec2 stageSize = ImGui::CalcTextSize(stageText);
    drawList->AddText(ImVec2(center.x - stageSize.x * 0.5f, curY),
                      IM_COL32(175, 195, 220, 220), stageText);

    // Skip Hint
    const char* hint = "Click anywhere or press Space to skip";
    ImVec2 hintSize = ImGui::CalcTextSize(hint);
    drawList->AddText(ImVec2(center.x - hintSize.x * 0.5f, cardMax.y - 22.0f),
                      IM_COL32(95, 115, 135, 180), hint);

    ImGui::End();
}

void EditorUI::RenderProjectLauncher(Scene& scene, Camera& camera) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + 32.0f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - 32.0f));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.12f, 1.0f));
    ImGui::Begin("##ProjectLauncherBg", nullptr, bgFlags);
    ImGui::PopStyleColor();

    float hubWidth = 920.0f;
    float hubHeight = 600.0f;
    ImVec2 center = ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + 32.0f + (vp->Size.y - 32.0f) * 0.5f);

    ImGui::SetNextWindowPos(ImVec2(center.x - hubWidth * 0.5f, center.y - hubHeight * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(hubWidth, hubHeight));

    ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.38f, 0.58f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.13f, 0.17f, 1.0f));

    if (ImGui::Begin("ProjectLauncherHub", nullptr, hubFlags)) {
        // --- TOP HEADER ---
        ImGui::Spacing();
        ImVec2 pPos = ImGui::GetCursorScreenPos();
        RenderEngineEmblem(ImGui::GetWindowDrawList(), ImVec2(pPos.x + 14.0f, pPos.y + 11.0f), 12.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 30.0f);
        ImGui::TextColored(ImVec4(0.25f, 0.78f, 1.0f, 1.0f), "TESSERA ENGINE");
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", TR("Project Hub & Workspace Manager"));

        // Language selector on Project Loader
        const auto& languages = Localization::Get().GetAvailableLanguages();
        std::string currentLangName = Localization::Get().GetCurrentLanguageName();
        float langComboWidth = 115.0f;
        float rightMargin = !m_currentProjectName.empty() ? (225.0f + langComboWidth + 12.0f) : (langComboWidth + 15.0f);
        ImGui::SameLine(hubWidth - rightMargin);
        ImGui::SetNextItemWidth(langComboWidth);
        if (ImGui::BeginCombo("##HubLangSelect", currentLangName.c_str())) {
            for (const auto& lang : languages) {
                bool isSelected = (lang.code == Localization::Get().GetCurrentLanguage());
                if (ImGui::Selectable(lang.displayName.c_str(), isSelected)) {
                    Localization::Get().SetLanguage(lang.code);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (!m_currentProjectName.empty()) {
            ImGui::SameLine(hubWidth - 230.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.65f, 1.0f));
            if (ImGui::Button(TR("Continue Current Project >>"), ImVec2(210, 24))) {
                m_startupState = StartupState::Editor;
            }
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- TWO COLUMNS: Recent Projects (Left) vs Create New (Right) ---
        float colLeftWidth = 460.0f;
        float colRightWidth = hubWidth - colLeftWidth - 45.0f;

        // LEFT COLUMN: Recent Projects & Open Existing
        ImGui::BeginChild("RecentProjectsCol", ImVec2(colLeftWidth, hubHeight - 110), true);
        ImGui::TextColored(ImVec4(0.9f, 0.92f, 0.98f, 1.0f), "%s", TR("Recent Projects"));
        ImGui::Separator();
        ImGui::Spacing();

        auto recents = Project::GetRecentProjects();
        if (recents.empty()) {
            ImGui::TextDisabled("%s", TR("No recent projects found."));
            ImGui::Spacing();
            ImGui::TextWrapped("%s", TR("Create a new project on the right, or browse your disk for an existing .tess project file."));
        } else {
            for (size_t i = 0; i < recents.size(); ++i) {
                const auto& rPath = recents[i];
                std::filesystem::path p(rPath);
                std::string projName = p.stem().string();
                bool exists = std::filesystem::exists(rPath);

                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginGroup();

                if (exists) {
                    ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "%s", projName.c_str());
                    ImGui::SameLine(colLeftWidth - 95.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.28f, 1.0f));
                    if (ImGui::Button(TR("Open##Rec"), ImVec2(75, 22))) {
                        if (m_onOpenProject) {
                            m_onOpenProject(rPath);
                            m_startupState = StartupState::Editor;
                        }
                    }
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextDisabled("%s %s", projName.c_str(), TR("[Missing]"));
                }

                ImGui::TextDisabled("%s", rPath.c_str());
                ImGui::EndGroup();

                ImGui::Separator();
                ImGui::PopID();
            }
        }

        ImGui::SetCursorPosY(hubHeight - 155.0f);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.38f, 0.60f, 1.0f));
        if (ImGui::Button(TR("Browse Existing Project File (*.tess)..."), ImVec2(-1, 32))) {
            OpenProjectDialog();
        }
        ImGui::PopStyleColor();

        ImGui::EndChild();

        ImGui::SameLine();

        // RIGHT COLUMN: Create New Project
        ImGui::BeginChild("NewProjectCol", ImVec2(colRightWidth, hubHeight - 110), true);
        ImGui::TextColored(ImVec4(0.9f, 0.92f, 0.98f, 1.0f), "%s", TR("Create New Project"));
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("%s", TR("Project Name:"));
        ImGui::InputText("##LauncherProjName", m_launcherNewProjectName, sizeof(m_launcherNewProjectName));

        ImGui::Spacing();
        ImGui::Text("%s", TR("Directory Location:"));
        float launcherBrowseW = 80.0f;
        float launcherDeskW = 65.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - launcherBrowseW - launcherDeskW - 12.0f);
        ImGui::InputText("##LauncherProjDir", m_launcherNewProjectDir, sizeof(m_launcherNewProjectDir));
        ImGui::SameLine();
        if (ImGui::Button(TR("Browse...##LaunchDir"), ImVec2(launcherBrowseW, 0))) {
            std::string chosen;
            if (BrowseForFolderDialog(chosen, TR("Select Location for New Project"))) {
                strncpy(m_launcherNewProjectDir, chosen.c_str(), sizeof(m_launcherNewProjectDir));
                m_launcherNewProjectDir[sizeof(m_launcherNewProjectDir) - 1] = '\0';
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Desktop##LaunchDesk"), ImVec2(launcherDeskW, 0))) {
            std::string d = GetDesktopDirectoryPath();
            if (!d.empty()) {
                strncpy(m_launcherNewProjectDir, d.c_str(), sizeof(m_launcherNewProjectDir));
                m_launcherNewProjectDir[sizeof(m_launcherNewProjectDir) - 1] = '\0';
            }
        }

        ImGui::Spacing();
        std::string previewPath = std::string(m_launcherNewProjectDir) + "/" + m_launcherNewProjectName;
        ImGui::TextDisabled("Target: %s/%s.tess", previewPath.c_str(), m_launcherNewProjectName);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("%s", TR("Project Template:"));
        ImGui::RadioButton(TR("3D Standard (PBR + Lighting + Camera)"), &m_launcherTemplateIndex, 0);
        ImGui::RadioButton(TR("Blank Scene (Empty Slate)"), &m_launcherTemplateIndex, 1);
        ImGui::RadioButton(TR("Third-Person Character Sandbox"), &m_launcherTemplateIndex, 2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!m_launcherStatusMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_launcherStatusMessage.c_str());
            ImGui::Spacing();
        }

        ImGui::SetCursorPosY(hubHeight - 155.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.60f, 0.35f, 1.0f));
        if (ImGui::Button(TR("+ Create & Launch Project"), ImVec2(-1, 36))) {
            std::string pName = m_launcherNewProjectName;
            std::string pDir = m_launcherNewProjectDir;
            if (pName.empty()) {
                m_launcherStatusMessage = TR("Project name cannot be empty.");
            } else {
                if (m_onNewProject) {
                    m_onNewProject(pDir, pName);
                    if (m_launcherTemplateIndex == 1) {
                        scene.Clear();
                    } else if (m_launcherTemplateIndex == 2) {
                        scene.Clear();
                        auto floor = scene.AddNode(ShapeType::Plane, "FloorPlane");
                        floor->scale = glm::vec3(40.0f, 1.0f, 40.0f);
                        auto hero = scene.AddCharacterNode("PlayerHero");
                        hero->position = glm::vec3(0.0f, 1.0f, 0.0f);
                        auto sun = scene.AddLightNode(LightType::Directional, "SunLight");
                        sun->position = glm::vec3(10.0f, 15.0f, 10.0f);
                        auto cam = scene.AddCameraNode("MainCamera");
                        cam->position = glm::vec3(0.0f, 4.0f, 8.0f);
                    }
                    m_startupState = StartupState::Editor;
                }
            }
        }
        ImGui::PopStyleColor();

        ImGui::EndChild();

        // Footer status bar
        ImGui::Spacing();
        ImGui::TextDisabled("%s", TR("Tip: You can switch or create projects at any time from File -> Project Launcher / Hub..."));
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::End();
}

void EditorUI::RenderCustomTitleBar(Scene& scene, Camera& camera, Gameplay::GameLoop& gameLoop) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float titleBarHeight = 32.0f;

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, titleBarHeight));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_MenuBar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.05f, 0.06f, 0.08f, 1.0f));

    if (ImGui::Begin("##EngineCustomTitleBar", nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (ImGui::BeginMenuBar()) {
            // 1. Logo / Branding (Left)
            ImGui::Spacing();
            ImVec2 scrPos = ImGui::GetCursorScreenPos();
            RenderEngineEmblem(drawList, ImVec2(scrPos.x + 10.0f, scrPos.y + titleBarHeight * 0.5f - 1.0f), 8.5f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.85f, 1.0f, 1.0f));
            ImGui::Text("TESSERA");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // 2. State-specific content
            if (m_startupState == StartupState::Splash) {
                ImGui::TextDisabled("Startup");
            } else if (m_startupState == StartupState::ProjectLauncher) {
                ImGui::TextDisabled("%s", TR("Project Hub"));
            } else {
                // Editor Main Menus
                RenderMainMenuBar(scene, camera, gameLoop);
            }

            m_menuBarEndX = ImGui::GetCursorPosX();

            // 3. Center Project/Scene Title
            std::string centerTitle;
            if (m_startupState == StartupState::Splash) {
                centerTitle = "Tessera Engine - Initializing...";
            } else if (m_startupState == StartupState::ProjectLauncher) {
                centerTitle = "Tessera Engine - Project Hub & Workspace Manager";
            } else {
                std::string sceneStr = m_onGetActiveScene ? m_onGetActiveScene() : scene.GetName();
                centerTitle = m_currentProjectName + "  -  [" + sceneStr + "]";
            }

            ImVec2 textSize = ImGui::CalcTextSize(centerTitle.c_str());
            float centerPos = (vp->Size.x - textSize.x) * 0.5f;
            if (centerPos > ImGui::GetCursorPosX() && centerPos + textSize.x < vp->Size.x - 145.0f) {
                ImGui::SameLine(centerPos);
                ImGui::TextColored(ImVec4(0.72f, 0.78f, 0.88f, 0.90f), "%s", centerTitle.c_str());
            }

            // 4. Window Action Controls (Right: Minimize, Maximize/Restore, Close)
            const float rightWidth = 140.0f;
            ImGui::SameLine(vp->Size.x - rightWidth);

            // --- MINIMIZE BUTTON ---
            ImVec2 minBtnPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##TitleMinBtn", ImVec2(44.0f, titleBarHeight));
            bool minHovered = ImGui::IsItemHovered();
            bool minActive = ImGui::IsItemActive();
            if (minActive) drawList->AddRectFilled(minBtnPos, ImVec2(minBtnPos.x + 44.0f, minBtnPos.y + titleBarHeight), IM_COL32(50, 60, 80, 255));
            else if (minHovered) drawList->AddRectFilled(minBtnPos, ImVec2(minBtnPos.x + 44.0f, minBtnPos.y + titleBarHeight), IM_COL32(35, 42, 55, 255));

            float midX = minBtnPos.x + 22.0f;
            float midY = minBtnPos.y + titleBarHeight * 0.5f + 4.0f;
            ImU32 minCol = minHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 190, 205, 255);
            drawList->AddLine(ImVec2(midX - 5.0f, midY), ImVec2(midX + 5.0f, midY), minCol, 1.5f);
            if (ImGui::IsItemClicked()) {
                if (m_onMinimizeWindow) m_onMinimizeWindow();
            }
            if (minHovered) ImGui::SetTooltip("Minimize");

            // --- MAXIMIZE / RESTORE BUTTON ---
            ImGui::SameLine(0, 0);
            ImVec2 maxBtnPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##TitleMaxBtn", ImVec2(44.0f, titleBarHeight));
            bool maxHovered = ImGui::IsItemHovered();
            bool maxActive = ImGui::IsItemActive();
            if (maxActive) drawList->AddRectFilled(maxBtnPos, ImVec2(maxBtnPos.x + 44.0f, maxBtnPos.y + titleBarHeight), IM_COL32(50, 60, 80, 255));
            else if (maxHovered) drawList->AddRectFilled(maxBtnPos, ImVec2(maxBtnPos.x + 44.0f, maxBtnPos.y + titleBarHeight), IM_COL32(35, 42, 55, 255));

            bool isMax = m_isWindowMaximized ? m_isWindowMaximized() : false;
            midX = maxBtnPos.x + 22.0f;
            midY = maxBtnPos.y + titleBarHeight * 0.5f;
            ImU32 maxCol = maxHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 190, 205, 255);
            if (isMax) {
                // Restore icon: two overlapping squares
                drawList->AddRect(ImVec2(midX - 3.0f, midY - 6.0f), ImVec2(midX + 6.0f, midY + 3.0f), maxCol, 0.0f, 0, 1.2f);
                drawList->AddRectFilled(ImVec2(midX - 6.0f, midY - 3.0f), ImVec2(midX + 3.0f, midY + 6.0f),
                                        maxHovered ? IM_COL32(35, 42, 55, 255) : IM_COL32(12, 15, 20, 255));
                drawList->AddRect(ImVec2(midX - 6.0f, midY - 3.0f), ImVec2(midX + 3.0f, midY + 6.0f), maxCol, 0.0f, 0, 1.2f);
            } else {
                // Maximize icon: single square
                drawList->AddRect(ImVec2(midX - 5.0f, midY - 5.0f), ImVec2(midX + 5.0f, midY + 5.0f), maxCol, 0.0f, 0, 1.5f);
            }
            if (ImGui::IsItemClicked()) {
                if (m_onMaximizeWindow) m_onMaximizeWindow();
            }
            if (maxHovered) ImGui::SetTooltip(isMax ? "Restore Down" : "Maximize");

            // --- CLOSE BUTTON ---
            ImGui::SameLine(0, 0);
            ImVec2 closeBtnPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##TitleCloseBtn", ImVec2(48.0f, titleBarHeight));
            bool closeHovered = ImGui::IsItemHovered();
            bool closeActive = ImGui::IsItemActive();
            if (closeActive) drawList->AddRectFilled(closeBtnPos, ImVec2(closeBtnPos.x + 48.0f, closeBtnPos.y + titleBarHeight), IM_COL32(196, 15, 30, 255));
            else if (closeHovered) drawList->AddRectFilled(closeBtnPos, ImVec2(closeBtnPos.x + 48.0f, closeBtnPos.y + titleBarHeight), IM_COL32(232, 17, 35, 255));

            midX = closeBtnPos.x + 24.0f;
            midY = closeBtnPos.y + titleBarHeight * 0.5f;
            ImU32 closeCol = closeHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 195, 205, 255);
            float span = 4.5f;
            drawList->AddLine(ImVec2(midX - span, midY - span), ImVec2(midX + span, midY + span), closeCol, 1.5f);
            drawList->AddLine(ImVec2(midX + span, midY - span), ImVec2(midX - span, midY + span), closeCol, 1.5f);
            if (ImGui::IsItemClicked()) {
                if (m_onCloseWindow) m_onCloseWindow();
            }
            if (closeHovered) ImGui::SetTooltip("Close (Alt+F4)");

            ImGui::EndMenuBar();
        }

        // Title bar drag & double-click maximize fallback
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (m_onMaximizeWindow) m_onMaximizeWindow();
            } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                if (m_onMoveWindow && (delta.x != 0.0f || delta.y != 0.0f)) {
                    m_onMoveWindow(delta.x, delta.y);
                }
            }
        }

        // Bottom separator line
        drawList->AddLine(ImVec2(vp->Pos.x, vp->Pos.y + titleBarHeight),
                          ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + titleBarHeight),
                          IM_COL32(36, 42, 54, 255), 1.0f);

        // Subtle 1px outer window border when not maximized
        bool isMax = m_isWindowMaximized ? m_isWindowMaximized() : false;
        if (!isMax) {
            drawList->AddRect(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                              IM_COL32(42, 50, 68, 220), 0.0f, 0, 1.0f);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

bool EditorUI::IsMouseOverMenuBar(int clientX, int clientY) const {
    if (m_startupState != StartupState::Editor) {
        return false;
    }
    return (clientY >= 0 && clientY < 32 && clientX >= 135 && clientX <= static_cast<int>(m_menuBarEndX));
}

void EditorUI::RenderGameProfilerWindow(Scene& scene, Gameplay::GameLoop& gameLoop) {
    ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(TR("Game Profiler & Resource Monitor###GameProfilerWindow"), &m_showGameProfilerWindow)) {
        ImGui::End();
        return;
    }

    bool isGameRunning = gameLoop.IsPlaying() || gameLoop.IsPaused();
    auto& profiler = Monitoring::GameProfiler::Get();

    auto formatBytes = [](uint64_t bytes) -> std::string {
        char buf[64];
        if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
            std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        } else if (bytes >= 1024ULL * 1024ULL) {
            std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ULL) {
            std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
        }
        return std::string(buf);
    };

    auto formatSpeed = [](uint64_t bytesPerSec) -> std::string {
        char buf[64];
        if (bytesPerSec >= 1024ULL * 1024ULL) {
            std::snprintf(buf, sizeof(buf), "%.1f MB/s", static_cast<double>(bytesPerSec) / (1024.0 * 1024.0));
        } else if (bytesPerSec >= 1024ULL) {
            std::snprintf(buf, sizeof(buf), "%.1f KB/s", static_cast<double>(bytesPerSec) / 1024.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu B/s", static_cast<unsigned long long>(bytesPerSec));
        }
        return std::string(buf);
    };

    if (!isGameRunning) {
        // Idle state: Simulation inactive
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.16f, 0.20f, 0.85f));
        ImGui::BeginChild("ProfilerInactiveBanner", ImVec2(0, 160), true);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.25f, 1.0f));
        ImGui::Text("  [SIMULATION IDLE - GAME NOT RUNNING]");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextWrapped("The Game Profiler & Resource Monitor strictly isolates and tracks active game resources "
                           "(simulation heap, Jolt physics, particle nodes, Lua scripts, and offscreen game rendering) "
                           "without contamination or overhead from the editor UI.");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.25f, 1.0f));
        if (ImGui::Button("  Run Game Simulation (Play Mode)  ", ImVec2(280, 36))) {
            gameLoop.Play(scene);
        }
        ImGui::PopStyleColor(3);

        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextDisabled("Tracked Live Metrics upon starting simulation:");
        ImGui::BulletText("Memory: RAM Working Set, Committed, Peak Usage, Estimated VRAM Allocation & Scene Heap");
        ImGui::BulletText("CPU Profiler: Frame timeline breakdown for Lua Scripts, Jolt Physics, Animations, VFX, Rendering");
        ImGui::BulletText("GPU Profiler: measured time per pass (shadows, prepass, opaque, transparents and VFX, each post effect, UI)");
        ImGui::BulletText("Physics: Dynamic Rigid Bodies, Static Colliders, Joints/Constraints & Grounded Characters");
        ImGui::BulletText("Scene Hierarchy Audit: Active object counts by type and 1-Click Orphan Node Repair Tool");
        ImGui::BulletText("Disk & Network I/O: Live throughput (read/write, tx/rx), packets, latency and peer counts");

        ImGui::End();
        return;
    }

    // Active Game Simulation Telemetry
    const auto& stats = profiler.GetStats();
    const auto& cpuStages = profiler.GetCPUStages();
    const auto& frameHistory = profiler.GetFrameTimeHistory();

    // Top Status & Control Bar
    bool isPaused = gameLoop.IsPaused();
    if (isPaused) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::Text("  [PAUSED]");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.95f, 0.45f, 1.0f));
        ImGui::Text("  [PLAYING]");
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.50f, 1.0f), "FPS: %5.1f", stats.gameFps);
    ImGui::SameLine();
    ImGui::TextDisabled("(%5.2f ms)", stats.gameFrameTimeMs);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "CPU: %4.1f%%", stats.cpuUsagePercent);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "RAM: %s", formatBytes(stats.ramWorkingSetBytes).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.50f, 0.95f, 1.0f), "VRAM: %s", formatBytes(stats.vramEstimatedBytes).c_str());

    ImGui::SameLine();
    float availRight = ImGui::GetContentRegionAvail().x;
    if (availRight > 210.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availRight - 210.0f);
    }
    if (isPaused) {
        if (ImGui::Button("Resume##Prof", ImVec2(65, 22))) gameLoop.Resume();
    } else {
        if (ImGui::Button("Pause##Prof", ImVec2(65, 22))) gameLoop.Pause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop##Prof", ImVec2(60, 22))) gameLoop.Stop(scene);

    if (stats.orphanNodeCount > 0) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.20f, 1.0f));
        if (ImGui::Button("Fix Orphans##ProfTop", ImVec2(75, 22))) {
            profiler.ReparentOrphanNodesToRoot(gameLoop.GetPlayScene().get());
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to reparent %u orphan node(s) to scene root", stats.orphanNodeCount);
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("ProfilerTabs", ImGuiTabBarFlags_None)) {
        // --------------------------------------------------------------------
        // TAB 1: OVERVIEW
        // --------------------------------------------------------------------
        if (ImGui::BeginTabItem("Overview")) {
            ImGui::Spacing();

            // 4 Telemetry Metric Cards
            float cardWidth = (ImGui::GetContentRegionAvail().x - 30.0f) / 4.0f;
            if (cardWidth < 140.0f) cardWidth = 140.0f;

            auto renderCard = [&](const char* title, const char* mainVal, const char* sub1, const char* sub2, const char* sub3, ImVec4 titleCol) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.18f, 0.22f, 0.95f));
                ImGui::BeginChild(title, ImVec2(cardWidth, 115), true);
                ImGui::PushStyleColor(ImGuiCol_Text, titleCol);
                ImGui::TextUnformatted(title);
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::PushFont(nullptr);
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", mainVal);
                ImGui::PopFont();
                ImGui::TextDisabled("%s", sub1);
                ImGui::TextDisabled("%s", sub2);
                ImGui::TextDisabled("%s", sub3);
                ImGui::EndChild();
                ImGui::PopStyleColor();
            };

            // Card 1: Memory
            std::string peakStr = "Peak: " + formatBytes(stats.ramPeakWorkingSetBytes);
            std::string comStr = "Commit: " + formatBytes(stats.ramCommittedBytes);
            std::string vramStr = "VRAM: " + formatBytes(stats.vramEstimatedBytes);
            renderCard("Memory (RAM & VRAM)", formatBytes(stats.ramWorkingSetBytes).c_str(),
                       peakStr.c_str(), comStr.c_str(), vramStr.c_str(), ImVec4(0.35f, 0.75f, 1.0f, 1.0f));

            ImGui::SameLine();
            // Card 2: CPU & Framerate
            char cpuMain[64], fpsSub[64], dtSub[64], heapSub[64];
            std::snprintf(cpuMain, sizeof(cpuMain), "%4.1f %% CPU", stats.cpuUsagePercent);
            std::snprintf(fpsSub, sizeof(fpsSub), "Framerate: %5.1f FPS", stats.gameFps);
            std::snprintf(dtSub, sizeof(dtSub), "Frame Time: %5.2f ms", stats.gameFrameTimeMs);
            std::snprintf(heapSub, sizeof(heapSub), "Scene Heap: %s", formatBytes(stats.sceneHeapEstimatedBytes).c_str());
            renderCard("CPU & Framerate", cpuMain, fpsSub, dtSub, heapSub, ImVec4(0.40f, 0.90f, 0.50f, 1.0f));

            ImGui::SameLine();
            // Card 3: Disk I/O
            std::string rSpeed = "Read: " + formatSpeed(stats.diskReadBytesPerSec);
            std::string wSpeed = "Write: " + formatSpeed(stats.diskWriteBytesPerSec);
            std::string rTot = "Total R: " + formatBytes(stats.totalDiskReadBytes);
            renderCard("Storage & Disk I/O", formatSpeed(stats.diskReadBytesPerSec + stats.diskWriteBytesPerSec).c_str(),
                       rSpeed.c_str(), wSpeed.c_str(), rTot.c_str(), ImVec4(0.95f, 0.75f, 0.35f, 1.0f));

            ImGui::SameLine();
            // Card 4: Network Telemetry
            char netMain[64], netTxRx[64], netPkts[64], netPingPeers[64];
            std::snprintf(netMain, sizeof(netMain), "%s /s", formatSpeed(stats.netBytesSentPerSec + stats.netBytesRecvPerSec).c_str());
            std::snprintf(netTxRx, sizeof(netTxRx), "Tx: %s  Rx: %s", formatSpeed(stats.netBytesSentPerSec).c_str(), formatSpeed(stats.netBytesRecvPerSec).c_str());
            std::snprintf(netPkts, sizeof(netPkts), "Packets: %u Tx / %u Rx", stats.netPacketsSent, stats.netPacketsRecv);
            std::snprintf(netPingPeers, sizeof(netPingPeers), "Ping: %.1f ms | Peers: %u", stats.netPingMs, stats.netActivePeers);
            renderCard("Multiplayer Network", netMain, netTxRx, netPkts, netPingPeers, ImVec4(0.85f, 0.50f, 0.95f, 1.0f));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Summary Panels: Left = Scene & Objects, Right = Physics & Rendering
            float halfWidth = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;

            ImGui::BeginChild("OverviewSceneSummary", ImVec2(halfWidth, 180), true);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.75f, 1.0f, 1.0f));
            ImGui::Text("Scene Nodes & Entities");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Columns(2, "SceneSumCols", false);
            ImGui::Text("Total Scene Nodes:"); ImGui::NextColumn(); ImGui::Text("%u", stats.totalNodes); ImGui::NextColumn();
            ImGui::Text("Active Entities:"); ImGui::NextColumn(); ImGui::Text("%u", stats.activeEntities); ImGui::NextColumn();
            ImGui::Text("Mesh Nodes:"); ImGui::NextColumn(); ImGui::Text("%u", stats.meshNodes); ImGui::NextColumn();
            ImGui::Text("Light / Camera Nodes:"); ImGui::NextColumn(); ImGui::Text("%u / %u", stats.lightNodes, stats.cameraNodes); ImGui::NextColumn();
            ImGui::Text("Particle / Audio:"); ImGui::NextColumn(); ImGui::Text("%u / %u", stats.particleNodes, stats.audioNodes); ImGui::NextColumn();
            ImGui::Text("2D UI Canvas Elements:"); ImGui::NextColumn(); ImGui::Text("%u", stats.uiNodes); ImGui::NextColumn();
            ImGui::Text("Orphan Nodes:"); ImGui::NextColumn();
            if (stats.orphanNodeCount > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%u (Action Required!)", stats.orphanNodeCount);
            } else {
                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "0 (Healthy)");
            }
            ImGui::Columns(1);
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("OverviewSimSummary", ImVec2(halfWidth, 180), true);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.90f, 0.50f, 1.0f));
            ImGui::Text("Simulation & Rendering Telemetry");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Columns(2, "SimSumCols", false);
            ImGui::Text("Game Draw Calls:"); ImGui::NextColumn(); ImGui::Text("%u", stats.gameDrawCalls); ImGui::NextColumn();
            ImGui::Text("Game Triangles Count:"); ImGui::NextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(stats.gameTrianglesCount)); ImGui::NextColumn();
            ImGui::Text("Game Vertices Count:"); ImGui::NextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(stats.gameVerticesCount)); ImGui::NextColumn();
            ImGui::Text("Active Lights Evaluated:"); ImGui::NextColumn(); ImGui::Text("%u / 32", stats.activeLightsEvaluated); ImGui::NextColumn();
            ImGui::Text("Jolt Dynamic Bodies:"); ImGui::NextColumn(); ImGui::Text("%u", stats.activeDynamicBodies); ImGui::NextColumn();
            ImGui::Text("Jolt Static Bodies:"); ImGui::NextColumn(); ImGui::Text("%u", stats.staticBodies); ImGui::NextColumn();
            ImGui::Text("Grounded Characters:"); ImGui::NextColumn(); ImGui::Text("%u", stats.groundedCharacters); ImGui::NextColumn();
            ImGui::Columns(1);
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // --------------------------------------------------------------------
        // TAB 2: CPU PROFILER
        // --------------------------------------------------------------------
        if (ImGui::BeginTabItem("CPU Profiler")) {
            ImGui::Spacing();
            ImGui::Text("CPU Stage Execution Breakdown (Frame Time Budget)");

            // Visual Segmented Bar
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 barPos = ImGui::GetCursorScreenPos();
            float barWidth = ImGui::GetContentRegionAvail().x;
            float barHeight = 24.0f;

            // Background
            drawList->AddRectFilled(barPos, ImVec2(barPos.x + barWidth, barPos.y + barHeight), IM_COL32(28, 32, 40, 255), 4.0f);

            float curX = barPos.x;
            for (const auto& stage : cpuStages) {
                float segWidth = (stage.percentage / 100.0f) * barWidth;
                if (segWidth < 2.0f && stage.durationMs > 0.01f) segWidth = 2.0f;
                if (curX + segWidth > barPos.x + barWidth) segWidth = barPos.x + barWidth - curX;

                if (segWidth > 0.0f) {
                    ImVec2 p0(curX, barPos.y);
                    ImVec2 p1(curX + segWidth, barPos.y + barHeight);
                    drawList->AddRectFilled(p0, p1, stage.color, 2.0f);

                    // Check hover for tooltip
                    ImVec2 mouse = ImGui::GetMousePos();
                    if (mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y) {
                        ImGui::SetTooltip("%s\nDuration: %.2f ms (%.1f%% of frame budget)",
                                          stage.name.c_str(), stage.durationMs, stage.percentage);
                    }
                    curX += segWidth;
                }
            }
            ImGui::Dummy(ImVec2(barWidth, barHeight + 6.0f));

            // Stage Legend Chips
            for (size_t i = 0; i < cpuStages.size(); ++i) {
                const auto& stage = cpuStages[i];
                ImVec2 dotPos = ImGui::GetCursorScreenPos();
                drawList->AddCircleFilled(ImVec2(dotPos.x + 6, dotPos.y + 7), 5.0f, stage.color);
                ImGui::Dummy(ImVec2(14, 14));
                ImGui::SameLine();
                ImGui::Text("%s: %.2f ms", stage.name.c_str(), stage.durationMs);
                if (i + 1 < cpuStages.size() && (i % 3 != 2)) {
                    ImGui::SameLine(0, 16);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Rolling 120-Sample Frame Time History Graph
            ImGui::Text("Rolling Frame Time History (120 Frames) - Target: 16.6ms (60 FPS) / 8.3ms (120 FPS)");
            if (!frameHistory.empty()) {
                char overlay[64];
                std::snprintf(overlay, sizeof(overlay), "Current: %5.2f ms (%5.1f FPS)", stats.gameFrameTimeMs, stats.gameFps);
                ImGui::PlotLines("##FrameHistoryPlot", frameHistory.data(), static_cast<int>(frameHistory.size()), 0,
                                 overlay, 0.0f, 33.3f, ImVec2(-1, 85));
            }

            ImGui::Spacing();

            // CPU Stages Detailed Table
            if (ImGui::BeginTable("CPUStagesTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Simulation Stage", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Current (ms)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Min (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Frame %", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                ImGui::TableHeadersRow();

                for (const auto& stage : cpuStages) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImVec2 chipPos = ImGui::GetCursorScreenPos();
                    drawList->AddRectFilled(ImVec2(chipPos.x, chipPos.y + 3), ImVec2(chipPos.x + 10, chipPos.y + 15), stage.color, 2.0f);
                    ImGui::Dummy(ImVec2(14, 14));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(stage.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%5.2f ms", stage.durationMs);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%5.2f ms", (stage.minMs < 999.0f) ? stage.minMs : stage.durationMs);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%5.2f ms", stage.maxMs);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%5.2f ms", stage.avgMs);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%4.1f %%", stage.percentage);
                }
                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        // --------------------------------------------------------------------
        // TAB 3: GPU PROFILER
        // --------------------------------------------------------------------
        if (ImGui::BeginTabItem("GPU Profiler")) {
            ImGui::Spacing();
            ImGui::Text("GPU Time per Pass (measured with Vulkan timestamp queries)");
            DrawGpuTimings();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Game Camera Render Pipeline Telemetry:");
            ImGui::Columns(3, "GpuMetricsCols", true);
            ImGui::Text("Game Draw Calls:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%u", stats.gameDrawCalls);
            ImGui::Text("Game Triangles:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%llu", static_cast<unsigned long long>(stats.gameTrianglesCount));
            ImGui::Text("Game Vertices:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%llu", static_cast<unsigned long long>(stats.gameVerticesCount));
            ImGui::NextColumn();

            ImGui::Text("Active Shadow Depth Passes:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%u", stats.shadowPassesActive);
            ImGui::Text("Active Lights Evaluated:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%u / 32", stats.activeLightsEvaluated);
            ImGui::Text("Internal Viewport Resolution:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%.0f x %.0f", stats.gameViewportResolution.x, stats.gameViewportResolution.y);
            ImGui::NextColumn();

            ImGui::Text("Estimated VRAM Allocation:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.85f, 0.50f, 0.95f, 1.0f), "%s", formatBytes(stats.vramEstimatedBytes).c_str());
            ImGui::Text("Vulkan Render Target:"); ImGui::SameLine(); ImGui::TextDisabled("Offscreen RGBA8_UNORM");
            ImGui::Text("Depth / Stencil Target:"); ImGui::SameLine(); ImGui::TextDisabled("D32_SFLOAT");
            ImGui::Columns(1);

            ImGui::EndTabItem();
        }

        // --------------------------------------------------------------------
        // TAB 4: PHYSICS & SIMULATION
        // --------------------------------------------------------------------
        if (ImGui::BeginTabItem("Physics & Simulation")) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.90f, 0.50f, 1.0f));
            ImGui::Text("Jolt Physics 3D Simulation Subsystem");
            ImGui::PopStyleColor();
            ImGui::TextDisabled("Engine physics world stepped isolated in simulation space at 60Hz.");
            ImGui::Spacing();

            ImGui::Columns(2, "PhysicsCols", true);
            ImGui::Text("Active Dynamic Rigid Bodies:"); ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%u", stats.activeDynamicBodies); ImGui::NextColumn();

            ImGui::Text("Static Colliders / Bodies:"); ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%u", stats.staticBodies); ImGui::NextColumn();

            ImGui::Text("Active Constraints & Joints:"); ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%u (Fixed, Hinge, Distance, Swing-Twist)", stats.activeJoints); ImGui::NextColumn();

            ImGui::Text("Character Controllers Grounded:"); ImGui::NextColumn();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%u (Raycast floor contact active)", stats.groundedCharacters); ImGui::NextColumn();

            ImGui::Text("Jolt Physics Solver Execution:"); ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Multi-Threaded Jolt Job System Active"); ImGui::NextColumn();

            ImGui::Columns(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Optimization Guidelines:");
            ImGui::BulletText("Static geometry (buildings, terrain, platforms) should use RigidBodyType::Static for zero CPU solver overhead.");
            ImGui::BulletText("Complex meshes should use Convex Hull or simplified Box/Capsule/Sphere primitives for fast narrowphase SAT collisions.");
            ImGui::BulletText("Character Controllers automatically perform ground snapping and slope evaluation up to 45 degrees.");

            ImGui::EndTabItem();
        }

        // --------------------------------------------------------------------
        // TAB 5: SCENE DIAGNOSTICS & ORPHANS
        // --------------------------------------------------------------------
        if (ImGui::BeginTabItem("Scene Diagnostics & Orphans")) {
            ImGui::Spacing();

            // Orphan Detection & Repair Card
            if (stats.orphanNodeCount > 0) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.32f, 0.14f, 0.14f, 0.95f));
                ImGui::BeginChild("OrphanWarningBox", ImVec2(0, 110), true);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.35f, 1.0f));
                ImGui::Text("  [WARNING: %u ORPHAN NODE(S) DETECTED IN SCENE HIERARCHY]", stats.orphanNodeCount);
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::TextWrapped("Orphan nodes have a parent ID pointing to a missing or deleted entity. "
                                   "They will not inherit proper hierarchical transforms until repaired.");
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
                if (ImGui::Button("  1-Click Repair: Reparent All Orphans to Scene Root  ", ImVec2(380, 28))) {
                    profiler.ReparentOrphanNodesToRoot(gameLoop.GetPlayScene().get());
                }
                ImGui::PopStyleColor(2);
                ImGui::EndChild();
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::Text("Detected Orphan Nodes:");
                if (ImGui::BeginTable("OrphansTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Orphan Node ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                    ImGui::TableSetupColumn("Node Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Missing Parent ID", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& orphan : stats.orphanNodes) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%u", orphan.id);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(orphan.name.c_str());

                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%u (Missing)", orphan.missingParentId);

                        ImGui::TableSetColumnIndex(3);
                        std::string btnLabel = "Reparent##" + std::to_string(orphan.id);
                        if (ImGui::Button(btnLabel.c_str(), ImVec2(110, 20))) {
                            if (auto playScene = gameLoop.GetPlayScene()) {
                                auto n = playScene->GetNodeById(orphan.id);
                                if (n) n->parentId = 0;
                            }
                        }
                    }
                    ImGui::EndTable();
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.24f, 0.16f, 0.85f));
                ImGui::BeginChild("OrphansHealthyBox", ImVec2(0, 60), true);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.95f, 0.45f, 1.0f));
                ImGui::Text("  [SCENE HIERARCHY HEALTHY]");
                ImGui::PopStyleColor();
                ImGui::Text("  0 orphan nodes detected. All entity parent pointers resolve to existing scene nodes or root.");
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Scene Node Type Audit Table
            ImGui::Text("Scene Nodes Audit Breakdown:");
            if (ImGui::BeginTable("NodeAuditTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Entity Category / Type", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Active Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Notes & Diagnostics", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                auto addAuditRow = [](const char* name, uint32_t count, const char* note) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", count);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", note);
                };

                addAuditRow("3D Polygon Mesh Nodes", stats.meshNodes, "PBR textured and lit static / animated geometry");
                addAuditRow("Light Nodes (Dir, Point, Spot)", stats.lightNodes, "Dynamically contributing to PBR radiance and shadows");
                addAuditRow("Camera Nodes", stats.cameraNodes, "Active player camera viewport source");
                addAuditRow("3D / Spatial Audio Sources", stats.audioNodes, "Hardware playback with distance attenuation & reverb");
                addAuditRow("Particle Emitter Nodes", stats.particleNodes, "Real-time billboard / Niagara particle systems");
                addAuditRow("2D UI Canvas & HUD Nodes", stats.uiNodes, "Screenspace and worldspace interactive UI components");
                addAuditRow("Physics Collider Nodes", stats.colliderNodes, "Box, Sphere, and Capsule Jolt physics shapes");
                addAuditRow("Humanoid Character Nodes", stats.characterNodes, "NPCs with waypoints, dialogue, and kinematic controllers");
                addAuditRow("Terrain Landscape Nodes", stats.terrainNodes, "Procedurally sculpted heightmap surfaces");
                addAuditRow("Total Active Scene Entities", stats.activeEntities, "Isolated simulation scene entities");

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void EditorUI::RenderRulerOverlay(Scene& scene, Camera& camera, ImDrawList* drawList) {
    if (!drawList) return;

    // Check if there are any saved measurements to draw
    bool hasVisibleSaved = false;
    for (const auto& saved : m_rulerToolState.savedMeasurements) {
        if (saved.visible && saved.hasA && saved.hasB) {
            hasVisibleSaved = true;
            break;
        }
    }

    // If ruler is not active, clean up any interaction state
    if (!m_rulerToolState.active) {
        m_rulerToolState.isDragging = false;
        m_rulerToolState.draggingPoint = -1;
        m_rulerToolState.hasHoveredPoint = false;
        if (!m_rulerToolState.current.hasB) {
            m_rulerToolState.current.hasA = false;
        }
        if (!hasVisibleSaved) {
            return;
        }
    }

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = camera.GetProjectionMatrix();
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    auto ProjectPoint = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool {
        glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.001f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        screenPos.x = m_viewportPos.x + (ndc.x * 0.5f + 0.5f) * m_viewportSize.x;
        screenPos.y = m_viewportPos.y + (0.5f - ndc.y * 0.5f) * m_viewportSize.y;
        return true;
    };

    // Helper lambda to draw a complete measurement line with ticks, triad, badges
    auto DrawMeasurement = [&](const RulerItem& item, bool isLiveRubberband, const glm::vec3& rubberB) {
        glm::vec3 ptA = item.pointA;
        glm::vec3 ptB = isLiveRubberband ? rubberB : item.pointB;

        ImVec2 spA, spB;
        bool okA = ProjectPoint(ptA, spA);
        bool okB = ProjectPoint(ptB, spB);

        float dist = glm::distance(ptA, ptB);

        ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(item.color);
        ImU32 shadowColor = IM_COL32(10, 14, 20, 200);

        // 1. Draw Delta Triad (Bounding Box Orthogonal Dimensions)
        if (m_rulerToolState.showDeltaTriad && dist > 0.05f) {
            glm::vec3 pX(ptB.x, ptA.y, ptA.z);
            glm::vec3 pY(ptB.x, ptB.y, ptA.z);
            ImVec2 spX, spY;
            bool okX = ProjectPoint(pX, spX);
            bool okY = ProjectPoint(pY, spY);

            // X-leg (Red)
            if (okA && okX && std::abs(ptB.x - ptA.x) > 0.02f) {
                drawList->AddLine(spA, spX, IM_COL32(255, 80, 80, 180), 1.8f);
                ImVec2 spMidX((spA.x + spX.x) * 0.5f, (spA.y + spX.y) * 0.5f);
                std::string txt = "ΔX: " + m_rulerToolState.FormatDist(std::abs(ptB.x - ptA.x));
                drawList->AddText(spMidX, IM_COL32(255, 120, 120, 240), txt.c_str());
            }
            // Y-leg (Height - Green)
            if (okX && okY && std::abs(ptB.y - ptA.y) > 0.02f) {
                drawList->AddLine(spX, spY, IM_COL32(80, 245, 95, 180), 1.8f);
                ImVec2 spMidY((spX.x + spY.x) * 0.5f, (spX.y + spY.y) * 0.5f);
                std::string txt = "ΔY: " + m_rulerToolState.FormatDist(std::abs(ptB.y - ptA.y));
                drawList->AddText(spMidY, IM_COL32(110, 255, 125, 240), txt.c_str());
            }
            // Z-leg (Depth - Blue)
            if (okY && okB && std::abs(ptB.z - ptA.z) > 0.02f) {
                drawList->AddLine(spY, spB, IM_COL32(80, 160, 255, 180), 1.8f);
                ImVec2 spMidZ((spY.x + spB.x) * 0.5f, (spY.y + spB.y) * 0.5f);
                std::string txt = "ΔZ: " + m_rulerToolState.FormatDist(std::abs(ptB.z - ptA.z));
                drawList->AddText(spMidZ, IM_COL32(110, 185, 255, 240), txt.c_str());
            }
        }

        // 2. Draw Main 3D Ruler Line
        if (okA && okB) {
            float lineThickness = isLiveRubberband ? 2.0f : 3.0f;
            drawList->AddLine(spA, spB, shadowColor, lineThickness + 2.0f);
            drawList->AddLine(spA, spB, lineColor, lineThickness);

            // 3. Physical Ruler Tick Marks every 1m (and minor 0.25m / 0.5m)
            if (m_rulerToolState.showTicks && dist > 0.1f) {
                glm::vec3 dir = (ptB - ptA) / dist;
                float dx = spB.x - spA.x;
                float dy = spB.y - spA.y;
                float screenLen = std::hypot(dx, dy);
                if (screenLen > 10.0f) {
                    ImVec2 norm(-dy / screenLen, dx / screenLen);
                    float tickInterval = (dist <= 3.0f) ? 0.25f : ((dist <= 15.0f) ? 1.0f : 2.0f);
                    for (float d = tickInterval; d < dist - 0.05f; d += tickInterval) {
                        glm::vec3 tickPos = ptA + dir * d;
                        ImVec2 spTick;
                        if (ProjectPoint(tickPos, spTick)) {
                            bool isMajor = (std::fmod(d, 1.0f) < 0.01f || std::fmod(d, 1.0f) > 0.99f);
                            float tickLen = isMajor ? 8.0f : 4.5f;
                            ImVec2 tStart(spTick.x - norm.x * tickLen * 0.5f, spTick.y - norm.y * tickLen * 0.5f);
                            ImVec2 tEnd(spTick.x + norm.x * tickLen * 0.5f, spTick.y + norm.y * tickLen * 0.5f);
                            drawList->AddLine(tStart, tEnd, lineColor, isMajor ? 1.8f : 1.0f);

                            if (isMajor && screenLen > 80.0f) {
                                std::string tLabel = m_rulerToolState.FormatDist(d);
                                ImVec2 labelPos(tEnd.x + norm.x * 3.0f, tEnd.y + norm.y * 3.0f);
                                drawList->AddText(labelPos, IM_COL32(230, 230, 230, 200), tLabel.c_str());
                            }
                        }
                    }
                }
            }

            // 4. Midpoint Distance Badge
            glm::vec3 midWorld = (ptA + ptB) * 0.5f;
            ImVec2 spMid;
            if (ProjectPoint(midWorld, spMid)) {
                std::string badgeText = "📏 " + m_rulerToolState.FormatDist(dist);
                if (m_rulerToolState.showAngle) {
                    float horizDist = std::hypot(ptB.x - ptA.x, ptB.z - ptA.z);
                    float elevAngle = (horizDist > 0.0001f) ? glm::degrees(std::atan2(std::abs(ptB.y - ptA.y), horizDist)) : 90.0f;
                    char aBuf[32];
                    snprintf(aBuf, sizeof(aBuf), " (∠ %.1f°)", elevAngle);
                    badgeText += aBuf;
                }

                ImVec2 tSize = ImGui::CalcTextSize(badgeText.c_str());
                float px = 9.0f, py = 4.0f;
                ImVec2 bMin(spMid.x - tSize.x * 0.5f - px, spMid.y - tSize.y * 0.5f - py);
                ImVec2 bMax(spMid.x + tSize.x * 0.5f + px, spMid.y + tSize.y * 0.5f + py);

                drawList->AddRectFilled(bMin, bMax, IM_COL32(18, 24, 34, 235), 6.0f);
                drawList->AddRect(bMin, bMax, lineColor, 6.0f, 0, 1.5f);
                drawList->AddText(ImVec2(bMin.x + px, bMin.y + py), IM_COL32(255, 235, 120, 255), badgeText.c_str());
            }
        }

        // 5. Point A Handle
        if (okA) {
            drawList->AddCircleFilled(spA, 7.0f, IM_COL32(20, 26, 36, 230));
            drawList->AddCircleFilled(spA, 4.5f, IM_COL32(70, 210, 255, 255));
            drawList->AddCircle(spA, 7.0f, lineColor, 12, 1.8f);
            char aLabel[64];
            snprintf(aLabel, sizeof(aLabel), "A (%.1f, %.1f, %.1f)", ptA.x, ptA.y, ptA.z);
            drawList->AddText(ImVec2(spA.x + 9.0f, spA.y - 8.0f), IM_COL32(160, 230, 255, 240), aLabel);
        }

        // 6. Point B Handle
        if (okB && !isLiveRubberband) {
            drawList->AddCircleFilled(spB, 7.0f, IM_COL32(20, 26, 36, 230));
            drawList->AddCircleFilled(spB, 4.5f, IM_COL32(255, 130, 70, 255));
            drawList->AddCircle(spB, 7.0f, lineColor, 12, 1.8f);
            char bLabel[64];
            snprintf(bLabel, sizeof(bLabel), "B (%.1f, %.1f, %.1f)", ptB.x, ptB.y, ptB.z);
            drawList->AddText(ImVec2(spB.x + 9.0f, spB.y - 8.0f), IM_COL32(255, 185, 140, 240), bLabel);
        }
    };

    // If ruler is not active, draw ONLY saved measurements and return immediately
    if (!m_rulerToolState.active) {
        for (const auto& saved : m_rulerToolState.savedMeasurements) {
            if (!saved.visible || !saved.hasA || !saved.hasB) continue;
            DrawMeasurement(saved, false, glm::vec3(0.0f));
        }
        return;
    }

    // --- FROM HERE ON, RULER IS ACTIVE IN THE VIEWPORT ---
    ImGuiIO& io = ImGui::GetIO();
    float localX = io.MousePos.x - m_viewportPos.x;
    float localY = io.MousePos.y - m_viewportPos.y;
    bool isMouseInViewport = (m_viewportHovered && localX >= 0.0f && localX <= m_viewportSize.x && localY >= 0.0f && localY <= m_viewportSize.y);

    // Compute 3D ray for candidate cursor point
    glm::vec3 candidatePt(0.0f);
    bool hasCandidate = false;

    if (isMouseInViewport) {
        float ndcX = (2.0f * localX / m_viewportSize.x) - 1.0f;
        float ndcY = 1.0f - (2.0f * localY / m_viewportSize.y);

        glm::mat4 invVP = glm::inverse(proj * view);

        glm::vec4 nearPointNDC(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farPointNDC(ndcX, ndcY, 1.0f, 1.0f);

        glm::vec4 nearWorld = invVP * nearPointNDC;
        if (nearWorld.w != 0.0f) nearWorld /= nearWorld.w;

        glm::vec4 farWorld = invVP * farPointNDC;
        if (farWorld.w != 0.0f) farWorld /= farWorld.w;

        glm::vec3 rayOrigin = glm::vec3(nearWorld);
        glm::vec3 rayDir = glm::normalize(glm::vec3(farWorld - nearWorld));

        if (m_rulerToolState.snapToGeometry) {
            Physics::Ray ray{rayOrigin, rayDir};
            Physics::RayHit hit = Physics::Raycast(scene, ray, 2000.0f);
            if (hit.hit) {
                candidatePt = hit.point;
                hasCandidate = true;
                auto hitNode = scene.GetNodeById(hit.nodeId);
                if (hitNode) {
                    glm::vec3 nodeCenter = hitNode->GetWorldPosition(scene);
                    if (glm::distance(candidatePt, nodeCenter) < 0.65f) {
                        candidatePt = nodeCenter;
                    }
                }
            }
        }

        if (!hasCandidate) {
            if (std::abs(rayDir.y) > 0.0001f) {
                float t = -rayOrigin.y / rayDir.y;
                if (t > 0.0f && t < 2000.0f) {
                    candidatePt = rayOrigin + t * rayDir;
                    hasCandidate = true;
                }
            }
        }

        if (!hasCandidate) {
            candidatePt = rayOrigin + rayDir * camera.GetDistance();
            hasCandidate = true;
        }

        if (m_rulerToolState.snapToGrid) {
            candidatePt = RulerMath::SnapToGrid(candidatePt, m_rulerToolState.gridSnapStep);
        }

        m_rulerToolState.hoveredPoint = candidatePt;
        m_rulerToolState.hasHoveredPoint = true;
    } else {
        m_rulerToolState.hasHoveredPoint = false;
    }

    // Handle user clicks & drag interactions when ruler active
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (m_rulerToolState.current.hasA || m_rulerToolState.current.hasB) {
            m_rulerToolState.current.hasA = false;
            m_rulerToolState.current.hasB = false;
            m_rulerToolState.isDragging = false;
            m_rulerToolState.draggingPoint = -1;
        } else {
            SetRulerActive(false);
        }
    }

    int hoveredHandle = -1;
    if (m_rulerToolState.current.hasA) {
        ImVec2 spA;
        if (ProjectPoint(m_rulerToolState.current.pointA, spA)) {
            if (std::hypot(io.MousePos.x - spA.x, io.MousePos.y - spA.y) < 14.0f) {
                hoveredHandle = 0;
            }
        }
    }
    if (hoveredHandle == -1 && m_rulerToolState.current.hasB) {
        ImVec2 spB;
        if (ProjectPoint(m_rulerToolState.current.pointB, spB)) {
            if (std::hypot(io.MousePos.x - spB.x, io.MousePos.y - spB.y) < 14.0f) {
                hoveredHandle = 1;
            }
        }
    }

    if (m_rulerToolState.isDragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (hasCandidate) {
                if (m_rulerToolState.draggingPoint == 0) {
                    m_rulerToolState.current.pointA = candidatePt;
                } else if (m_rulerToolState.draggingPoint == 1) {
                    m_rulerToolState.current.pointB = candidatePt;
                }
            }
        } else {
            m_rulerToolState.isDragging = false;
            m_rulerToolState.draggingPoint = -1;
        }
    } else if (isMouseInViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        if (hoveredHandle != -1) {
            m_rulerToolState.isDragging = true;
            m_rulerToolState.draggingPoint = hoveredHandle;
        } else if (hasCandidate) {
            if (!m_rulerToolState.current.hasA) {
                m_rulerToolState.current.pointA = candidatePt;
                m_rulerToolState.current.hasA = true;
                m_rulerToolState.current.hasB = false;
            } else if (!m_rulerToolState.current.hasB) {
                m_rulerToolState.current.pointB = candidatePt;
                m_rulerToolState.current.hasB = true;
            } else {
                // Start fresh measurement
                m_rulerToolState.current.pointA = candidatePt;
                m_rulerToolState.current.hasA = true;
                m_rulerToolState.current.hasB = false;
            }
        }
    }

    // Draw all saved persistent measurements
    for (const auto& saved : m_rulerToolState.savedMeasurements) {
        if (!saved.visible || !saved.hasA || !saved.hasB) continue;
        DrawMeasurement(saved, false, glm::vec3(0.0f));
    }

    // Draw active measurement ONLY when ruler is active
    if (m_rulerToolState.current.hasA) {
        if (m_rulerToolState.current.hasB) {
            DrawMeasurement(m_rulerToolState.current, false, glm::vec3(0.0f));
        } else if (isMouseInViewport && hasCandidate) {
            // Live rubberband line while dragging or placing Point B
            DrawMeasurement(m_rulerToolState.current, true, candidatePt);
        } else {
            // Only Point A placed
            ImVec2 spA;
            if (ProjectPoint(m_rulerToolState.current.pointA, spA)) {
                drawList->AddCircleFilled(spA, 7.0f, IM_COL32(20, 26, 36, 230));
                drawList->AddCircleFilled(spA, 4.5f, IM_COL32(70, 210, 255, 255));
                drawList->AddCircle(spA, 7.0f, IM_COL32(255, 215, 60, 255), 12, 1.8f);
                char aLabel[64];
                snprintf(aLabel, sizeof(aLabel), "A (%.1f, %.1f, %.1f)", m_rulerToolState.current.pointA.x, m_rulerToolState.current.pointA.y, m_rulerToolState.current.pointA.z);
                drawList->AddText(ImVec2(spA.x + 9.0f, spA.y - 8.0f), IM_COL32(160, 230, 255, 240), aLabel);
            }
        }
    }

    // Hover cursor reticle when ruler is active
    if (isMouseInViewport && hasCandidate && !m_rulerToolState.isDragging) {
        ImVec2 spRet;
        if (ProjectPoint(candidatePt, spRet)) {
            drawList->AddCircle(spRet, 6.0f, IM_COL32(255, 220, 50, 220), 12, 1.5f);
            drawList->AddCircleFilled(spRet, 2.0f, IM_COL32(255, 255, 255, 240));
            if (!m_rulerToolState.current.hasA) {
                drawList->AddText(ImVec2(spRet.x + 8.0f, spRet.y + 6.0f), IM_COL32(255, 220, 70, 220), "Click to set [A]");
            }
        }
    }
}

void EditorUI::RenderRulerWindow(Scene& scene, Camera& camera) {
    if (!m_rulerToolState.showRulerWindow) return;

    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(TR("3D Ruler & Measurement Tool###RulerWindow"), &m_rulerToolState.showRulerWindow)) {
        ImGui::End();
        if (!m_rulerToolState.showRulerWindow) {
            SetRulerActive(false);
        }
        return;
    }

    // Status / Mode Toolbar
    bool isActive = m_rulerToolState.active;
    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.15f, 1.0f));
        if (ImGui::Button("● Ruler Tool ACTIVE in Viewport (Click to Turn OFF)", ImVec2(-1, 30))) {
            SetRulerActive(false);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
        if (ImGui::Button("○ Turn Ruler Tool ON in Viewport [M]", ImVec2(-1, 30))) {
            SetRulerActive(true);
        }
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Interactive 3D tape measure. Click in the Viewport to measure distances.");
    ImGui::Separator();
    ImGui::Spacing();

    // Snapping and Display Settings
    if (ImGui::CollapsingHeader("Snapping & Units", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Unit Selector
        const char* unitNames[] = { "Meters (m)", "Centimeters (cm)", "Millimeters (mm)", "Feet (ft)", "Inches (in)" };
        int currentUnitIdx = static_cast<int>(m_rulerToolState.unit);
        ImGui::Text("Measurement Unit:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("##RulerUnits", &currentUnitIdx, unitNames, IM_ARRAYSIZE(unitNames))) {
            m_rulerToolState.unit = static_cast<RulerUnit>(currentUnitIdx);
        }

        ImGui::Spacing();
        ImGui::Checkbox("Snap to 3D Surfaces / Objects", &m_rulerToolState.snapToGeometry);
        ImGui::SameLine(260.0f);
        ImGui::Checkbox("Show Delta Triad (ΔX, ΔY, ΔZ)", &m_rulerToolState.showDeltaTriad);

        ImGui::Checkbox("Snap to Grid", &m_rulerToolState.snapToGrid);
        if (m_rulerToolState.snapToGrid) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("##GridSnapStep", &m_rulerToolState.gridSnapStep, 0.1f, 2.0f, "Step: %.2f m");
        }
        ImGui::SameLine(260.0f);
        ImGui::Checkbox("Show Tick Marks", &m_rulerToolState.showTicks);

        ImGui::Checkbox("Show Angles (Pitch & Compass)", &m_rulerToolState.showAngle);
    }

    ImGui::Spacing();

    // Active Measurement Card
    if (ImGui::CollapsingHeader("Active Measurement", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& cur = m_rulerToolState.current;

        if (cur.hasA && cur.hasB) {
            float totalDist = RulerMath::CalculateDistance(cur.pointA, cur.pointB);
            glm::vec3 delta = RulerMath::CalculateDelta(cur.pointA, cur.pointB);
            float deltaX = delta.x;
            float deltaY = delta.y;
            float deltaZ = delta.z;
            float horizDist = RulerMath::CalculateHorizontalDistance(cur.pointA, cur.pointB);
            float elevAngle = RulerMath::CalculateElevationAngle(cur.pointA, cur.pointB);
            float heading = RulerMath::CalculateHeading(cur.pointA, cur.pointB);
            const char* compassStr = RulerMath::GetCompassDirection(heading);

            // Large Hero Readout
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.16f, 0.22f, 1.0f));
            ImGui::BeginChild("##HeroDistanceCard", ImVec2(-1, 95), true);
            ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.0f, 1.0f), "TOTAL 3D DISTANCE:");
            ImGui::SetWindowFontScale(1.45f);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "  %s", m_rulerToolState.FormatDist(totalDist).c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextDisabled("Horizontal (XZ): %s  |  Height (ΔY): %s  |  Elevation: %.1f°  |  Heading: %.1f° (%s)",
                m_rulerToolState.FormatDist(horizDist).c_str(),
                m_rulerToolState.FormatDist(deltaY).c_str(),
                elevAngle, heading, compassStr);
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Columns(3, "DeltaColumns", false);
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Width (ΔX):");
            ImGui::Text("%s", m_rulerToolState.FormatDist(deltaX).c_str());
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "Height (ΔY):");
            ImGui::Text("%s", m_rulerToolState.FormatDist(deltaY).c_str());
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.45f, 0.70f, 1.0f, 1.0f), "Depth (ΔZ):");
            ImGui::Text("%s", m_rulerToolState.FormatDist(deltaZ).c_str());
            ImGui::Columns(1);
        } else if (cur.hasA) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Point A placed. Click in viewport to place Point B.");
        } else {
            ImGui::TextDisabled("No points placed yet. Click in the 3D viewport or use the buttons below.");
        }

        ImGui::Spacing();
        ImGui::Text("Point A Position:");
        ImGui::DragFloat3("##PointA", &cur.pointA.x, 0.05f);
        if (ImGui::IsItemEdited()) cur.hasA = true;

        ImGui::Text("Point B Position:");
        ImGui::DragFloat3("##PointB", &cur.pointB.x, 0.05f);
        if (ImGui::IsItemEdited()) cur.hasB = true;

        ImGui::Spacing();
        if (ImGui::Button("Swap A <-> B", ImVec2(120, 24))) {
            std::swap(cur.pointA, cur.pointB);
            std::swap(cur.hasA, cur.hasB);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Measurement [Esc]", ImVec2(170, 24))) {
            cur.hasA = false;
            cur.hasB = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save to List", ImVec2(120, 24))) {
            if (cur.hasA && cur.hasB) {
                RulerItem saved = cur;
                saved.name = "Measure " + std::to_string(m_rulerToolState.savedMeasurements.size() + 1);
                m_rulerToolState.savedMeasurements.push_back(saved);
            }
        }
    }

    // Node-to-Node Quick Measurement
    if (ImGui::CollapsingHeader("Measure Between Scene Nodes", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto sel = scene.GetSelectedNode();
        if (sel) {
            glm::vec3 selPos = sel->GetWorldPosition(scene);
            ImGui::Text("Selected Node: \"%s\" at (%.2f, %.2f, %.2f)", sel->name.c_str(), selPos.x, selPos.y, selPos.z);
            ImGui::Spacing();
            if (ImGui::Button("Set Point A = Selected Node")) {
                m_rulerToolState.current.pointA = selPos;
                m_rulerToolState.current.hasA = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Set Point B = Selected Node")) {
                m_rulerToolState.current.pointB = selPos;
                m_rulerToolState.current.hasB = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Measure Node to Origin (0,0,0)")) {
                m_rulerToolState.current.pointA = selPos;
                m_rulerToolState.current.pointB = glm::vec3(0.0f);
                m_rulerToolState.current.hasA = true;
                m_rulerToolState.current.hasB = true;
            }
        } else {
            ImGui::TextDisabled("Select any node in the Scene Hierarchy or Viewport to measure its position or distance.");
        }
    }

    ImGui::Spacing();

    // Saved Measurements List
    if (ImGui::CollapsingHeader("Saved Measurements", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_rulerToolState.savedMeasurements.empty()) {
            ImGui::TextDisabled("No measurements saved yet. Click 'Save to List' above to pin a measurement.");
        } else {
            if (ImGui::BeginTable("SavedRulersTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Vis", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                int toDelete = -1;
                for (size_t i = 0; i < m_rulerToolState.savedMeasurements.size(); ++i) {
                    auto& item = m_rulerToolState.savedMeasurements[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(static_cast<int>(i));
                    char nBuf[64];
                    strncpy(nBuf, item.name.c_str(), sizeof(nBuf));
                    nBuf[sizeof(nBuf) - 1] = '\0';
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##RName", nBuf, sizeof(nBuf))) {
                        item.name = nBuf;
                    }

                    ImGui::TableSetColumnIndex(1);
                    float d = glm::distance(item.pointA, item.pointB);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "%s", m_rulerToolState.FormatDist(d).c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Checkbox("##RVis", &item.visible);

                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::Button("Del")) {
                        toDelete = static_cast<int>(i);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();

                if (toDelete >= 0 && toDelete < static_cast<int>(m_rulerToolState.savedMeasurements.size())) {
                    m_rulerToolState.savedMeasurements.erase(m_rulerToolState.savedMeasurements.begin() + toDelete);
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Clear All Saved Measurements")) {
                m_rulerToolState.savedMeasurements.clear();
            }
        }
    }

    ImGui::End();
    if (!m_rulerToolState.showRulerWindow) {
        SetRulerActive(false);
    }
}


void EditorUI::RenderCutsceneSequencerWindow(Scene& scene, Camera& camera, Gameplay::GameLoop& gameLoop) {
    if (!m_showCutsceneWindow) return;

    ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(TR("Cutscene & Cinematic Sequencer###CutsceneWindow"), &m_showCutsceneWindow)) {
        ImGui::End();
        return;
    }

    Scene& activeScene = gameLoop.GetActivePlayScene(scene);
    auto& cutscenes = scene.GetCutscenes();
    auto& csPlayer = gameLoop.GetCutscenePlayer();

    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), "Cinematic Cutscene Sequencer");
    ImGui::SameLine();
    ImGui::TextDisabled("| Real-time in-engine character actions, camera shots, audio/subtitles, location triggers & camera effects");

    ImGui::Spacing();

    // Top Quick-Action Bar
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.52f, 0.32f, 1.0f));
    if (ImGui::Button("+ Create Demo Cinematic Cutscene", ImVec2(240, 28))) {
        // Find an existing CharacterNode or MeshNode, or create a CharacterNode if none exists
        std::shared_ptr<Node> actorNode = nullptr;
        for (const auto& n : scene.GetNodes()) {
            if (n && n->nodeType == NodeType::Character) { actorNode = n; break; }
        }
        if (!actorNode) {
            for (const auto& n : scene.GetNodes()) {
                if (n && n->nodeType == NodeType::Mesh && !n->isOcean) { actorNode = n; break; }
            }
        }
        if (!actorNode) {
            actorNode = scene.AddCharacterNode("HeroCharacter");
            actorNode->position = glm::vec3(-3.0f, 0.0f, 2.0f);
        }

        Animation::CutsceneSequence demo;
        demo.name = "ArrivalCutscene";
        demo.duration = 6.0f;
        demo.skippable = true;
        demo.silentAudio = false;
        demo.letterboxAmount = 0.12f;
        demo.colorFilter = Animation::CutsceneColorFilter::Sepia;
        demo.overrideVignette = true;
        demo.vignetteAmount = 0.50f;
        demo.filmGrainAmount = 0.10f;
        demo.enableDof = true;
        demo.autoFocusOnLookAt = true;
        demo.dofAperture = 0.35f;

        glm::vec3 startP = actorNode->position;
        glm::vec3 targetP = startP + glm::vec3(3.0f, 0.0f, -2.0f);

        // Location trigger at targetP (or current position if testing immediately)
        demo.triggerOnLocation = true;
        demo.triggerActorName = "*";
        demo.triggerPosition = targetP;
        demo.triggerRadius = 2.5f;
        demo.triggerOnce = true;

        // Shot 1: Wide establishing dolly shot (0s -> 3s)
        Animation::CutsceneCameraShot shot1;
        shot1.startTime = 0.0f;
        shot1.duration = 3.0f;
        shot1.startPos = startP + glm::vec3(-4.0f, 2.6f, 5.5f);
        shot1.endPos = targetP + glm::vec3(-2.5f, 2.0f, 4.2f);
        shot1.lookAtNodeName = actorNode->name;
        shot1.lookAtOffset = glm::vec3(0.0f, 1.0f, 0.0f);
        shot1.startFov = 58.0f;
        shot1.endFov = 46.0f;
        shot1.smoothBlend = true;
        demo.cameraShots.push_back(shot1);

        // Shot 2: Close-up dramatic push-in (3s -> 6s)
        Animation::CutsceneCameraShot shot2;
        shot2.startTime = 3.0f;
        shot2.duration = 3.0f;
        shot2.startPos = targetP + glm::vec3(1.8f, 1.5f, 2.8f);
        shot2.endPos = targetP + glm::vec3(1.0f, 1.35f, 1.9f);
        shot2.lookAtNodeName = actorNode->name;
        shot2.lookAtOffset = glm::vec3(0.0f, 1.1f, 0.0f);
        shot2.startFov = 45.0f;
        shot2.endFov = 35.0f;
        shot2.smoothBlend = true;
        demo.cameraShots.push_back(shot2);

        // Character walks to target mark during Shot 1, then idles during Shot 2
        Animation::CutsceneActorAction act;
        act.actorNodeName = actorNode->name;
        act.startTime = 0.0f;
        act.duration = 3.2f;
        act.useCurrentStartPos = true;
        act.startPos = startP;
        act.endPos = targetP;
        act.animState = "Walk";
        act.endAnimState = "Idle";
        act.faceMovementDirection = true;
        demo.actorActions.push_back(act);

        // Subtitles & Audio cues
        Animation::CutsceneSubtitleCue cue1;
        cue1.startTime = 0.2f;
        cue1.duration = 2.6f;
        cue1.speaker = actorNode->name;
        cue1.text = "We finally reached the ancient sector...";
        cue1.synthFrequencyHz = 330.0f;
        cue1.audioVolume = 0.65f;
        demo.subtitleCues.push_back(cue1);

        Animation::CutsceneSubtitleCue cue2;
        cue2.startTime = 3.1f;
        cue2.duration = 2.7f;
        cue2.speaker = actorNode->name;
        cue2.text = "Stay alert. Something has changed here.";
        cue2.synthFrequencyHz = 440.0f;
        cue2.audioVolume = 0.65f;
        demo.subtitleCues.push_back(cue2);

        scene.AddCutscene(demo);
        if (&activeScene != &scene) {
            activeScene.AddCutscene(demo);
        }
        m_selectedCutsceneIdx = static_cast<int>(scene.GetCutscenes().size()) - 1;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("+ New Empty Cutscene", ImVec2(160, 28))) {
        Animation::CutsceneSequence seq;
        seq.name = "Cutscene_" + std::to_string(cutscenes.size() + 1);
        scene.AddCutscene(seq);
        if (&activeScene != &scene) activeScene.AddCutscene(seq);
        m_selectedCutsceneIdx = static_cast<int>(cutscenes.size()) - 1;
    }

    if (cutscenes.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("No cutscenes in this scene yet. Click '+ Create Demo Cinematic Cutscene' above to generate a complete skippable scene!");
        ImGui::End();
        return;
    }

    m_selectedCutsceneIdx = std::clamp(m_selectedCutsceneIdx, 0, static_cast<int>(cutscenes.size()) - 1);
    auto& seq = cutscenes[m_selectedCutsceneIdx];

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##SelectCutscene", seq.name.c_str())) {
        for (int i = 0; i < static_cast<int>(cutscenes.size()); ++i) {
            bool isSel = (i == m_selectedCutsceneIdx);
            if (ImGui::Selectable((cutscenes[i].name + "##cs" + std::to_string(i)).c_str(), isSel)) {
                m_selectedCutsceneIdx = i;
            }
            if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.68f, 0.22f, 0.22f, 1.0f));
    if (ImGui::Button("Delete", ImVec2(70, 28))) {
        if (csPlayer.IsPlaying()) csPlayer.Stop(activeScene, false);
        std::string toRemove = seq.name;
        scene.RemoveCutscene(toRemove);
        if (&activeScene != &scene) activeScene.RemoveCutscene(toRemove);
        m_selectedCutsceneIdx = std::max(0, m_selectedCutsceneIdx - 1);
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Playback Transport Bar
    if (!csPlayer.IsPlaying()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.58f, 0.85f, 1.0f));
        if (ImGui::Button("> Play / Preview Cutscene Now", ImVec2(230, 30))) {
            if (&activeScene != &scene) {
                activeScene.AddCutscene(seq);
            }
            csPlayer.Start(activeScene, seq.name);
            m_showCameraPreviewWindow = true;
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
        if (ImGui::Button(">> Skip Cutscene [Space / Enter]", ImVec2(230, 30))) {
            csPlayer.Skip(activeScene, true);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.22f, 0.22f, 1.0f));
        if (ImGui::Button("Stop [X]", ImVec2(90, 30))) {
            csPlayer.Stop(activeScene, false);
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    float progress = (csPlayer.IsPlaying() && csPlayer.GetDuration() > 0.0f)
        ? (csPlayer.GetPlaybackTime() / csPlayer.GetDuration()) : 0.0f;
    char progOverlay[96];
    if (csPlayer.IsPlaying()) {
        snprintf(progOverlay, sizeof(progOverlay), "PLAYING: %s (%.2fs / %.2fs)",
                 csPlayer.GetActiveCutsceneName().c_str(), csPlayer.GetPlaybackTime(), csPlayer.GetDuration());
    } else {
        snprintf(progOverlay, sizeof(progOverlay), "Ready (Duration: %.1fs)", seq.duration);
    }
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 30.0f), progOverlay);

    ImGui::Spacing();

    // 1. General & Audio Settings
    if (ImGui::CollapsingHeader("1. General, Skippable & Audio Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        char nameBuf[128];
        strncpy(nameBuf, seq.name.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Cutscene Name", nameBuf, sizeof(nameBuf))) {
            seq.name = nameBuf;
        }
        ImGui::SliderFloat("Total Duration (s)", &seq.duration, 1.0f, 120.0f, "%.1f s");
        ImGui::Checkbox("Skippable (Space / Enter / Backspace / On-Screen Skip Button)", &seq.skippable);
        ImGui::SameLine();
        ImGui::Checkbox("Silent Mode (Mute Audio Cues)", &seq.silentAudio);
        ImGui::Checkbox("Restore Gameplay Camera on End", &seq.restoreCameraOnEnd);
        ImGui::SliderFloat("Widescreen Letterbox Bars", &seq.letterboxAmount, 0.0f, 0.25f, "%.2f");
    }

    // 2. Location / Proximity Trigger ("Character goes to a location and an animated scene plays")
    if (ImGui::CollapsingHeader("2. Location Trigger (Play When Character Reaches Location)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Trigger Automatically When Character Reaches Location", &seq.triggerOnLocation);
        if (seq.triggerOnLocation) {
            char actorBuf[128];
            strncpy(actorBuf, seq.triggerActorName.c_str(), sizeof(actorBuf));
            actorBuf[sizeof(actorBuf) - 1] = '\0';
            if (ImGui::InputText("Trigger Character Name ('*' = Any Character/Player)", actorBuf, sizeof(actorBuf))) {
                seq.triggerActorName = actorBuf;
            }
            ImGui::DragFloat3("Trigger World Position (X, Y, Z)", &seq.triggerPosition.x, 0.2f);
            if (auto sel = scene.GetSelectedNode()) {
                ImGui::SameLine();
                if (ImGui::Button("Use Selected Node Pos")) {
                    seq.triggerPosition = sel->GetWorldPosition(scene);
                }
            }
            ImGui::SliderFloat("Trigger Radius (m)", &seq.triggerRadius, 0.5f, 25.0f, "%.1f m");
            ImGui::Checkbox("Trigger Once Per Play Session", &seq.triggerOnce);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset Trigger State")) {
                seq.runtimeHasTriggered = false;
                csPlayer.ResetTriggers(activeScene);
            }
        }
    }

    // 3. Camera Effects & Color Grading (Vignette, Sepia, Black & White, Noir, DOF, Grain)
    if (ImGui::CollapsingHeader("3. Camera Effects (Vignette, Sepia, Black & White, DOF, Film Grain)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* filterNames[] = {
            "None (Scene Default)",
            "Sepia Tone",
            "Black & White (Monochrome)",
            "Film Noir (High-Contrast B&W)",
            "Vintage 35mm Warm",
            "Cold Moonlight / Thriller",
            "Dreamy Flashback (Soft Bloom)"
        };
        int curFilter = static_cast<int>(seq.colorFilter);
        if (ImGui::Combo("Color Grading Filter", &curFilter, filterNames, IM_ARRAYSIZE(filterNames))) {
            seq.colorFilter = static_cast<Animation::CutsceneColorFilter>(curFilter);
        }

        // Quick 1-click preset pills
        ImGui::TextDisabled("Quick Filter Presets:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Normal")) seq.colorFilter = Animation::CutsceneColorFilter::None;
        ImGui::SameLine();
        if (ImGui::SmallButton("Sepia")) seq.colorFilter = Animation::CutsceneColorFilter::Sepia;
        ImGui::SameLine();
        if (ImGui::SmallButton("Black & White")) seq.colorFilter = Animation::CutsceneColorFilter::BlackAndWhite;
        ImGui::SameLine();
        if (ImGui::SmallButton("Film Noir")) seq.colorFilter = Animation::CutsceneColorFilter::NoirHighContrast;
        ImGui::SameLine();
        if (ImGui::SmallButton("Vintage Warm")) seq.colorFilter = Animation::CutsceneColorFilter::VintageWarm;
        ImGui::SameLine();
        if (ImGui::SmallButton("Cold Moonlight")) seq.colorFilter = Animation::CutsceneColorFilter::ColdMoonlight;

        ImGui::Checkbox("Override Vignette", &seq.overrideVignette);
        if (seq.overrideVignette) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderFloat("Vignette Strength", &seq.vignetteAmount, 0.0f, 1.0f, "%.2f");
        }
        ImGui::SliderFloat("Film Grain", &seq.filmGrainAmount, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Chromatic Aberration", &seq.chromaticAberrationAmount, 0.0f, 1.0f, "%.2f");

        ImGui::Checkbox("Enable Cinematic Depth of Field (DOF)", &seq.enableDof);
        if (seq.enableDof) {
            ImGui::SameLine();
            ImGui::Checkbox("Auto-Focus on Camera Target", &seq.autoFocusOnLookAt);
            if (!seq.autoFocusOnLookAt) {
                ImGui::SliderFloat("Focus Distance (m)", &seq.dofFocusDistance, 0.5f, 50.0f, "%.1f m");
            }
            ImGui::SliderFloat("Lens Aperture Blur", &seq.dofAperture, 0.05f, 1.0f, "%.2f");
        }
    }

    // 4. Camera Shots
    if (ImGui::CollapsingHeader("4. Camera Shots & Dolly Cuts", ImGuiTreeNodeFlags_DefaultOpen)) {
        int shotToDelete = -1;
        for (int i = 0; i < static_cast<int>(seq.cameraShots.size()); ++i) {
            auto& shot = seq.cameraShots[i];
            ImGui::PushID(i);
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Shot #%d (%.1fs -> %.1fs)", i + 1, shot.startTime, shot.startTime + shot.duration);
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete Shot")) shotToDelete = i;

            ImGui::DragFloat("Start Time (s)", &shot.startTime, 0.1f, 0.0f, 120.0f);
            ImGui::DragFloat("Duration (s)", &shot.duration, 0.1f, 0.1f, 120.0f);
            ImGui::DragFloat3("Start Camera Pos", &shot.startPos.x, 0.2f);
            ImGui::SameLine();
            if (ImGui::SmallButton("From Editor Cam##S")) {
                shot.startPos = camera.GetPosition();
                shot.startLookAt = camera.GetTarget();
            }
            ImGui::DragFloat3("End Camera Pos", &shot.endPos.x, 0.2f);
            ImGui::SameLine();
            if (ImGui::SmallButton("From Editor Cam##E")) {
                shot.endPos = camera.GetPosition();
                shot.endLookAt = camera.GetTarget();
            }

            char trackBuf[128];
            strncpy(trackBuf, shot.lookAtNodeName.c_str(), sizeof(trackBuf));
            trackBuf[sizeof(trackBuf) - 1] = '\0';
            if (ImGui::InputText("Track Target Node Name (Optional)", trackBuf, sizeof(trackBuf))) {
                shot.lookAtNodeName = trackBuf;
            }
            if (shot.lookAtNodeName.empty()) {
                ImGui::DragFloat3("Start Look-At Target", &shot.startLookAt.x, 0.2f);
                ImGui::DragFloat3("End Look-At Target", &shot.endLookAt.x, 0.2f);
            } else {
                ImGui::DragFloat3("Look-At Height/Offset", &shot.lookAtOffset.x, 0.1f);
            }
            ImGui::SliderFloat("Start FOV", &shot.startFov, 15.0f, 110.0f, "%.1f deg");
            ImGui::SliderFloat("End FOV", &shot.endFov, 15.0f, 110.0f, "%.1f deg");
            ImGui::Checkbox("Smooth Cubic Ease-In / Ease-Out", &shot.smoothBlend);
            ImGui::PopID();
        }
        if (shotToDelete >= 0) {
            seq.cameraShots.erase(seq.cameraShots.begin() + shotToDelete);
        }
        if (ImGui::Button("+ Add Camera Shot")) {
            Animation::CutsceneCameraShot s;
            if (!seq.cameraShots.empty()) {
                s.startTime = seq.cameraShots.back().startTime + seq.cameraShots.back().duration;
            }
            s.startPos = camera.GetPosition();
            s.endPos = camera.GetPosition() + glm::vec3(0.0f, 0.0f, -2.0f);
            s.startLookAt = camera.GetTarget();
            s.endLookAt = camera.GetTarget();
            seq.cameraShots.push_back(s);
        }
    }

    // 5. Character Movement & Animation Actions
    if (ImGui::CollapsingHeader("5. Character Walk-to-Mark & Animation Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        int actToDelete = -1;
        for (int i = 0; i < static_cast<int>(seq.actorActions.size()); ++i) {
            auto& act = seq.actorActions[i];
            ImGui::PushID(1000 + i);
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Character Action #%d (%s)", i + 1, act.actorNodeName.empty() ? "Unassigned" : act.actorNodeName.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete Action")) actToDelete = i;

            char actorBuf[128];
            strncpy(actorBuf, act.actorNodeName.c_str(), sizeof(actorBuf));
            actorBuf[sizeof(actorBuf) - 1] = '\0';
            if (ImGui::InputText("Actor Node Name", actorBuf, sizeof(actorBuf))) {
                act.actorNodeName = actorBuf;
            }
            if (auto sel = scene.GetSelectedNode()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Use Selected Node")) {
                    act.actorNodeName = sel->name;
                }
            }
            ImGui::DragFloat("Start Time (s)", &act.startTime, 0.1f, 0.0f, 120.0f);
            ImGui::DragFloat("Duration (s)", &act.duration, 0.1f, 0.1f, 120.0f);
            ImGui::Checkbox("Start Walk From Character's Live Position", &act.useCurrentStartPos);
            if (!act.useCurrentStartPos) {
                ImGui::DragFloat3("Explicit Start Position", &act.startPos.x, 0.2f);
            }
            ImGui::DragFloat3("Target End Mark (X, Y, Z)", &act.endPos.x, 0.2f);

            char animBuf[64], endAnimBuf[64];
            strncpy(animBuf, act.animState.c_str(), sizeof(animBuf)); animBuf[sizeof(animBuf) - 1] = '\0';
            strncpy(endAnimBuf, act.endAnimState.c_str(), sizeof(endAnimBuf)); endAnimBuf[sizeof(endAnimBuf) - 1] = '\0';
            if (ImGui::InputText("Action Animation Clip", animBuf, sizeof(animBuf))) act.animState = animBuf;
            if (ImGui::InputText("Finish Animation Clip", endAnimBuf, sizeof(endAnimBuf))) act.endAnimState = endAnimBuf;
            ImGui::Checkbox("Auto-Rotate Character to Face Movement Direction", &act.faceMovementDirection);
            ImGui::PopID();
        }
        if (actToDelete >= 0) {
            seq.actorActions.erase(seq.actorActions.begin() + actToDelete);
        }
        if (ImGui::Button("+ Add Character Action")) {
            Animation::CutsceneActorAction a;
            if (auto sel = scene.GetSelectedNode()) {
                a.actorNodeName = sel->name;
                a.startPos = sel->position;
                a.endPos = sel->position + glm::vec3(2.0f, 0.0f, 0.0f);
            }
            seq.actorActions.push_back(a);
        }
    }

    // 6. Subtitles & Audio Cues
    if (ImGui::CollapsingHeader("6. Subtitles & Voice / Audio Cues", ImGuiTreeNodeFlags_DefaultOpen)) {
        int cueToDelete = -1;
        for (int i = 0; i < static_cast<int>(seq.subtitleCues.size()); ++i) {
            auto& cue = seq.subtitleCues[i];
            ImGui::PushID(2000 + i);
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "Cue #%d (%.1fs -> %.1fs)", i + 1, cue.startTime, cue.startTime + cue.duration);
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete Cue")) cueToDelete = i;

            ImGui::DragFloat("Start Time (s)", &cue.startTime, 0.1f, 0.0f, 120.0f);
            ImGui::DragFloat("Duration (s)", &cue.duration, 0.1f, 0.1f, 120.0f);

            char spkBuf[64], txtBuf[256], wavBuf[256];
            strncpy(spkBuf, cue.speaker.c_str(), sizeof(spkBuf)); spkBuf[sizeof(spkBuf) - 1] = '\0';
            strncpy(txtBuf, cue.text.c_str(), sizeof(txtBuf)); txtBuf[sizeof(txtBuf) - 1] = '\0';
            strncpy(wavBuf, cue.audioClipPath.c_str(), sizeof(wavBuf)); wavBuf[sizeof(wavBuf) - 1] = '\0';
            if (ImGui::InputText("Speaker", spkBuf, sizeof(spkBuf))) cue.speaker = spkBuf;
            if (ImGui::InputText("Subtitle Text", txtBuf, sizeof(txtBuf))) cue.text = txtBuf;
            if (ImGui::InputText("Audio Clip (.wav path, empty = synth/silent)", wavBuf, sizeof(wavBuf))) cue.audioClipPath = wavBuf;
            ImGui::SliderFloat("Audio Volume", &cue.audioVolume, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Procedural Voice Tone (Hz, 0 = off)", &cue.synthFrequencyHz, 0.0f, 880.0f, "%.0f Hz");
            ImGui::PopID();
        }
        if (cueToDelete >= 0) {
            seq.subtitleCues.erase(seq.subtitleCues.begin() + cueToDelete);
        }
        if (ImGui::Button("+ Add Subtitle / Audio Cue")) {
            Animation::CutsceneSubtitleCue c;
            if (!seq.subtitleCues.empty()) {
                c.startTime = seq.subtitleCues.back().startTime + seq.subtitleCues.back().duration;
            }
            c.speaker = "Character";
            c.text = "New cinematic dialogue line...";
            seq.subtitleCues.push_back(c);
        }
    }

    // Keep active Play Scene's cutscene copy synced if editing live during Play Mode
    if (&activeScene != &scene) {
        activeScene.AddCutscene(seq);
    }

    ImGui::End();
}


void EditorUI::RenderGameLocalizationWindow() {
    if (!m_showGameLocalizationWindow) return;

    auto& gameLoc = GameLocalization::Get();
    const auto& languages = gameLoc.GetAvailableLanguages();

    // Verify selected language exists
    bool hasSelected = false;
    for (const auto& l : languages) {
        if (l.code == m_selectedLocLang) {
            hasSelected = true;
            break;
        }
    }
    if (!hasSelected && !languages.empty()) {
        m_selectedLocLang = gameLoc.GetCurrentLanguage();
    }

    ImGui::SetNextWindowSize(ImVec2(880, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(TR("Game Localization Editor###GameLocalizationEditor"), &m_showGameLocalizationWindow)) {
        ImGui::End();
        return;
    }

    // Top Header: Title and Folder Info
    std::string locDir = gameLoc.GetLocalizationDirectory();
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s", TR("Game Localization & Translation Manager"));
    ImGui::SameLine();
    ImGui::TextDisabled("| %s: %s", TR("Path"), locDir.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Toolbar Row
    ImGui::Text("%s", TR("Editing Language:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    std::string currentLabel = m_selectedLocLang + " (" + gameLoc.GetLanguageDisplayName(m_selectedLocLang) + ")";
    if (ImGui::BeginCombo("##GameLocLangCombo", currentLabel.c_str())) {
        for (const auto& lang : languages) {
            bool isSelected = (m_selectedLocLang == lang.code);
            std::string itemText = lang.code + " (" + lang.displayName + ")";
            if (ImGui::Selectable(itemText.c_str(), isSelected)) {
                m_selectedLocLang = lang.code;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button(TR("+ New Language"))) {
        m_showAddLanguagePopup = !m_showAddLanguagePopup;
    }

    ImGui::SameLine();
    if (ImGui::Button(TR("Save All Files"))) {
        gameLoc.SaveAll();
        m_locStatusMessage = TR("All translation files successfully saved to disk!");
        m_locStatusTimer = 3.5f;
    }

    ImGui::SameLine();
    if (ImGui::Button(TR("Reload"))) {
        gameLoc.Reload();
        m_locStatusMessage = TR("Translations reloaded from disk.");
        m_locStatusTimer = 2.0f;
    }

    // Status message toast
    if (m_locStatusTimer > 0.0f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.95f, 0.45f, 1.0f), "  %s", m_locStatusMessage.c_str());
        m_locStatusTimer -= ImGui::GetIO().DeltaTime;
    }

    // Add Language Drawer/Form
    if (m_showAddLanguagePopup) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.22f, 0.28f, 0.8f));
        ImGui::BeginChild("##AddLangDrawer", ImVec2(-1, 80), true);
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "%s", TR("Add New Game Language:"));
        
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputTextWithHint("##NewLangCode", "Code (e.g. fr, de, it)", m_newLangCodeBuffer, sizeof(m_newLangCodeBuffer));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##NewLangName", "Name (e.g. Français)", m_newLangNameBuffer, sizeof(m_newLangNameBuffer));
        ImGui::SameLine();
        if (ImGui::Button(TR("Create Language"))) {
            if (m_newLangCodeBuffer[0] != '\0') {
                std::string code = m_newLangCodeBuffer;
                std::string name = m_newLangNameBuffer;
                if (gameLoc.AddLanguage(code, name)) {
                    m_selectedLocLang = code;
                    m_newLangCodeBuffer[0] = '\0';
                    m_newLangNameBuffer[0] = '\0';
                    m_showAddLanguagePopup = false;
                    m_locStatusMessage = TR("New language created!");
                    m_locStatusTimer = 3.0f;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) {
            m_showAddLanguagePopup = false;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Add Key Section
    ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1.0f), "%s", TR("Add New Translation Key:"));
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##NewLocKey", "Key (e.g. menu_play)", m_newLocKeyBuffer, sizeof(m_newLocKeyBuffer));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##NewLocDefVal", "English / Default Text", m_newLocDefaultValBuffer, sizeof(m_newLocDefaultValBuffer));
    ImGui::SameLine();
    if (ImGui::Button(TR("+ Add Key"))) {
        if (m_newLocKeyBuffer[0] != '\0') {
            if (gameLoc.AddKey(m_newLocKeyBuffer, m_newLocDefaultValBuffer)) {
                gameLoc.SaveAll();
                m_newLocKeyBuffer[0] = '\0';
                m_newLocDefaultValBuffer[0] = '\0';
                m_locStatusMessage = TR("Key added successfully!");
                m_locStatusTimer = 2.5f;
            }
        }
    }

    ImGui::Spacing();

    // Search / Filter
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##LocSearch", TR("Filter keys or translation text..."), m_locSearchFilter, sizeof(m_locSearchFilter));

    std::string filterStr = m_locSearchFilter;
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    // Translations Table
    ImGui::Spacing();
    const auto& allKeys = gameLoc.GetAllKeys();
    int totalKeys = static_cast<int>(allKeys.size());
    int translatedCount = gameLoc.GetTranslatedCount(m_selectedLocLang);
    float pct = totalKeys > 0 ? (float)translatedCount / (float)totalKeys * 100.0f : 100.0f;

    ImGui::TextDisabled("Language: %s | Keys: %d | Translated: %d (%.0f%%)",
        gameLoc.GetLanguageDisplayName(m_selectedLocLang).c_str(), totalKeys, translatedCount, pct);

    ImVec2 tableSize = ImVec2(-1, ImGui::GetContentRegionAvail().y - 120.0f);
    if (tableSize.y < 150.0f) tableSize.y = 150.0f;

    if (ImGui::BeginTable("##LocGridTable", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
        tableSize)) {

        ImGui::TableSetupColumn(TR("Key"), ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn(TR("Reference (English)"), ImGuiTableColumnFlags_WidthFixed, 220.0f);
        std::string transColHeader = std::string(TR("Translation")) + " (" + m_selectedLocLang + ")";
        ImGui::TableSetupColumn(transColHeader.c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Status"), ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn(TR("Actions"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        std::string keyToDelete = "";

        int rowIdx = 0;
        for (const auto& key : allKeys) {
            std::string enVal = gameLoc.GetTranslation("en", key);
            std::string curVal = gameLoc.GetTranslation(m_selectedLocLang, key);

            // Filter check
            if (!filterStr.empty()) {
                std::string kLower = key;
                std::string enLower = enVal;
                std::string curLower = curVal;
                std::transform(kLower.begin(), kLower.end(), kLower.begin(), ::tolower);
                std::transform(enLower.begin(), enLower.end(), enLower.begin(), ::tolower);
                std::transform(curLower.begin(), curLower.end(), curLower.begin(), ::tolower);

                if (kLower.find(filterStr) == std::string::npos &&
                    enLower.find(filterStr) == std::string::npos &&
                    curLower.find(filterStr) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();

            // Col 0: Key
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(key.c_str());

            // Col 1: English Reference
            ImGui::TableSetColumnIndex(1);
            if (m_selectedLocLang == "en") {
                ImGui::TextDisabled("(Editing this below)");
            } else {
                ImGui::TextUnformatted(enVal.c_str());
            }

            // Col 2: Translation input
            ImGui::TableSetColumnIndex(2);
            char valBuf[512];
            std::strncpy(valBuf, curVal.c_str(), sizeof(valBuf));
            valBuf[sizeof(valBuf) - 1] = '\0';
            std::string inputId = "##Val_" + key + "_" + std::to_string(rowIdx);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText(inputId.c_str(), valBuf, sizeof(valBuf))) {
                gameLoc.SetTranslation(m_selectedLocLang, key, valBuf);
            }

            // Col 3: Status
            ImGui::TableSetColumnIndex(3);
            if (!curVal.empty()) {
                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "%s", TR("Translated"));
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "%s", TR("Untranslated"));
            }

            // Col 4: Delete
            ImGui::TableSetColumnIndex(4);
            std::string delBtnId = std::string(TR("Delete")) + "##Del_" + key;
            if (ImGui::Button(delBtnId.c_str())) {
                keyToDelete = key;
            }

            rowIdx++;
        }

        if (!keyToDelete.empty()) {
            gameLoc.RemoveKey(keyToDelete);
            gameLoc.SaveAll();
        }

        ImGui::EndTable();
    }

    // In-Game Integration Cheatsheet
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(TR("In-Game Integration & Developer Cheatsheet"))) {
        ImGui::BulletText("%s", TR("Option 1 (UI Node): Select any UITextNode or UIButtonNode, and set 'Localization Key' in Inspector."));
        ImGui::BulletText("%s", TR("Option 2 (Lua Script Text): local title = TR(\"game_title\") or Localization.Translate(\"game_title\")"));
        ImGui::BulletText("%s", TR("Option 3 (Change Language in Game Menu): Localization.SetLanguage(\"es\")"));
        ImGui::BulletText("%s", TR("Option 4 (Player Dropdown Menu): local langs = Localization.GetAvailableLanguages()"));
    }

    ImGui::End();
}
