#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <glm/glm.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include "Scene/Scene.h"
#include "Scene/SkyClock.h"
#include "Scene/Weather.h"
#include "Core/Camera.h"
#include "Renderer/RenderCommon.h"
#include "Renderer/IRenderTarget.h"
#include "Renderer/VulkanBuffer.h"
#include "Renderer/ShaderGraph.h"
#include "Renderer/RayTracing/RayTracingManager.h"
#include "Navigation/NavMesh.h"
#include "Networking/NetworkManager.h"

using Graphics::PolygonMode;
#include "Animation/Animation.h"
#include "Gameplay/GameLoop.h"
#include "UI/UINodeRenderer.h"
#include "UI/NodeInspector.h"
#include "UI/RulerTool.h"
#include "Localization/Localization.h"
#include "Localization/GameLocalization.h"

class EditorUI {
public:
    EditorUI() = default;
    ~EditorUI() = default;

    void Init();
    void Render(Scene& scene, Camera& camera, Graphics::IRenderTarget& offscreen,
                Graphics::IRenderTarget& cameraPreviewOffscreen, Gameplay::GameLoop& gameLoop);

    bool IsViewportHovered() const { return m_viewportHovered; }
    bool IsViewportFocused() const { return m_viewportFocused; }

    PolygonMode GetPolygonMode() const { return m_polygonMode; }
    void SetPolygonMode(PolygonMode mode) { m_polygonMode = mode; }
    bool IsGameWindowFocused() const { return m_isGameWindowFocused; }
    Networking::NetworkManager& GetNetworkManager() { return m_networkManager; }

    bool IsGridEnabled() const { return m_showGrid; }
    void SetGridEnabled(bool enabled) { m_showGrid = enabled; }

    bool IsRulerActive() const { return m_rulerToolState.active; }
    void SetRulerActive(bool active) {
        m_rulerToolState.active = active;
        if (!active) {
            m_rulerToolState.isDragging = false;
            m_rulerToolState.draggingPoint = -1;
            m_rulerToolState.hasHoveredPoint = false;
            if (!m_rulerToolState.current.hasB) {
                m_rulerToolState.current.hasA = false;
            }
        }
    }
    void ToggleRuler() {
        bool newState = !m_rulerToolState.active;
        SetRulerActive(newState);
        m_rulerToolState.showRulerWindow = newState;
    }
    void OpenRulerWindow() { m_rulerToolState.showRulerWindow = true; SetRulerActive(true); }
    void CloseRulerWindow() { m_rulerToolState.showRulerWindow = false; SetRulerActive(false); }
    RulerToolState& GetRulerToolState() { return m_rulerToolState; }
    const RulerToolState& GetRulerToolState() const { return m_rulerToolState; }

    enum class TouchNavMode { Select, Orbit, Pan };
    TouchNavMode GetTouchNavMode() const { return m_touchNavMode; }
    void SetTouchNavMode(TouchNavMode mode) { m_touchNavMode = mode; }

    enum class StartupState {
        Splash,
        ProjectLauncher,
        Editor
    };
    StartupState GetStartupState() const { return m_startupState; }
    void SetStartupState(StartupState state) { m_startupState = state; }
    void OpenProjectLauncher() { m_startupState = StartupState::ProjectLauncher; }
    void CloseProjectLauncher() { m_startupState = StartupState::Editor; }

    // lights: the view's lights (the sky's sun follows the first directional one)
    void PopulateEnvironmentUBO(const Scene& scene, UniformBufferObject& ubo, const std::vector<LightData>& lights) const {
        const auto& env = scene.GetEnvironment();
        auto& rt = Graphics::RayTracing::RayTracingManager::Get();
        auto& rtSettings = rt.GetSettings();
        const bool rtActive = rtSettings.enableHardwareRT;
        const bool rtgiActive = rtActive && rtSettings.enableRTGI;
        const bool rtaoActive = rtActive && rtSettings.enableRTAO;
        const bool rtrActive = rtActive && rtSettings.enableRTR;

        const bool giActive = env.vxgiEnabled || rtgiActive;
        const float giIntensity = rtgiActive ? std::max(env.vxgiIntensity, rtSettings.rtgiIntensity) : env.vxgiIntensity;

        ubo.enableACES = env.enableACES ? 1 : 0;
        ubo.enableShadows = env.enableShadows ? 1 : 0;
        ubo.enableGI = (env.enableGI || giActive) ? 1 : 0;
        ubo.vxgiParams = glm::vec4(giActive ? 1.0f : 0.0f, std::max(giIntensity, 0.0f),
                                   std::clamp(env.vxgiRange, 8.0f, 80.0f), 5.0f);
        ubo.giSkyColor = glm::vec4(env.giSkyColor, env.giIntensity);
        ubo.fogColorAndDensity = glm::vec4(env.fogColor, env.fogDensity);
        const float ssaoVal = rtaoActive ? std::max(env.ssaoIntensity, 0.85f) : env.ssaoIntensity;
        const float ssrVal = rtrActive ? std::max(env.ssrIntensity, 1.0f) : env.ssrIntensity;
        ubo.postProcessParams = glm::vec4(env.bloomIntensity, ssaoVal, ssrVal, env.volumetricScattering);
        ubo.timeParams.y = env.hdrExposure;
        ubo.volumetricParams = glm::vec4(std::max(env.volumetricFogDensity, 0.0f), std::max(env.volumetricHeightFalloff, 0.0f),
                                         env.volumetricAnisotropy, env.volumetricBaseHeight);

        // The sky's sun follows the clock, or else the first directional light
        glm::vec3 sunDir(0.3f, 0.8f, 0.4f);
        for (const LightData& light : lights) {
            if (light.position.w < 0.5f && glm::length(glm::vec3(light.position)) > 0.0001f) {
                sunDir = glm::vec3(light.position);
                break;
            }
        }
        ubo.moonParams = glm::vec4(0.0f);
        ubo.skyClockParams = glm::vec4(0.0f);
        if (env.timeOfDayEnabled) {
            const SkyClock::State clock = SkyClock::Evaluate(env);
            sunDir = clock.sunDir;
            ubo.moonParams = glm::vec4(clock.moonDir, env.moonEnabled ? std::max(env.moonBrightness, 0.0f) : 0.0f);
            ubo.skyClockParams = glm::vec4(clock.starRotation, glm::radians(env.latitude), glm::radians(env.northAngle), 1.0f);
        }
        ubo.skyParams = glm::vec4(glm::normalize(sunDir), env.enableSky ? std::max(env.skyIntensity, 0.0001f) : 0.0f);
        ubo.weatherParams = glm::clamp(glm::vec4(env.rainAmount, env.snowAmount, env.wetness, env.snowCover), 0.0f, 1.0f);
        ubo.weatherParams2 = glm::vec4(std::max(env.snowDepth, 0.0f), 0.0f, 0.0f, 0.0f);
        ubo.lightningParams = glm::vec4(env.lightningFlash, env.boltAzimuth, env.boltSeed,
                                        (env.lightningAge >= 0.0f && env.lightningAge < 0.35f) ? 1.0f : 0.0f);
        ubo.cloudParams = glm::vec4(0.0f);
        ubo.cloudWind = glm::vec4(0.0f);
        ubo.atmosphereParams = glm::vec4(env.physicalAtmosphere && env.enableSky && !env.useHDRI ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        ubo.volumetricCloudParams = glm::vec4(0.0f);
        if (env.cloudsEnabled && (env.enableSky || env.useHDRI)) {
            ubo.volumetricCloudParams = glm::vec4(env.volumetricClouds ? 1.0f : 0.0f, std::max(env.cloudThickness, 50.0f),
                                                  glm::clamp(env.cloudDetail, 0.0f, 1.5f), 0.0f);
            ubo.cloudParams = glm::vec4(glm::clamp(env.cloudCoverage, 0.0f, 1.0f), glm::clamp(env.cloudOpacity, 0.0f, 1.0f),
                                        std::max(env.cloudHeight, 50.0f), 2000.0f * std::max(env.cloudScale, 0.05f));
            const float wind = glm::radians(env.windDirection);
            ubo.cloudWind = glm::vec4(std::sin(wind) * env.windSpeed, -std::cos(wind) * env.windSpeed,
                                      glm::clamp(env.cloudShadows, 0.0f, 1.0f), 0.0f);
        }
        // Fog is lit by the sky: it darkens at night (a little moonlight) and lights up in a flash
        if (env.enableSky && !env.useHDRI) {
            const float sunY = glm::normalize(sunDir).y;
            const float dayT = glm::clamp((sunY + 0.12f) / 0.37f, 0.0f, 1.0f);
            const float day = dayT * dayT * (3.0f - 2.0f * dayT);
            const float moon = ubo.moonParams.w > 0.0f ? 0.03f * glm::clamp(ubo.moonParams.y * 4.0f, 0.0f, 1.0f) : 0.0f;
            const glm::vec3 fog = env.fogColor * (0.02f + 0.98f * day + moon) + glm::vec3(0.5f, 0.55f, 0.7f) * env.lightningFlash * 0.6f;
            ubo.fogColorAndDensity = glm::vec4(fog, env.fogDensity);
        }
    }
    void PopulateEnvironmentUBO(UniformBufferObject& ubo) const {
        ubo.enableACES = m_enableACES ? 1 : 0;
        ubo.enableShadows = m_enableShadows ? 1 : 0;
        ubo.enableGI = m_enableGI ? 1 : 0;
        ubo.giSkyColor = glm::vec4(m_giSkyColor, m_giIntensity);
        ubo.fogColorAndDensity = glm::vec4(m_fogColor, m_fogDensity);
        ubo.postProcessParams = glm::vec4(m_bloomIntensity, m_ssaoIntensity, m_ssrIntensity, m_volumetricScattering);
        ubo.timeParams.y = m_hdrExposure;
    }

    void SetImportCallback(std::function<void(const std::string&)> cb) { m_onImportObj = cb; }
    void SetAssignModelCallback(std::function<bool(const std::string& filepath, Node& targetNode)> cb) { m_onAssignModel = cb; }
    void SetGetAvailableModelsCallback(std::function<std::vector<std::string>()> cb) { m_onGetAvailableModels = cb; }
    void SetGenerateLODsCallback(std::function<bool(const std::string& baseMeshKey, Node& targetNode)> cb) { m_onGenerateLODs = cb; }
    void OpenImportDialog();

    // Project callbacks
    void SetProjectName(const std::string& name) { m_currentProjectName = name; }
    void SetProjectRoot(const std::string& root) {
        m_projectRoot = root;
        std::replace(m_projectRoot.begin(), m_projectRoot.end(), '\\', '/');
        if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot)) {
            m_fileViewerCurrentDir = std::filesystem::absolute(m_projectRoot).string();
            std::replace(m_fileViewerCurrentDir.begin(), m_fileViewerCurrentDir.end(), '\\', '/');
        }
        GameLocalization::Get().SetProjectRoot(m_projectRoot);
    }
    void SetNewProjectCallback(std::function<void(const std::string& parentDir, const std::string& name)> cb) { m_onNewProject = cb; }
    void SetOpenProjectCallback(std::function<void(const std::string& filePath)> cb) { m_onOpenProject = cb; }
    void SetSaveProjectCallback(std::function<void()> cb) { m_onSaveProject = cb; }
    void SetSaveProjectAsCallback(std::function<void(const std::string& parentDir, const std::string& name)> cb) { m_onSaveProjectAs = cb; }

    // Multi-Scene callbacks
    void SetNewSceneCallback(std::function<void(const std::string&)> cb) { m_onNewScene = cb; }
    void SetOpenSceneCallback(std::function<void(const std::string&)> cb) { m_onOpenScene = cb; }
    void SetSaveSceneCallback(std::function<void()> cb) { m_onSaveScene = cb; }
    void SetSaveSceneAsCallback(std::function<void(const std::string&)> cb) { m_onSaveSceneAs = cb; }
    void SetGetScenesCallback(std::function<std::vector<std::string>()> cb) { m_onGetScenes = cb; }
    void SetGetActiveSceneCallback(std::function<std::string()> cb) { m_onGetActiveScene = cb; }
    void SetSkeletalScrubCallback(std::function<void(const std::string&, const std::string&, float, bool)> cb) { m_onSkeletalScrub = cb; }
    void SetGetSkeletalAnimInfoCallback(std::function<bool(const std::string&, float&, float&, bool&)> cb) { m_onGetSkeletalAnimInfo = cb; }
    void SetImportTextureCallback(std::function<std::string(const std::string&)> cb) { m_onImportTexture = cb; }
    // Material previews: (key, material, shape: 0 sphere, 1 cube, 2 plane) -> ImGui texture, or null
    // while the first is being rendered. Ask each frame the preview is shown.
    void SetMaterialPreviewCallback(std::function<void*(const std::string&, const Node&, int)> cb) { m_onMaterialPreview = cb; }
    // Incremented by the "Recapture Probes" button; the renderer recaptures when it changes
    int GetProbeRecaptureRequests() const { return m_probeRecaptureRequests; }
    // Baked lighting: the Bake button sets a request the engine picks up; it reports progress back
    bool ConsumeGIBakeRequest() { const bool requested = m_giBakeRequested; m_giBakeRequested = false; return requested; }
    void SetGIBakeStatus(const std::string& status, bool baking) { m_giBakeStatus = status; m_giBaking = baking; }

    // Window management callbacks
    void SetWindowMinimizeCallback(std::function<void()> cb) { m_onMinimizeWindow = cb; }
    void SetWindowMaximizeCallback(std::function<void()> cb) { m_onMaximizeWindow = cb; }
    void SetWindowCloseCallback(std::function<void()> cb) { m_onCloseWindow = cb; }
    void SetWindowMoveCallback(std::function<void(float, float)> cb) { m_onMoveWindow = cb; }
    void SetIsWindowMaximizedCallback(std::function<bool()> cb) { m_isWindowMaximized = cb; }
    bool IsMouseOverMenuBar(int clientX, int clientY) const;

    void OpenProjectDialog();
    void OpenNewProjectDialog();
    void OpenSaveProjectAsDialog();
    void OpenNewProjectModal() { m_showNewProjectModal = true; }
    void OpenSaveProjectAsModal() { m_showSaveAsModal = true; }
    void OpenNewSceneModal() { m_showNewSceneModal = true; }
    void OpenSaveSceneAsModal() { m_showSaveSceneAsModal = true; }
    void OpenInputSettingsModal() { m_showInputSettingsModal = true; }
    void OpenExportGameModal() { m_showExportGameModal = true; }
    void RenderUIElements(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize, bool isEditorMode, Gameplay::GameLoop* gameLoop = nullptr);

private:
    void RenderCustomTitleBar(Scene& scene, Camera& camera, Gameplay::GameLoop& gameLoop);
    void RenderSplashScreen(float dt);
    void RenderProjectLauncher(Scene& scene, Camera& camera);
    void RenderMainMenuBar(Scene& scene, Camera& camera, Gameplay::GameLoop& gameLoop);
    void RenderLeftPanel(Scene& scene, Camera& camera);
    void RenderHierarchy(Scene& scene, Camera& camera);
    void RenderAddNodePopup(Scene& scene);
    void RenderNodeTree(Scene& scene, uint32_t nodeId, uint32_t& nodeToDelete, bool& requestDelete);
    void RenderInspector(Scene& scene, Camera& camera);
    void RenderViewport(Scene& scene, Camera& camera, Graphics::IRenderTarget& offscreen, Gameplay::GameLoop& gameLoop);
    void RenderCameraPreview(Scene& scene, Camera& camera, Graphics::IRenderTarget& previewOffscreen, Gameplay::GameLoop& gameLoop);
    void RenderAnimationTimeline(Scene& scene, Animation::AnimationPlayer& animPlayer);
    void RenderAnimationStateMachine(Scene& scene);
    void RenderGameWindow(Scene& scene, Graphics::IRenderTarget& gameOffscreen, Gameplay::GameLoop& gameLoop);
    void RenderScriptEditor();
    void LoadScriptFile(const std::string& path);
    void SaveScriptFile(const std::string& path);
    void InsertScriptTemplate(int index);

    // New Roadmap Feature Windows
    void RenderEnvironmentAndPostProcessWindow(Scene& scene);
    void RenderCutsceneSequencerWindow(Scene& scene, Camera& camera, Gameplay::GameLoop& gameLoop);
    void RenderShaderGraphWindow(Scene& scene);
    void RenderAudioMixerWindow(Scene& scene);
    void RenderGameLocalizationWindow();
    void RenderNavMeshWindow(Scene& scene);
    void RenderGpuProfilerWindow();
    void DrawGpuTimings();
    void RenderNetworkingWindow(Scene& scene, Gameplay::GameLoop* gameLoop = nullptr);
    void RenderProjectFilesWindow(Scene& scene);
    void RenderGameProfilerWindow(Scene& scene, Gameplay::GameLoop& gameLoop);
    void RenderRulerWindow(Scene& scene, Camera& camera);
    void RenderRulerOverlay(Scene& scene, Camera& camera, ImDrawList* drawList);
    void RenderAboutModal();
    static void RenderEngineEmblem(ImDrawList* drawList, ImVec2 center, float radius, float alpha = 1.0f);

    void RenderImportModal();
    void RenderNewProjectModal();
    void RenderSaveProjectAsModal();
    void RenderNewSceneModal();
    void RenderSaveSceneAsModal();
    void RenderInputSettingsModal();
    void RenderExportGameModal(Scene& scene);
    bool ExportGame(Scene& scene, const std::string& destinationDir, const std::string& gameTitle, const std::string& startScene, bool fullscreen, int width, int height, bool embedInExe = true);
    bool ExportWebGame(Scene& scene, const std::string& destinationDir, const std::string& gameTitle, const std::string& startScene);
    bool ExportLinuxGame(Scene& scene, const std::string& destinationDir, const std::string& gameTitle, const std::string& startScene, bool fullscreen, int width, int height, bool steamDeckSupport = true);

    StartupState m_startupState{StartupState::Splash};
    float m_splashTimer{0.0f};
    float m_splashDuration{2.2f};
    char m_launcherNewProjectName[128]{"MyGameProject"};
    char m_launcherNewProjectDir[512]{"projects"};
    int m_launcherTemplateIndex{0};
    std::string m_launcherStatusMessage{""};

    std::string m_currentProjectName{"Untitled Project"};
    std::string m_projectRoot;
    std::string m_activeScriptFile;
    std::vector<char> m_scriptTextBuffer;
    bool m_scriptModified{false};
    char m_newScriptNameBuffer[128]{"PlayerController.lua"};
    std::function<void(const std::string&, const std::string&)> m_onNewProject;
    std::function<void(const std::string&)> m_onOpenProject;
    std::function<void()> m_onSaveProject;
    std::function<void(const std::string&, const std::string&)> m_onSaveProjectAs;

    // Multi-Scene state & callbacks
    std::function<void(const std::string&)> m_onNewScene;
    std::function<void(const std::string&)> m_onOpenScene;
    std::function<void()> m_onSaveScene;
    std::function<void(const std::string&)> m_onSaveSceneAs;
    std::function<std::vector<std::string>()> m_onGetScenes;
    std::function<std::string()> m_onGetActiveScene;
    std::function<void(const std::string&, const std::string&, float, bool)> m_onSkeletalScrub;
    std::function<bool(const std::string&, float&, float&, bool&)> m_onGetSkeletalAnimInfo;
    std::function<std::string(const std::string&)> m_onImportTexture;
    int m_probeRecaptureRequests{0};
    bool m_giBakeRequested{false};
    bool m_giBaking{false};
    std::string m_giBakeStatus;

    std::function<void()> m_onMinimizeWindow;
    std::function<void()> m_onMaximizeWindow;
    std::function<void()> m_onCloseWindow;
    std::function<void(float, float)> m_onMoveWindow;
    std::function<bool()> m_isWindowMaximized;
    float m_menuBarEndX{0.0f};

    bool m_showNewSceneModal{false};
    bool m_showGameWindow{false};
    bool m_isGameWindowFocused{false};
    char m_newSceneName[128]{"Level1"};

    // Animation State Machine Visual Graph
    bool m_showAnimStateMachineWindow{false};
    struct AnimTransitionUI {
        std::string fromState;
        std::string toState;
        float blendDuration{0.2f};
    };
    std::vector<AnimTransitionUI> m_uiAnimTransitions;
    std::map<std::string, ImVec2> m_animNodeCanvasPositions;
    ImVec2 m_animGraphScroll{60.0f, 60.0f};
    std::string m_selectedGraphState{""};
    int m_selectedTransitionIdx{-1};
    std::string m_lastRiggedModelName{""};
    int m_newTransitionFromIdx{0};
    int m_newTransitionToIdx{0};
    float m_newTransitionBlend{0.2f};
    bool m_showAddTransitionModal{false};

    // Core Dockable Workspace Panels
    bool m_showHierarchyWindow{true};
    bool m_showInspectorWindow{true};
    bool m_showViewportWindow{true};
    bool m_showCameraPreviewWindow{true};
    bool m_showTimelineWindow{true};
    bool m_showProjectFilesWindow{true};
    bool m_showScriptEditor{false};
    bool m_showGameProfilerWindow{false};
    bool m_showGpuProfilerWindow{false};
    std::function<void*(const std::string&, const Node&, int)> m_onMaterialPreview;
    int m_materialPreviewShape{0};
    // Shows a material preview image (or a placeholder while it renders)
    void DrawMaterialPreview(const std::string& key, const Node& material, float size, int shape);
    bool m_resetDockLayoutRequested{false};
    bool m_showAboutModal{false};

    // Project Files / File Viewer State
    std::string m_fileViewerCurrentDir{""};
    std::string m_fileViewerSelectedPath{""};
    char m_fileViewerSearchFilter[128]{""};
    bool m_fileViewerGridView{false};
    char m_newFolderName[128]{"NewFolder"};
    char m_newScriptName[128]{"NewScript.lua"};
    bool m_showCreateFolderPopup{false};
    bool m_showCreateScriptPopup{false};

    // New Roadmap Feature Window Toggles & State
    bool m_showEnvironmentWindow{false};
    bool m_showCutsceneWindow{false};
    int m_selectedCutsceneIdx{0};
    bool m_showRenderSettingsWindow{false};
    bool m_showShaderGraphWindow{false};
    bool m_showAudioMixerWindow{false};
    bool m_showNavMeshWindow{false};
    bool m_showNetworkingWindow{false};
    bool m_showGameLocalizationWindow{false};
    char m_newLocKeyBuffer[128]{""};
    char m_newLocDefaultValBuffer[256]{""};
    char m_newLangCodeBuffer[32]{""};
    char m_newLangNameBuffer[64]{""};
    char m_locSearchFilter[128]{""};
    std::string m_selectedLocLang{"en"};
    std::string m_locStatusMessage{""};
    float m_locStatusTimer{0.0f};
    bool m_showAddLanguagePopup{false};

    float m_hdrExposure{1.0f};
    bool m_enableACES{true};
    bool m_enableShadows{true};
    bool m_enableGI{true};
    glm::vec3 m_giSkyColor{0.35f, 0.48f, 0.68f};
    float m_giIntensity{1.0f};
    glm::vec3 m_fogColor{0.55f, 0.68f, 0.85f};
    float m_fogDensity{0.0f};
    float m_bloomIntensity{0.35f};
    float m_ssaoIntensity{1.0f};
    float m_ssrIntensity{0.35f};
    float m_volumetricScattering{0.0f};

    ShaderGraph m_shaderGraph;
    std::string m_shaderGraphPath; // file the editor's graph was loaded from / saved to
    bool m_shaderGraphInitialized{false};
    std::string m_compiledGLSLPreview;
    Navigation::NavMesh m_navMesh;
    bool m_showNavMeshOverlay{true};
    glm::vec3 m_navBoundsMin{-20.0f, 0.0f, -20.0f};
    glm::vec3 m_navBoundsMax{20.0f, 0.0f, 20.0f};
    float m_navCellSize{2.0f};
    float m_navMaxSlope{45.0f};
    glm::vec3 m_navStartPos{-6.0f, 0.0f, -6.0f};
    glm::vec3 m_navGoalPos{6.0f, 0.0f, 6.0f};
    std::vector<glm::vec3> m_lastNavPath;

    Networking::NetworkManager m_networkManager;
    char m_netAddress[64]{"127.0.0.1"};
    int m_netPort{27015};
    std::vector<std::string> m_netLogs;

    bool m_showSaveSceneAsModal{false};
    bool m_openAddNodePopup{false};
    bool m_showInputSettingsModal{false};
    bool m_showExportGameModal{false};
    int m_exportPlatformIndex{0}; // 0 = Windows (.exe), 1 = Web / HTML5 (WebGL2)
    char m_exportDirBuffer[512]{"exports/MyGame_Windows"};
    char m_exportExeNameBuffer[128]{"MyGame"};
    char m_exportWebTitle[128]{"MyGame"};
    char m_exportWebDirBuffer[512]{"exports/MyGame_Web"};
    int m_exportWebCanvasMode{0}; // 0 = Responsive, 1 = Fixed (1280x720), 2 = Fill Window
    bool m_exportWebIncludeServer{true};
    bool m_exportWebEmbedScene{true};
    bool m_exportWebTouchControls{true};
    char m_exportLinuxDirBuffer[512]{"exports/MyGame_Linux"};
    char m_exportLinuxNameBuffer[128]{"MyGame"};
    bool m_exportLinuxSteamDeckSupport{true};
    bool m_exportLinuxTarArchive{false};
    bool m_exportLinuxDualProton{true};
    std::string m_exportSelectedScene{"main.scene"};
    bool m_exportFullscreen{false};
    int m_exportResolutionPreset{1};
    bool m_exportEmbedInExe{true};
    bool m_exportDone{false};
    bool m_exportSuccess{false};
    std::string m_exportStatusMessage{""};
    std::string m_lastExportedPath{""};
    std::string m_rebindingActionId{""};
    bool m_rebindingIsPrimary{true};
    int m_rebindStartFrame{0};
    char m_newCustomActionId[64]{"CustomAction"};
    char m_newCustomActionName[64]{"Custom Action"};
    char m_saveSceneAsName[128]{"NewLevel"};

    bool m_showNewProjectModal{false};
    char m_newProjectName[128]{"MyGameProject"};
    char m_newProjectDir[512]{"projects"};

    bool m_showSaveAsModal{false};
    char m_saveAsProjectName[128]{"MyGameProjectCopy"};
    char m_saveAsProjectDir[512]{"projects"};

    std::function<void(const std::string&)> m_onImportObj;
    std::function<bool(const std::string& filepath, Node& targetNode)> m_onAssignModel;
    std::function<std::vector<std::string>()> m_onGetAvailableModels;
    std::function<bool(const std::string& baseMeshKey, Node& targetNode)> m_onGenerateLODs;
    bool m_showImportModal{false};
    char m_importPathBuffer[512]{"assets/models/teapot.obj"};

    bool m_viewportHovered{false};
    bool m_viewportFocused{false};
    bool m_isRotatingCamera{false};
    bool m_isPanningCamera{false};
    TouchNavMode m_touchNavMode{TouchNavMode::Select};
    bool m_isDraggingUINode{false};
    uint32_t m_draggedUINodeId{0};
    glm::vec2 m_viewportSize{800.0f, 600.0f};
public:
    // Size of the 3D viewport panel's image area, in framebuffer pixels (last frame's layout)
    glm::vec2 GetViewportPixelSize() const {
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        return m_viewportSize * glm::vec2(scale.x > 0.0f ? scale.x : 1.0f, scale.y > 0.0f ? scale.y : 1.0f);
    }
    // Size the game camera's image was shown at recently, in framebuffer pixels: the Game window
    // if it is open, else the Camera Preview panel; (0, 0) when neither is showing
    glm::vec2 GetGameImagePixelSize() const {
        const int frame = ImGui::GetFrameCount();
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const glm::vec2 pixels(scale.x > 0.0f ? scale.x : 1.0f, scale.y > 0.0f ? scale.y : 1.0f);
        if (frame - m_gameImageFrame <= 2) return m_gameImageSize * pixels;
        if (frame - m_previewImageFrame <= 2) return m_previewImageSize * pixels;
        return glm::vec2(0.0f);
    }
    // Editor viewport performance options (the editor's 3D view only: never the Game view, an
    // exported game or the saved scene). Saved in editor_settings.json.
    struct ViewportPerformance {
        enum Preset { Full = 0, Balanced = 1, Fast = 2, Custom = 3 };
        int preset{Full};
        float resolutionScale{1.0f};  // of the viewport panel's pixels
        bool rayTracing{true};        // hardware RT shadows / reflections in the viewport
        bool ambientOcclusion{true};  // SSAO
        bool screenReflections{true}; // SSR
        bool volumetrics{true};       // volumetric clouds, fog and light shafts
        bool cinematicEffects{true};  // depth of field, motion blur, film grain, lens flare, chromatic aberration
        bool temporalAA{true};        // off: FXAA instead of TAA
        bool shortShadows{false};     // shadow maps reach 60 m (fewer casters drawn)
        bool realtime{true};          // animate water, wind, weather and the sky clock while editing
        bool showFrameStats{false};   // FPS / CPU / GPU overlay in the viewport corner
        bool lessCpuInBackground{true}; // ~10 fps while the editor window isn't focused
    };
    const ViewportPerformance& GetViewportPerformance() const { return m_viewportPerf; }
    // Frame timings for the viewport overlay (from the main loop)
    void SetFrameStats(float cpuMs, float gpuMs, uint32_t renderWidth, uint32_t renderHeight) {
        m_statCpuMs = m_statCpuMs <= 0.0f ? cpuMs : m_statCpuMs + (cpuMs - m_statCpuMs) * 0.05f;
        m_statGpuMs = gpuMs;
        m_statRenderSize = glm::uvec2(renderWidth, renderHeight);
    }
    bool IsGameOrPreviewVisible() const {
        const int frame = ImGui::GetFrameCount();
        return (frame - m_gameImageFrame <= 2) || (frame - m_previewImageFrame <= 2);
    }
private:
    ViewportPerformance m_viewportPerf;
    float m_statCpuMs{0.0f};
    float m_statGpuMs{0.0f};
    glm::uvec2 m_statRenderSize{0u};
    void ApplyViewportPreset(int preset);
    void RenderViewportPerformanceMenu();
    void RenderViewportFrameStats(const ImVec2& imageMin, const ImVec2& imageSize);
    void LoadViewportPerformance();
    void SaveViewportPerformance() const;
    glm::vec2 m_gameImageSize{0.0f};
    int m_gameImageFrame{-100};
    glm::vec2 m_previewImageSize{0.0f};
    int m_previewImageFrame{-100};
    glm::vec2 m_viewportPos{0.0f, 0.0f};

    // Viewport Display Features
    bool m_showGrid{true};
    float m_gridSize{16.0f};
    bool m_showPivotAxisMarker{true};
    PolygonMode m_polygonMode{PolygonMode::Solid};

    // 2D UI & Node Inspector State
    UINodeRendererState m_uiRendererState;
    NodeInspectorState m_inspectorState;

    // Universal Automatic Undo/Redo Scene State Tracker. Checks for edits only when an interaction
    // ends (mouse/key release, widget or gizmo release), comparing cheap fingerprints of the scene;
    // the full scene is serialized only when something really changed.
    struct UndoFingerprint {
        uint64_t structure{0};   // every saved node's id, parent, name, type, children and transform
        uint64_t environment{0};
        std::unordered_map<uint32_t, uint64_t> nodes; // full settings of nodes that have been selected
        bool operator==(const UndoFingerprint& o) const { return structure == o.structure && environment == o.environment && nodes == o.nodes; }
    };
    void UpdateUndoTracker(Scene& scene, Gameplay::GameLoop& gameLoop);
    UndoFingerprint ComputeUndoFingerprint(const Scene& scene, const UndoFingerprint& watched) const;
    bool m_undoBaselineInitialized{false};
    JsonValue m_lastCommittedSceneState;
    UndoFingerprint m_lastCommittedFingerprint;
    int32_t m_lastCommittedSelectedId{-1};
    uint64_t m_lastSeenUndoVersion{0};
    bool m_undoCheckPending{false};

    // Ruler & Measurement Tool State
    RulerToolState m_rulerToolState;

    // ImGuizmo settings
    ImGuizmo::OPERATION m_currentGizmoOperation{ImGuizmo::TRANSLATE};
    ImGuizmo::MODE m_currentGizmoMode{ImGuizmo::LOCAL};
    bool m_useSnap{false};
    float m_snapTranslation[3]{0.5f, 0.5f, 0.5f};
    float m_snapRotation{15.0f};
    float m_snapScale{0.5f};
};
