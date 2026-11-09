#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <glm/gtc/packing.hpp>
#include <stdexcept>
#include <algorithm>
#include <stb_image_write.h>

static std::ofstream g_log("engine.log");
#define LOG(msg) do { g_log << msg << std::endl; std::cout << msg << std::endl; } while(0)

#include "Core/Camera.h"
#include "Core/InputManager.h"
#include "Scene/Scene.h"
#include "Scene/Mesh.h"
#include "Scene/MeshSimplifier.h"
#include "Scene/ObjLoader.h"
#include "Scene/FbxLoader.h"
#include "Scene/GltfLoader.h"
#include "Scene/Nodes/3D/CharacterNode.h"
#include "Scene/Nodes/3D/TerrainNode.h"
#include "Animation/IKSolver.h"
#include "Animation/MorphTarget.h"
#include "Video/VideoPlayer.h"
#include "Project/Project.h"
#include "Localization/Localization.h"
#include "Scripting/LuaScriptSystem.h"
#include "Renderer/VulkanContext.h"
#include "Renderer/RayTracing/RayTracingManager.h"
#include "Renderer/RayTracing/VulkanRayTracing.h"
#include "Renderer/Meshlet/MeshletManager.h"
#include "Renderer/Pipeline/VulkanPipelineCache.h"
#include "Renderer/Upscaling/TemporalUpscaler.h"
#include "Core/LargeWorldCoordinates.h" 
#include "Streaming/WorldPartitionManager.h"
#include "Streaming/WorldStreamer.h"
#include "Tools/ModelOptimizer.h"
#include "Tools/WorldBuilder.h"
#include "Physics/XPBDClothSim.h"
#include <mutex>
#include <unordered_set> 
#include "Core/EngineIconData.h"
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <shellapi.h>

class EngineApp;
static WNDPROC s_originalWndProc = nullptr;
static EngineApp* s_appInstance = nullptr;
static LRESULT CALLBACK CustomWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif
#include "Renderer/VulkanBuffer.h"
#include "Renderer/VulkanTexture.h"
#include "Renderer/VulkanOffscreen.h"
#include "Renderer/LightmapBaker.h"
#include "Scene/OceanWaves.h"
#include "Scene/Liquids.h"
#include "Scene/SkyClock.h"
#include "Scene/Weather.h"
#include "Scene/FoliageMeshes.h"
#include "Audio/AudioEngine.h"
#include "Renderer/VulkanFluidSim.h"
#include "Renderer/VulkanParticleSim.h"
#include "Renderer/VulkanGpuProfiler.h"
#include "Renderer/VulkanPreviewImage.h"
#include "Scene/Nodes/3D/ParticleNode.h"
#include "Renderer/VulkanFluidTargets.h"
#include <stb_image.h>
#include "Renderer/VulkanPipeline.h"
#include "UI/EditorUI.h"
#include "Gameplay/GameLoop.h"
#include "Monitoring/GameProfiler.h"

const uint32_t WINDOW_WIDTH = 1600;
const uint32_t WINDOW_HEIGHT = 900;
const int MAX_FRAMES_IN_FLIGHT = 2;

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};


struct GameConfig {
    bool isStandaloneGame{false};
    std::string gameTitle{"Tessera Game"};
    std::string startingScene{"scenes/main.scene"};
    bool sceneOnCommandLine{false}; // --scene given: the editor opens it too (without changing the project)
    std::string selectNode;         // --select=<node name>: selected at startup (editor)
    uint32_t windowWidth{1600};
    uint32_t windowHeight{900};
    bool fullscreen{false};
    // --screenshot=<file.png>: render screenshotFrame frames, save the final image, then exit. With
    // --game it is the game camera's image; in the editor, the 3D viewport's (counted from when
    // the editor, not the project hub, is showing)
    std::string screenshotPath{""};
    int screenshotFrame{90};
    // --bake-lighting: bake the scene's lighting (irradiance volume) at startup; a screenshot is
    // taken once the bake has finished
    bool bakeLighting{false};
    // --gpu-profile: log the GPU time of every pass (averaged) on exit
    bool gpuProfileLog{false};
    // --start-at=<node name>: (standalone) the active camera starts where that node is
    std::string startAt;
    // --project=<file.tess>: open a specific project directly at startup
    std::string startingProject;

    static GameConfig Detect(int argc, char* argv[]) {
        GameConfig cfg;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--game") {
                cfg.isStandaloneGame = true;
            } else if (arg == "--fullscreen") {
                cfg.fullscreen = true;
            } else if (arg.rfind("--project=", 0) == 0) {
                cfg.startingProject = arg.substr(10);
            } else if (arg.rfind("--scene=", 0) == 0) {
                cfg.startingScene = arg.substr(8);
                cfg.sceneOnCommandLine = true;
            } else if (arg.rfind("--select=", 0) == 0) {
                cfg.selectNode = arg.substr(9);
            } else if (arg.rfind("--screenshot=", 0) == 0) {
                cfg.screenshotPath = arg.substr(13);
            } else if (arg.rfind("--screenshot-frame=", 0) == 0) {
                cfg.screenshotFrame = std::max(1, std::atoi(arg.c_str() + 19));
            } else if (arg == "--bake-lighting") {
                cfg.bakeLighting = true;
            } else if (arg == "--gpu-profile") {
                cfg.gpuProfileLog = true;
            } else if (arg.rfind("--start-at=", 0) == 0) {
                // Standalone: the camera starts at this node's position and rotation
                cfg.startAt = arg.substr(11);
            } else if (arg == "--no-raytracing") {
                // Use the rasterised fallback (shadow maps, SSR) even on ray tracing GPUs
                VulkanContext::Get().SetRayTracingAllowed(false);
            } else if (arg == "--no-texture-compression" || arg == "--texture-compression=off") {
                // Load material textures as plain RGBA8 (for comparing quality or debugging)
                VulkanTexture::SetCompressionQuality(VulkanTexture::CompressionQuality::Off);
            } else if (arg == "--texture-compression=high") {
                // BC7 for colour textures: better quality, twice BC1's memory
                VulkanTexture::SetCompressionQuality(VulkanTexture::CompressionQuality::High);
            } else if (arg == "--texture-compression=standard") {
                VulkanTexture::SetCompressionQuality(VulkanTexture::CompressionQuality::Standard);
            }
        }

        std::filesystem::path configPath = "game.agconfig";
#ifdef _WIN32
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        if (std::filesystem::exists(exeDir / "game.agconfig")) {
            configPath = exeDir / "game.agconfig";
        }
#endif
        if (std::filesystem::exists(configPath)) {
            std::ifstream file(configPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                file.close();
                JsonValue root = JsonValue::Parse(ss.str());
                if (root.IsObject()) {
                    cfg.isStandaloneGame = true;
                    cfg.gameTitle = root["gameTitle"].AsString(cfg.gameTitle);
                    cfg.startingScene = root["startingScene"].AsString(cfg.startingScene);
                    cfg.windowWidth = root["windowWidth"].AsUInt(cfg.windowWidth);
                    cfg.windowHeight = root["windowHeight"].AsUInt(cfg.windowHeight);
                    cfg.fullscreen = root["fullscreen"].AsBool(cfg.fullscreen);
                }
            }
        }
        return cfg;
    }
};

class EngineApp {
public:
    bool IsStandalone() const { return m_gameConfig.isStandaloneGame; }
#ifdef _WIN32
    LRESULT HandleNcHitTest(LPARAM lParam);
#endif
    void Run(int argc = 0, char* argv[] = nullptr) {
        LOG("Starting EngineApp::Run()");
        m_gameConfig = GameConfig::Detect(argc, argv);
        InitWindow();
        LOG("InitWindow() done");
        InitVulkan();
        LOG("InitVulkan() done");
        InitImGui();
        LOG("InitImGui() done");
        InitRendererAndOffscreen();
        LOG("InitRendererAndOffscreen() done");
        InitScene();
        LOG("InitScene() done");
        LOG("Entering MainLoop");
        MainLoop();
        LOG("Exited MainLoop, cleaning up");
        Cleanup();
        LOG("Cleanup() done");
    }

private:
    void InitWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        if (!m_gameConfig.isStandaloneGame) {
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        }

        uint32_t w = m_gameConfig.isStandaloneGame ? m_gameConfig.windowWidth : WINDOW_WIDTH;
        uint32_t h = m_gameConfig.isStandaloneGame ? m_gameConfig.windowHeight : WINDOW_HEIGHT;
        std::string title = m_gameConfig.isStandaloneGame ? m_gameConfig.gameTitle : "Tessera Game Engine";

        GLFWmonitor* monitor = nullptr;
        if (m_gameConfig.isStandaloneGame && m_gameConfig.fullscreen) {
            monitor = glfwGetPrimaryMonitor();
            if (monitor) {
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                if (mode) {
                    w = mode->width;
                    h = mode->height;
                }
            }
        }

        m_window = glfwCreateWindow(w, h, title.c_str(), monitor, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);
        glfwSetDropCallback(m_window, DropCallback);

        // Set engine window and taskbar icons
        GLFWimage icons[2];
        icons[0].width = EngineIcon::ICON_32_SIZE;
        icons[0].height = EngineIcon::ICON_32_SIZE;
        icons[0].pixels = const_cast<unsigned char*>(EngineIcon::ICON_32_RGBA);

        icons[1].width = EngineIcon::ICON_48_SIZE;
        icons[1].height = EngineIcon::ICON_48_SIZE;
        icons[1].pixels = const_cast<unsigned char*>(EngineIcon::ICON_48_RGBA);

        glfwSetWindowIcon(m_window, 2, icons);

#ifdef _WIN32
        if (!m_gameConfig.isStandaloneGame) {
            HWND hwnd = glfwGetWin32Window(m_window);
            if (hwnd) {
                s_appInstance = this;
                s_originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)CustomWindowProc);
            }
        }
#endif
    }

    static void FramebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<EngineApp*>(glfwGetWindowUserPointer(window));
        app->m_framebufferResized = true;
    }

    static void DropCallback(GLFWwindow* window, int count, const char** paths) {
        auto app = reinterpret_cast<EngineApp*>(glfwGetWindowUserPointer(window));
        for (int i = 0; i < count; ++i) {
            std::string path = paths[i];
            std::string ext = std::filesystem::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".tess") {
                app->OpenProject(path);
            } else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                app->ImportModel(path);
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                auto sel = app->m_scene.GetSelectedNode();
                if (sel) {
                    std::string targetPath = path;
                    if (app->m_project && app->m_project->IsLoaded()) {
                        targetPath = app->m_project->ImportTexture(path);
                    }
                    std::string stem = std::filesystem::path(path).stem().string();
                    std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                    if (stem.find("norm") != std::string::npos || stem.find("_n") != std::string::npos) {
                        sel->normalMapPath = targetPath;
                    } else if (stem.find("metal") != std::string::npos || stem.find("_m") != std::string::npos) {
                        sel->metallicMapPath = targetPath;
                    } else if (stem.find("rough") != std::string::npos || stem.find("_r") != std::string::npos) {
                        sel->roughnessMapPath = targetPath;
                    } else {
                        sel->texturePath = targetPath;
                    }
                }
            }
        }
    }

    void InitVulkan() {
        LOG("InitVulkan(): step 1 ctx.Init()");
        auto& ctx = VulkanContext::Get();
        ctx.Init(m_window);

        // Initialize Hardware Ray Tracing with active Vulkan device
        {
            auto& rt = Graphics::RayTracing::RayTracingManager::Get();
            rt.Initialize(Graphics::RayTracing::RayTracingBackendType::Vulkan_KHR);
            if (auto* vkRT = dynamic_cast<Graphics::RayTracing::VulkanRayTracing*>(rt.GetBackend())) {
                vkRT->SetDeviceHandles(ctx.GetPhysicalDevice(), ctx.GetDevice());
            }
        }

        // Initialize Meshlet GPU Virtualization subsystem
        // Initialize Meshlet GPU Virtualization subsystem
        Graphics::Meshlet::MeshletManager::Get().Initialize(ctx.GetPhysicalDevice(), ctx.GetDevice());

        // Initialize Vulkan Pipeline Cache (Zero Shader Stutter)
        Graphics::Pipeline::VulkanPipelineCache::Get().Initialize(ctx.GetDevice());

        // Initialize Temporal Upscaler (DLSS / FSR 3 / XeSS)
        Graphics::Upscaling::TemporalUpscaler::Get().Initialize();

        // Initialize World Partition & DirectStorage Streaming
        Streaming::WorldPartitionManager::GetInstance().Initialize();

        LOG("InitVulkan(): step 2 CreateSwapchain()");
        CreateSwapchain();
        LOG("InitVulkan(): step 3 CreateSwapchainImageViews()");
        CreateSwapchainImageViews();
        LOG("InitVulkan(): step 4 CreateMainRenderPass()");
        CreateMainRenderPass();
        LOG("InitVulkan(): step 5 CreateMainFramebuffers()");
        CreateMainFramebuffers();
        LOG("InitVulkan(): step 6 CreateSyncObjects()");
        CreateSyncObjects();
        LOG("InitVulkan(): step 7 CreateCommandBuffers()");
        CreateCommandBuffers();
        LOG("InitVulkan(): all basic Vulkan resources ready");
    }

    void InitRendererAndOffscreen() {
        LOG("InitRendererAndOffscreen(): step 1 m_offscreen.Init()");
        m_offscreen.Init(1280, 720);
        LOG("InitRendererAndOffscreen(): step 2 m_cameraPreviewOffscreen.Init()");
        m_cameraPreviewOffscreen.Init(1280, 720);
        // Planar reflection targets, one per viewport (same size, so screen UVs line up)
        m_editorMirrorOffscreen.Init(1280, 720);
        m_previewMirrorOffscreen.Init(1280, 720);
        // Reflection probe faces are rendered here, one per viewport, then copied into the probe array
        m_editorProbeOffscreen.Init(VulkanReflectionProbes::kFaceSize, VulkanReflectionProbes::kFaceSize);
        m_previewProbeOffscreen.Init(VulkanReflectionProbes::kFaceSize, VulkanReflectionProbes::kFaceSize);
        // Material previews: one shared target, and a studio scene around the previewed object
        m_materialPreviewTarget.Init(kMaterialPreviewSize, kMaterialPreviewSize);
        InitMaterialPreviewScene();
        LOG("InitRendererAndOffscreen(): step 3 m_pipeline.Init()");
        // Hardware ray tracing: the pipelines need its descriptor set layout (set 1) at creation
        if (auto* vkRT = dynamic_cast<Graphics::RayTracing::VulkanRayTracing*>(Graphics::RayTracing::RayTracingManager::Get().GetBackend())) {
            if (vkRT->IsSupported()) {
                m_pipeline.SetRayTracingSetLayout(vkRT->GetDescriptorSetLayout());
                vkRT->SetTextureResolver([this](const std::string& path, VkImageView& view, VkSampler& sampler) {
                    auto tex = m_pipeline.GetOrLoadTexture(path);
                    if (!tex) return false;
                    view = tex->GetImageView();
                    sampler = tex->GetSampler();
                    return view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE;
                });
                // On by default where the GPU supports it: ray-traced shadows and reflections
                auto& rtSettings = vkRT->GetSettings();
                rtSettings.enableHardwareRT = true;
                rtSettings.enableRTS = true;
                rtSettings.enableRTR = true;
                rtSettings.enableRTAO = false;
                rtSettings.enableRTGI = false;
            }
        }
        m_pipeline.Init(m_offscreen.GetRenderPass(), m_offscreen.GetPrepassRenderPass(), "shaders/vert.spv", "shaders/frag.spv");
        m_skinning.Init();
        // Mixed sound output (thunder, surf and crashes overlap each other and other sounds)
        if (!Audio::AudioEngine::Get().StartOutput()) LOG("[Audio] No sound device: mixed output is off");
        m_pipeline.SetSkinning(&m_skinning);
        m_pipeline.CreateOverlayPipeline(m_offscreen.GetOverlayRenderPass());
        m_fluidTargets[0].Init(m_offscreen.GetWidth(), m_offscreen.GetHeight());
        m_fluidTargets[1].Init(m_cameraPreviewOffscreen.GetWidth(), m_cameraPreviewOffscreen.GetHeight());
        m_pipeline.CreateFluidPipelines(m_fluidTargets[0].GetParticleRenderPass(), m_fluidTargets[0].GetThicknessRenderPass(),
                                        m_fluidTargets[0].GetSmoothRenderPass());
        m_fluidSim.Init();
        m_particleSim.Init();
        m_gpuProfiler.Init();
        // Built-in foliage meshes (grass, flowers, bush, trees, rock) for terrain foliage
        for (int kind = 0; kind < FoliageMeshes::Count; ++kind) {
            m_customMeshes[FoliageMeshes::MeshName(kind)] = VulkanBufferUtils::UploadMesh(FoliageMeshes::Build(kind));
        }
        BindViewTargets();
        LOG("InitRendererAndOffscreen(): complete");
    }

    // Hands every view's render-target images (scene copy, mirror image, SSAO) to the pipeline's
    // descriptor sets. Again after any target is resized, since its images are recreated.
    void BindViewTargets() {
        m_pipeline.SetViewportSSAOTexture(false, m_offscreen.GetSSAOImageView(), m_offscreen.GetSSAOSampler());
        m_pipeline.SetViewportSSAOTexture(true, m_cameraPreviewOffscreen.GetSSAOImageView(), m_cameraPreviewOffscreen.GetSSAOSampler());
        // Every view refracts its own opaque copy; the two main views show their mirror's image
        const std::pair<int, VulkanOffscreen*> viewTargets[] = {
            { VulkanPipeline::kViewEditor, &m_offscreen },
            { VulkanPipeline::kViewPreview, &m_cameraPreviewOffscreen },
            { VulkanPipeline::kViewEditorMirror, &m_editorMirrorOffscreen },
            { VulkanPipeline::kViewPreviewMirror, &m_previewMirrorOffscreen },
            { VulkanPipeline::kViewEditorProbe, &m_editorProbeOffscreen },
            { VulkanPipeline::kViewPreviewProbe, &m_previewProbeOffscreen },
            { VulkanPipeline::kViewMaterialPreview, &m_materialPreviewTarget },
        };
        for (const auto& [slot, target] : viewTargets) {
            m_pipeline.SetViewTexture(slot, VulkanPipeline::kSceneColorBinding, target->GetSceneColorImageView(), target->GetLinearSampler());
        }
        // Fluid images follow their view's size
        m_fluidTargets[0].Resize(m_offscreen.GetWidth(), m_offscreen.GetHeight());
        m_fluidTargets[1].Resize(m_cameraPreviewOffscreen.GetWidth(), m_cameraPreviewOffscreen.GetHeight());
        for (int view = 0; view < 2; ++view) {
            const VulkanFluidTargets& fluid = m_fluidTargets[view];
            m_pipeline.SetViewTexture(view, VulkanPipeline::kFluidDistanceBinding, fluid.GetDistanceView(), fluid.GetSampler());
            m_pipeline.SetViewTexture(view, VulkanPipeline::kFluidSurfaceBinding, fluid.GetSmoothDistanceView(), fluid.GetSampler());
            m_pipeline.SetViewTexture(view, VulkanPipeline::kFluidThicknessBinding, fluid.GetThicknessView(), fluid.GetSampler());
            m_pipeline.SetViewTexture(view, VulkanPipeline::kFluidColorBinding, fluid.GetColorView(), fluid.GetSampler());
            m_pipeline.SetViewTexture(view, VulkanPipeline::kFluidFirstSmoothBinding, fluid.GetFirstSmoothView(), fluid.GetSampler());
        }
        // Water in the main views reads the opaque depth from their prepass
        m_pipeline.SetViewTexture(VulkanPipeline::kViewEditor, VulkanPipeline::kSceneDepthBinding,
                                  m_offscreen.GetPrepassDepthImageView(), m_offscreen.GetPointSampler());
        m_pipeline.SetViewTexture(VulkanPipeline::kViewPreview, VulkanPipeline::kSceneDepthBinding,
                                  m_cameraPreviewOffscreen.GetPrepassDepthImageView(), m_cameraPreviewOffscreen.GetPointSampler());
        // Decals read the prepass normals too
        m_pipeline.SetViewTexture(VulkanPipeline::kViewEditor, VulkanPipeline::kSceneNormalBinding,
                                  m_offscreen.GetPrepassNormalImageView(), m_offscreen.GetPointSampler());
        m_pipeline.SetViewTexture(VulkanPipeline::kViewPreview, VulkanPipeline::kSceneNormalBinding,
                                  m_cameraPreviewOffscreen.GetPrepassNormalImageView(), m_cameraPreviewOffscreen.GetPointSampler());
        m_pipeline.SetViewTexture(VulkanPipeline::kViewEditor, VulkanPipeline::kMirrorBinding,
                                  m_editorMirrorOffscreen.GetHdrImageView(), m_editorMirrorOffscreen.GetLinearSampler());
        m_pipeline.SetViewTexture(VulkanPipeline::kViewPreview, VulkanPipeline::kMirrorBinding,
                                  m_previewMirrorOffscreen.GetHdrImageView(), m_previewMirrorOffscreen.GetLinearSampler());
    }

    // Renders each view at the size it is shown at, rather than stretching a fixed 1280x720 image
    // (which blurs everything and magnifies TAA's sub-pixel jitter): the editor viewport at its
    // panel's size, the game camera at the Game window's (or Camera Preview panel's) size, or the
    // window's size in a standalone game. Waits until a size has stayed the same for a few
    // frames, so dragging a splitter doesn't recreate the targets every frame. Call before the
    // UI is built for the frame.
    struct TargetSizer {
        glm::uvec2 pending{0, 0};
        int stableFrames{0};
    };
    // Returns true when main (and its mirror target, which must match: mirrors look their
    // reflection up by screen position) were resized
    bool SyncTargetSize(TargetSizer& sizer, const glm::vec2& wantedPixels, VulkanOffscreen& main, VulkanOffscreen& mirror) {
        if (wantedPixels.x < 1.0f || wantedPixels.y < 1.0f) return false;
        const uint32_t width = static_cast<uint32_t>(std::clamp(std::round(wantedPixels.x), 64.0f, 7680.0f));
        const uint32_t height = static_cast<uint32_t>(std::clamp(std::round(wantedPixels.y), 64.0f, 4320.0f));
        if (width == main.GetWidth() && height == main.GetHeight()) {
            sizer.stableFrames = 0;
            return false;
        }
        if (width != sizer.pending.x || height != sizer.pending.y) {
            sizer.pending = glm::uvec2(width, height);
            sizer.stableFrames = 0;
            return false;
        }
        if (++sizer.stableFrames < 6) return false;
        main.Resize(width, height);
        mirror.Resize(width, height);
        sizer.stableFrames = 0;
        return true;
    }

    void SyncViewSizes() {
        bool resized = false;
        if (!m_gameConfig.isStandaloneGame && m_editorUI.GetStartupState() == EditorUI::StartupState::Editor) {
            if (SyncTargetSize(m_editorViewSizer, m_editorUI.GetViewportPixelSize(), m_offscreen, m_editorMirrorOffscreen)) {
                m_prevViewProjValid[0] = false;
                resized = true;
                LOG("Editor viewport render size: " << m_offscreen.GetWidth() << "x" << m_offscreen.GetHeight());
            }
        }
        glm::vec2 gameSize(0.0f);
        if (m_gameConfig.isStandaloneGame) {
            int fbWidth = 0, fbHeight = 0;
            glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
            gameSize = glm::vec2(static_cast<float>(fbWidth), static_cast<float>(fbHeight));
        } else if (m_editorUI.GetStartupState() == EditorUI::StartupState::Editor) {
            gameSize = m_editorUI.GetGameImagePixelSize();
        }
        if (SyncTargetSize(m_gameViewSizer, gameSize, m_cameraPreviewOffscreen, m_previewMirrorOffscreen)) {
            m_prevViewProjValid[1] = false;
            resized = true;
            LOG("Game view render size: " << m_cameraPreviewOffscreen.GetWidth() << "x" << m_cameraPreviewOffscreen.GetHeight());
        }
        if (resized) BindViewTargets();
    }

    void InitImGui() {
        auto& ctx = VulkanContext::Get();

        // 1. Create dedicated descriptor pool for ImGui
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
        poolInfo.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(poolSizes));
        poolInfo.pPoolSizes = poolSizes;

        if (vkCreateDescriptorPool(ctx.GetDevice(), &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool!");
        }

        // 2. Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        Localization::Get().Init();
        m_editorUI.Init();

        // 3. Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForVulkan(m_window, true);

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = ctx.GetInstance();
        initInfo.PhysicalDevice = ctx.GetPhysicalDevice();
        initInfo.Device = ctx.GetDevice();
        initInfo.QueueFamily = ctx.GetQueueIndices().graphicsFamily.value();
        initInfo.Queue = ctx.GetGraphicsQueue();
        initInfo.PipelineCache = VK_NULL_HANDLE;
        initInfo.DescriptorPool = m_imguiDescriptorPool;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = static_cast<uint32_t>(m_swapchainImages.size());
        initInfo.PipelineInfoMain.RenderPass = m_mainRenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&initInfo);
    }

    bool LoadModelAsset(const std::string& filepath, std::string& outStoredMeshName, LoadedMesh& outLoadedMesh) {
        LOG("Loading 3D Model Asset: " << filepath);

        // If a project is loaded, copy the asset into the project's models directory
        std::string actualPath = filepath;
        outStoredMeshName = std::filesystem::path(filepath).filename().string();
        if (m_project && m_project->IsLoaded()) {
            std::error_code ec;
            std::filesystem::path projModels = std::filesystem::canonical(std::filesystem::path(m_project->GetModelsDirectory()), ec);
            std::filesystem::path fileP = std::filesystem::canonical(filepath, ec);
            if (ec || fileP.parent_path() != projModels) {
                outStoredMeshName = m_project->ImportAsset(filepath);
            }
            actualPath = (std::filesystem::path(m_project->GetModelsDirectory()) / outStoredMeshName).string();
        } else if (!std::filesystem::exists(actualPath)) {
            if (std::filesystem::exists(std::string("assets/models/") + filepath)) {
                actualPath = std::string("assets/models/") + filepath;
            } else if (std::filesystem::exists(std::string("projects/Headland/") + filepath)) {
                actualPath = std::string("projects/Headland/") + filepath;
            }
        }

        std::string ext = std::filesystem::path(actualPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool loaded = false;
        if (ext == ".fbx") {
            loaded = FbxLoader::Load(actualPath, outLoadedMesh, true, filepath) || FbxLoader::Load(filepath, outLoadedMesh, true);
        } else if (ext == ".gltf" || ext == ".glb") {
            loaded = GltfLoader::Load(actualPath, outLoadedMesh, true) || GltfLoader::Load(filepath, outLoadedMesh, true);
        } else {
            loaded = ObjLoader::Load(actualPath, outLoadedMesh, true) || ObjLoader::Load(filepath, outLoadedMesh, true);
        }

        if (!loaded) {
            LOG("Failed to load model file: " << filepath);
            return false;
        }

        auto& ctx = VulkanContext::Get();
        VkDevice device = ctx.GetDevice();
        vkDeviceWaitIdle(device);

        std::filesystem::path p(filepath);
        if (outStoredMeshName.empty()) outStoredMeshName = p.filename().string();

        // If mesh was already uploaded/cached, clean up old GPU resources first to allow fresh upload
        auto existingIt = m_customMeshes.find(outStoredMeshName);
        if (existingIt != m_customMeshes.end()) {
            existingIt->second.Cleanup(device);
        }

        GPUMesh gpuMesh = VulkanBufferUtils::UploadMesh(outLoadedMesh.meshData);
        m_customMeshes[outStoredMeshName] = gpuMesh;
        m_cpuMeshes[outStoredMeshName] = outLoadedMesh;

        return true;
    }

    bool AssignModelToNode(const std::string& filepath, Node& targetNode, bool forceReload = false) {
        if (filepath.empty()) return false;

        std::string storedMeshName = std::filesystem::path(filepath).filename().string();
        LoadedMesh loadedMesh;

        auto it = m_cpuMeshes.find(filepath);
        if (it == m_cpuMeshes.end()) it = m_cpuMeshes.find(storedMeshName);

        if (!forceReload && it != m_cpuMeshes.end()) {
            storedMeshName = it->first;
            loadedMesh = it->second;
        } else {
            if (!LoadModelAsset(filepath, storedMeshName, loadedMesh)) {
                return false;
            }
        }

        targetNode.type = ShapeType::Custom;
        targetNode.customMeshName = storedMeshName;
        targetNode.boundsMin = loadedMesh.boundsMin;
        targetNode.boundsMax = loadedMesh.boundsMax;
        targetNode.vertexCount = loadedMesh.vertexCount;
        targetNode.triangleCount = loadedMesh.triangleCount;

        if (!loadedMesh.diffuseTexturePath.empty() && targetNode.texturePath.empty()) {
            targetNode.texturePath = loadedMesh.diffuseTexturePath;
        }
        if (!loadedMesh.normalMapPath.empty() && targetNode.normalMapPath.empty()) {
            targetNode.normalMapPath = loadedMesh.normalMapPath;
        }
        if (!loadedMesh.metallicMapPath.empty() && targetNode.metallicMapPath.empty()) {
            targetNode.metallicMapPath = loadedMesh.metallicMapPath;
        }
        if (!loadedMesh.roughnessMapPath.empty() && targetNode.roughnessMapPath.empty()) {
            targetNode.roughnessMapPath = loadedMesh.roughnessMapPath;
        }
        if (!loadedMesh.materialName.empty() && targetNode.materialName.empty()) {
            targetNode.materialName = loadedMesh.materialName;
        }

        targetNode.hasSkeleton = loadedMesh.hasSkeleton;
        targetNode.skeletonName = loadedMesh.skeletonName;
        targetNode.boneCount = static_cast<uint32_t>(loadedMesh.boneNames.size());
        targetNode.boneNames = loadedMesh.boneNames;
        targetNode.animClipNames = loadedMesh.animClipNames;
        if (!loadedMesh.animClipNames.empty()) {
            targetNode.currentAnimState = loadedMesh.animClipNames[0];
        } else if (targetNode.hasSkeleton) {
            targetNode.currentAnimState = "Idle";
        }

        auto charNode = std::dynamic_pointer_cast<CharacterNode>(m_scene.GetNodeById(targetNode.id));
        if (charNode) {
            for (const auto& clip : loadedMesh.animClipNames) {
                std::string lowerClip = clip;
                std::transform(lowerClip.begin(), lowerClip.end(), lowerClip.begin(), ::tolower);
                if (lowerClip.find("idle") != std::string::npos && charNode->idleAnimClip.empty()) {
                    charNode->idleAnimClip = clip;
                } else if ((lowerClip.find("walk") != std::string::npos || lowerClip.find("run") != std::string::npos) && charNode->walkAnimClip.empty()) {
                    charNode->walkAnimClip = clip;
                }
            }
        }

        LOG("Assigned 3D model asset '" << storedMeshName << "' to Node " << targetNode.name << " (ID: " << targetNode.id << ")");
        return true;
    }

    std::vector<std::string> GetAvailableModels() {
        std::vector<std::string> models;
        for (const auto& pair : m_customMeshes) {
            models.push_back(pair.first);
        }
        std::vector<std::string> searchDirs;
        if (m_project && m_project->IsLoaded()) {
            searchDirs.push_back(m_project->GetModelsDirectory());
        }
        searchDirs.push_back("assets/models");

        for (const auto& dir : searchDirs) {
            std::error_code ec;
            if (std::filesystem::exists(dir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") {
                            std::string filename = entry.path().filename().string();
                            if (std::find(models.begin(), models.end(), filename) == models.end()) {
                                models.push_back(filename);
                            }
                        }
                    }
                }
            }
        }
        std::sort(models.begin(), models.end());
        return models;
    }

    bool ImportModel(const std::string& filepath) {
        std::string storedMeshName;
        LoadedMesh loadedMesh;
        if (!LoadModelAsset(filepath, storedMeshName, loadedMesh)) {
            return false;
        }

        std::filesystem::path p(filepath);
        std::string nodeName = p.stem().string();
        if (nodeName.empty()) nodeName = "Imported Model";

        auto node = m_scene.AddCustomMeshNode(
            storedMeshName,
            loadedMesh.boundsMin,
            loadedMesh.boundsMax,
            loadedMesh.vertexCount,
            loadedMesh.triangleCount,
            nodeName
        );

        if (node) {
            node->color = loadedMesh.diffuseColor;
            node->texturePath = loadedMesh.diffuseTexturePath;
            node->normalMapPath = loadedMesh.normalMapPath;
            node->metallicMapPath = loadedMesh.metallicMapPath;
            node->roughnessMapPath = loadedMesh.roughnessMapPath;
            node->materialName = loadedMesh.materialName;
            node->roughness = loadedMesh.roughness;
            node->metallic = loadedMesh.metallic;

            node->hasSkeleton = loadedMesh.hasSkeleton;
            node->skeletonName = loadedMesh.skeletonName;
            node->boneCount = static_cast<uint32_t>(loadedMesh.boneNames.size());
            node->boneNames = loadedMesh.boneNames;
            node->animClipNames = loadedMesh.animClipNames;
            if (!loadedMesh.animClipNames.empty()) {
                node->currentAnimState = loadedMesh.animClipNames[0];
            } else if (node->hasSkeleton) {
                node->currentAnimState = "Idle";
            }

            m_scene.SelectNode(node->id);
            m_camera.FocusOn(node->GetWorldPosition(m_scene));
        }

        LOG("Successfully imported 3D model node: " << nodeName << " (id=" << (node ? node->id : 0) << ")");
        return true;
    }

    bool ImportObj(const std::string& filepath) {
        return ImportModel(filepath);
    }

    void UpdateWindowTitle() {
        std::string title = "Tessera Game Engine";
        if (m_project && m_project->IsLoaded()) {
            title += " - [" + m_project->GetName() + " - " + m_project->GetActiveSceneFile() + "]";
            m_editorUI.SetProjectName(m_project->GetName());
            m_editorUI.SetProjectRoot(m_project->GetRootPath());
            Scripting::LuaScriptSystem::Get().SetProjectRoot(m_project->GetRootPath());
        } else {
            title += " - [Untitled Project - " + m_scene.GetName() + "]";
            m_editorUI.SetProjectName("Untitled Project");
            Scripting::LuaScriptSystem::Get().SetProjectRoot("");
        }
        glfwSetWindowTitle(m_window, title.c_str());
    }

    bool CreateNewScene(const std::string& name) {
        if (!m_project || !m_project->IsLoaded()) {
            CreateNewProject("projects", "DefaultProject");
        }
        if (m_project && m_project->IsLoaded()) {
            bool ok = m_project->CreateNewScene(name, m_scene);
            if (ok) {
                m_project->Save(m_scene, m_camera, m_editorUI);
                UpdateWindowTitle();
                LOG("Created new scene: " << name);
            }
            return ok;
        }
        return false;
    }

    // Loads and uploads a model a scene uses (once per mesh key)
    bool LoadSceneMesh(const std::string& fullPath, const std::string& meshKey) {
        // Several nodes can use one model: load and upload it once (re-uploading leaked the old buffers)
        if (m_customMeshes.count(meshKey) && m_cpuMeshes.count(meshKey)) return true;
        LoadedMesh loadedMesh;
        std::string ext = std::filesystem::path(fullPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        bool ok = false;
        if (ext == ".gltf" || ext == ".glb") {
            ok = GltfLoader::Load(fullPath, loadedMesh, true);
        } else if (ext == ".fbx") {
            ok = FbxLoader::Load(fullPath, loadedMesh, true);
        } else {
            ok = ObjLoader::Load(fullPath, loadedMesh, true);
        }
        if (ok) {
            GPUMesh gpuMesh = VulkanBufferUtils::UploadMesh(loadedMesh.meshData);
            m_customMeshes[meshKey] = gpuMesh;
            m_cpuMeshes[meshKey] = loadedMesh;
            return true;
        }
        return false;
    }

    bool OpenScene(const std::string& sceneFileName) {
        if (m_project && m_project->IsLoaded()) {
            auto loadMeshCb = [this](const std::string& fullPath, const std::string& meshKey) -> bool { return LoadSceneMesh(fullPath, meshKey); };
            bool ok = m_project->LoadScene(sceneFileName, m_scene, loadMeshCb);
            if (ok) {
                UpdateWindowTitle();
                LOG("Opened scene: " << sceneFileName);
            }
            return ok;
        }
        return false;
    }

    bool SaveScene() {
        if (m_project && m_project->IsLoaded()) {
            StreamingSaveGuard guard(*this);
            bool ok = m_project->SaveActiveScene(m_scene);
            LOG("SaveScene result: " << (ok ? "SUCCESS" : "FAILED"));
            return ok;
        }
        return false;
    }

    bool SaveSceneAs(const std::string& name) {
        if (m_project && m_project->IsLoaded()) {
            StreamingSaveGuard guard(*this);
            bool ok = m_project->SaveSceneAs(name, m_scene);
            if (ok) {
                UpdateWindowTitle();
                LOG("SaveSceneAs result: SUCCESS (" << name << ")");
            }
            return ok;
        }
        return false;
    }

    bool CreateNewProject(const std::string& parentDir, const std::string& name) {
        auto& ctx = VulkanContext::Get();
        vkDeviceWaitIdle(ctx.GetDevice());

        // Clear existing custom meshes from GPU
        for (auto& pair : m_customMeshes) {
            pair.second.Cleanup(ctx.GetDevice());
        }
        m_customMeshes.clear();
        m_cpuMeshes.clear();

        m_project = Project::CreateNew(parentDir, name, m_scene);
        if (m_project) {
            m_camera.Reset();
            m_project->Save(m_scene, m_camera, m_editorUI);
            UpdateWindowTitle();
            LOG("Created new project: " << m_project->GetFilePath());
            return true;
        }
        return false;
    }

    bool OpenProject(const std::string& agprojPath) {
        auto& ctx = VulkanContext::Get();
        vkDeviceWaitIdle(ctx.GetDevice());

        // Clear existing custom meshes from GPU
        for (auto& pair : m_customMeshes) {
            pair.second.Cleanup(ctx.GetDevice());
        }
        m_customMeshes.clear();
        m_cpuMeshes.clear();

        auto loadMeshCb = [this](const std::string& fullPath, const std::string& meshKey) -> bool {
            // Several nodes can use one model: load and upload it once (re-uploading leaked the old buffers)
            if (m_customMeshes.count(meshKey) && m_cpuMeshes.count(meshKey)) return true;
            LoadedMesh loadedMesh;
            std::string ext = std::filesystem::path(fullPath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            bool ok = false;
            if (ext == ".gltf" || ext == ".glb") {
                ok = GltfLoader::Load(fullPath, loadedMesh, true);
            } else if (ext == ".fbx") {
                ok = FbxLoader::Load(fullPath, loadedMesh, true);
            } else {
                ok = ObjLoader::Load(fullPath, loadedMesh, true);
            }
            if (ok) {
                GPUMesh gpuMesh = VulkanBufferUtils::UploadMesh(loadedMesh.meshData);
                m_customMeshes[meshKey] = gpuMesh;
                m_cpuMeshes[meshKey] = loadedMesh;
                return true;
            }
            return false;
        };

        m_project = Project::Load(agprojPath, m_scene, m_camera, m_editorUI, loadMeshCb);
        if (m_project) {
            UpdateWindowTitle();
            LOG("Successfully opened project: " << agprojPath);
            return true;
        }
        return false;
    }

    bool SaveProject() {
        if (m_project && m_project->IsLoaded()) {
            StreamingSaveGuard guard(*this);
            bool ok = m_project->Save(m_scene, m_camera, m_editorUI);
            LOG("SaveProject result: " << (ok ? "SUCCESS" : "FAILED"));
            return ok;
        } else {
            m_editorUI.OpenSaveProjectAsDialog();
            return false;
        }
    }

    bool SaveProjectAs(const std::string& parentDir, const std::string& name) {
        if (!m_project) {
            m_project = std::make_shared<Project>();
        }
        bool ok = m_project->SaveAs(parentDir, name, m_scene, m_camera, m_editorUI);
        if (ok) {
            UpdateWindowTitle();
            LOG("SaveProjectAs succeeded: " << m_project->GetFilePath());
        }
        return ok;
    }

    void InitScene() {
        m_editorUI.SetWindowMinimizeCallback([this]() {
            if (m_window) glfwIconifyWindow(m_window);
        });
        m_editorUI.SetWindowMaximizeCallback([this]() {
            if (m_window) {
                if (glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED)) {
                    glfwRestoreWindow(m_window);
                } else {
                    glfwMaximizeWindow(m_window);
                }
            }
        });
        m_editorUI.SetWindowCloseCallback([this]() {
            if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        });
        m_editorUI.SetWindowMoveCallback([this](float dx, float dy) {
            if (m_window) {
                int x, y;
                glfwGetWindowPos(m_window, &x, &y);
                glfwSetWindowPos(m_window, x + static_cast<int>(dx), y + static_cast<int>(dy));
            }
        });
        m_editorUI.SetIsWindowMaximizedCallback([this]() -> bool {
            if (m_window) {
                return glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0;
            }
            return false;
        });

        m_editorUI.SetImportCallback([this](const std::string& path) {
            ImportObj(path);
        });
        m_editorUI.SetNewProjectCallback([this](const std::string& p, const std::string& n) {
            CreateNewProject(p, n);
        });
        m_editorUI.SetOpenProjectCallback([this](const std::string& path) {
            OpenProject(path);
        });
        m_editorUI.SetSaveProjectCallback([this]() {
            SaveProject();
        });
        m_editorUI.SetSaveProjectAsCallback([this](const std::string& p, const std::string& n) {
            SaveProjectAs(p, n);
        });
        m_editorUI.SetNewSceneCallback([this](const std::string& n) {
            CreateNewScene(n);
        });
        m_editorUI.SetOpenSceneCallback([this](const std::string& p) {
            OpenScene(p);
        });
        m_editorUI.SetSaveSceneCallback([this]() {
            SaveScene();
        });
        m_editorUI.SetSaveSceneAsCallback([this](const std::string& n) {
            SaveSceneAs(n);
        });
        m_editorUI.SetGetScenesCallback([this]() -> std::vector<std::string> {
            if (m_project) return m_project->GetAvailableScenes();
            return {};
        });
        m_editorUI.SetGetActiveSceneCallback([this]() -> std::string {
            if (m_project) return m_project->GetActiveSceneFile();
            return m_scene.GetName();
        });

        // Upload procedural primitive meshes to GPU
        m_editorUI.SetMaterialPreviewCallback([this](const std::string& key, const Node& material, int shape) {
            return RequestMaterialPreview(key, material, shape);
        });
        m_editorUI.SetImportTextureCallback([this](const std::string& srcPath) -> std::string {
            if (m_project && m_project->IsLoaded()) {
                return m_project->ImportTexture(srcPath);
            }
            return srcPath;
        });

        // The editor's animation timeline shows and drives the selected character's own clock
        m_editorUI.SetGetSkeletalAnimInfoCallback([this](const std::string& meshKey, float& outTime, float& outDuration, bool& outPlaying) -> bool {
            auto it = m_cpuMeshes.find(meshKey);
            if (it != m_cpuMeshes.end()) {
                auto selected = m_scene.GetSelectedNode();
                const bool usesMesh = selected && selected->customMeshName == meshKey;
                outTime = usesMesh ? selected->animationTime : 0.0f;
                outPlaying = usesMesh && selected->animationPreviewPlaying;
                outDuration = 1.0f;
                for (const auto& c : it->second.skeletalClips) {
                    outDuration = (std::max)(outDuration, c->duration);
                }
                return true;
            }
            return false;
        });

        m_editorUI.SetSkeletalScrubCallback([this](const std::string& meshKey, const std::string& clipName, float time, bool isPlaying) {
            if (m_cpuMeshes.find(meshKey) == m_cpuMeshes.end()) return;
            // The selected character when it uses this model, otherwise every character that does
            auto selected = m_scene.GetSelectedNode();
            for (auto& node : m_scene.GetNodes()) {
                if (!node || node->customMeshName != meshKey) continue;
                if (selected && selected->customMeshName == meshKey && node != selected) continue;
                node->animationTime = time;
                node->animationPreviewPlaying = isPlaying;
            }
            UpdateSkeletalAnimations(0.0f, false);
        });

        m_editorUI.SetGenerateLODsCallback([this](const std::string& baseMeshKey, Node& targetNode) -> bool {
            auto it = m_cpuMeshes.find(baseMeshKey);
            if (it == m_cpuMeshes.end()) return false;

            const MeshData& baseMesh = it->second.meshData;
            auto chain = Geometry::MeshSimplifier::GenerateLODChain(baseMesh);

            targetNode.lodLevels.clear();
            for (size_t i = 0; i < chain.levels.size(); ++i) {
                std::string lodKey = baseMeshKey + "_LOD" + std::to_string(i + 1);

                // Upload to GPU
                m_customMeshes[lodKey] = VulkanBufferUtils::UploadMesh(chain.levels[i]);

                // Cache CPU copy
                LoadedMesh lodCpu = it->second;
                lodCpu.meshData = chain.levels[i];
                lodCpu.vertexCount = static_cast<uint32_t>(chain.levels[i].vertices.size());
                lodCpu.triangleCount = static_cast<uint32_t>(chain.levels[i].indices.size() / 3);
                m_cpuMeshes[lodKey] = lodCpu;

                LODLevel lvl;
                lvl.meshKey = lodKey;
                lvl.distance = chain.suggestedDistances[i];
                lvl.reductionRatio = chain.reductionRatios[i];
                lvl.triangleCount = chain.triangleCounts[i];
                targetNode.lodLevels.push_back(lvl);
            }
            targetNode.enableLOD = true;
            return true;
        });

        m_editorUI.SetAssignModelCallback([this](const std::string& filepath, Node& targetNode) -> bool {
            return AssignModelToNode(filepath, targetNode, true);
        });

        m_editorUI.SetGetAvailableModelsCallback([this]() -> std::vector<std::string> {
            return GetAvailableModels();
        });

        // CPU copies of the primitives: ray tracing builds its acceleration structures from them
        m_cpuPrimitives[ShapeType::Cube] = MeshGenerator::CreateCube(1.0f);
        m_cpuPrimitives[ShapeType::Sphere] = MeshGenerator::CreateSphere(0.5f);
        m_cpuPrimitives[ShapeType::Cone] = MeshGenerator::CreateCone(0.5f, 1.0f);
        m_cpuPrimitives[ShapeType::Cylinder] = MeshGenerator::CreateCylinder(0.5f, 1.0f);
        m_cpuPrimitives[ShapeType::Plane] = MeshGenerator::CreatePlane(2.0f);
        m_cpuPrimitives[ShapeType::Quad] = MeshGenerator::CreateQuad(1.0f);
        m_meshes[ShapeType::Cube] = VulkanBufferUtils::UploadMesh(MeshGenerator::CreateCube(1.0f));
        m_meshes[ShapeType::Sphere] = VulkanBufferUtils::UploadMesh(MeshGenerator::CreateSphere(0.5f));
        m_meshes[ShapeType::Cone] = VulkanBufferUtils::UploadMesh(MeshGenerator::CreateCone(0.5f, 1.0f));
        m_meshes[ShapeType::Cylinder] = VulkanBufferUtils::UploadMesh(MeshGenerator::CreateCylinder(0.5f, 1.0f));
        m_meshes[ShapeType::Plane] = VulkanBufferUtils::UploadMesh(MeshGenerator::CreatePlane(2.0f));
        m_meshes[ShapeType::Quad] = VulkanBufferUtils::UploadMesh(MeshGenerator::CreateQuad(1.0f));

        // Upload ground grid and center axes meshes
        m_gridMesh = VulkanBufferUtils::UploadMesh(MeshGenerator::CreateGrid(40.0f, 40));
        m_axesMesh = VulkanBufferUtils::UploadMesh(MeshGenerator::CreateAxes(40.0f));

        if (m_gameConfig.isStandaloneGame) {
            // Standalone Game Mode
            m_gameLoop.SetStandaloneMode(true);
            // 1. Load custom input bindings if present
            if (std::filesystem::exists("input_bindings.json")) {
                Core::InputManager::Get().LoadFromFile("input_bindings.json");
            }

            // 2. Load starting scene
            std::string sceneToLoad = m_gameConfig.startingScene;
            if (!m_gameConfig.startingProject.empty() && std::filesystem::exists(m_gameConfig.startingProject)) {
                auto prj = Project::Load(m_gameConfig.startingProject, m_scene, m_camera, m_editorUI, nullptr);
                if (prj) {
                    m_project = prj;
                    GameLocalization::Get().SetProjectRoot(prj->GetRootPath());
                    if (!m_gameConfig.sceneOnCommandLine) {
                        sceneToLoad = (std::filesystem::path(prj->GetRootPath()) / "scenes" / prj->GetActiveSceneFile()).string();
                    }
                }
            }
            if (GameLocalization::Get().GetAvailableLanguages().empty()) {
                std::filesystem::path scPath(sceneToLoad);
                std::filesystem::path candidate = scPath.parent_path();
                while (!candidate.empty() && candidate != candidate.root_path()) {
                    if (std::filesystem::exists(candidate / "assets" / "localization") ||
                        std::filesystem::exists(candidate / "localization")) {
                        GameLocalization::Get().SetProjectRoot(candidate.string());
                        break;
                    }
                    candidate = candidate.parent_path();
                }
            }
            if (!std::filesystem::exists(sceneToLoad)) {
                if (std::filesystem::exists("scenes/" + sceneToLoad)) {
                    sceneToLoad = "scenes/" + sceneToLoad;
                } else if (std::filesystem::exists("scenes/main.scene")) {
                    sceneToLoad = "scenes/main.scene";
                }
            }

            if (std::filesystem::exists(sceneToLoad)) {
                std::cout << "[Standalone] Loading game scene: " << sceneToLoad << std::endl;
                auto standaloneLoadMeshCb = [this](const std::string& rawPath, const std::string& meshKey) -> bool {
                    // Several nodes can use one model: load and upload it once (re-uploading leaked the old buffers)
                    if (m_customMeshes.count(meshKey) && m_cpuMeshes.count(meshKey)) return true;
                    std::string fullPath = rawPath;
                    if (!std::filesystem::exists(fullPath)) {
                        if (std::filesystem::exists("assets/models/" + rawPath)) fullPath = "assets/models/" + rawPath;
                        else if (std::filesystem::exists("assets/" + rawPath)) fullPath = "assets/" + rawPath;
                        else if (std::filesystem::exists("projects/Headland/" + rawPath)) fullPath = "projects/Headland/" + rawPath;
                        else if (std::filesystem::exists("projects/Headland/assets/" + rawPath)) fullPath = "projects/Headland/assets/" + rawPath;
                    }
                    LoadedMesh loadedMesh;
                    std::string ext = std::filesystem::path(fullPath).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    bool ok = false;
                    if (ext == ".gltf" || ext == ".glb") ok = GltfLoader::Load(fullPath, loadedMesh, true);
                    else if (ext == ".fbx") ok = FbxLoader::Load(fullPath, loadedMesh, true);
                    else ok = ObjLoader::Load(fullPath, loadedMesh, true);
                    if (ok) {
                        m_customMeshes[meshKey] = VulkanBufferUtils::UploadMesh(loadedMesh.meshData);
                        m_cpuMeshes[meshKey] = loadedMesh;
                        return true;
                    }
                    return false;
                };
                // A world manifest: its base scene is loaded; sectors stream in during play
                if (std::filesystem::path(sceneToLoad).extension() == ".world") {
                    Streaming::WorldManifest manifest;
                    std::string error;
                    if (Streaming::WorldManifest::Load(sceneToLoad, manifest, &error)) {
                        m_editorStreamer.SetHooks(MakeStreamHooks(false, manifest.rootDir));
                        m_editorStreamer.Open(manifest);
                        sceneToLoad = manifest.BaseScenePath();
                        m_worldBoundScenePath = sceneToLoad;
                    } else {
                        std::cerr << "[Standalone] " << error << std::endl;
                    }
                }
                m_scene.LoadFromFile(sceneToLoad, standaloneLoadMeshCb);
            } else {
                std::cout << "[Standalone] Starting scene not found (" << sceneToLoad << "), using default scene." << std::endl;
                m_scene.ResetToDefault();
            }

            // 3. Ensure a camera exists for rendering
            if (!m_scene.GetActiveCamera()) {
                auto cam = m_scene.AddCameraNode("MainCamera");
                cam->position = glm::vec3(0.0f, 3.0f, 8.0f);
            }

            // 4. Ensure lighting exists
            if (m_scene.GetLights().empty()) {
                auto sun = m_scene.AddLightNode(LightType::Directional, "SunLight");
                sun->position = glm::vec3(5.0f, 10.0f, 5.0f);
                sun->lightIntensity = 1.0f;
            }

            // 4b. --start-at: the camera starts at a named node (viewpoints for screenshots)
            if (!m_gameConfig.startAt.empty()) {
                auto spot = m_scene.GetNodeByName(m_gameConfig.startAt);
                auto cam = m_scene.GetActiveCamera();
                if (spot && cam) {
                    cam->position = spot->GetWorldPosition(m_scene);
                    cam->rotation = spot->rotation;
                } else {
                    std::cerr << "[Standalone] --start-at: no node named '" << m_gameConfig.startAt << "'" << std::endl;
                }
            }

            // 5. Start Gameplay Simulation
            std::cout << "[Standalone] Initializing and starting game loop..." << std::endl;
            m_gameLoop.Play(m_scene);
        } else {
            // Editor Mode: Ensure an active project and its generated files always exist on disk
            bool projectLoaded = false;
            if (!m_gameConfig.startingProject.empty() && std::filesystem::exists(m_gameConfig.startingProject)) {
                if (OpenProject(m_gameConfig.startingProject)) {
                    projectLoaded = true;
                }
            }
            if (!projectLoaded) {
                auto recents = Project::GetRecentProjects();
                for (const auto& r : recents) {
                    if (std::filesystem::exists(r)) {
                        if (OpenProject(r)) {
                            projectLoaded = true;
                            break;
                        }
                    }
                }
            }
            if (!projectLoaded) {
                CreateNewProject("projects", "DefaultProject");
            }
            // A scene named on the command line is opened as it is (the project isn't changed)
            if (m_gameConfig.sceneOnCommandLine && std::filesystem::exists(m_gameConfig.startingScene)) {
                if (m_scene.LoadFromFile(m_gameConfig.startingScene, [this](const std::string& path, const std::string& key) { return LoadSceneMesh(path, key); })) {
                    LOG("Opened scene: " << m_gameConfig.startingScene);
                } else {
                    LOG("Could not open scene: " << m_gameConfig.startingScene);
                }
            }
            if (!m_gameConfig.selectNode.empty()) {
                if (auto node = m_scene.GetNodeByName(m_gameConfig.selectNode)) m_scene.SelectNode(static_cast<int32_t>(node->id));
                else LOG("No node named " << m_gameConfig.selectNode << " to select");
            }

            UpdateWindowTitle();
            // Start engine with the splash screen which leads to the Project Launcher Hub!
            // (--screenshot or explicit --project goes straight to the editor so it can capture the 3D viewport)
            if (!m_gameConfig.startingProject.empty() || !m_gameConfig.screenshotPath.empty()) {
                m_editorUI.SetStartupState(EditorUI::StartupState::Editor);
            } else {
                m_editorUI.SetStartupState(EditorUI::StartupState::Splash);
            }
        }
    }

    // Poses every animated character (clip, then IK) and queues it for GPU skinning this frame.
    // The edited scene is posed too while a play session runs, so the editor viewport stays live.
    void UpdateSkeletalAnimations(float dt, bool isPlaying) {
        m_skinJobs.clear();

        Scene* playScene = (m_gameLoop.GetMode() == Gameplay::EngineMode::Play && m_gameLoop.GetPlayScene())
                           ? m_gameLoop.GetPlayScene().get() : nullptr;
        std::vector<Scene*> scenes = { playScene ? playScene : &m_scene };
        if (playScene && playScene != &m_scene) scenes.push_back(&m_scene);

        for (Scene* scene : scenes) {
            for (auto& node : scene->GetNodes()) {
                if (!node || !node->hasSkeleton || node->customMeshName.empty()) continue;
                auto it = m_cpuMeshes.find(node->customMeshName);
                if (it == m_cpuMeshes.end()) {
                    std::string fname = std::filesystem::path(node->customMeshName).filename().string();
                    it = m_cpuMeshes.find(fname);
                }
                if (it == m_cpuMeshes.end()) continue;
                auto& loaded = it->second;
                if (!loaded.hasSkeleton || !loaded.skeleton || loaded.skeletalClips.empty() || loaded.skinData.empty()) continue;

                // Find clip matching node->currentAnimState
                std::shared_ptr<Animation::SkeletalAnimationClip> activeClip = nullptr;
                for (auto& clip : loaded.skeletalClips) {
                    if (clip->name == node->currentAnimState) {
                        activeClip = clip;
                        break;
                    }
                }
                if (!activeClip && !loaded.skeletalClips.empty()) {
                    activeClip = loaded.skeletalClips[0];
                }
                if (!activeClip) continue;

                // Each character has its own clock: advance it in play mode, or while the editor previews it
                bool shouldAdvance = node->playAnimation && (isPlaying || node->animationPreviewPlaying);
                if (shouldAdvance && dt > 0.0f) {
                    node->animationTime += dt * loaded.animSpeed * std::max(node->animationSpeed, 0.0f);
                    if (activeClip->duration > 0.001f) {
                        if (loaded.animLoop) {
                            node->animationTime = std::fmod(node->animationTime, activeClip->duration);
                        } else if (node->animationTime > activeClip->duration) {
                            node->animationTime = activeClip->duration;
                        }
                    }
                }
                // Where this character is in the clip, after its start offset
                float sampleTime = node->animationTime + std::max(node->animationStartOffset, 0.0f);
                if (activeClip->duration > 0.001f) {
                    sampleTime = loaded.animLoop ? std::fmod(sampleTime, activeClip->duration)
                                                 : std::min(sampleTime, activeClip->duration);
                }

                // Like Godot: show the rest (bind) pose until animation is enabled and played or scrubbed away from 0
                if (!node->playAnimation || (!shouldAdvance && node->animationTime <= 0.0f)) {
                    for (auto& bone : loaded.skeleton->bones) {
                        bone.currentPosition = bone.bindPosition;
                        bone.currentRotation = bone.bindRotation;
                        bone.currentScale = bone.bindScale;
                    }
                } else {
                    activeClip->Apply(*loaded.skeleton, sampleTime);
                }

                // Apply Inverse Kinematics (Two-Bone IK) if enabled on this node
                if (node->enableIK && loaded.skeleton->bones.size() >= 3) {
                    int endIdx = loaded.skeleton->FindBoneIndex(node->ikTargetBone);
                    if (endIdx < 0) endIdx = static_cast<int>(loaded.skeleton->bones.size()) - 1;
                    const auto& bones = loaded.skeleton->bones;
                    int midIdx = (endIdx >= 0 && endIdx < (int)bones.size()) ? bones[endIdx].parentId : -1;
                    int rootIdx = (midIdx >= 0 && midIdx < (int)bones.size()) ? bones[midIdx].parentId : -1;
                    if (rootIdx >= 0 && midIdx >= 0 && endIdx >= 0) {
                        Animation::IKSolver::SolveTwoBoneIK(*loaded.skeleton, rootIdx, midIdx, endIdx, node->ikTargetPosition, node->ikPoleVector, node->ikWeight);
                    }
                }

                // Procedural morph sliders, applied in skinning.comp
                glm::vec4 morphs(0.0f);
                for (const auto& [morphName, morphW] : node->morphWeights) {
                    if (std::abs(morphW) <= 0.0001f) continue;
                    if (morphName == "Smile / Facial") morphs.x += morphW;
                    else if (morphName == "Blink") morphs.y += morphW;
                    else if (morphName == "Muscle Bulk") morphs.z += morphW;
                    else morphs.w += morphW;
                }

                SkinJob job;
                job.node = node.get();
                job.meshKey = it->first;
                job.gpuMeshName = node->customMeshName;
                job.loaded = &loaded;
                job.centerScale = glm::vec4(loaded.center, loaded.scaleFactor);
                job.morphs = morphs;
                // Skinning matrices: WorldBone * InverseBind
                loaded.skeleton->EvaluateSkinningMatrices(job.bones);
                m_skinJobs.push_back(std::move(job));
            }
        }
    }

    // Uploads each animated model's bind pose once, then queues this frame's characters. Call
    // after the frame's fence wait, before recording any pass that draws them.
    void SubmitSkinning(VkCommandBuffer cmd) {
        m_skinning.BeginFrame(m_currentFrame);
        for (const SkinJob& job : m_skinJobs) {
            // (Re)upload when the model is new or was re-imported (its vertex array was replaced)
            const void* sourceId = job.loaded->meshData.vertices.data();
            auto known = m_skinSourceIds.find(job.meshKey);
            if (!m_skinning.HasSource(job.meshKey) || known == m_skinSourceIds.end() || known->second != sourceId) {
                m_skinSourceIds[job.meshKey] = sourceId;
                const LoadedMesh& loaded = *job.loaded;
                std::vector<VulkanSkinning::SourceVertex> source(loaded.meshData.vertices.size());
                for (size_t i = 0; i < source.size(); ++i) {
                    const Vertex& v = loaded.meshData.vertices[i];
                    VulkanSkinning::SourceVertex& out = source[i];
                    out.positionU = glm::vec4(v.pos, v.texCoord.x);
                    out.normalV = glm::vec4(v.normal, v.texCoord.y);
                    out.color = v.color;
                    if (i < loaded.skinData.size()) {
                        const VertexSkinData& skin = loaded.skinData[i];
                        const float total = skin.boneWeights[0] + skin.boneWeights[1] + skin.boneWeights[2] + skin.boneWeights[3];
                        if (total > 0.0001f) {
                            // Skinned from the raw bind pose; the shader centres and scales the result
                            out.positionU = glm::vec4(skin.origPos, v.texCoord.x);
                            out.normalV = glm::vec4(skin.origNormal, v.texCoord.y);
                            out.bones = glm::ivec4(skin.boneIndices[0], skin.boneIndices[1], skin.boneIndices[2], skin.boneIndices[3]);
                            out.weights = glm::vec4(skin.boneWeights[0], skin.boneWeights[1], skin.boneWeights[2], skin.boneWeights[3]);
                        }
                    }
                }
                m_skinning.SetSource(job.meshKey, source);
            }

            auto gpuIt = m_customMeshes.find(job.gpuMeshName);
            if (gpuIt == m_customMeshes.end()) {
                gpuIt = m_customMeshes.find(std::filesystem::path(job.gpuMeshName).filename().string());
            }
            if (gpuIt == m_customMeshes.end()) continue;
            m_skinning.Submit(job.node, job.meshKey, gpuIt->second, job.bones, job.centerScale, job.morphs);
        }
        m_skinning.Record(cmd);
    }

    void MainLoop() {
        double lastTime = glfwGetTime();
        int screenshotFrameCounter = 0;
        while (!glfwWindowShouldClose(m_window)) {
            ++m_uiFrameCounter; // material previews asked for during this frame's UI
            double currentTime = glfwGetTime();
            float deltaTime = static_cast<float>(currentTime - lastTime);
            lastTime = currentTime;

            // Clamp delta time to 0.1s to prevent huge jumps
            if (deltaTime > 0.1f) deltaTime = 0.1f;

            glfwPollEvents();

            // Begin ImGui frame immediately so input events from glfwPollEvents are available to game loop
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // In standalone game, Escape unlocks pointer or closes game if already unlocked
            if (m_gameConfig.isStandaloneGame && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                if (m_gameLoop.IsPointerLocked()) {
                    m_gameLoop.SetPointerLocked(false);
                } else {
                    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
                }
            }

            OceanWaves::SetTime(static_cast<float>(glfwGetTime()));

            // Run decoupled game loop (fixed updates, player input, collisions, animation)
            if (m_gameConfig.isStandaloneGame) {
                m_gameLoop.Update(m_scene, deltaTime, true);
                if (m_gameLoop.GetPlayScene()) {
                    Scripting::LuaScriptSystem::Get().SetLocalPeerId(m_editorUI.GetNetworkManager().GetLocalPeerId());
                    m_editorUI.GetNetworkManager().Update(*m_gameLoop.GetPlayScene(), deltaTime);
                }
            } else {
                bool isGameFocused = m_editorUI.IsGameWindowFocused();
                if (m_editorUI.GetStartupState() == EditorUI::StartupState::Editor) {
                    m_gameLoop.Update(m_scene, deltaTime, isGameFocused);
                }
            }

            // Update Game Profiler with isolated game simulation stats
            Monitoring::GameProfiler::Get().Update(m_gameLoop.GetPlayScene().get(), &m_gameLoop, &m_editorUI.GetNetworkManager(), deltaTime);

            bool isPlayingMode = (m_gameLoop.GetMode() == Gameplay::EngineMode::Play);
            UpdateSkeletalAnimations(deltaTime, isPlayingMode);

            // Time of day: the clock runs in Play, and while editing when the scene asks for it.
            // The weather always runs for the scene being shown, so presets can be previewed.
            {
                Scene& clockScene = m_gameLoop.GetActivePlayScene(m_scene);
                const bool playingScene = isPlayingMode && &clockScene != &m_scene;
                if (playingScene || clockScene.GetEnvironment().clockRunsInEditor) SkyClock::Advance(clockScene.GetEnvironment(), deltaTime);
                SceneEnvironment& weatherEnv = clockScene.GetEnvironment();
                Weather::Update(weatherEnv, deltaTime);
                // Thunder plays in Play only (not while editing), louder the nearer the strike
                if (weatherEnv.thunderDue) {
                    weatherEnv.thunderDue = false;
                    if (playingScene && weatherEnv.thunderVolume > 0.0f) {
                        const float closeness = std::clamp(1.0f - (weatherEnv.thunderDistance - 200.0f) / 1800.0f, 0.0f, 1.0f);
                        auto& audio = Audio::AudioEngine::Get();
                        const auto wav = Audio::AudioEngine::GenerateThunderWav(static_cast<uint32_t>(weatherEnv.strikeCount) * 9781u + 17u, closeness);
                        // Each of the last four strikes keeps its own clip, so thunders can overlap
                        const std::string clip = "Thunder" + std::to_string(m_thunderClip);
                        m_thunderClip = (m_thunderClip + 1) % 4;
                        audio.LoadWavFromMemory(clip, wav.data(), wav.size());
                        const float gain = std::clamp(weatherEnv.thunderVolume, 0.0f, 1.0f) * (0.35f + 0.65f * closeness);
                        const bool played = audio.IsOutputRunning() ? audio.Play2D(clip, "Ambient", gain) != 0
                                                                    : audio.TriggerHardwarePlayback(clip, "Ambient", gain, gain, 1.0f, false, 0.3f, 0.85f, 0.5f);
                        LOG("[Weather] Thunder from " << static_cast<int>(weatherEnv.thunderDistance) << " m" << (played ? "" : " (audio muted)"));
                    }
                }
            }

            // Baked lighting: a bake runs a few probes per frame so the editor stays responsive
            const bool editorReady = !m_gameConfig.isStandaloneGame && m_editorUI.GetStartupState() == EditorUI::StartupState::Editor;
            if ((editorReady && m_editorUI.ConsumeGIBakeRequest()) || (m_gameConfig.bakeLighting && !m_bakeRequestedAtStartup)) {
                m_bakeRequestedAtStartup = true;
                if (!m_giBake.active && !m_lightmapBaking) StartLightingBake();
            }
            if (m_lightmapBaking) StepLightmapBake();
            if (m_giBake.active) StepGIBake();

            DrawFrame();

            // In the editor, frames count once the editor itself (not the project hub) is showing
            const bool screenshotReady = (m_gameConfig.isStandaloneGame || m_editorUI.GetStartupState() == EditorUI::StartupState::Editor) &&
                                         !m_giBake.active && !m_lightmapBaking && (!m_gameConfig.bakeLighting || m_bakeRequestedAtStartup);
            if (!m_gameConfig.screenshotPath.empty() && screenshotReady &&
                ++screenshotFrameCounter >= m_gameConfig.screenshotFrame) {
                SaveGameScreenshot(m_gameConfig.screenshotPath);
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
        }
        vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
        if (m_gameConfig.gpuProfileLog) {
            if (!m_gpuProfiler.Supported()) {
                LOG("[GPU Profile] Timestamps aren't supported on this GPU");
            } else {
                LOG("[GPU Profile] GPU frame " << m_gpuProfiler.AverageFrameMs() << " ms (smoothed)");
                for (const auto& t : m_gpuProfiler.Timings()) {
                    char line[160];
                    std::snprintf(line, sizeof(line), "[GPU Profile] %*s%-*s %7.3f ms", t.depth * 2, "", 28 - t.depth * 2, t.name.c_str(), t.averageMs);
                    LOG(line);
                }
            }
        }
    }

    // Loads the scene's colour LUT when its path changes and hands it to both viewports' composite
    // pass. Waits for the GPU, so call it outside command buffer recording.
    void UpdateColorLUT(const std::string& path) {
        if (path == m_colorLutPath) return;
        m_colorLutPath = path; // also stops a bad file being retried every frame

        uint32_t size = 0;
        std::shared_ptr<VulkanTexture> lut = path.empty() ? nullptr : VulkanTexture::LoadColorLUT(path, size);
        vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
        for (VulkanOffscreen* target : { &m_offscreen, &m_cameraPreviewOffscreen }) {
            target->SetColorLUT(lut ? lut->GetImageView() : VK_NULL_HANDLE, lut ? lut->GetSampler() : VK_NULL_HANDLE, size);
        }
        if (m_colorLut) m_colorLut->Cleanup();
        m_colorLut = lut;
    }

    static void FillLightEntry(const std::shared_ptr<Node>& light, const Scene& sc, LightData& outL) {
        glm::mat4 t = light->GetWorldTransformMatrix(sc);
        glm::vec3 worldPos = glm::vec3(t[3]);
        glm::vec3 forwardDir = -glm::normalize(glm::vec3(t[2]));
        if (glm::length(glm::vec3(t[2])) < 0.0001f) forwardDir = glm::vec3(0.0f, -1.0f, 0.0f);

        if (light->lightType == LightType::Point) {
            outL.position = glm::vec4(worldPos, 1.0f);
        } else if (light->lightType == LightType::Spot) {
            outL.position = glm::vec4(worldPos, 2.0f);
            if (glm::length(glm::vec3(t[1])) > 0.0001f) forwardDir = -glm::normalize(glm::vec3(t[1]));
        } else if (light->lightType == LightType::AreaRect) {
            outL.position = glm::vec4(worldPos, 3.0f);
        } else {
            outL.position = glm::vec4(-forwardDir, 0.0f);
        }
        outL.color = glm::vec4(light->lightColor, light->lightIntensity);
        outL.spotDirAndRange = glm::vec4(forwardDir, std::max(0.5f, light->lightRange));
        float innerRad = glm::radians(std::clamp(light->spotInnerConeAngle, 1.0f, 89.0f));
        float outerRad = glm::radians(std::clamp(std::max(light->spotOuterConeAngle, light->spotInnerConeAngle + 0.5f), 1.5f, 89.5f));
        outL.spotAnglesAndShadow = glm::vec4(std::cos(innerRad), std::cos(outerRad), light->castShadows ? 1.0f : 0.0f, light->shadowBias);
        if (light->lightType == LightType::AreaRect) {
            outL.position = glm::vec4(worldPos, 3.0f);
            const glm::vec3 upRef = std::abs(forwardDir.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 basisRight = glm::normalize(glm::cross(upRef, forwardDir));
            const glm::vec3 basisUp = glm::cross(forwardDir, basisRight);
            const glm::vec3 nodeRight = glm::length(glm::vec3(t[0])) > 1e-4f ? glm::normalize(glm::vec3(t[0])) : basisRight;
            const float rollAngle = std::atan2(glm::dot(nodeRight, basisUp), glm::dot(nodeRight, basisRight));
            outL.spotAnglesAndShadow = glm::vec4(std::max(light->areaLightSize.x * 0.5f, 0.05f),
                                                 std::max(light->areaLightSize.y * 0.5f, 0.05f),
                                                 light->areaLightTwoSided ? 1.0f : 0.0f,
                                                 rollAngle);
        }
    }

    // The scene's lights (directional, point and spot), up to kMaxSceneLights. With the time of
    // day on, the clock points the first directional light at the sun, or the moon at night.
    static std::vector<LightData> CollectLights(const Scene& sc) {
        auto nodes = sc.GetLights();
        std::vector<LightData> out(std::min(nodes.size(), static_cast<size_t>(kMaxSceneLights)));
        for (size_t i = 0; i < out.size(); ++i) FillLightEntry(nodes[i], sc, out[i]);
        if (sc.GetEnvironment().timeOfDayEnabled) {
            for (LightData& light : out) {
                if (light.position.w >= 0.5f) continue;
                const SkyClock::State clock = SkyClock::Evaluate(sc.GetEnvironment());
                light.position = glm::vec4(clock.lightDir, 0.0f);
                light.color = glm::vec4(glm::vec3(light.color) * clock.lightTint, light.color.a * clock.lightScale);
                break;
            }
        }
        return out;
    }

    // ---------------- Baked lighting (irradiance volume) ----------------
    // Probes on a grid each render the scene around them (6 cube faces) and store the incoming
    // light as L1 spherical harmonics. Every bounce after the first renders with the previous
    // bounce's volume lighting the surfaces, so light keeps bouncing. Runs a few probes per frame.
    static constexpr uint32_t kGIFaceSize = 32;

    struct GIBakeState {
        bool active{false};
        int bounce{1};
        int bounces{2};
        size_t next{0};
        glm::vec3 minCorner{0.0f};
        glm::vec3 size{1.0f};
        glm::uvec3 dims{2};
        std::vector<glm::vec4> red, green, blue;
    };
    GIBakeState m_giBake;
    bool m_bakeRequestedAtStartup{false};
    VulkanOffscreen m_giBakeTarget;
    bool m_giBakeTargetReady{false};
    struct BakedGIInfo {
        bool loaded{false};
        glm::vec3 minCorner{0.0f};
        glm::vec3 size{1.0f};
        glm::uvec3 dims{1};
    };
    BakedGIInfo m_bakedGI;
    std::string m_loadedGIPath;

    size_t GIProbeCount() const { return static_cast<size_t>(m_giBake.dims.x) * m_giBake.dims.y * m_giBake.dims.z; }

    void StartGIBake() {
        const auto& env = m_scene.GetEnvironment();
        glm::vec3 lo(1e9f), hi(-1e9f);
        for (const auto& node : m_scene.GetNodes()) {
            if (node && node->isGIVolume) {
                const glm::vec3 center = node->GetWorldPosition(m_scene);
                lo = center - node->giVolumeSize * 0.5f;
                hi = center + node->giVolumeSize * 0.5f;
                break;
            }
        }
        if (lo.x > hi.x) {
            // No GI Volume node: the bounds of the scene's meshes, plus a little room
            for (const auto& node : m_scene.GetNodes()) {
                if (!node || node->IsUINode() || (node->nodeType != NodeType::Mesh && node->nodeType != NodeType::Character)) continue;
                const glm::vec3 bmin = node->type == ShapeType::Custom ? node->boundsMin : glm::vec3(-0.5f);
                const glm::vec3 bmax = node->type == ShapeType::Custom ? node->boundsMax : glm::vec3(0.5f);
                const glm::mat4 world = node->GetWorldTransformMatrix(m_scene);
                for (int c = 0; c < 8; ++c) {
                    const glm::vec3 corner((c & 1) ? bmax.x : bmin.x, (c & 2) ? bmax.y : bmin.y, (c & 4) ? bmax.z : bmin.z);
                    const glm::vec3 w = glm::vec3(world * glm::vec4(corner, 1.0f));
                    lo = glm::min(lo, w);
                    hi = glm::max(hi, w);
                }
            }
            if (lo.x > hi.x) {
                m_editorUI.SetGIBakeStatus("Nothing to bake: the scene has no meshes.", false);
                return;
            }
            lo -= glm::vec3(0.5f);
            hi += glm::vec3(0.5f);
        }

        float spacing = std::max(env.bakedGISpacing, 0.25f);
        glm::uvec3 dims;
        for (int attempt = 0; attempt < 16; ++attempt) {
            const glm::vec3 counts = glm::round((hi - lo) / spacing) + 1.0f;
            dims = glm::uvec3(glm::clamp(counts, glm::vec3(2.0f), glm::vec3(64.0f)));
            if (static_cast<size_t>(dims.x) * dims.y * dims.z <= 32768) break;
            spacing *= 1.25f; // keep the bake to a sensible size
        }
        m_giBake = GIBakeState{};
        m_giBake.active = true;
        m_giBake.bounces = std::clamp(env.bakedGIBounces, 1, 4);
        m_giBake.minCorner = lo;
        m_giBake.size = glm::max(hi - lo, glm::vec3(0.01f));
        m_giBake.dims = dims;
        const size_t count = GIProbeCount();
        m_giBake.red.assign(count, glm::vec4(0.0f));
        m_giBake.green.assign(count, glm::vec4(0.0f));
        m_giBake.blue.assign(count, glm::vec4(0.0f));
        // The old bake isn't shown while the new one is built
        m_bakedGI.loaded = false;
        if (!m_giBakeTargetReady) {
            m_giBakeTarget.Init(kGIFaceSize, kGIFaceSize);
            m_giBakeTargetReady = true;
        }
        LOG("Baking lighting: " << dims.x << "x" << dims.y << "x" << dims.z << " probes, " << m_giBake.bounces << " bounce(s)");
    }

    // One cube face of a probe, rendered like a probe capture (no SSAO, fog or volumetrics)
    UniformBufferObject BuildGIBakeUBO(const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& up,
                                       const std::vector<LightData>& lights) {
        const auto& env = m_scene.GetEnvironment();
        UniformBufferObject ubo{};
        ubo.view = glm::lookAt(pos, pos + dir, up);
        ubo.proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 500.0f);
        ubo.proj[1][1] *= -1.0f;
        ubo.cameraPos = glm::vec4(pos, 1.0f);
        ubo.numLights = static_cast<int>(lights.size());
        m_editorUI.PopulateEnvironmentUBO(m_scene, ubo, lights);
        if (env.useHDRI && m_pipeline.HasEnvironmentHDRI()) {
            ubo.envParams = glm::vec4(1.0f, env.hdriIntensity, glm::radians(env.hdriRotation), 0.0f);
            ubo.skyParams.w = 0.0f;
        } else {
            ubo.envParams = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        }
        ubo.timeParams.x = static_cast<float>(glfwGetTime());
        ubo.unjitteredViewProj = ubo.prevViewProj = ubo.proj * ubo.view;
        ubo.postProcessParams.y = 0.0f; // no SSAO
        ubo.postProcessParams.w = 0.0f; // no volumetric fog
        ubo.fogColorAndDensity.a = 0.0f;
        ubo.probeParams = glm::vec4(0.0f);
        // Lightmapped surfaces show their bounce light in the captures (the bake runs after them)
        if (m_pipeline.HasLightmaps()) ubo.lightmapParams = glm::vec4(1.0f, std::max(env.bakedGIIntensity, 0.0f), 0.0f, 0.0f);
        if (m_giBake.bounce > 1) {
            // Surfaces are lit by the previous bounce, so the light bounces again
            ubo.giVolumeMin = glm::vec4(m_giBake.minCorner, 1.0f);
            ubo.giVolumeSize = glm::vec4(m_giBake.size, 1.0f);
            ubo.giVolumeDims = glm::vec4(glm::vec3(m_giBake.dims), 0.0f);
        }
        return ubo;
    }

    void StepGIBake() {
        auto& ctx = VulkanContext::Get();
        vkDeviceWaitIdle(ctx.GetDevice()); // frame resources are borrowed below
        const auto lights = CollectLights(m_scene);
        m_pipeline.SetViewLights(VulkanPipeline::kViewEditor, lights);
        static const glm::vec3 kFaceDirs[6] = {
            { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
        };
        const size_t count = GIProbeCount();
        const double start = glfwGetTime();
        std::vector<uint16_t> pixels;
        while (m_giBake.next < count && glfwGetTime() - start < 0.04) {
            const size_t index = m_giBake.next++;
            const glm::uvec3 cell(index % m_giBake.dims.x, (index / m_giBake.dims.x) % m_giBake.dims.y,
                                  index / (static_cast<size_t>(m_giBake.dims.x) * m_giBake.dims.y));
            const glm::vec3 pos = m_giBake.minCorner + m_giBake.size * glm::vec3(cell) / glm::vec3(m_giBake.dims - 1u);

            glm::vec4 sh[3] = {}; // per colour channel: (L0, L1x, L1y, L1z)
            for (int face = 0; face < 6; ++face) {
                const glm::vec3 f = kFaceDirs[face];
                const glm::vec3 up = std::abs(f.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
                m_pipeline.BeginFrame(0);
                UniformBufferObject ubo = BuildGIBakeUBO(pos, f, up, lights);
                VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
                m_pipeline.RenderShadowPass(cmd, m_scene, m_meshes, m_customMeshes, VulkanPipeline::kViewEditor,
                                            m_scene.GetEnvironment().shadowDistance, ubo);
                m_pipeline.UpdateUniformBufferSlot(VulkanPipeline::kViewEditorProbe, ubo);
                m_giBakeTarget.BeginRenderPass(cmd);
                m_pipeline.RenderScene(cmd, m_scene, m_meshes, m_customMeshes, PolygonMode::Solid, false,
                                       VulkanPipeline::kViewEditorProbe, pos, ScenePhase::Opaque);
                m_giBakeTarget.NextScenePass(cmd);
                m_pipeline.RenderScene(cmd, m_scene, m_meshes, m_customMeshes, PolygonMode::Solid, false,
                                       VulkanPipeline::kViewEditorProbe, pos, ScenePhase::Transparent);
                m_giBakeTarget.EndRenderPass(cmd);
                ctx.EndSingleTimeCommands(cmd);
                m_giBakeTarget.ReadbackHdrImage(pixels);

                // Project the face onto L1 SH (same face layout as SampleProbe in env_common.glsl)
                const glm::vec3 side = glm::normalize(glm::cross(f, up));
                const glm::vec3 faceUp = glm::cross(side, f);
                const float n = static_cast<float>(kGIFaceSize);
                for (uint32_t y = 0; y < kGIFaceSize; ++y) {
                    for (uint32_t x = 0; x < kGIFaceSize; ++x) {
                        const float u = (x + 0.5f) / n * 2.0f - 1.0f;
                        const float v = (y + 0.5f) / n * 2.0f - 1.0f;
                        const glm::vec3 d = glm::normalize(f + u * side - v * faceUp);
                        const float solidAngle = 4.0f / (n * n) / std::pow(1.0f + u * u + v * v, 1.5f);
                        const size_t p = (static_cast<size_t>(y) * kGIFaceSize + x) * 4;
                        glm::vec3 radiance(glm::unpackHalf1x16(pixels[p]), glm::unpackHalf1x16(pixels[p + 1]),
                                           glm::unpackHalf1x16(pixels[p + 2]));
                        // Very bright texels (the sun's disc) are direct light, already added in real time
                        const float luma = glm::dot(radiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                        if (luma > 8.0f) radiance *= 8.0f / luma;
                        for (int c = 0; c < 3; ++c) {
                            sh[c] += glm::vec4(0.282095f, 0.488603f * d) * (radiance[c] * solidAngle);
                        }
                    }
                }
            }
            m_giBake.red[index] = sh[0];
            m_giBake.green[index] = sh[1];
            m_giBake.blue[index] = sh[2];
        }

        char status[160];
        std::snprintf(status, sizeof(status), "Baking: bounce %d of %d, probe %zu of %zu",
                      m_giBake.bounce, m_giBake.bounces, m_giBake.next, count);
        m_editorUI.SetGIBakeStatus(status, true);
        if (m_giBake.next < count) return;

        // Bounce finished: it lights the next bounce's surfaces, or it's the result
        m_pipeline.SetBakedGIVolume(m_giBake.dims, m_giBake.red, m_giBake.green, m_giBake.blue);
        if (m_giBake.bounce < m_giBake.bounces) {
            ++m_giBake.bounce;
            m_giBake.next = 0;
            return;
        }
        m_giBake.active = false;
        auto& env = m_scene.GetEnvironment();
        std::string path = m_scene.GetFilePath();
        path = path.empty() ? std::string("baked_lighting.gi") : path + ".gi";
        if (SaveBakedGI(path)) {
            env.bakedGIPath = path;
            env.bakedGIEnabled = true;
            // A running play session (or standalone game) shows the bake too
            auto& playEnv = m_gameLoop.GetActivePlayScene(m_scene).GetEnvironment();
            playEnv.bakedGIPath = path;
            playEnv.bakedGIEnabled = true;
            m_loadedGIPath = path;
            m_bakedGI = { true, m_giBake.minCorner, m_giBake.size, m_giBake.dims };
            m_editorUI.SetGIBakeStatus("Baked " + std::to_string(count) + " probes" + m_lightmapSummary + ". Save the scene to keep it.", false);
            LOG("Baked lighting saved: " << path);
        } else {
            m_editorUI.SetGIBakeStatus("Bake finished but could not be saved to " + path, false);
        }
    }

    // File: "TGI1", dims (3 x uint32), first probe (3 floats), size (3 floats), then the red,
    // green and blue SH arrays (vec4 per probe each)
    bool SaveBakedGI(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        const uint32_t magic = 0x31494754; // "TGI1"
        out.write(reinterpret_cast<const char*>(&magic), 4);
        out.write(reinterpret_cast<const char*>(&m_giBake.dims), sizeof(glm::uvec3));
        out.write(reinterpret_cast<const char*>(&m_giBake.minCorner), sizeof(glm::vec3));
        out.write(reinterpret_cast<const char*>(&m_giBake.size), sizeof(glm::vec3));
        for (const auto* channel : { &m_giBake.red, &m_giBake.green, &m_giBake.blue }) {
            out.write(reinterpret_cast<const char*>(channel->data()), static_cast<std::streamsize>(channel->size() * sizeof(glm::vec4)));
        }
        return static_cast<bool>(out);
    }

    // Loads the scene's baked lighting when its path changes (empty = none). Waits for the GPU.
    void UpdateBakedGI(const std::string& path) {
        if (path == m_loadedGIPath || m_giBake.active) return;
        m_loadedGIPath = path;
        m_bakedGI.loaded = false;
        if (path.empty()) return;
        std::ifstream in(path, std::ios::binary);
        uint32_t magic = 0;
        BakedGIInfo info;
        in.read(reinterpret_cast<char*>(&magic), 4);
        in.read(reinterpret_cast<char*>(&info.dims), sizeof(glm::uvec3));
        in.read(reinterpret_cast<char*>(&info.minCorner), sizeof(glm::vec3));
        in.read(reinterpret_cast<char*>(&info.size), sizeof(glm::vec3));
        const size_t count = static_cast<size_t>(info.dims.x) * info.dims.y * info.dims.z;
        if (!in || magic != 0x31494754 || count == 0 || count > 262144 || glm::any(glm::lessThan(info.dims, glm::uvec3(2)))) {
            LOG("Could not read baked lighting: " << path);
            return;
        }
        std::vector<glm::vec4> channels[3];
        for (auto& channel : channels) {
            channel.resize(count);
            in.read(reinterpret_cast<char*>(channel.data()), static_cast<std::streamsize>(count * sizeof(glm::vec4)));
        }
        if (!in) {
            LOG("Baked lighting file is truncated: " << path);
            return;
        }
        m_pipeline.SetBakedGIVolume(info.dims, channels[0], channels[1], channels[2]);
        info.loaded = true;
        m_bakedGI = info;
    }

    // Ocean spray where waves hit rocks, and whether each main view's camera is under water
    OceanWaves::Spray m_oceanSpray;
    std::map<std::string, std::pair<uint32_t, double>> m_uploadedSplats; // terrain splat maps on the GPU: version, upload time
    // Ocean sound (in Play): the surf's wash (or its rumble under water) and waves crashing on rocks
    uint32_t m_surfVoice{0};
    uint32_t m_underwaterVoice{0};
    bool m_oceanSoundsLoaded{false};
    float m_crashCooldown{0.0f};
    int m_crashVariant{0};
    int m_thunderClip{0};
    int m_crashesPlayed{0};

    void UpdateOceanAudio(Scene& scene, bool playing, float dt) {
        auto& audio = Audio::AudioEngine::Get();
        if (!audio.IsOutputRunning()) return;
        std::shared_ptr<Node> ocean;
        for (const auto& node : scene.GetNodes()) {
            if (node && node->isOcean) { ocean = node; break; }
        }
        auto camera = scene.GetActiveCamera();
        if (!playing || !ocean || !camera) {
            for (uint32_t* voice : { &m_surfVoice, &m_underwaterVoice }) {
                if (*voice) audio.StopVoice(*voice);
                *voice = 0;
            }
            return;
        }
        if (!m_oceanSoundsLoaded) {
            const auto surf = Audio::AudioEngine::GenerateSurfWav(11u, false);
            audio.LoadWavFromMemory("OceanSurf", surf.data(), surf.size());
            const auto under = Audio::AudioEngine::GenerateSurfWav(23u, true);
            audio.LoadWavFromMemory("OceanUnderwater", under.data(), under.size());
            for (int v = 0; v < 3; ++v) {
                const auto crash = Audio::AudioEngine::GenerateCrashWav(101u + v * 37u);
                audio.LoadWavFromMemory("OceanCrash" + std::to_string(v), crash.data(), crash.size());
            }
            m_oceanSoundsLoaded = true;
        }
        if (!m_surfVoice || !audio.IsVoicePlaying(m_surfVoice)) {
            m_surfVoice = audio.Play2D("OceanSurf", "Ambient", 0.0f, 1.0f, true);
            if (m_surfVoice) LOG("[Ocean] Surf sound playing");
        }
        if (!m_underwaterVoice || !audio.IsVoicePlaying(m_underwaterVoice)) m_underwaterVoice = audio.Play2D("OceanUnderwater", "Ambient", 0.0f, 1.0f, true);

        // The wash fades with height above the water and grows with the waves; under the
        // surface only the muffled rumble is left
        const glm::vec3 eye = camera->GetWorldPosition(scene);
        const float level = ocean->GetWorldPosition(scene).y;
        const float height = eye.y - level;
        const float waves = std::clamp(ocean->waveHeight, 0.2f, 1.5f);
        const glm::vec2 half = ocean->oceanSize * 0.5f;
        const glm::vec3 center = ocean->GetWorldPosition(scene);
        const float outside = std::max(std::abs(eye.x - center.x) - half.x, std::abs(eye.z - center.z) - half.y);
        const float nearOcean = std::clamp(1.0f - std::max(outside, 0.0f) / 150.0f, 0.0f, 1.0f);
        const float above = std::pow(std::clamp(1.0f - height / 80.0f, 0.0f, 1.0f), 1.5f);
        const float master = std::clamp(ocean->oceanSoundVolume, 0.0f, 1.0f);
        audio.SetVoiceVolume(m_surfVoice, height >= 0.0f ? master * above * nearOcean * (0.3f + 0.4f * waves) : 0.0f);
        audio.SetVoiceVolume(m_underwaterVoice, height < 0.0f ? master : 0.0f);

        // A crash for each wave breaking on a rock nearby (a few a second at most)
        m_crashCooldown -= dt;
        for (const auto& burst : m_oceanSpray.Bursts()) {
            const float distance = glm::distance(burst.position, eye);
            if (distance > 150.0f || m_crashCooldown > 0.0f || height < 0.0f) continue;
            const float volume = std::clamp(0.4f + 0.6f * burst.strength / 1.5f, 0.0f, 1.0f) * std::clamp(ocean->oceanSoundVolume, 0.0f, 1.0f);
            const uint32_t crash = audio.Play3D("OceanCrash" + std::to_string(m_crashVariant), burst.position, "Ambient", volume, 6.0f, 150.0f);
            if (crash && m_crashesPlayed++ == 0) LOG("[Ocean] Waves crashing (" << static_cast<int>(distance) << " m away)");
            m_crashVariant = (m_crashVariant + 1) % 3;
            m_crashCooldown = 0.2f;
        }
    }
    Liquids::Simulation m_liquids; // liquid surfaces, pour streams and splashes
    VulkanFluidSim m_fluidSim;     // fluid volumes, simulated on the GPU
    VulkanParticleSim m_particleSim; // particle emitters in GPU mode
    VulkanGpuProfiler m_gpuProfiler;   // per-pass GPU timings (timestamp queries)
    VulkanFluidTargets m_fluidTargets[2]; // screen-space fluid images for the editor and game views
    bool m_cameraUnderwater[2]{false, false};
    bool m_cameraHalfUnderwater[2]{false, false};
    float m_waterlineSide[2]{1.0f, 1.0f};
    glm::vec4 m_waterline[2][2]{};

    // ---- Baked lightmaps (Renderer/LightmapBaker.h) ----
    // Bake Lighting first bakes lightmaps for Lightmap Static meshes on worker threads (the editor
    // keeps running), then the probe volume, whose captures then include the lightmaps' bounce light.
    Lightmap::Baker m_lightmapBaker;
    bool m_lightmapBaking{false};
    std::vector<Lightmap::NodeLayout> m_lightmapLayouts;
    uint32_t m_lightmapAtlasSize{0};
    std::string m_lightmapSummary;  // added to the bake's final status
    std::string m_loadedLightmapPath;
    int m_lightmapLoadAttempts{0};
    int m_lightmapRetryFrames{0};
    std::map<std::string, glm::vec3> m_averageTextureColors;

    void StartLightingBake() {
        m_lightmapSummary.clear();
        if (m_scene.GetEnvironment().bakeLightmaps && StartLightmapBake()) return;
        StartGIBake();
    }

    // Opaque meshes that never move: lightmapped when Lightmap Static is on
    static bool IsStaticOpaqueMesh(const Node& node) {
        if (node.IsUINode() || node.nodeType != NodeType::Mesh || node.hasSkeleton || node.isMirror || node.isOcean || node.isLiquid) return false;
        if (node.rigidBodyType == RigidBodyType::Dynamic || node.rigidBodyType == RigidBodyType::Kinematic) return false;
        return node.color.a >= 0.99f || node.alphaCutoff > 0.0f;
    }

    // The mesh data a node is drawn from: primitives are regenerated, models come from their CPU copy
    const MeshData* LightmapSourceMesh(const Node& node, MeshData& scratch) const {
        switch (node.type) {
            case ShapeType::Cube: scratch = MeshGenerator::CreateCube(1.0f); return &scratch;
            case ShapeType::Sphere: scratch = MeshGenerator::CreateSphere(0.5f); return &scratch;
            case ShapeType::Cone: scratch = MeshGenerator::CreateCone(0.5f, 1.0f); return &scratch;
            case ShapeType::Cylinder: scratch = MeshGenerator::CreateCylinder(0.5f, 1.0f); return &scratch;
            case ShapeType::Plane: scratch = MeshGenerator::CreatePlane(2.0f); return &scratch;
            case ShapeType::Quad: scratch = MeshGenerator::CreateQuad(1.0f); return &scratch;
            case ShapeType::Custom: {
                auto it = m_cpuMeshes.find(node.customMeshName);
                if (it == m_cpuMeshes.end()) it = m_cpuMeshes.find(std::filesystem::path(node.customMeshName).filename().string());
                return it != m_cpuMeshes.end() ? &it->second.meshData : nullptr;
            }
        }
        return nullptr;
    }

    // Average linear colour of an image (white when there is none), for bounce light colour
    glm::vec3 AverageTextureColor(const std::string& path) {
        if (path.empty()) return glm::vec3(1.0f);
        auto cached = m_averageTextureColors.find(path);
        if (cached != m_averageTextureColors.end()) return cached->second;
        glm::vec3 average(1.0f);
        int w = 0, h = 0, channels = 0;
        if (stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4)) {
            const size_t count = static_cast<size_t>(w) * h;
            const size_t step = std::max<size_t>(1, count / 65536);
            glm::dvec3 sum(0.0);
            size_t used = 0;
            for (size_t i = 0; i < count; i += step) {
                for (int c = 0; c < 3; ++c) sum[c] += std::pow(pixels[i * 4 + c] / 255.0, 2.2);
                ++used;
            }
            if (used > 0) average = glm::vec3(sum / static_cast<double>(used));
            stbi_image_free(pixels);
        }
        m_averageTextureColors[path] = average;
        return average;
    }

    // The sky as seen from the scene, for rays that leave it (empty when the sky and HDRI are off)
    std::vector<glm::vec3> CaptureSkyCube(const std::vector<LightData>& lights, uint32_t& faceSize) {
        faceSize = 0;
        const auto& env = m_scene.GetEnvironment();
        if (!env.enableSky && !(env.useHDRI && m_pipeline.HasEnvironmentHDRI())) return {};
        auto& ctx = VulkanContext::Get();
        vkDeviceWaitIdle(ctx.GetDevice());
        if (!m_giBakeTargetReady) {
            m_giBakeTarget.Init(kGIFaceSize, kGIFaceSize);
            m_giBakeTargetReady = true;
        }
        m_pipeline.SetViewLights(VulkanPipeline::kViewEditor, lights);
        static const glm::vec3 kFaceDirs[6] = {
            { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
        };
        const size_t faceTexels = static_cast<size_t>(kGIFaceSize) * kGIFaceSize;
        std::vector<glm::vec3> cube(6 * faceTexels);
        std::vector<uint16_t> pixels;
        for (int face = 0; face < 6; ++face) {
            const glm::vec3 f = kFaceDirs[face];
            const glm::vec3 up = std::abs(f.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            m_pipeline.BeginFrame(0);
            UniformBufferObject ubo = BuildGIBakeUBO(glm::vec3(0.0f), f, up, lights);
            m_pipeline.UpdateUniformBufferSlot(VulkanPipeline::kViewEditorProbe, ubo);
            VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
            m_giBakeTarget.BeginRenderPass(cmd);
            m_pipeline.RenderSky(cmd, VulkanPipeline::kViewEditorProbe);
            m_giBakeTarget.NextScenePass(cmd);
            m_giBakeTarget.EndRenderPass(cmd);
            ctx.EndSingleTimeCommands(cmd);
            m_giBakeTarget.ReadbackHdrImage(pixels);
            for (size_t i = 0; i < faceTexels; ++i) {
                glm::vec3 radiance(glm::unpackHalf1x16(pixels[i * 4]), glm::unpackHalf1x16(pixels[i * 4 + 1]),
                                   glm::unpackHalf1x16(pixels[i * 4 + 2]));
                // The sun's disc is direct light, already added in real time (as in the probe bake)
                const float luma = glm::dot(radiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                if (luma > 8.0f) radiance *= 8.0f / luma;
                cube[face * faceTexels + i] = glm::max(radiance, glm::vec3(0.0f));
            }
        }
        faceSize = kGIFaceSize;
        return cube;
    }

    bool StartLightmapBake() {
        const auto& env = m_scene.GetEnvironment();
        std::vector<Lightmap::NodeLayout> layouts;
        std::vector<std::shared_ptr<Node>> lightmapNodes;
        MeshData scratch;
        for (const auto& node : m_scene.GetNodes()) {
            if (!node || !node->lightmapStatic || !IsStaticOpaqueMesh(*node)) continue;
            const MeshData* source = LightmapSourceMesh(*node, scratch);
            if (!source || source->indices.size() < 3) continue;
            const glm::mat4 world = node->GetWorldTransformMatrix(m_scene);
            const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
            Lightmap::NodeLayout layout = Lightmap::Unwrap(*source, scale);
            layout.nodeId = node->id;
            layout.sourceKey = VulkanPipeline::LightmapSourceKey(*node);
            layouts.push_back(std::move(layout));
            lightmapNodes.push_back(node);
        }
        if (layouts.empty()) return false;
        float density = std::clamp(env.lightmapDensity, 0.25f, 64.0f);
        const uint32_t atlasSize = Lightmap::PackAtlas(layouts, density, 2048);
        if (atlasSize == 0) return false;

        // What blocks and bounces light: the lightmapped meshes and terrain
        std::vector<Lightmap::BakeObject> objects;
        auto addObject = [&](const Node& node, const MeshData& mesh, const Lightmap::NodeLayout* layout) {
            Lightmap::BakeObject object;
            const glm::mat4 world = node.GetWorldTransformMatrix(m_scene);
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
            auto addVertex = [&](const Vertex& v) {
                object.positions.push_back(glm::vec3(world * glm::vec4(v.pos, 1.0f)));
                const glm::vec3 n = normalMatrix * v.normal;
                object.normals.push_back(glm::dot(n, n) > 1e-12f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f));
            };
            if (layout) {
                for (uint32_t s : layout->sourceIndex) addVertex(mesh.vertices[s]);
                object.atlasUV = layout->uv;
                object.indices = layout->indices;
            } else {
                for (const Vertex& v : mesh.vertices) addVertex(v);
                object.indices = mesh.indices;
            }
            const glm::vec3 albedo = glm::vec3(node.color) * AverageTextureColor(node.texturePath);
            object.albedo = glm::min(albedo * (1.0f - std::clamp(node.metallic, 0.0f, 1.0f)), glm::vec3(0.95f));
            glm::vec3 emissive = node.emissiveColor * node.emissiveIntensity;
            if (!node.emissiveMapPath.empty()) {
                const glm::vec3 tint = glm::dot(node.emissiveColor, glm::vec3(1.0f)) > 0.0f ? node.emissiveColor : glm::vec3(1.0f);
                emissive = AverageTextureColor(node.emissiveMapPath) * tint * node.emissiveIntensity;
            }
            object.emissive = emissive;
            objects.push_back(std::move(object));
        };
        for (size_t i = 0; i < lightmapNodes.size(); ++i) {
            if (const MeshData* source = LightmapSourceMesh(*lightmapNodes[i], scratch)) addObject(*lightmapNodes[i], *source, &layouts[i]);
        }
        for (const auto& node : m_scene.GetNodes()) {
            if (!node || node->nodeType != NodeType::Terrain) continue;
            if (auto terrain = std::dynamic_pointer_cast<TerrainNode>(node)) {
                const MeshData terrainMesh = terrain->GenerateMeshData();
                addObject(*node, terrainMesh, nullptr);
            }
        }

        Lightmap::BakeSettings settings;
        settings.atlasSize = atlasSize;
        settings.samples = std::clamp(env.lightmapSamples, 16, 1024);
        settings.bounces = std::clamp(env.bakedGIBounces, 1, 4);
        settings.lights = CollectLights(m_scene);
        const UniformBufferObject ubo = BuildGIBakeUBO(glm::vec3(0.0f), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), settings.lights);
        settings.sky = ubo.skyParams.w > 0.0f;
        const glm::vec3 sunDir(ubo.skyParams);
        settings.skyDirection = glm::dot(sunDir, sunDir) > 1e-8f ? glm::normalize(sunDir) : glm::vec3(0, 1, 0);
        settings.giStrength = ubo.enableGI == 1 ? ubo.giSkyColor.a : 0.08f;
        settings.flatSky = glm::vec3(ubo.giSkyColor);
        settings.skyCube = CaptureSkyCube(settings.lights, settings.skyFaceSize);

        m_lightmapLayouts = std::move(layouts);
        m_lightmapAtlasSize = atlasSize;
        m_lightmapBaker.Start(std::move(objects), std::move(settings));
        m_lightmapBaking = true;
        m_editorUI.SetGIBakeStatus("Lightmaps: starting", true);
        LOG("Baking lightmaps: " << m_lightmapLayouts.size() << " objects, " << atlasSize << "x" << atlasSize
            << " atlas at " << density << " texels/m");
        return true;
    }

    void StepLightmapBake() {
        if (!m_lightmapBaker.Done()) {
            m_editorUI.SetGIBakeStatus(m_lightmapBaker.Status(), true);
            return;
        }
        m_lightmapBaker.Finish();
        m_lightmapBaking = false;
        const auto& texels = m_lightmapBaker.Texels();
        if (texels.empty()) {
            m_editorUI.SetGIBakeStatus("Lightmap bake was cancelled.", false);
            return;
        }
        int pending = 0;
        const size_t applied = ApplyLightmaps(m_lightmapAtlasSize, texels, m_lightmapLayouts, pending);
        auto& env = m_scene.GetEnvironment();
        std::string path = m_scene.GetFilePath();
        path = path.empty() ? std::string("baked_lighting.lightmap") : path + ".lightmap";
        if (SaveLightmaps(path, m_lightmapAtlasSize, texels, m_lightmapLayouts)) {
            env.lightmapPath = path;
            env.bakedGIEnabled = true;
            auto& playEnv = m_gameLoop.GetActivePlayScene(m_scene).GetEnvironment();
            playEnv.lightmapPath = path;
            playEnv.bakedGIEnabled = true;
            m_loadedLightmapPath = path;
            m_lightmapSummary = " and lightmaps for " + std::to_string(applied) + " objects (" +
                                std::to_string(m_lightmapAtlasSize) + "x" + std::to_string(m_lightmapAtlasSize) + ")";
            LOG("Baked lightmaps saved: " << path << " (" << m_lightmapBaker.TexelCount() << " texels)");
        } else {
            LOG("Lightmaps baked but could not be saved to " << path);
        }
        // Then the probe volume, which now sees the lightmapped surfaces' bounce light
        StartGIBake();
    }

    // Uploads the atlas and gives each layout's node its lightmapped mesh. pending counts nodes
    // whose model isn't loaded yet. Waits for the GPU.
    size_t ApplyLightmaps(uint32_t size, const std::vector<uint32_t>& texels,
                          const std::vector<Lightmap::NodeLayout>& layouts, int& pending) {
        pending = 0;
        m_pipeline.ClearLightmaps();
        m_pipeline.SetLightmapAtlas(size, size, texels);
        size_t applied = 0;
        MeshData scratch;
        for (const auto& layout : layouts) {
            auto node = m_scene.GetNodeById(layout.nodeId);
            if (!node || VulkanPipeline::LightmapSourceKey(*node) != layout.sourceKey) continue;
            const MeshData* source = LightmapSourceMesh(*node, scratch);
            if (!source) {
                ++pending;
                continue;
            }
            const bool matches = std::all_of(layout.sourceIndex.begin(), layout.sourceIndex.end(),
                                             [&](uint32_t s) { return s < source->vertices.size(); });
            if (!matches || layout.uv.size() != layout.sourceIndex.size()) continue;
            MeshData lit;
            lit.vertices = Lightmap::BuildVertices(*source, layout);
            lit.indices = layout.indices;
            m_pipeline.SetLightmapMesh(layout.nodeId, layout.sourceKey, VulkanBufferUtils::UploadMesh(lit));
            ++applied;
        }
        return applied;
    }

    // File: "TLM1", atlas size, atlas texels (B10G11R11), layout count, then per layout: node id,
    // source key (length + bytes), vertex count, source indices, atlas UVs, index count, indices
    static bool SaveLightmaps(const std::string& path, uint32_t size, const std::vector<uint32_t>& texels,
                              const std::vector<Lightmap::NodeLayout>& layouts) {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        auto writeU32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        writeU32(0x314D4C54); // "TLM1"
        writeU32(size);
        out.write(reinterpret_cast<const char*>(texels.data()), static_cast<std::streamsize>(texels.size() * 4));
        writeU32(static_cast<uint32_t>(layouts.size()));
        for (const auto& layout : layouts) {
            writeU32(layout.nodeId);
            writeU32(static_cast<uint32_t>(layout.sourceKey.size()));
            out.write(layout.sourceKey.data(), static_cast<std::streamsize>(layout.sourceKey.size()));
            writeU32(static_cast<uint32_t>(layout.sourceIndex.size()));
            out.write(reinterpret_cast<const char*>(layout.sourceIndex.data()), static_cast<std::streamsize>(layout.sourceIndex.size() * 4));
            out.write(reinterpret_cast<const char*>(layout.uv.data()), static_cast<std::streamsize>(layout.uv.size() * sizeof(glm::vec2)));
            writeU32(static_cast<uint32_t>(layout.indices.size()));
            out.write(reinterpret_cast<const char*>(layout.indices.data()), static_cast<std::streamsize>(layout.indices.size() * 4));
        }
        return static_cast<bool>(out);
    }

    static bool LoadLightmapFile(const std::string& path, uint32_t& size, std::vector<uint32_t>& texels,
                                 std::vector<Lightmap::NodeLayout>& layouts) {
        std::ifstream in(path, std::ios::binary);
        auto readU32 = [&]() { uint32_t v = 0; in.read(reinterpret_cast<char*>(&v), 4); return v; };
        if (!in || readU32() != 0x314D4C54) return false;
        size = readU32();
        if (!in || size == 0 || size > 8192) return false;
        texels.resize(static_cast<size_t>(size) * size);
        in.read(reinterpret_cast<char*>(texels.data()), static_cast<std::streamsize>(texels.size() * 4));
        const uint32_t count = readU32();
        if (!in || count > 1000000) return false;
        layouts.resize(count);
        for (auto& layout : layouts) {
            layout.nodeId = readU32();
            const uint32_t keyLength = readU32();
            if (!in || keyLength > 4096) return false;
            layout.sourceKey.resize(keyLength);
            in.read(layout.sourceKey.data(), keyLength);
            const uint32_t vertices = readU32();
            if (!in || vertices > 50000000) return false;
            layout.sourceIndex.resize(vertices);
            layout.uv.resize(vertices);
            in.read(reinterpret_cast<char*>(layout.sourceIndex.data()), static_cast<std::streamsize>(vertices) * 4);
            in.read(reinterpret_cast<char*>(layout.uv.data()), static_cast<std::streamsize>(vertices) * sizeof(glm::vec2));
            const uint32_t indexCount = readU32();
            if (!in || indexCount > 150000000) return false;
            layout.indices.resize(indexCount);
            in.read(reinterpret_cast<char*>(layout.indices.data()), static_cast<std::streamsize>(indexCount) * 4);
            if (std::any_of(layout.indices.begin(), layout.indices.end(), [&](uint32_t i) { return i >= vertices; })) return false;
        }
        return static_cast<bool>(in);
    }

    // Loads the scene's lightmaps when the path changes (empty = none). Models that are still
    // loading are retried a few times. Waits for the GPU.
    void UpdateLightmaps(const std::string& path) {
        if (m_lightmapBaking || path == m_loadedLightmapPath) return;
        if (m_lightmapRetryFrames > 0) {
            --m_lightmapRetryFrames;
            return;
        }
        if (path.empty()) {
            m_loadedLightmapPath.clear();
            if (m_pipeline.HasLightmaps()) m_pipeline.ClearLightmaps();
            return;
        }
        uint32_t size = 0;
        std::vector<uint32_t> texels;
        std::vector<Lightmap::NodeLayout> layouts;
        if (!LoadLightmapFile(path, size, texels, layouts)) {
            LOG("Could not read lightmaps: " << path);
            m_pipeline.ClearLightmaps();
            m_loadedLightmapPath = path;
            return;
        }
        int pending = 0;
        const size_t applied = ApplyLightmaps(size, texels, layouts, pending);
        if (pending > 0 && ++m_lightmapLoadAttempts < 10) {
            m_lightmapRetryFrames = 30; // a model isn't loaded yet: try again shortly
            return;
        }
        m_lightmapLoadAttempts = 0;
        m_loadedLightmapPath = path;
        LOG("Loaded lightmaps for " << applied << " of " << layouts.size() << " objects: " << path);
    }

    // ---- Real-time Voxel Global Illumination (VXGI) ----
    // Dynamically voxelizes scene meshes and terrain into a 32^3 radiance + opacity volume,
    // shades occupied voxels with direct sun/local lights + voxel raycast shadows + multi-bounce
    // feedback, builds the 6-level mip pyramid, and uploads it for Voxel Cone Tracing in shader.frag.
    struct VXGIState {
        bool ready{false};
        uint64_t signature{0};
        double lastUpdateTime{-10.0};
        glm::vec3 minCorner{0.0f};
        float extent{24.0f};
        float voxelSize{0.75f};
        std::vector<std::vector<glm::vec4>> mips;
    };
    VXGIState m_vxgi;

    void UpdateVXGI(VkCommandBuffer cmd, const Scene& sc, const glm::vec3& focusPos, const std::vector<LightData>& lights) {
        const auto& env = sc.GetEnvironment();
        auto& rt = Graphics::RayTracing::RayTracingManager::Get();
        auto& rtSettings = rt.GetSettings();
        const bool rtgiActive = rtSettings.enableHardwareRT && rtSettings.enableRTGI;
        if (!env.vxgiEnabled && !rtgiActive) {
            m_vxgi.ready = false;
            m_vxgi.signature = 0;
            return;
        }
        constexpr uint32_t kDim = VulkanVXGIVolume::kBaseDim;
        constexpr uint32_t kMips = VulkanVXGIVolume::kMipLevels;

        float extent = std::clamp(env.vxgiRange, 8.0f, 80.0f);
        glm::vec3 center = focusPos;
        bool hasGIVolumeNode = false;
        for (const auto& node : sc.GetNodes()) {
            if (node && node->isGIVolume) {
                center = node->GetWorldPosition(sc);
                extent = std::clamp(std::max(node->giVolumeSize.x, std::max(node->giVolumeSize.y, node->giVolumeSize.z)), 4.0f, 120.0f);
                hasGIVolumeNode = true;
                break;
            }
        }
        if (!hasGIVolumeNode) {
            // If all scene meshes fit comfortably inside the VXGI range around their centroid, anchor to the scene;
            // otherwise snap the camera-centered volume to 4-voxel increments so camera motion never causes voxel swimming.
            glm::vec3 lo(1e9f), hi(-1e9f);
            for (const auto& node : sc.GetNodes()) {
                if (!node || node->IsUINode() || (node->nodeType != NodeType::Mesh && node->nodeType != NodeType::Character)) continue;
                const glm::vec3 p = node->GetWorldPosition(sc);
                lo = glm::min(lo, p - glm::abs(node->scale));
                hi = glm::max(hi, p + glm::abs(node->scale));
            }
            const glm::vec3 span = hi - lo;
            const float maxSpan = std::max(span.x, std::max(span.y, span.z));
            if (lo.x <= hi.x && maxSpan > 0.2f) {
                center = (lo + hi) * 0.5f;
                // Compactly encapsulate local scene meshes to dramatically increase voxel density and near-field bounce
                extent = std::clamp(maxSpan * 1.6f, 5.0f, std::clamp(env.vxgiRange, 8.0f, 80.0f));
            }
            const float localVoxelSize = extent / static_cast<float>(kDim);
            const float snap = localVoxelSize * 4.0f;
            center = glm::floor(center / snap + 0.5f) * snap;
        }
        const float voxelSize = extent / static_cast<float>(kDim);
        const glm::vec3 minCorner = center - glm::vec3(extent * 0.5f);
        const glm::vec3 maxCorner = minCorner + glm::vec3(extent);

        // Hash scene state, lights and VXGI parameters to skip redundant rebuilds when static
        uint64_t sig = 1469598103934665603ULL;
        auto mixHash = [&](uint64_t v) { sig ^= v; sig *= 1099511628211ULL; };
        auto mixFloat = [&](float f) { uint32_t u = 0; std::memcpy(&u, &f, 4); mixHash(u); };
        mixFloat(minCorner.x); mixFloat(minCorner.y); mixFloat(minCorner.z);
        mixFloat(extent);
        mixFloat(env.vxgiBounce);
        mixHash(rtgiActive ? 1u : 0u);
        mixFloat(rtSettings.rtgiIntensity);
        mixHash(env.enableGI ? 1u : 0u);
        mixFloat(env.giSkyColor.x); mixFloat(env.giSkyColor.y); mixFloat(env.giSkyColor.z); mixFloat(env.giIntensity);
        mixHash(env.enableSky ? 1u : 0u);
        mixFloat(env.skyIntensity);
        if (env.timeOfDayEnabled) mixFloat(std::floor(env.timeOfDay * 20.0f));
        for (const auto& l : lights) {
            mixFloat(l.position.x); mixFloat(l.position.y); mixFloat(l.position.z); mixFloat(l.position.w);
            mixFloat(l.color.x); mixFloat(l.color.y); mixFloat(l.color.z); mixFloat(l.color.w);
        }
        for (const auto& node : sc.GetNodes()) {
            if (!node || node->IsUINode() || (node->nodeType != NodeType::Mesh && node->nodeType != NodeType::Character && node->nodeType != NodeType::Terrain)) continue;
            mixHash(node->id);
            mixFloat(node->position.x); mixFloat(node->position.y); mixFloat(node->position.z);
            mixFloat(node->rotation.x); mixFloat(node->rotation.y); mixFloat(node->rotation.z);
            mixFloat(node->scale.x); mixFloat(node->scale.y); mixFloat(node->scale.z);
            mixFloat(node->color.x); mixFloat(node->color.y); mixFloat(node->color.z);
            mixFloat(node->emissiveColor.x); mixFloat(node->emissiveColor.y); mixFloat(node->emissiveColor.z);
            mixFloat(node->emissiveIntensity);
        }

        const double now = glfwGetTime();
        if (m_vxgi.ready && sig == m_vxgi.signature) return;
        if (m_vxgi.ready && (now - m_vxgi.lastUpdateTime) < 0.06) return; // cap dynamic updates to ~16 Hz

        if (m_vxgi.mips.size() != kMips) {
            m_vxgi.mips.resize(kMips);
            for (uint32_t m = 0; m < kMips; ++m) {
                const uint32_t d = std::max(1u, kDim >> m);
                m_vxgi.mips[m].assign(static_cast<size_t>(d) * d * d, glm::vec4(0.0f));
            }
        }
        // Keep a copy of mip 2 (8^3) from the previous state for multi-bounce indirect feedback
        const std::vector<glm::vec4> prevMip2 = m_vxgi.mips[2];
        constexpr uint32_t kBounceDim = kDim >> 2;
        const bool hasPrevBounce = m_vxgi.ready && env.vxgiBounce > 0.001f && prevMip2.size() == static_cast<size_t>(kBounceDim) * kBounceDim * kBounceDim;

        constexpr size_t kVoxCount = static_cast<size_t>(kDim) * kDim * kDim;
        struct VoxelAccum {
            glm::vec3 albedo{0.0f};
            glm::vec3 normal{0.0f};
            glm::vec3 emissive{0.0f};
            float weight{0.0f};
        };
        std::vector<VoxelAccum> grid(kVoxCount);
        const float invExtent = 1.0f / extent;
        auto splatPoint = [&](const glm::vec3& wp, const glm::vec3& wn, const glm::vec3& alb, const glm::vec3& emi) {
            const glm::vec3 uvw = (wp - minCorner) * invExtent;
            if (uvw.x < 0.0f || uvw.y < 0.0f || uvw.z < 0.0f || uvw.x >= 1.0f || uvw.y >= 1.0f || uvw.z >= 1.0f) return;
            const uint32_t ix = std::min(static_cast<uint32_t>(uvw.x * kDim), kDim - 1);
            const uint32_t iy = std::min(static_cast<uint32_t>(uvw.y * kDim), kDim - 1);
            const uint32_t iz = std::min(static_cast<uint32_t>(uvw.z * kDim), kDim - 1);
            VoxelAccum& cell = grid[(static_cast<size_t>(iz) * kDim + iy) * kDim + ix];
            cell.albedo += alb;
            cell.normal += wn;
            cell.emissive += emi;
            cell.weight += 1.0f;
        };

        MeshData scratch;
        for (const auto& node : sc.GetNodes()) {
            if (!node || node->IsUINode() || node->isMirror || node->isOcean || node->isLiquid) continue;
            if (node->nodeType == NodeType::Terrain) {
                if (auto terrain = std::dynamic_pointer_cast<TerrainNode>(node)) {
                    const glm::vec3 alb = glm::vec3(node->color) * 0.45f;
                    for (uint32_t iz = 0; iz < kDim; ++iz) {
                        const float wz = minCorner.z + (iz + 0.5f) * voxelSize;
                        for (uint32_t ix = 0; ix < kDim; ++ix) {
                            const float wx = minCorner.x + (ix + 0.5f) * voxelSize;
                            const float wy = terrain->SampleHeight(wx, wz, sc);
                            const glm::vec3 n = terrain->SampleNormal(wx, wz, sc);
                            splatPoint(glm::vec3(wx, wy, wz), n, alb, glm::vec3(0.0f));
                        }
                    }
                }
                continue;
            }
            if (node->nodeType != NodeType::Mesh && node->nodeType != NodeType::Character) continue;
            if (node->color.a < 0.25f) continue;

            const glm::vec3 bmin = node->type == ShapeType::Custom ? node->boundsMin : glm::vec3(-0.5f);
            const glm::vec3 bmax = node->type == ShapeType::Custom ? node->boundsMax : glm::vec3(0.5f);
            const glm::mat4 world = node->GetWorldTransformMatrix(sc);
            glm::vec3 wlo(1e9f), whi(-1e9f);
            for (int c = 0; c < 8; ++c) {
                const glm::vec3 corner((c & 1) ? bmax.x : bmin.x, (c & 2) ? bmax.y : bmin.y, (c & 4) ? bmax.z : bmin.z);
                const glm::vec3 w = glm::vec3(world * glm::vec4(corner, 1.0f));
                wlo = glm::min(wlo, w);
                whi = glm::max(whi, w);
            }
            if (whi.x < minCorner.x || whi.y < minCorner.y || whi.z < minCorner.z ||
                wlo.x > maxCorner.x || wlo.y > maxCorner.y || wlo.z > maxCorner.z) continue;

            const MeshData* source = LightmapSourceMesh(*node, scratch);
            if (!source || source->indices.size() < 3) continue;

            const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(world)));
            const glm::vec3 albedo = glm::min(glm::vec3(node->color) * AverageTextureColor(node->texturePath) *
                                              (1.0f - 0.8f * std::clamp(node->metallic, 0.0f, 1.0f)), glm::vec3(0.95f));
            glm::vec3 emissive = node->emissiveColor * node->emissiveIntensity;
            if (!node->emissiveMapPath.empty()) {
                const glm::vec3 tint = glm::dot(node->emissiveColor, glm::vec3(1.0f)) > 0.0f ? node->emissiveColor : glm::vec3(1.0f);
                const glm::vec3 mapCol = node->isVideoScreen ? Video::VideoSystem::Get().GetAverageScreenColor(node->id) : AverageTextureColor(node->emissiveMapPath);
                emissive = mapCol * tint * node->emissiveIntensity;
            }

            const size_t triCount = source->indices.size() / 3;
            const size_t triStride = std::max<size_t>(1, triCount / 1536);
            for (size_t t = 0; t < triCount; t += triStride) {
                const uint32_t i0 = source->indices[t * 3 + 0];
                const uint32_t i1 = source->indices[t * 3 + 1];
                const uint32_t i2 = source->indices[t * 3 + 2];
                if (i0 >= source->vertices.size() || i1 >= source->vertices.size() || i2 >= source->vertices.size()) continue;
                const glm::vec3 p0 = glm::vec3(world * glm::vec4(source->vertices[i0].pos, 1.0f));
                const glm::vec3 p1 = glm::vec3(world * glm::vec4(source->vertices[i1].pos, 1.0f));
                const glm::vec3 p2 = glm::vec3(world * glm::vec4(source->vertices[i2].pos, 1.0f));
                const glm::vec3 tmin = glm::min(p0, glm::min(p1, p2));
                const glm::vec3 tmax = glm::max(p0, glm::max(p1, p2));
                if (tmax.x < minCorner.x || tmax.y < minCorner.y || tmax.z < minCorner.z ||
                    tmin.x > maxCorner.x || tmin.y > maxCorner.y || tmin.z > maxCorner.z) continue;

                glm::vec3 wn = normalMat * (source->vertices[i0].normal + source->vertices[i1].normal + source->vertices[i2].normal);
                if (glm::dot(wn, wn) < 1e-8f) wn = glm::cross(p1 - p0, p2 - p0);
                wn = glm::dot(wn, wn) > 1e-8f ? glm::normalize(wn) : glm::vec3(0.0f, 1.0f, 0.0f);

                const float maxEdge = std::max(glm::distance(p0, p1), std::max(glm::distance(p1, p2), glm::distance(p2, p0)));
                const int steps = std::clamp(static_cast<int>(std::ceil(maxEdge / (voxelSize * 0.7f))), 1, 6);
                for (int u = 0; u <= steps; ++u) {
                    for (int v = 0; v <= steps - u; ++v) {
                        const float bu = static_cast<float>(u) / static_cast<float>(steps);
                        const float bv = static_cast<float>(v) / static_cast<float>(steps);
                        const float bw = 1.0f - bu - bv;
                        splatPoint(p0 * bw + p1 * bu + p2 * bv, wn, albedo, emissive);
                    }
                }
            }
        }

        // Fast voxel occupancy shadow ray (checks whether another voxel blocks direct light)
        auto voxelShadow = [&](const glm::vec3& startPos, const glm::vec3& L, float maxDist) {
            const float stepDist = voxelSize * 1.1f;
            float t = voxelSize * 1.4f;
            for (int s = 0; s < 10 && t < maxDist; ++s, t += stepDist) {
                const glm::vec3 uvw = (startPos + L * t - minCorner) * invExtent;
                if (uvw.x < 0.0f || uvw.y < 0.0f || uvw.z < 0.0f || uvw.x >= 1.0f || uvw.y >= 1.0f || uvw.z >= 1.0f) break;
                const uint32_t ix = static_cast<uint32_t>(uvw.x * kDim);
                const uint32_t iy = static_cast<uint32_t>(uvw.y * kDim);
                const uint32_t iz = static_cast<uint32_t>(uvw.z * kDim);
                if (grid[(static_cast<size_t>(iz) * kDim + iy) * kDim + ix].weight > 0.0f) return 0.15f;
            }
            return 1.0f;
        };

        auto& mip0 = m_vxgi.mips[0];
        std::fill(mip0.begin(), mip0.end(), glm::vec4(0.0f));
        const glm::vec3 skyAmb = env.enableGI ? (env.giSkyColor * std::max(env.giIntensity, 0.0f) * 0.22f) : glm::vec3(0.04f);

        for (uint32_t iz = 0; iz < kDim; ++iz) {
            for (uint32_t iy = 0; iy < kDim; ++iy) {
                for (uint32_t ix = 0; ix < kDim; ++ix) {
                    const size_t idx = (static_cast<size_t>(iz) * kDim + iy) * kDim + ix;
                    const VoxelAccum& cell = grid[idx];
                    if (cell.weight <= 0.0f) continue;

                    const float invW = 1.0f / cell.weight;
                    const glm::vec3 alb = cell.albedo * invW;
                    const glm::vec3 emi = cell.emissive * invW;
                    const glm::vec3 N = glm::dot(cell.normal, cell.normal) > 1e-8f ? glm::normalize(cell.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
                    const glm::vec3 wpos = minCorner + (glm::vec3(ix, iy, iz) + 0.5f) * voxelSize;

                    glm::vec3 direct(0.0f);
                    for (const auto& light : lights) {
                        const glm::vec3 lcol = glm::vec3(light.color) * std::max(light.color.a, 0.0f);
                        if (light.position.w < 0.5f) {
                            const glm::vec3 L = glm::normalize(glm::vec3(light.position));
                            const float ndotl = std::max(glm::dot(N, L), 0.0f) * 0.75f + 0.25f;
                            const float vis = voxelShadow(wpos, L, extent);
                            direct += lcol * ndotl * vis;
                        } else {
                            const glm::vec3 toLight = glm::vec3(light.position) - wpos;
                            const float dist = glm::length(toLight);
                            const float range = std::max(light.spotDirAndRange.w, 0.1f);
                            if (dist >= range || dist < 1e-4f) continue;
                            const glm::vec3 L = toLight / dist;
                            float atten = 1.0f / (1.0f + 0.15f * dist * dist);
                            const float win = std::max(1.0f - std::pow(dist / range, 4.0f), 0.0f);
                            atten *= win * win;
                            if (light.position.w > 1.5f) {
                                const float theta = glm::dot(L, glm::normalize(-glm::vec3(light.spotDirAndRange)));
                                const float eps = std::max(light.spotAnglesAndShadow.x - light.spotAnglesAndShadow.y, 1e-4f);
                                atten *= std::clamp((theta - light.spotAnglesAndShadow.y) / eps, 0.0f, 1.0f);
                            }
                            if (atten <= 1e-4f) continue;
                            const float ndotl = std::max(glm::dot(N, L), 0.0f) * 0.7f + 0.3f;
                            const float vis = voxelShadow(wpos, L, dist);
                            direct += lcol * (ndotl * atten * vis);
                        }
                    }

                    glm::vec3 bounce(0.0f);
                    if (hasPrevBounce) {
                        const glm::vec3 sampleUVW = glm::clamp((wpos + N * (voxelSize * 2.5f) - minCorner) * invExtent, 0.0f, 0.999f);



                        const uint32_t bx = static_cast<uint32_t>(sampleUVW.x * static_cast<float>(kBounceDim));
                        const uint32_t by = static_cast<uint32_t>(sampleUVW.y * static_cast<float>(kBounceDim));
                        const uint32_t bz = static_cast<uint32_t>(sampleUVW.z * static_cast<float>(kBounceDim));
                        bounce = glm::vec3(prevMip2[(bz * kBounceDim + by) * kBounceDim + bx]) * env.vxgiBounce;
                    }

                    const glm::vec3 radiance = glm::min(alb * (direct + skyAmb + bounce) + emi, glm::vec3(16.0f));
                    mip0[idx] = glm::vec4(radiance, 1.0f);
                }
            }
        }

        // Build 6-level mip pyramid (32 -> 16 -> 8 -> 4 -> 2 -> 1) preserving planar wall opacity
        for (uint32_t m = 1; m < kMips; ++m) {
            const uint32_t srcDim = std::max(1u, kDim >> (m - 1));
            const uint32_t dstDim = std::max(1u, kDim >> m);
            const auto& src = m_vxgi.mips[m - 1];
            auto& dst = m_vxgi.mips[m];
            for (uint32_t z = 0; z < dstDim; ++z) {
                for (uint32_t y = 0; y < dstDim; ++y) {
                    for (uint32_t x = 0; x < dstDim; ++x) {
                        glm::vec3 sumRgb(0.0f);
                        float sumAlpha = 0.0f;
                        for (uint32_t dz = 0; dz < 2; ++dz) {
                            for (uint32_t dy = 0; dy < 2; ++dy) {
                                for (uint32_t dx = 0; dx < 2; ++dx) {
                                    const glm::vec4& c = src[((2 * z + dz) * srcDim + (2 * y + dy)) * srcDim + (2 * x + dx)];
                                    sumRgb += glm::vec3(c);
                                    sumAlpha += c.a;
                                }
                            }
                        }
                        const float parentAlpha = std::clamp(sumAlpha * 0.25f, 0.0f, 1.0f);
                        const glm::vec3 avgRgb = sumAlpha > 1e-4f ? (sumRgb / sumAlpha) : glm::vec3(0.0f);
                        dst[(z * dstDim + y) * dstDim + x] = glm::vec4(avgRgb * parentAlpha, parentAlpha);
                    }
                }
            }
        }

        m_pipeline.RecordVXGIUpload(cmd, m_vxgi.mips);
        m_vxgi.minCorner = minCorner;
        m_vxgi.extent = extent;
        m_vxgi.voxelSize = voxelSize;
        m_vxgi.ready = true;
        m_vxgi.signature = sig;
        m_vxgi.lastUpdateTime = now;
    }

    // Writes the game camera's final image (after post-processing) to a PNG
    void SaveGameScreenshot(const std::string& path) {
        vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
        // Game mode: the game camera's image; editor: the 3D viewport's image (with grid)
        VulkanOffscreen& source = m_gameConfig.isStandaloneGame ? m_cameraPreviewOffscreen : m_offscreen;
        std::vector<uint8_t> pixels;
        source.ReadbackDisplayImage(pixels);
        const int w = static_cast<int>(source.GetWidth());
        const int h = static_cast<int>(source.GetHeight());
        const auto occlusion = m_pipeline.GetOcclusionStats(VulkanPipeline::kViewPreview);
        if (occlusion.tested > 0) {
            LOG("Occlusion culling: " << occlusion.hidden << " of " << occlusion.tested << " tested objects hidden");
        }
        if (stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4)) {
            LOG("Saved screenshot (" << w << "x" << h << "): " << path);
        } else {
            LOG("Failed to write screenshot: " << path);
        }
        // In the editor, also the material preview drawn last (the Inspector's), as <name>_material.png
        if (!m_gameConfig.isStandaloneGame && std::any_of(m_materialPreviews.begin(), m_materialPreviews.end(),
                                                          [](const auto& entry) { return entry.second.renderedFrame > 0; })) {
            std::vector<uint8_t> preview;
            m_materialPreviewTarget.ReadbackDisplayImage(preview);
            std::filesystem::path previewPath(path);
            previewPath.replace_filename(previewPath.stem().string() + "_material.png");
            const int size = static_cast<int>(kMaterialPreviewSize);
            if (stbi_write_png(previewPath.string().c_str(), size, size, 4, preview.data(), size * 4)) {
                LOG("Saved material preview: " << previewPath.string());
            }
        }
    }

    void DrawFrame() {
        static bool firstFrame = true;
        if (firstFrame) {
            LOG("DrawFrame(): first frame started successfully");
        }

        auto& ctx = VulkanContext::Get();
        VkDevice device = ctx.GetDevice();

        vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, m_swapchain, UINT64_MAX,
                                                m_imageAvailableSemaphores[m_currentFrame],
                                                VK_NULL_HANDLE, &imageIndex);

        if (firstFrame) {
            LOG("DrawFrame(): vkAcquireNextImageKHR result=" << result << " imageIndex=" << imageIndex);
            firstFrame = false;
        }

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            RecreateSwapchain();
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swapchain image!");
        }

        vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);

        // Render targets follow the size their views are shown at (before the UI uses their images)
        SyncViewSizes();

        if (m_gameConfig.isStandaloneGame) {
            Scene& gameScene = m_gameLoop.GetActivePlayScene(m_scene);

            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::SetNextWindowViewport(vp->ID);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::Begin("##StandaloneGameCanvas", nullptr, flags)) {
                ImTextureID tex = (ImTextureID)(intptr_t)m_cameraPreviewOffscreen.GetTextureId();
                if (tex && vp->Size.x > 0.0f && vp->Size.y > 0.0f) {
                    ImGui::Image(tex, vp->Size);
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        m_gameLoop.SetPointerLocked(true);
                    }

                    // Render 2D in-game UI overlay
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    m_editorUI.RenderUIElements(gameScene, drawList, vp->Pos, vp->Size, false, &m_gameLoop);
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
        } else {
            ImGuizmo::BeginFrame();
            m_editorUI.Render(m_scene, m_camera, m_offscreen, m_cameraPreviewOffscreen, m_gameLoop);
        }

        ImGui::Render();

        // HDRI environment: loading waits for the GPU, so it happens before recording. Both
        // viewports share one environment image; the game scene is a copy of the edited one.
        {
            const Scene& envScene = m_gameConfig.isStandaloneGame ? m_gameLoop.GetActivePlayScene(m_scene) : m_scene;
            const auto& env = envScene.GetEnvironment();
            m_pipeline.SetEnvironmentHDRI(env.useHDRI ? env.hdriPath : std::string());
            UpdateColorLUT(env.colorLutPath);
            UpdateBakedGI(env.bakedGIPath);
            UpdateLightmaps(env.lightmapPath);
        }
        const double frameTime = glfwGetTime();
        const float frameDelta = m_lastFrameTime > 0.0 ? static_cast<float>(std::min(frameTime - m_lastFrameTime, 0.25)) : 0.016f;
        m_lastFrameTime = frameTime;

        // Ocean spray, liquids and pours for the scene being shown. Liquid moves between
        // containers only in Play mode; the edited scene's surfaces still follow their fill level.
        {
            Scene& shownScene = m_gameLoop.GetActivePlayScene(m_scene);
            const bool playing = m_gameLoop.GetMode() == Gameplay::EngineMode::Play && &shownScene != &m_scene;
            m_oceanSpray.Update(shownScene, frameDelta);
            UpdateOceanAudio(shownScene, playing, frameDelta);
            m_liquids.Update(shownScene, frameDelta, true, playing);
            if (&shownScene != &m_scene) m_liquids.Update(m_scene, frameDelta, false, false);
            std::vector<VulkanPipeline::SpraySprite> sprites;
            sprites.reserve(m_oceanSpray.Sprites().size() + m_liquids.Droplets().size());
            for (const auto& s : m_oceanSpray.Sprites()) sprites.push_back({ s.position, s.size, s.color, glm::vec3(0.0f), s.frame, s.seed });
            for (const auto& d : m_liquids.Droplets()) sprites.push_back({ d.position, d.size, d.color, d.stretch });
            m_pipeline.SetSprayParticles(std::move(sprites));
            std::vector<VulkanPipeline::LiquidStreamSegment> streams;
            for (const auto& s : m_liquids.Segments()) streams.push_back({ s.start, s.end, s.radius, s.color });
            m_pipeline.SetLiquidStreams(std::move(streams));
            auto uploadCb = [](const std::string& key, const unsigned char* rgba, uint32_t width, uint32_t height, void* userData) {
                auto pipe = reinterpret_cast<VulkanPipeline*>(userData);
                pipe->SetRuntimeTexture(key, rgba, width, height);
            };
            Video::VideoSystem::Get().UploadTextures(shownScene, frameTime, uploadCb, &m_pipeline);
            if (&shownScene != &m_scene) Video::VideoSystem::Get().UploadTextures(m_scene, frameTime, uploadCb, &m_pipeline);
        }

        // Record command buffer
        m_pipeline.BeginFrame(m_currentFrame);
        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);
        // GPU timings: reads this frame slot's last timings, then times this frame's passes
        m_gpuProfiler.BeginFrame(cmd, m_currentFrame);
        auto gpuBegin = [&](const char* name) { m_gpuProfiler.Begin(cmd, name); };
        auto gpuEnd = [&]() { m_gpuProfiler.End(cmd); };

        // Animated characters: GPU skinning before any pass draws them
        gpuBegin("Skinning");
        SubmitSkinning(cmd);
        gpuEnd();
        // Fluid volumes: this frame's simulation steps, then their particles are drawn as they are
        {
            Scene& shownScene = m_gameLoop.GetActivePlayScene(m_scene);
            const bool playingFluid = m_gameLoop.GetMode() == Gameplay::EngineMode::Play && &shownScene != &m_scene;
            // Pour streams landing in a fluid volume pour into it (in Play, as liquids move then)
            std::vector<VulkanFluidSim::Feed> feeds;
            if (playingFluid) {
                for (const auto& feed : m_liquids.FluidFeeds()) feeds.push_back({ feed.fluidNodeId, feed.position, feed.velocity, feed.volumePerSecond });
            }
            gpuBegin("Fluid Simulation");
            const auto contacts = m_fluidSim.Update(cmd, shownScene, frameDelta, m_currentFrame, playingFluid, feeds);
            gpuEnd();
            // Dynamic rigid bodies in the fluid float at its waterline and drift with its flow
            if (playingFluid) {
                auto& physics = m_gameLoop.GetPhysicsWorld();
                for (const auto& contact : contacts) {
                    auto node = shownScene.GetNodeById(contact.nodeId);
                    if (!node || node->rigidBodyType != RigidBodyType::Dynamic || !physics.HasBodyForNode(contact.nodeId)) continue;
                    const glm::vec3 position = physics.GetPosition(contact.nodeId);
                    physics.ApplyBuoyancy(contact.nodeId, glm::vec3(position.x, contact.waterline, position.z), glm::vec3(0.0f, 1.0f, 0.0f),
                                          node->buoyancy, contact.fluidVelocity, frameDelta);
                }
            }
            std::vector<VulkanPipeline::FluidBody> fluidBodies;
            for (const auto& body : m_fluidSim.Bodies(shownScene)) {
                fluidBodies.push_back({ body.positions, body.velocities, body.count, body.color, body.colorDepth, body.radius });
            }
            m_pipeline.SetFluidBodies(std::move(fluidBodies));
            // Particle emitters in GPU mode: this frame's steps, then their quads are drawn as they are
            gpuBegin("GPU Particles");
            m_particleSim.Update(cmd, shownScene, m_currentFrame);
            gpuEnd();
            std::map<uint32_t, VulkanPipeline::GpuParticleDraw> particleDraws;
            for (const auto& [id, draw] : m_particleSim.Draws()) particleDraws[id] = { draw.quads, draw.count, draw.sprites };
            m_pipeline.SetGpuParticles(std::move(particleDraws));
        }
        // Occlusion results from two frames ago; resets this frame's queries
        m_pipeline.BeginOcclusionFrame(cmd);

        // 1. Scene lights (directional, point and spot), up to kMaxSceneLights per view
        auto gatherLights = [&](const Scene& sc) { return CollectLights(sc); };

        // 2. Render from Scene Camera into Game / Camera Preview Offscreen Framebuffer
        // Uses the isolated simulation play scene (or edit scene if editing)
        Scene& activePlayScene = m_gameLoop.GetActivePlayScene(m_scene);
        {
            const Scene& vxgiScene = m_gameConfig.isStandaloneGame ? activePlayScene : m_scene;
            glm::vec3 vxgiFocus = m_camera.GetPosition();
            if (m_gameConfig.isStandaloneGame) {
                if (auto cam = activePlayScene.GetActiveCamera()) vxgiFocus = cam->GetWorldPosition(activePlayScene);
            }
            gpuBegin("VXGI Voxelization");
            UpdateVXGI(cmd, vxgiScene, vxgiFocus, gatherLights(vxgiScene));
            gpuEnd();
        }

        // Bloom, exposure and tone mapping settings for a scene's viewport
        // Planar reflection for the nearest visible mirror in a viewport (after that viewport's UBO upload)
        auto renderMirror = [&](VulkanOffscreen& target, const Scene& sc, int viewSlot) {
            UniformBufferObject mirrorUbo{};
            if (!m_pipeline.PrepareMirrorView(sc, viewSlot, mirrorUbo)) return;
            const int mirrorSlot = VulkanPipeline::MirrorSlotFor(viewSlot);
            m_pipeline.UpdateUniformBufferSlot(mirrorSlot, mirrorUbo);
            const glm::vec3 mirrorCamPos = glm::vec3(mirrorUbo.cameraPos);
            target.BeginRenderPass(cmd);
            m_pipeline.RenderScene(cmd, sc, m_meshes, m_customMeshes, PolygonMode::Solid, false, mirrorSlot, mirrorCamPos, ScenePhase::Opaque);
            target.NextScenePass(cmd);
            m_pipeline.RenderScene(cmd, sc, m_meshes, m_customMeshes, PolygonMode::Solid, false, mirrorSlot, mirrorCamPos, ScenePhase::Transparent);
            target.EndRenderPass(cmd);
        };

        // Depth/normal/motion prepass for one viewport, before its scene pass, then SSAO. Runs when
        // SSAO, TAA, motion blur or SSR needs it; returns whether it ran.
        auto renderPrepass = [&](VulkanOffscreen& target, const Scene& sc, bool isPreview, const glm::mat4& proj) {
            const auto& env = sc.GetEnvironment();
            const bool ssaoOn = env.ssaoIntensity > 0.001f;
            // Water needs the scene depth under it (depth colour, shore foam)
            const bool hasOcean = std::any_of(sc.GetNodes().begin(), sc.GetNodes().end(),
                                              [](const auto& node) { return node && (node->isOcean || node->isFluidVolume); });
            // Volumetric clouds stop at the opaque scene's depth
            const bool volumetricClouds = env.cloudsEnabled && env.volumetricClouds && (env.enableSky || env.useHDRI);
            // Decals are projected onto the prepass depth and normals
            const bool hasDecals = std::any_of(sc.GetNodes().begin(), sc.GetNodes().end(),
                                               [](const auto& node) { return node && node->nodeType == NodeType::Decal; });
            // Soft particles fade against it
            const bool softParticles = std::any_of(sc.GetNodes().begin(), sc.GetNodes().end(), [](const auto& node) {
                if (!node || node->nodeType != NodeType::ParticleEmitter) return false;
                const auto* emitter = dynamic_cast<const ParticleNode*>(node.get());
                return emitter && emitter->softness > 0.0f && emitter->renderMode != ParticleRenderMode::MeshSphere &&
                       emitter->renderMode != ParticleRenderMode::MeshCube;
            });
            if (!ssaoOn && env.antiAliasing != 2 && env.motionBlur <= 0.0f && env.ssrIntensity <= 0.0f && !hasOcean && !volumetricClouds && !hasDecals &&
                !softParticles) return false;
            target.BeginPrepass(cmd);
            m_pipeline.RenderNormalDepthPrepass(cmd, sc, m_meshes, m_customMeshes, isPreview);
            target.EndPrepass(cmd);
            if (ssaoOn) {
                SSAOSettings ssao;
                ssao.radius = env.ssaoRadius;
                target.ComputeSSAO(cmd, proj, ssao);
            }
            return true;
        };

        // Simulated fluid for a main view (0 editor, 1 game), after its prepass: the particle
        // spheres, then the smoothed surface that the scene pass shades
        auto renderFluid = [&](int view) {
            if (!m_pipeline.HasFluids()) return;
            m_fluidTargets[view].BeginParticles(cmd);
            m_pipeline.DrawFluidParticles(cmd, view, false);
            m_fluidTargets[view].End(cmd);
            m_fluidTargets[view].BeginThickness(cmd);
            m_pipeline.DrawFluidParticles(cmd, view, true);
            m_fluidTargets[view].End(cmd);
            for (int pass = 0; pass < 2; ++pass) {
                m_fluidTargets[view].BeginSmooth(cmd, pass);
                m_pipeline.DrawFluidSmooth(cmd, view, pass);
                m_fluidTargets[view].End(cmd);
            }
        };

        // Per-view environment, motion and probe values (after PopulateEnvironmentUBO)
        auto fillViewUBO = [&](const Scene& sc, int viewIndex, const glm::mat4& unjitteredViewProj, UniformBufferObject& ubo) {
            const auto& env = sc.GetEnvironment();
            if (env.useHDRI && m_pipeline.HasEnvironmentHDRI()) {
                ubo.envParams = glm::vec4(1.0f, env.hdriIntensity, glm::radians(env.hdriRotation), 0.0f);
                ubo.skyParams.w = 0.0f; // the HDRI replaces the procedural sky
            } else {
                ubo.envParams = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            }
            ubo.unjitteredViewProj = unjitteredViewProj;
            if (m_pipeline.HasLightmaps() && env.bakedGIEnabled && !env.lightmapPath.empty()) {
                ubo.lightmapParams = glm::vec4(1.0f, std::max(env.bakedGIIntensity, 0.0f), 0.0f, 0.0f);
            }
            if (m_bakedGI.loaded && env.bakedGIEnabled && !env.bakedGIPath.empty()) {
                ubo.giVolumeMin = glm::vec4(m_bakedGI.minCorner, 1.0f);
                ubo.giVolumeSize = glm::vec4(m_bakedGI.size, std::max(env.bakedGIIntensity, 0.0f));
                ubo.giVolumeDims = glm::vec4(glm::vec3(m_bakedGI.dims), 0.0f);
            }
            if (m_vxgi.ready && env.vxgiEnabled) {
                ubo.vxgiParams = glm::vec4(1.0f, std::max(env.vxgiIntensity, 0.0f), m_vxgi.extent,
                                           static_cast<float>(VulkanVXGIVolume::kMipLevels - 1));
                ubo.vxgiCenter = glm::vec4(m_vxgi.minCorner, m_vxgi.voxelSize);
            }
            {
                // Hardware ray tracing (read only by the ray-traced pipelines, which are used
                // only for views that have a TLAS this frame)
                const auto& rtSettings = Graphics::RayTracing::RayTracingManager::Get().GetSettings();
                const bool rtOn = rtSettings.enableHardwareRT && Graphics::RayTracing::RayTracingManager::Get().IsHardwareRayTracingSupported();
                ubo.rtParams = glm::vec4(rtOn && rtSettings.enableRTS ? 1.0f : 0.0f,
                                         rtOn && rtSettings.enableRTR ? 1.0f : 0.0f,
                                         rtOn && rtSettings.enableRTAO ? 1.0f : 0.0f,
                                         rtSettings.rtrRoughnessThreshold);
                // The sun's disc is ~0.27 degrees across; the penumbra scale widens it
                ubo.rtParams2 = glm::vec4(0.00465f * 2.0f * std::max(rtSettings.rtsPenumbraScale, 0.0f),
                                          rtSettings.rtaoRadius, 600.0f, 80.0f);
            }
            ubo.timeParams.w = static_cast<float>(m_taaFrameIndex % 1024); // steps temporal noise each frame
            // The ocean around the camera: caustics below it, and the underwater view
            ubo.waterParams = ubo.waterColor = ubo.waterBounds = glm::vec4(0.0f);
            m_cameraUnderwater[viewIndex % 2] = false;
            m_cameraHalfUnderwater[viewIndex % 2] = false;
            {
                const glm::vec3 cam(ubo.cameraPos);
                std::shared_ptr<Node> ocean = OceanWaves::FindOceanAt(sc, cam.x, cam.z);
                const bool cameraOverOcean = ocean != nullptr;
                if (!ocean) {
                    for (const auto& node : sc.GetNodes()) {
                        if (node && node->isOcean && node->nodeType == NodeType::Mesh) { ocean = node; break; }
                    }
                }
                if (ocean) {
                    const glm::vec3 center = ocean->GetWorldPosition(sc);
                    const glm::vec2 half = glm::max(ocean->oceanSize, glm::vec2(1.0f)) * 0.5f;
                    const bool under = cameraOverOcean &&
                                       cam.y < OceanWaves::SampleSurface(*ocean, sc, cam.x, cam.z, OceanWaves::Time()).height;
                    ubo.waterParams = glm::vec4(under ? 1.0f : 0.0f, center.y, std::max(ocean->waterClarity, 0.05f), 0.0f);
                    ubo.waterColor = glm::vec4(ocean->waterDeepColor, 1.0f);
                    ubo.waterBounds = glm::vec4(center.x - half.x, center.z - half.y, center.x + half.x, center.z + half.y);
                    m_cameraUnderwater[viewIndex % 2] = under;
                    // Near the surface the near plane can cross it: find where, in 16 columns across
                    // the screen, so the view is split at the waterline
                    if (cameraOverOcean && std::abs(cam.y - center.y) < ocean->waveHeight + 2.0f) {
                        const glm::mat4 invViewProj = glm::inverse(unjitteredViewProj);
                        const glm::vec3 forward = -glm::vec3(glm::inverse(ubo.view)[2]);
                        float nearZ = ubo.proj[3][2] / (ubo.proj[2][2] - 1.0f);
                        if (!(std::abs(nearZ) > 1e-4f) || std::abs(nearZ) > 10.0f) nearZ = 0.1f;
                        nearZ = std::abs(nearZ) * 1.05f;
                        // How far below the waves a point of the near plane is (> 0 = under water)
                        auto depthAt = [&](float x, float y) {
                            glm::vec4 farPoint = invViewProj * glm::vec4(x, y, 0.5f, 1.0f);
                            const glm::vec3 dir = glm::normalize(glm::vec3(farPoint) / farPoint.w - cam);
                            const glm::vec3 p = cam + dir * (nearZ / std::max(glm::dot(dir, forward), 0.05f));
                            return OceanWaves::SampleSurface(*ocean, sc, p.x, p.z, OceanWaves::Time()).height - p.y;
                        };
                        // Which side of the screen is lower (under water first)
                        const bool positiveUnder = depthAt(0.0f, 1.0f) > depthAt(0.0f, -1.0f);
                        float line[16];
                        int crossed = 0, allUnder = 0;
                        for (int c = 0; c < 16; ++c) {
                            const float x = -1.0f + 2.0f * c / 15.0f;
                            const float lo = depthAt(x, -1.0f), hi = depthAt(x, 1.0f);
                            if ((lo > 0.0f) == (hi > 0.0f)) {
                                // The whole column is on one side
                                const bool columnUnder = lo > 0.0f;
                                allUnder += columnUnder ? 1 : 0;
                                line[c] = (columnUnder == positiveUnder) ? -2.0f : 2.0f;
                                continue;
                            }
                            float a = -1.0f, b = 1.0f, da = lo;
                            for (int it = 0; it < 14; ++it) {
                                const float m = 0.5f * (a + b);
                                const float dm = depthAt(x, m);
                                if ((dm > 0.0f) == (da > 0.0f)) { a = m; da = dm; } else { b = m; }
                            }
                            line[c] = 0.5f * (a + b);
                            ++crossed;
                        }
                        if (crossed > 0) {
                            ubo.waterParams.x = 2.0f;
                            ubo.waterParams.w = positiveUnder ? 1.0f : 0.0f;
                            for (int c = 0; c < 16; ++c) ubo.waterline[c / 4][c % 4] = line[c];
                            m_cameraHalfUnderwater[viewIndex % 2] = true;
                            m_waterlineSide[viewIndex % 2] = positiveUnder ? 1.0f : -1.0f;
                            for (int k = 0; k < 8; ++k) {
                                const float at = k / 7.0f * 15.0f;
                                const int i0 = std::min(static_cast<int>(at), 14);
                                m_waterline[viewIndex % 2][k / 4][k % 4] = glm::mix(line[i0], line[i0 + 1], at - i0);
                            }
                        } else {
                            ubo.waterParams.x = allUnder == 16 ? 1.0f : 0.0f;
                            m_cameraUnderwater[viewIndex % 2] = allUnder == 16;
                        }
                    }
                }
            }
            ubo.prevViewProj = m_prevViewProjValid[viewIndex] ? m_prevViewProj[viewIndex] : unjitteredViewProj;
        };

        // Reflection probes: renders the next cube face of the next probe from the viewport's scene,
        // then fills the probe values the main view uses. Call after the shadow pass (it reuses the
        // view's shadow maps), before the view's UBO upload.
        auto captureProbes = [&](VulkanOffscreen& target, const Scene& sc, int viewSlot, UniformBufferObject& viewUbo) {
            ProbeCaptureState& state = m_probeCapture[viewSlot];
            const auto& env = sc.GetEnvironment();

            std::vector<glm::vec4> probes;
            std::vector<glm::vec4> boxes; // box projection doesn't change the capture, only its use
            for (const auto& node : sc.GetNodes()) {
                if (!node || !node->isReflectionProbe || node->IsUINode()) continue;
                probes.emplace_back(node->GetWorldPosition(sc), std::max(node->probeRadius, 0.1f));
                boxes.emplace_back(glm::max(node->probeBoxSize * 0.5f, glm::vec3(0.05f)), node->probeBoxProjection ? 1.0f : 0.0f);
                if (probes.size() == VulkanReflectionProbes::kMaxProbes) break;
            }

            std::string signature = std::to_string(m_editorUI.GetProbeRecaptureRequests()) + "|" +
                                    (env.useHDRI ? env.hdriPath : std::string("sky")) + "|" +
                                    std::to_string(env.enableSky) + "|" + std::to_string(env.skyIntensity) + "|" +
                                    // A running clock recaptures every quarter hour of scene time
                                    (env.timeOfDayEnabled ? std::to_string(std::floor(env.timeOfDay * 4.0f)) + "," + std::to_string(env.moonPhase) : std::string("-")) + "|" +
                                    (env.cloudsEnabled ? std::to_string(env.cloudCoverage) + "," + std::to_string(env.cloudOpacity) + "," + std::to_string(env.cloudScale) : std::string("-"));
            for (const auto& p : probes) {
                signature += "|" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + "," + std::to_string(p.w);
            }
            if (signature != state.signature) {
                state = ProbeCaptureState{};
                state.signature = signature;
                state.probes = probes;
            }

            if (state.nextProbe < static_cast<int>(state.probes.size())) {
                static const glm::vec3 kFaceDirs[6] = {
                    { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
                };
                // Must match SampleProbe() in env_common.glsl
                const glm::vec3 dir = kFaceDirs[state.nextFace];
                const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
                const glm::vec3 pos = glm::vec3(state.probes[state.nextProbe]);

                UniformBufferObject captureUbo = viewUbo;
                captureUbo.volumetricCloudParams.x = 0.0f; // probes capture the flat cloud layer
                captureUbo.view = glm::lookAt(pos, pos + dir, up);
                captureUbo.proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 500.0f);
                captureUbo.proj[1][1] *= -1.0f;
                captureUbo.cameraPos = glm::vec4(pos, 1.0f);
                captureUbo.unjitteredViewProj = captureUbo.prevViewProj = captureUbo.proj * captureUbo.view;
                captureUbo.probeParams = glm::vec4(0.0f); // probes don't see other probes
                captureUbo.postProcessParams.y = 0.0f;    // no SSAO in captures

                const int probeSlot = VulkanPipeline::ProbeSlotFor(viewSlot);
                m_pipeline.UpdateUniformBufferSlot(probeSlot, captureUbo);
                target.BeginRenderPass(cmd);
                m_pipeline.RenderScene(cmd, sc, m_meshes, m_customMeshes, PolygonMode::Solid, false, probeSlot, pos, ScenePhase::Opaque);
                target.NextScenePass(cmd);
                m_pipeline.RenderScene(cmd, sc, m_meshes, m_customMeshes, PolygonMode::Solid, false, probeSlot, pos, ScenePhase::Transparent);
                target.EndRenderPass(cmd);

                VulkanReflectionProbes& storage = m_pipeline.GetReflectionProbes(viewSlot);
                storage.CopyFaceFrom(cmd, target.GetHdrImage(), state.nextProbe, state.nextFace);
                if (++state.nextFace == 6) {
                    storage.GenerateMips(cmd);
                    state.nextFace = 0;
                    state.readyCount = ++state.nextProbe;
                }
            }

            for (int i = 0; i < state.readyCount; ++i) {
                viewUbo.probePosRadius[i] = state.probes[i];
                viewUbo.probeBoxExtents[i] = (i < static_cast<int>(boxes.size())) ? boxes[i] : glm::vec4(0.0f);
            }
            viewUbo.probeParams = glm::vec4(static_cast<float>(state.readyCount), 0.0f, 0.0f, 0.0f);
        };

        // TAA: an 8-step Halton (2,3) sub-pixel jitter, the same for every view this frame
        auto halton = [](uint32_t index, uint32_t base) {
            float f = 1.0f, r = 0.0f;
            for (uint32_t i = index; i > 0; i /= base) { f /= base; r += f * (i % base); }
            return r;
        };
        const uint32_t haltonIndex = (m_taaFrameIndex++ % 8) + 1;
        const glm::vec2 jitterPixels(halton(haltonIndex, 2) - 0.5f, halton(haltonIndex, 3) - 0.5f);

        // Offsets a viewport's projection by this frame's jitter when the scene uses TAA.
        // Returns the jitter in NDC (zero when TAA is off).
        auto applyJitter = [&](const Scene& sc, const VulkanOffscreen& target, glm::mat4& proj) {
            if (sc.GetEnvironment().antiAliasing != 2) return glm::vec2(0.0f);
            glm::vec2 jitterNdc(jitterPixels.x * 2.0f / target.GetWidth(), jitterPixels.y * 2.0f / target.GetHeight());
            proj[2][0] -= jitterNdc.x;
            proj[2][1] -= jitterNdc.y;
            return jitterNdc;
        };

        // TAA inputs for a viewport; also remembers this frame's view-projection for the next
        auto taaSettingsFor = [&](PostProcessSettings settings, int viewIndex, const glm::mat4& unjitteredViewProj,
                                  const glm::vec2& jitterNdc, const glm::mat4& unjitteredProj, bool velocityValid) {
            settings.velocityValid = velocityValid;
            settings.prepassValid = velocityValid; // the prepass ran this frame
            settings.projectionScale = glm::vec2(unjitteredProj[0][0], unjitteredProj[1][1]);
            settings.jitterNdc = jitterNdc;
            settings.reprojection = m_prevViewProj[viewIndex] * glm::inverse(unjitteredViewProj);
            if (!m_prevViewProjValid[viewIndex]) {
                // First frame: nothing to reproject from or blur along
                settings.reprojection = glm::mat4(1.0f);
                settings.velocityValid = false;
                settings.motionBlur = 0.0f;
            }
            m_prevViewProj[viewIndex] = unjitteredViewProj;
            m_prevViewProjValid[viewIndex] = true;
            // Linear depth for depth of field, from the unjittered projection
            settings.depthLinearize = glm::vec2(unjitteredProj[2][2], unjitteredProj[3][2]);
            settings.underwater = m_cameraUnderwater[viewIndex % 2];
            settings.halfUnderwater = m_cameraHalfUnderwater[viewIndex % 2];
            settings.waterlineSide = m_waterlineSide[viewIndex % 2];
            settings.waterline[0] = m_waterline[viewIndex % 2][0];
            settings.waterline[1] = m_waterline[viewIndex % 2][1];
            return settings;
        };

        auto postSettingsFor = [&](const Scene& sc) {
            const auto& env = sc.GetEnvironment();
            PostProcessSettings settings;
            settings.exposure = env.hdrExposure;
            settings.bloomStrength = env.bloomIntensity;
            settings.acesToneMapping = env.enableACES;
            settings.fxaa = env.antiAliasing == 1;
            settings.taa = env.antiAliasing == 2;
            settings.sharpen = env.sharpen;
            settings.autoExposure = env.autoExposure;
            settings.minAutoExposure = std::min(env.autoExposureMin, env.autoExposureMax);
            settings.maxAutoExposure = std::max(env.autoExposureMin, env.autoExposureMax);
            settings.adaptationSpeed = env.autoExposureSpeed;
            settings.deltaTime = frameDelta;
            settings.contrast = env.contrast;
            settings.saturation = env.saturation;
            settings.temperature = env.temperature;
            settings.tint = env.tint;
            settings.vignette = env.vignette;
            settings.filmGrain = env.filmGrain;
            settings.lensFlare = env.lensFlare;
            settings.time = static_cast<float>(std::fmod(frameTime, 1000.0));
            settings.aperture = env.depthOfField ? env.aperture : 0.0f;
            settings.focusDistance = env.focusDistance;
            settings.maxBlurRadius = env.dofMaxBlur;
            settings.motionBlur = env.motionBlur;
            auto& rt = Graphics::RayTracing::RayTracingManager::Get();
            auto& rtSettings = rt.GetSettings();
            // Ray-traced reflections are already in the shaded image: screen-space reflections
            // would add a second, screen-limited copy on top
            const bool rtrActive = rtSettings.enableHardwareRT && rtSettings.enableRTR && rt.IsHardwareRayTracingSupported() &&
                                   m_pipeline.HasRayTracingPipelines();
            settings.ssrStrength = rtrActive ? 0.0f : env.ssrIntensity;
            settings.ssrMaxDistance = env.ssrMaxDistance;
            settings.ssrThickness = env.ssrThickness;
            settings.frameIndex = m_taaFrameIndex;
            settings.lutStrength = env.lutStrength;
            settings.chromaticAberration = env.chromaticAberration;
            return settings;
        };
        const std::vector<LightData> playLightData = gatherLights(activePlayScene);
        const int numPlayLights = static_cast<int>(playLightData.size());

        auto UpdateTerrainMeshes = [&](Scene& sc) {
            for (const auto& n : sc.GetNodes()) {
                if (n->nodeType == NodeType::Terrain) {
                    auto tNode = std::dynamic_pointer_cast<TerrainNode>(n);
                    if (tNode && (tNode->isMeshDirty || m_customMeshes.find(tNode->customMeshName) == m_customMeshes.end())) {
                        std::string key = tNode->GetTerrainMeshKey();
                        auto it = m_customMeshes.find(key);
                        if (it != m_customMeshes.end()) {
                            it->second.Cleanup(VulkanContext::Get().GetDevice());
                        }
                        // Kept on the CPU too: ray tracing builds the terrain's BLAS from it (and
                        // rebuilds it when the data changes)
                        m_terrainCpuMeshes[key] = tNode->GenerateMeshData();
                        m_customMeshes[key] = VulkanBufferUtils::UploadMesh(m_terrainCpuMeshes[key]);
                        tNode->customMeshName = key;
                        tNode->isMeshDirty = false;
                        tNode->NotifyHeightsChanged(); // foliage follows the ground
                    }
                    // The painted splat map: uploaded when it changes (at most 20 times a second
                    // while painting, as each upload waits for the GPU)
                if (n->isCloth) {
                    if (!n->clothSim) {
                        n->clothSim = std::make_shared<Physics::XPBDClothSimulator>();
                        n->clothSim->InitializeGridCloth(n->clothWidth, n->clothHeight, n->clothResX, n->clothResY, n->clothMass);
                        if (n->clothPinTop) {
                            for (uint32_t px = 0; px < n->clothResX; ++px) {
                                n->clothSim->PinParticle(px);
                            }
                        }
                    }
                    if (!m_gameLoop.IsPlaying()) {
                        Physics::XPBDClothSettings cSettings;
                        cSettings.windVelocity = n->clothWind;
                        cSettings.stretchCompliance = n->clothCompliance;
                        n->clothSim->Step(0.016f, cSettings);
                    }
                    std::string clothKey = "cloth_node_" + std::to_string(n->id);
                    n->customMeshName = clothKey;
                    n->type = ShapeType::Custom;

                    const auto& parts = n->clothSim->GetParticles();
                    if (!parts.empty()) {
                        uint32_t rx = std::max(2u, n->clothResX);
                        uint32_t ry = std::max(2u, n->clothResY);
                        std::vector<Vertex> clothVerts(parts.size());
                        for (uint32_t y = 0; y < ry; ++y) {
                            for (uint32_t x = 0; x < rx; ++x) {
                                uint32_t idx = y * rx + x;
                                if (idx >= clothVerts.size()) break;
                                clothVerts[idx].pos = parts[idx].position;
                                clothVerts[idx].texCoord = glm::vec2(static_cast<float>(x) / (rx - 1), static_cast<float>(y) / (ry - 1));
                                glm::vec3 px1 = (x + 1 < rx) ? parts[y * rx + (x + 1)].position : parts[idx].position;
                                glm::vec3 px0 = (x > 0) ? parts[y * rx + (x - 1)].position : parts[idx].position;
                                glm::vec3 py1 = (y + 1 < ry) ? parts[(y + 1) * rx + x].position : parts[idx].position;
                                glm::vec3 py0 = (y > 0) ? parts[(y - 1) * rx + x].position : parts[idx].position;
                                glm::vec3 norm = glm::cross(px1 - px0, py1 - py0);
                                clothVerts[idx].normal = (glm::length(norm) > 1e-4f) ? glm::normalize(norm) : glm::vec3(0.0f, 0.0f, 1.0f);
                                clothVerts[idx].color = glm::vec4(1.0f);
                            }
                        }
                        auto it = m_customMeshes.find(clothKey);
                        if (it == m_customMeshes.end()) {
                            MeshData md;
                            md.vertices = clothVerts;
                            for (uint32_t y = 0; y < ry - 1; ++y) {
                                for (uint32_t x = 0; x < rx - 1; ++x) {
                                    uint32_t p00 = y * rx + x;
                                    uint32_t p10 = y * rx + (x + 1);
                                    uint32_t p01 = (y + 1) * rx + x;
                                    uint32_t p11 = (y + 1) * rx + (x + 1);
                                    md.indices.push_back(p00); md.indices.push_back(p01); md.indices.push_back(p10);
                                    md.indices.push_back(p10); md.indices.push_back(p01); md.indices.push_back(p11);
                                }
                            }
                            m_customMeshes[clothKey] = VulkanBufferUtils::UploadMesh(md);
                        } else {
                            VulkanBufferUtils::UpdateVertexBuffer(it->second, clothVerts);
                        }
                    }
                }
                    if (tNode) {
                        const std::string splatKey = tNode->GetSplatTextureKey();
                        auto uploaded = m_uploadedSplats.find(splatKey);
                        const double now = glfwGetTime();
                        const bool first = uploaded == m_uploadedSplats.end();
                        if ((first || uploaded->second.first != tNode->splatVersion) && (first || now - uploaded->second.second > 0.05) &&
                            tNode->splatData.size() == static_cast<size_t>(tNode->splatResolution) * tNode->splatResolution * 4) {
                            m_pipeline.SetRuntimeTexture(splatKey, tNode->splatData.data(), tNode->splatResolution, tNode->splatResolution);
                            m_uploadedSplats[splatKey] = { tNode->splatVersion, now };
                        }
                    }
                }
            }
        };
        UpdateWorldStreaming();
        UpdateTerrainMeshes(activePlayScene);
        UpdateTerrainMeshes(m_scene);

        // Hardware ray tracing: acceleration structures of the scenes shown this frame, built on
        // the GPU before any view renders. The game view (and its mirror / probe views) trace the
        // play scene; the editor views trace the edited scene (the same one when not playing).
        {
            auto& rtMgr = Graphics::RayTracing::RayTracingManager::Get();
            auto* vkRT = dynamic_cast<Graphics::RayTracing::VulkanRayTracing*>(rtMgr.GetBackend());
            for (int v = 0; v < VulkanPipeline::kViewCount; ++v) m_pipeline.SetViewRayTracing(v, VK_NULL_HANDLE);
            if (vkRT && vkRT->IsSupported()) {
                const bool rtOn = rtMgr.GetSettings().enableHardwareRT && m_pipeline.HasRayTracingPipelines();
                const bool sameScene = &activePlayScene == &m_scene;
                const bool editorShown = !m_gameConfig.isStandaloneGame;
                constexpr float kRTRange = 1500.0f; // instances further than this from the camera are left out
                auto meshResolver = [this](const Node& node, std::string& key, const MeshData*& data) {
                    return ResolveRayTracingMesh(node, key, data);
                };
                auto materialResolver = [this](const Node& node, int submesh, Graphics::RayTracing::RayTracingInstance& inst) {
                    ResolveRayTracingMaterial(node, submesh, inst);
                };
                if (rtOn) {
                    glm::vec3 gameFocus = m_camera.GetPosition();
                    if (m_gameConfig.isStandaloneGame || m_gameLoop.IsPlaying() || m_gameLoop.IsPaused()) {
                        auto gameCam = activePlayScene.GetActiveCamera();
                        if (gameCam) gameFocus = gameCam->GetWorldPosition(activePlayScene);
                    }
                    rtMgr.SyncScene(activePlayScene, 1, meshResolver, materialResolver, gameFocus, kRTRange);
                    if (editorShown && !sameScene) {
                        rtMgr.SyncScene(m_scene, 0, meshResolver, materialResolver, m_camera.GetPosition(), kRTRange);
                    } else {
                        rtMgr.ClearContext(0);
                    }
                } else {
                    rtMgr.ClearContext(0);
                    rtMgr.ClearContext(1);
                }
                vkRT->RecordFrame(cmd, m_currentFrame);
                if (rtOn) {
                    const VkDescriptorSet gameSet = vkRT->GetDescriptorSet(1);
                    const VkDescriptorSet editorSet = sameScene ? gameSet : vkRT->GetDescriptorSet(0);
                    const bool rts = rtMgr.GetSettings().enableRTS;
                    m_pipeline.SetViewRayTracing(VulkanPipeline::kViewPreview, gameSet, rts);
                    m_pipeline.SetViewRayTracing(VulkanPipeline::kViewPreviewMirror, gameSet, rts);
                    m_pipeline.SetViewRayTracing(VulkanPipeline::kViewPreviewProbe, gameSet, rts);
                    m_pipeline.SetViewRayTracing(VulkanPipeline::kViewEditor, editorSet, rts);
                    m_pipeline.SetViewRayTracing(VulkanPipeline::kViewEditorMirror, editorSet, rts);
                    m_pipeline.SetViewRayTracing(VulkanPipeline::kViewEditorProbe, editorSet, rts);
                }
            }
        }

        auto activeCam = activePlayScene.GetActiveCamera();
        const bool gameViewNeeded = m_gameConfig.isStandaloneGame ||
                                    m_gameLoop.IsPlaying() ||
                                    m_gameLoop.IsPaused() ||
                                    m_editorUI.IsGameOrPreviewVisible();
        if (activeCam && gameViewNeeded) {
            UniformBufferObject camUbo{};
            camUbo.view = activeCam->GetCameraViewMatrix(&activePlayScene);
            float aspect = (float)m_cameraPreviewOffscreen.GetWidth() / (float)m_cameraPreviewOffscreen.GetHeight();
            camUbo.proj = activeCam->GetCameraProjectionMatrix(aspect);
            const glm::mat4 gameUnjitteredProj = camUbo.proj;
            const glm::mat4 gameUnjitteredViewProj = camUbo.proj * camUbo.view;
            const glm::vec2 gameJitter = applyJitter(activePlayScene, m_cameraPreviewOffscreen, camUbo.proj);
            camUbo.cameraPos = glm::vec4(activeCam->GetWorldPosition(activePlayScene), 1.0f);
            camUbo.numLights = numPlayLights;
            m_pipeline.SetViewLights(VulkanPipeline::kViewPreview, playLightData);
            m_pipeline.SetOcclusionCulling(VulkanPipeline::kViewPreview, activePlayScene.GetEnvironment().occlusionCulling);
            m_editorUI.PopulateEnvironmentUBO(activePlayScene, camUbo, playLightData);
            fillViewUBO(activePlayScene, 1, gameUnjitteredViewProj, camUbo);
            camUbo.timeParams.x = static_cast<float>(glfwGetTime());

            gpuBegin("Game View");
            gpuBegin("Shadows");
            m_pipeline.RenderShadowPass(cmd, activePlayScene, m_meshes, m_customMeshes, true,
                                        activePlayScene.GetEnvironment().shadowDistance, camUbo);
            gpuEnd();
            gpuBegin("Reflection Probes");
            captureProbes(m_previewProbeOffscreen, activePlayScene, VulkanPipeline::kViewPreview, camUbo);
            gpuEnd();
            m_pipeline.UpdateUniformBufferPreview(camUbo);
            gpuBegin("Mirrors");
            renderMirror(m_previewMirrorOffscreen, activePlayScene, VulkanPipeline::kViewPreview);
            gpuEnd();
            gpuBegin("Prepass & SSAO");
            const bool gameVelocity = renderPrepass(m_cameraPreviewOffscreen, activePlayScene, true, camUbo.proj);
            gpuEnd();
            m_pipeline.SetViewHasSceneDepth(VulkanPipeline::kViewPreview, gameVelocity);
            gpuBegin("Fluid Surface");
            renderFluid(1);
            gpuEnd();

            gpuBegin("Opaque");
            m_cameraPreviewOffscreen.BeginRenderPass(cmd);
            Monitoring::GameProfiler::Get().BeginStage("Game Render Submission");
            // false = don't draw camera/light icons
            const glm::vec3 gameCamPos = activeCam->GetWorldPosition(activePlayScene);
            m_pipeline.RenderScene(cmd, activePlayScene, m_meshes, m_customMeshes, PolygonMode::Solid, false,
                                   VulkanPipeline::kViewPreview, gameCamPos, ScenePhase::Opaque);
            gpuEnd();
            gpuBegin("Transparent & VFX");
            m_cameraPreviewOffscreen.NextScenePass(cmd);
            m_pipeline.RenderScene(cmd, activePlayScene, m_meshes, m_customMeshes, PolygonMode::Solid, false,
                                   VulkanPipeline::kViewPreview, gameCamPos, ScenePhase::Transparent);
            // Heat haze bends everything drawn so far
            if (m_pipeline.HasDistortionParticles(activePlayScene)) {
                m_cameraPreviewOffscreen.RefreshSceneColor(cmd);
                m_pipeline.DrawDistortionParticles(cmd, activePlayScene, VulkanPipeline::kViewPreview, gameCamPos);
            }
            Monitoring::GameProfiler::Get().EndStage("Game Render Submission");
            m_cameraPreviewOffscreen.EndRenderPass(cmd);
            gpuEnd();
            gpuBegin("Post-Processing");
            m_cameraPreviewOffscreen.ApplyPostProcess(cmd, taaSettingsFor(postSettingsFor(activePlayScene), 1,
                                                                           gameUnjitteredViewProj, gameJitter,
                                                                           gameUnjitteredProj, gameVelocity));
            gpuEnd();
            gpuEnd(); // Game View

            // Record live game rendering telemetry into GameProfiler
            if (m_gameLoop.IsPlaying() || m_gameLoop.IsPaused() || m_gameConfig.isStandaloneGame) {
                uint32_t playDrawCalls = 0;
                uint64_t playTriangles = 0;
                uint64_t playVertices = 0;
                for (const auto& node : activePlayScene.GetNodes()) {
                    if (!node || node->IsUINode() || node->nodeType == NodeType::Empty || node->nodeType == NodeType::AudioSource || node->nodeType == NodeType::Spline) continue;
                    playDrawCalls++;
                    playTriangles += node->triangleCount;
                    playVertices += node->vertexCount;
                }
                uint32_t activeLights = numPlayLights;
                uint32_t shadowPasses = 0;
                for (int i = 0; i < numPlayLights; ++i) {
                    if (playLightData[i].spotAnglesAndShadow.z > 0.5f) shadowPasses++;
                }
                glm::vec2 gameRes(static_cast<float>(m_cameraPreviewOffscreen.GetWidth()), static_cast<float>(m_cameraPreviewOffscreen.GetHeight()));
                Monitoring::GameProfiler::Get().RecordGameRenderMetrics(playDrawCalls, playTriangles, playVertices, activeLights, shadowPasses, gameRes);
            }
        } else if (activeCam) {
            // Game / Preview window is not currently visible or active: skip heavy scene passes
            m_prevViewProjValid[1] = false;
        } else {
            // When no camera exists in scene, cleanly clear preview offscreen framebuffer
            gpuBegin("Game View");
            m_cameraPreviewOffscreen.BeginRenderPass(cmd);
            m_cameraPreviewOffscreen.EndRenderPass(cmd);
            PostProcessSettings emptySettings = postSettingsFor(activePlayScene);
            emptySettings.motionBlur = 0.0f;
            emptySettings.aperture = 0.0f;
            m_cameraPreviewOffscreen.ApplyPostProcess(cmd, emptySettings);
            gpuEnd();
            m_prevViewProjValid[1] = false;
        }

        if (!m_gameConfig.isStandaloneGame) {
        // 3. Render 3D Scene into Viewport Offscreen Framebuffer (Editor Camera)
        // Viewport strictly displays the pristine editor scene; game simulation runs isolated in the Game window
        Scene& activeViewportScene = m_scene;
        const std::vector<LightData> viewportLightData = gatherLights(activeViewportScene);
        const int numViewportLights = static_cast<int>(viewportLightData.size());

        UniformBufferObject editorUbo{};
        editorUbo.view = m_camera.GetViewMatrix();
        editorUbo.proj = m_camera.GetProjectionMatrix();
        const glm::mat4 editorUnjitteredProj = editorUbo.proj;
        const glm::mat4 editorUnjitteredViewProj = editorUbo.proj * editorUbo.view;
        const glm::vec2 editorJitter = applyJitter(activeViewportScene, m_offscreen, editorUbo.proj);
        editorUbo.cameraPos = glm::vec4(m_camera.GetPosition(), 1.0f);
        editorUbo.numLights = numViewportLights;
        m_pipeline.SetViewLights(VulkanPipeline::kViewEditor, viewportLightData);
        m_pipeline.SetOcclusionCulling(VulkanPipeline::kViewEditor, activeViewportScene.GetEnvironment().occlusionCulling);
        m_editorUI.PopulateEnvironmentUBO(activeViewportScene, editorUbo, viewportLightData);
        fillViewUBO(activeViewportScene, 0, editorUnjitteredViewProj, editorUbo);
        editorUbo.timeParams.x = static_cast<float>(glfwGetTime());


        // Synchronize and cull Meshlet GPU Virtualization clusters
        // Synchronize and cull Meshlet GPU Virtualization clusters (when enabled)
        if (Graphics::Meshlet::MeshletManager::Get().GetSettings().enableMeshletPipeline) {
            Graphics::Meshlet::MeshletManager::Get().SyncScene(activeViewportScene, m_camera.GetPosition(), editorUbo.proj * editorUbo.view);
        }

        // Update Temporal Upscaling & Large World Coordinates telemetry
        Graphics::Upscaling::TemporalUpscaler::Get().UpdateStats(m_offscreen.GetWidth(), m_offscreen.GetHeight(), m_taaFrameIndex);
        Core::LWC::CameraRelativeCoordinator::Get().SetCameraWorldPosition(glm::dvec3(m_camera.GetPosition()));
        Streaming::WorldPartitionManager::GetInstance().Update(m_camera.GetPosition(), editorUbo.proj * editorUbo.view, activeViewportScene, 0.016f);

        gpuBegin("Editor View");
        gpuBegin("Shadows");
        m_pipeline.RenderShadowPass(cmd, activeViewportScene, m_meshes, m_customMeshes, false,
                                    activeViewportScene.GetEnvironment().shadowDistance, editorUbo);
        gpuEnd();
        gpuBegin("Reflection Probes");
        captureProbes(m_editorProbeOffscreen, activeViewportScene, VulkanPipeline::kViewEditor, editorUbo);
        gpuEnd();
        m_pipeline.UpdateUniformBuffer(editorUbo);
        gpuBegin("Mirrors");
        renderMirror(m_editorMirrorOffscreen, activeViewportScene, VulkanPipeline::kViewEditor);
        gpuEnd();
        gpuBegin("Prepass & SSAO");
        const bool editorVelocity = renderPrepass(m_offscreen, activeViewportScene, false, editorUbo.proj);
        gpuEnd();
        m_pipeline.SetViewHasSceneDepth(VulkanPipeline::kViewEditor, editorVelocity);
        gpuBegin("Fluid Surface");
        renderFluid(0);
        gpuEnd();

        gpuBegin("Opaque");
        m_offscreen.BeginRenderPass(cmd);
        m_pipeline.RenderScene(cmd, activeViewportScene, m_meshes, m_customMeshes, m_editorUI.GetPolygonMode(), true,
                               VulkanPipeline::kViewEditor, m_camera.GetPosition(), ScenePhase::Opaque);
        gpuEnd();
        gpuBegin("Transparent & VFX");
        m_offscreen.NextScenePass(cmd);
        m_pipeline.RenderScene(cmd, activeViewportScene, m_meshes, m_customMeshes, m_editorUI.GetPolygonMode(), true,
                               VulkanPipeline::kViewEditor, m_camera.GetPosition(), ScenePhase::Transparent);
        if (m_pipeline.HasDistortionParticles(activeViewportScene)) {
            m_offscreen.RefreshSceneColor(cmd);
            m_pipeline.DrawDistortionParticles(cmd, activeViewportScene, VulkanPipeline::kViewEditor, m_camera.GetPosition());
        }
        m_offscreen.EndRenderPass(cmd);
        gpuEnd();
        gpuBegin("Post-Processing");
        m_offscreen.ApplyPostProcess(cmd, taaSettingsFor(postSettingsFor(activeViewportScene), 0,
                                                          editorUnjitteredViewProj, editorJitter,
                                                          editorUnjitteredProj, editorVelocity));
        gpuEnd();
        // Grid and axes on top of the finished image: unlit, unshadowed and not jittered by TAA
        if (m_editorUI.IsGridEnabled()) {
            gpuBegin("Grid Overlay");
            m_offscreen.BeginOverlayPass(cmd);
            m_pipeline.RenderGrid(cmd, m_gridMesh, m_axesMesh, false);
            m_offscreen.EndOverlayPass(cmd);
            gpuEnd();
        }
        gpuEnd(); // Editor View

        // Material previews the editor asked for this frame (a few a frame, the longest waiting first)
        {
            std::vector<MaterialPreview*> due;
            for (auto& entry : m_materialPreviews) {
                if (entry.second.requestedFrame == m_uiFrameCounter && entry.second.material) due.push_back(&entry.second);
            }
            std::sort(due.begin(), due.end(), [](const MaterialPreview* a, const MaterialPreview* b) { return a->renderedFrame < b->renderedFrame; });
            if (due.size() > static_cast<size_t>(kMaterialPreviewsPerFrame)) due.resize(kMaterialPreviewsPerFrame);
            if (!due.empty()) {
                gpuBegin("Material Previews");
                // A key light, a cool rim light and the sky; the camera looks at the origin
                std::vector<LightData> lights(2);
                lights[0].position = glm::vec4(glm::normalize(glm::vec3(-0.55f, 0.75f, 0.6f)), 0.0f);
                lights[0].color = glm::vec4(1.0f, 0.96f, 0.9f, 2.4f);
                lights[0].spotAnglesAndShadow.z = 0.0f;
                lights[1].position = glm::vec4(glm::normalize(glm::vec3(0.85f, 0.25f, -0.6f)), 0.0f);
                lights[1].color = glm::vec4(0.6f, 0.7f, 0.9f, 1.2f);
                lights[1].spotAnglesAndShadow.z = 0.0f;
                m_pipeline.SetViewLights(VulkanPipeline::kViewMaterialPreview, lights);
                const glm::vec3 eye(0.0f, 0.55f, 3.4f);
                UniformBufferObject ubo{};
                m_editorUI.PopulateEnvironmentUBO(m_materialPreviewScene, ubo, lights);
                ubo.view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.proj = glm::perspective(glm::radians(30.0f), 1.0f, 0.05f, 100.0f);
                ubo.proj[1][1] *= -1.0f;
                ubo.cameraPos = glm::vec4(eye, 1.0f);
                ubo.numLights = static_cast<int>(lights.size());
                ubo.unjitteredViewProj = ubo.prevViewProj = ubo.proj * ubo.view;
                ubo.shadowParams = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
                ubo.postProcessParams.y = 0.0f; // no SSAO (no prepass)
                ubo.postProcessParams.z = 0.0f;
                ubo.timeParams.x = static_cast<float>(glfwGetTime());
                ubo.envParams = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
                m_pipeline.UpdateUniformBufferSlot(VulkanPipeline::kViewMaterialPreview, ubo);

                PostProcessSettings post = postSettingsFor(m_materialPreviewScene);
                post.taa = false;
                post.autoExposure = false;
                post.motionBlur = 0.0f;
                post.aperture = 0.0f;
                post.ssrStrength = 0.0f;
                post.prepassValid = post.velocityValid = false;
                post.projectionScale = glm::vec2(ubo.proj[0][0], ubo.proj[1][1]);
                post.depthLinearize = glm::vec2(ubo.proj[2][2], ubo.proj[3][2]);
                for (MaterialPreview* preview : due) {
                    // The studio: just the object, with the requested material
                    m_materialPreviewScene.ClearNodes();
                    auto object = m_materialPreviewScene.AddNode(preview->shape == 1 ? ShapeType::Cube : (preview->shape == 2 ? ShapeType::Plane : ShapeType::Sphere), "Preview");
                    const uint32_t id = object->id;
                    static_cast<Node&>(*object) = *preview->material;
                    object->id = id;
                    object->name = "Preview";
                    object->parentId = 0;
                    object->childrenIds.clear();
                    object->nodeType = NodeType::Mesh;
                    object->type = preview->shape == 1 ? ShapeType::Cube : (preview->shape == 2 ? ShapeType::Plane : ShapeType::Sphere);
                    object->customMeshName.clear();
                    object->position = glm::vec3(0.0f);
                    object->rotation = preview->shape == 1 ? glm::vec3(0.0f, 35.0f, 0.0f) : (preview->shape == 2 ? glm::vec3(55.0f, 0.0f, 0.0f) : glm::vec3(0.0f));
                    object->scale = preview->shape == 2 ? glm::vec3(0.75f) : glm::vec3(preview->shape == 1 ? 1.05f : 1.45f);
                    object->isOcean = object->isLiquid = object->isFluidVolume = object->isMirror = false;
                    object->isReflectionProbe = object->isGIVolume = false;
                    object->hasSkeleton = false;
                    object->maxDrawDistance = 0.0f;
                    object->scriptPath.clear();
                    m_materialPreviewTarget.BeginRenderPass(cmd);
                    m_pipeline.RenderScene(cmd, m_materialPreviewScene, m_meshes, m_customMeshes, PolygonMode::Solid, false,
                                           VulkanPipeline::kViewMaterialPreview, eye, ScenePhase::Opaque);
                    m_materialPreviewTarget.NextScenePass(cmd);
                    m_pipeline.RenderScene(cmd, m_materialPreviewScene, m_meshes, m_customMeshes, PolygonMode::Solid, false,
                                           VulkanPipeline::kViewMaterialPreview, eye, ScenePhase::Transparent);
                    m_materialPreviewTarget.EndRenderPass(cmd);
                    m_materialPreviewTarget.ApplyPostProcess(cmd, post);
                    preview->image.CopyFrom(cmd, m_materialPreviewTarget);
                    preview->renderedFrame = m_uiFrameCounter;
                }
                gpuEnd();
            }
        }
        } // end if (!m_gameConfig.isStandaloneGame)

        // 2. Render ImGui Interface to Swapchain
        VkRenderPassBeginInfo mainRenderPassInfo{};
        mainRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        mainRenderPassInfo.renderPass = m_mainRenderPass;
        mainRenderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
        mainRenderPassInfo.renderArea.offset = {0, 0};
        mainRenderPassInfo.renderArea.extent = m_swapchainExtent;

        VkClearValue clearColor = {{{0.12f, 0.12f, 0.14f, 1.0f}}};
        mainRenderPassInfo.clearValueCount = 1;
        mainRenderPassInfo.pClearValues = &clearColor;

        gpuBegin("Editor UI");
        vkCmdBeginRenderPass(cmd, &mainRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
        gpuEnd();
        m_gpuProfiler.EndFrame(cmd);
        // The game view's passes, for the Profiler window
        {
            std::vector<Monitoring::GPUPassTiming> passes;
            bool inGame = false;
            for (const auto& t : m_gpuProfiler.Timings()) {
                if (t.depth == 0) inGame = t.name == "Game View";
                if (!inGame || t.depth != 1) continue;
                Monitoring::GPUPassTiming pass;
                pass.passName = t.name;
                pass.estimatedDurationMs = t.averageMs;
                passes.push_back(pass);
            }
            Monitoring::GameProfiler::Get().SetMeasuredGPUPasses(std::move(passes));
        }

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            throw std::runtime_error("Failed to record command buffer!");
        }

        // Submit command buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(ctx.GetGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit draw command buffer!");
        }

        // Present swapchain
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapchains[] = { m_swapchain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(ctx.GetPresentQueue(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
            m_framebufferResized = false;
            RecreateSwapchain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swapchain image!");
        }

        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void CreateSwapchain() {
        auto& ctx = VulkanContext::Get();
        SwapchainSupportDetails details = QuerySwapchainSupport(ctx.GetPhysicalDevice(), ctx.GetSurface());

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.formats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(details.presentModes);
        VkExtent2D extent = ChooseSwapExtent(details.capabilities);

        uint32_t imageCount = details.capabilities.minImageCount + 1;
        if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
            imageCount = details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = ctx.GetSurface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = ctx.GetQueueIndices();
        uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = details.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(ctx.GetDevice(), &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain!");
        }

        vkGetSwapchainImagesKHR(ctx.GetDevice(), m_swapchain, &imageCount, nullptr);
        m_swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(ctx.GetDevice(), m_swapchain, &imageCount, m_swapchainImages.data());

        m_swapchainImageFormat = surfaceFormat.format;
        m_swapchainExtent = extent;
    }

    void CreateSwapchainImageViews() {
        auto& ctx = VulkanContext::Get();
        m_swapchainImageViews.resize(m_swapchainImages.size());

        for (size_t i = 0; i < m_swapchainImages.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_swapchainImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(ctx.GetDevice(), &createInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image views!");
            }
        }
    }

    void CreateMainRenderPass() {
        auto& ctx = VulkanContext::Get();

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(ctx.GetDevice(), &renderPassInfo, nullptr, &m_mainRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create main render pass!");
        }
    }

    void CreateMainFramebuffers() {
        auto& ctx = VulkanContext::Get();
        m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

        for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
            VkImageView attachments[] = { m_swapchainImageViews[i] };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_mainRenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_swapchainExtent.width;
            framebufferInfo.height = m_swapchainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(ctx.GetDevice(), &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create framebuffer!");
            }
        }
    }

    void CreateCommandBuffers() {
        auto& ctx = VulkanContext::Get();
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = ctx.GetCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

        if (vkAllocateCommandBuffers(ctx.GetDevice(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffers!");
        }
    }

    void CreateSyncObjects() {
        auto& ctx = VulkanContext::Get();
        VkDevice device = ctx.GetDevice();

        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create synchronization objects!");
            }
        }
    }

    void CleanupSwapchain() {
        auto& ctx = VulkanContext::Get();
        VkDevice device = ctx.GetDevice();

        for (auto framebuffer : m_swapchainFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        for (auto imageView : m_swapchainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    void RecreateSwapchain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_window, &width, &height);
            glfwWaitEvents();
        }

        auto& ctx = VulkanContext::Get();
        vkDeviceWaitIdle(ctx.GetDevice());

        CleanupSwapchain();

        CreateSwapchain();
        CreateSwapchainImageViews();
        CreateMainFramebuffers();
    }

    void Cleanup() {
        auto& ctx = VulkanContext::Get();
        VkDevice device = ctx.GetDevice();

        vkDeviceWaitIdle(device);

        for (auto& pair : m_meshes) {
            pair.second.Cleanup(device);
        }
        for (auto& pair : m_customMeshes) {
            pair.second.Cleanup(device);
        }
        m_customMeshes.clear();
        m_cpuMeshes.clear();
        m_gridMesh.Cleanup(device);
        m_axesMesh.Cleanup(device);

        m_skinning.Cleanup();
        Audio::AudioEngine::Get().StopOutput();
        m_fluidSim.Cleanup();
        m_particleSim.Cleanup();
        m_gpuProfiler.Cleanup();
        if (m_playStreamer) m_playStreamer->ReleaseAll();
        m_editorStreamer.ReleaseAll();
        FlushPendingMeshFrees(true);
        Graphics::Meshlet::MeshletManager::Get().Shutdown();
        // Acceleration structures and their buffers belong to the device: free them now, not in
        // the singleton's destructor (which runs after the device is gone)
        Graphics::RayTracing::RayTracingManager::Get().Shutdown();
        Graphics::Pipeline::VulkanPipelineCache::Get().Shutdown();
        Graphics::Upscaling::TemporalUpscaler::Get().Shutdown();
        Streaming::WorldPartitionManager::GetInstance().Shutdown();
        m_pipeline.Cleanup();
        m_cameraPreviewOffscreen.Cleanup();
        m_editorMirrorOffscreen.Cleanup();
        m_previewMirrorOffscreen.Cleanup();
        m_editorProbeOffscreen.Cleanup();
        m_previewProbeOffscreen.Cleanup();
        for (auto& entry : m_materialPreviews) entry.second.image.Destroy();
        m_materialPreviews.clear();
        m_materialPreviewTarget.Cleanup();
        if (m_giBakeTargetReady) m_giBakeTarget.Cleanup();
        if (m_colorLut) {
            m_colorLut->Cleanup();
            m_colorLut = nullptr;
        }
        m_offscreen.Cleanup();
        for (auto& fluid : m_fluidTargets) fluid.Cleanup();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_imguiDescriptorPool, nullptr);
            m_imguiDescriptorPool = VK_NULL_HANDLE;
        }

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, m_inFlightFences[i], nullptr);
        }

        CleanupSwapchain();

        if (m_mainRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_mainRenderPass, nullptr);
            m_mainRenderPass = VK_NULL_HANDLE;
        }

        ctx.Cleanup();

#ifdef _WIN32
        if (!m_gameConfig.isStandaloneGame && m_window) {
            HWND hwnd = glfwGetWin32Window(m_window);
            if (hwnd && s_originalWndProc) {
                SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)s_originalWndProc);
                s_originalWndProc = nullptr;
                s_appInstance = nullptr;
            }
        }
#endif
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
        SwapchainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return capabilities.currentExtent;
        } else {
            int width, height;
            glfwGetFramebufferSize(m_window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    GLFWwindow* m_window{nullptr};

    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> m_swapchainImages;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    std::vector<VkImageView> m_swapchainImageViews;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;
    VkRenderPass m_mainRenderPass{VK_NULL_HANDLE};

    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame{0};
    bool m_framebufferResized{false};

    VkDescriptorPool m_imguiDescriptorPool{VK_NULL_HANDLE};

    // Engine Core Systems
    std::shared_ptr<Project> m_project;
    Camera m_camera;
    Scene m_scene;
    std::map<ShapeType, GPUMesh> m_meshes;
    std::map<std::string, GPUMesh> m_customMeshes;
    std::map<std::string, LoadedMesh> m_cpuMeshes;

    // ------------------------------------------------------------------------------------------
    // World streaming (Streaming::WorldStreamer). The edited scene streams sectors around the
    // editor camera; in Play the play scene (a clone) takes over those sectors and streams around
    // the game camera. Models are reference counted across both and freed a few frames after the
    // last sector using them leaves (the GPU may still be drawing them).
    // ------------------------------------------------------------------------------------------
    Streaming::WorldStreamer m_editorStreamer;
    std::unique_ptr<Streaming::WorldStreamer> m_playStreamer;
    const Scene* m_playStreamerScene{nullptr};
    std::string m_worldBoundScenePath;   // the m_scene file the editor streamer was bound for
    std::mutex m_streamMeshMutex;
    std::unordered_set<std::string> m_residentMeshNames; // models resident on the GPU (worker threads ask)
    std::unordered_map<std::string, int> m_streamMeshRefs;
    std::unordered_set<std::string> m_pinnedMeshes;      // resident before streaming used them: never freed by it
    std::vector<std::pair<uint64_t, GPUMesh>> m_pendingMeshFrees;
    uint64_t m_streamFrame{0};

    struct StreamingSaveGuard {
        EngineApp& app;
        explicit StreamingSaveGuard(EngineApp& a) : app(a) { if (app.m_editorStreamer.IsOpen()) app.m_editorStreamer.SuspendForSave(app.m_scene); }
        ~StreamingSaveGuard() { if (app.m_editorStreamer.IsOpen()) app.m_editorStreamer.ResumeAfterSave(app.m_scene); }
    };

    // A model file's CPU data (any thread: no GPU or engine state is touched)
    bool LoadMeshFileCPU(const std::string& name, const std::string& worldRoot, LoadedMesh& out) const {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        candidates.emplace_back(name);
        if (!worldRoot.empty()) {
            candidates.push_back(fs::path(worldRoot) / name);
            candidates.push_back(fs::path(worldRoot).parent_path() / name);
        }
        candidates.push_back(fs::path("assets/models") / name);
        candidates.push_back(fs::path("projects/Headland") / name);
        candidates.push_back(fs::path("projects/Headland/assets") / name);
        std::error_code ec;
        for (const auto& path : candidates) {
            if (!fs::exists(path, ec)) continue;
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".gltf" || ext == ".glb") return GltfLoader::Load(path.string(), out, true);
            if (ext == ".fbx") return FbxLoader::Load(path.string(), out, true);
            return ObjLoader::Load(path.string(), out, true);
        }
        return false;
    }

    bool AcquireStreamMesh(const std::string& name, LoadedMesh* cpu, const std::string& worldRoot) {
        int& refs = m_streamMeshRefs[name];
        if (m_customMeshes.count(name)) {
            if (refs == 0 && !m_residentMeshNames.count(name)) m_pinnedMeshes.insert(name); // the base scene's own
            ++refs;
            std::lock_guard<std::mutex> lock(m_streamMeshMutex);
            m_residentMeshNames.insert(name);
            return true;
        }
        LoadedMesh local;
        if (!cpu) {
            if (!LoadMeshFileCPU(name, worldRoot, local)) {
                if (refs == 0) m_streamMeshRefs.erase(name);
                std::cerr << "[WorldStreamer] Model not found: " << name << std::endl;
                return false;
            }
            cpu = &local;
        }
        m_customMeshes[name] = VulkanBufferUtils::UploadMesh(cpu->meshData);
        m_cpuMeshes[name] = std::move(*cpu);
        ++refs;
        std::lock_guard<std::mutex> lock(m_streamMeshMutex);
        m_residentMeshNames.insert(name);
        return true;
    }

    void ReleaseStreamMesh(const std::string& name) {
        auto it = m_streamMeshRefs.find(name);
        if (it == m_streamMeshRefs.end()) return;
        if (--it->second > 0) return;
        m_streamMeshRefs.erase(it);
        if (m_pinnedMeshes.count(name)) return;
        Graphics::RayTracing::RayTracingManager::Get().UnregisterMesh("mesh:" + name);
        auto gpu = m_customMeshes.find(name);
        if (gpu != m_customMeshes.end()) {
            m_pendingMeshFrees.emplace_back(m_streamFrame, gpu->second);
            m_customMeshes.erase(gpu);
        }
        m_cpuMeshes.erase(name);
        std::lock_guard<std::mutex> lock(m_streamMeshMutex);
        m_residentMeshNames.erase(name);
    }

    // A streamed-out terrain tile's generated mesh and splat map (keys are unique to the node id)
    void ReleaseTerrainMesh(const std::string& key, const std::string& splatKey) {
        if (!splatKey.empty()) {
            m_pipeline.ReleaseRuntimeTexture(splatKey);
            m_uploadedSplats.erase(splatKey);
        }
        if (key.empty()) return;
        Graphics::RayTracing::RayTracingManager::Get().UnregisterMesh("terrain:" + key);
        m_terrainCpuMeshes.erase(key);
        auto gpu = m_customMeshes.find(key);
        if (gpu != m_customMeshes.end()) {
            m_pendingMeshFrees.emplace_back(m_streamFrame, gpu->second);
            m_customMeshes.erase(gpu);
        }
    }

    void FlushPendingMeshFrees(bool all) {
        VkDevice device = VulkanContext::Get().GetDevice();
        auto it = m_pendingMeshFrees.begin();
        while (it != m_pendingMeshFrees.end()) {
            if (all || it->first + MAX_FRAMES_IN_FLIGHT + 1 <= m_streamFrame) {
                it->second.Cleanup(device);
                it = m_pendingMeshFrees.erase(it);
            } else {
                ++it;
            }
        }
    }

    Streaming::WorldStreamer::Hooks MakeStreamHooks(bool playScene, const std::string& worldRoot) {
        Streaming::WorldStreamer::Hooks h;
        h.isMeshResident = [this](const std::string& n) {
            std::lock_guard<std::mutex> lock(m_streamMeshMutex);
            return m_residentMeshNames.count(n) > 0;
        };
        h.loadMeshCPU = [this, worldRoot](const std::string& n, LoadedMesh& out) { return LoadMeshFileCPU(n, worldRoot, out); };
        h.acquireMesh = [this, worldRoot](const std::string& n, LoadedMesh* cpu) { return AcquireStreamMesh(n, cpu, worldRoot); };
        h.releaseMesh = [this](const std::string& n) { ReleaseStreamMesh(n); };
        h.onAttached = [this, playScene](Scene& sc, const std::vector<std::shared_ptr<Node>>& nodes) {
            auto& physics = m_gameLoop.GetPhysicsWorld();
            if (playScene && physics.IsInitialized()) physics.AddBodiesForNodes(sc, nodes);
        };
        h.onDetaching = [this, playScene](Scene& sc, const std::vector<std::shared_ptr<Node>>& nodes) {
            auto& physics = m_gameLoop.GetPhysicsWorld();
            for (const auto& n : nodes) {
                if (playScene) {
                    if (physics.IsInitialized()) physics.RemoveBodiesForNode(n->id);
                    Scripting::LuaScriptSystem::Get().DestroyNodeScript(n->id);
                }
                if (n->nodeType == NodeType::Terrain) {
                    auto terrain = std::dynamic_pointer_cast<TerrainNode>(n);
                    ReleaseTerrainMesh(n->customMeshName, terrain ? terrain->GetSplatTextureKey() : std::string());
                }
                if (sc.GetSelectedId() == static_cast<int32_t>(n->id)) sc.ClearSelection();
            }
        };
        return h;
    }

    // The editor streamer follows the edited scene: a .world manifest next to the scene file
    // whose base scene is that file turns streaming on
    void RebindEditorWorld() {
        namespace fs = std::filesystem;
        m_worldBoundScenePath = m_scene.GetFilePath();
        m_editorStreamer.ReleaseAll(); // the scene it streamed into was replaced
        if (m_worldBoundScenePath.empty()) return;
        std::error_code ec;
        const fs::path scenePath(m_worldBoundScenePath);
        for (const auto& entry : fs::directory_iterator(scenePath.parent_path(), ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".world") continue;
            Streaming::WorldManifest manifest;
            if (!Streaming::WorldManifest::Load(entry.path().string(), manifest)) continue;
            if (!fs::equivalent(manifest.BaseScenePath(), scenePath, ec)) continue;
            m_editorStreamer.SetHooks(MakeStreamHooks(false, manifest.rootDir));
            m_editorStreamer.Open(manifest);
            return;
        }
    }

    void UpdateWorldStreaming() {
        ++m_streamFrame;
        if (!Scripting::LuaScriptSystem::Get().GetStreamingStatsProvider()) {
            Scripting::LuaScriptSystem::Get().SetStreamingStatsProvider([this]() {
                const Streaming::WorldStreamer* s = m_playStreamer ? m_playStreamer.get() : &m_editorStreamer;
                const auto st = s->GetStats();
                return std::vector<std::pair<std::string, double>>{
                    { "sectors", st.totalSectors }, { "active", st.activeSectors }, { "loading", st.loadingSectors },
                    { "ready", st.readySectors }, { "nodes", st.activeNodes }, { "streamedIn", st.sectorsStreamedIn },
                    { "streamedOut", st.sectorsStreamedOut }, { "mbRead", st.bytesRead / (1024.0 * 1024.0) },
                    { "lastMs", st.lastActivateMs }, { "maxMs", st.maxActivateMs } };
            });
        }
        const bool standalone = m_gameConfig.isStandaloneGame;
        if (!standalone && m_scene.GetFilePath() != m_worldBoundScenePath) RebindEditorWorld();
        if (!standalone && m_editorStreamer.IsOpen()) {
            m_editorStreamer.Update(m_scene, m_camera.GetPosition(), 4.0f);
        }

        std::shared_ptr<Scene> playScene = m_gameLoop.GetMode() != Gameplay::EngineMode::Edit ? m_gameLoop.GetPlayScene() : nullptr;
        if (playScene && m_editorStreamer.GetManifest().sectors.size() > 0) {
            if (m_playStreamerScene != playScene.get()) {
                if (m_playStreamer) m_playStreamer->ReleaseAll();
                m_playStreamer = std::make_unique<Streaming::WorldStreamer>();
                m_playStreamer->SetHooks(MakeStreamHooks(true, m_editorStreamer.GetManifest().rootDir));
                // New ids in the play scene never collide with the editor scene's (terrain mesh
                // keys and acceleration structure keys are made from node ids)
                playScene->EnsureNextIdAtLeast(m_scene.PeekNextId() + 1000000u);
                m_playStreamer->Adopt(m_editorStreamer, *playScene);
                m_playStreamerScene = playScene.get();
                if (standalone) {
                    // Loading screen: everything near the player is in before the first frame
                    auto cam = playScene->GetActiveCamera();
                    const glm::vec3 focus = cam ? cam->GetWorldPosition(*playScene) : glm::vec3(0.0f);
                    m_playStreamer->LoadAround(*playScene, focus, 60.0f);
                }
            }
            auto cam = playScene->GetActiveCamera();
            const glm::vec3 focus = cam ? cam->GetWorldPosition(*playScene) : m_camera.GetPosition();
            m_playStreamer->Update(*playScene, focus, 4.0f);
        } else if (m_playStreamer) {
            m_playStreamer->ReleaseAll();
            m_playStreamer.reset();
            m_playStreamerScene = nullptr;
        }
        FlushPendingMeshFrees(false);
    }

    // CPU copies for ray tracing BLAS builds: built-in primitives and generated terrain meshes
    std::map<ShapeType, MeshData> m_cpuPrimitives;
    std::map<std::string, MeshData> m_terrainCpuMeshes;

    // Ray tracing geometry for a node: the mesh it draws at full detail (imported models, terrain
    // tiles, primitives). Skinned characters use their bind pose.
    bool ResolveRayTracingMesh(const Node& node, std::string& key, const MeshData*& data) {
        if (node.nodeType == NodeType::Terrain) {
            auto it = m_terrainCpuMeshes.find(node.customMeshName);
            if (it == m_terrainCpuMeshes.end()) return false;
            key = "terrain:" + it->first;
            data = &it->second;
            return true;
        }
        if (node.type == ShapeType::Custom) {
            // Models with levels of detail trace against their first reduced level: shadows and
            // reflections don't need full detail, and dense scans would cost ~5x the BVH memory
            if (!node.lodLevels.empty() && !node.lodLevels[0].meshKey.empty()) {
                auto lod = m_cpuMeshes.find(node.lodLevels[0].meshKey);
                if (lod != m_cpuMeshes.end()) {
                    key = "mesh:" + lod->first;
                    data = &lod->second.meshData;
                    return true;
                }
            }
            auto it = m_cpuMeshes.find(node.customMeshName);
            if (it == m_cpuMeshes.end()) {
                it = m_cpuMeshes.find(std::filesystem::path(node.customMeshName).filename().string());
            }
            if (it == m_cpuMeshes.end()) return false;
            key = "mesh:" + it->first;
            data = &it->second.meshData;
            return true;
        }
        auto it = m_cpuPrimitives.find(node.type);
        if (it == m_cpuPrimitives.end()) return false;
        key = "prim:" + std::to_string(static_cast<int>(node.type));
        data = &it->second;
        return true;
    }

    // Surface data for shading ray hits: the submesh's material (models with several), else the
    // node's, with the albedo texture for colour and alpha-tested cutouts
    void ResolveRayTracingMaterial(const Node& node, int submesh, Graphics::RayTracing::RayTracingInstance& inst) {
        if (submesh >= 0 && node.type == ShapeType::Custom && node.texturePath.empty()) {
            // The same mesh ResolveRayTracingMesh traced (its first LOD when it has levels)
            auto it = m_cpuMeshes.end();
            if (!node.lodLevels.empty() && !node.lodLevels[0].meshKey.empty()) it = m_cpuMeshes.find(node.lodLevels[0].meshKey);
            if (it == m_cpuMeshes.end()) it = m_cpuMeshes.find(node.customMeshName);
            if (it != m_cpuMeshes.end() && submesh < static_cast<int>(it->second.meshData.submeshes.size())) {
                const SubmeshSection& sub = it->second.meshData.submeshes[submesh];
                inst.albedo = glm::vec3(sub.diffuseColor);
                inst.roughness = sub.roughness;
                inst.metallic = sub.metallic;
                inst.emissive = sub.emissiveColor * sub.emissiveIntensity;
                inst.alphaCutoff = std::max(node.alphaCutoff, sub.alphaCutoff);
                inst.albedoTexture = sub.diffuseTexturePath;
                return;
            }
        }
        std::string albedo = node.texturePath;
        if (albedo.empty() && node.type == ShapeType::Custom) {
            auto it = m_cpuMeshes.find(node.customMeshName);
            if (it != m_cpuMeshes.end()) {
                if (!it->second.diffuseTexturePath.empty()) albedo = it->second.diffuseTexturePath;
                for (const auto& sub : it->second.meshData.submeshes) {
                    if (!albedo.empty()) break;
                    albedo = sub.diffuseTexturePath;
                }
            }
        }
        // Terrain is painted from layers (no single albedo texture): its tint stands in
        if (node.nodeType == NodeType::Terrain) albedo.clear();
        inst.albedoTexture = albedo;
    }
    GPUMesh m_gridMesh;
    GPUMesh m_axesMesh;
    VulkanOffscreen m_offscreen;
    VulkanOffscreen m_cameraPreviewOffscreen;
    VulkanOffscreen m_editorMirrorOffscreen;
    // TAA: previous frame's unjittered view-projection per viewport (0 = editor, 1 = game camera)
    glm::mat4 m_prevViewProj[2]{glm::mat4(1.0f), glm::mat4(1.0f)};
    uint32_t m_taaFrameIndex{0};
    VulkanOffscreen m_previewMirrorOffscreen;
    // Whether m_prevViewProj holds a real previous frame (motion vectors are garbage otherwise)
    bool m_prevViewProjValid[2]{false, false};
    double m_lastFrameTime{0.0};

    // Reflection probe capture, progressive: one cube face per viewport per frame
    struct ProbeCaptureState {
        std::string signature;        // probe layout + recapture requests; a change restarts capture
        std::vector<glm::vec4> probes; // position, radius (in capture order)
        int nextProbe{0};
        int nextFace{0};
        int readyCount{0};            // probes whose 6 faces and mips are done
    };
    VulkanOffscreen m_editorProbeOffscreen;
    VulkanOffscreen m_previewProbeOffscreen;

    // Material previews for the editor (Inspector, file browser): the object with a node's
    // material, rendered into one shared target and copied into a small image per material
    static constexpr uint32_t kMaterialPreviewSize = 256;
    static constexpr size_t kMaxMaterialPreviews = 12;
    static constexpr int kMaterialPreviewsPerFrame = 3;
    struct MaterialPreview {
        VulkanPreviewImage image;
        std::shared_ptr<Node> material; // the latest request's copy of the material
        int shape{0};                    // 0 sphere, 1 cube, 2 plane
        uint64_t requestedFrame{0};
        uint64_t renderedFrame{0};
    };
    VulkanOffscreen m_materialPreviewTarget;
    std::map<std::string, MaterialPreview> m_materialPreviews;
    Scene m_materialPreviewScene;
    uint64_t m_uiFrameCounter{0};

    void InitMaterialPreviewScene() {
        m_materialPreviewScene.Clear();
        SceneEnvironment& env = m_materialPreviewScene.GetEnvironment();
        env.enableSky = true;
        env.skyIntensity = 1.0f;
        env.useHDRI = false;
        env.timeOfDayEnabled = false;
        env.cloudsEnabled = false;
        env.physicalAtmosphere = false;
        env.enableShadows = false;
        env.antiAliasing = 1; // FXAA
        env.autoExposure = false;
        env.hdrExposure = 1.0f;
        env.bloomIntensity = 0.12f;
        env.ssaoIntensity = 0.0f;
        env.ssrIntensity = 0.0f;
        env.fogDensity = 0.0f;
        env.volumetricFogDensity = 0.0f;
        env.motionBlur = 0.0f;
        env.depthOfField = false;
        env.vignette = 0.0f;
        env.filmGrain = 0.0f;
        env.lensFlare = 0.0f;
        env.chromaticAberration = 0.0f;
        env.rainAmount = env.snowAmount = env.wetness = env.snowCover = 0.0f;
    }

    // EditorUI asks for a preview each frame it shows one; the image comes from an earlier frame
    // (null until the first is ready). The least recently shown previews are dropped.
    void* RequestMaterialPreview(const std::string& key, const Node& material, int shape) {
        auto it = m_materialPreviews.find(key);
        if (it == m_materialPreviews.end()) {
            if (m_materialPreviews.size() >= kMaxMaterialPreviews) {
                auto oldest = std::min_element(m_materialPreviews.begin(), m_materialPreviews.end(),
                                               [](const auto& a, const auto& b) { return a.second.requestedFrame < b.second.requestedFrame; });
                vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
                oldest->second.image.Destroy();
                m_materialPreviews.erase(oldest);
            }
            it = m_materialPreviews.emplace(key, MaterialPreview{}).first;
            it->second.image.Create(kMaterialPreviewSize, kMaterialPreviewSize);
        }
        MaterialPreview& preview = it->second;
        preview.material = material.Clone();
        preview.shape = shape;
        preview.requestedFrame = m_uiFrameCounter;
        return preview.image.GetTextureId();
    }
    ProbeCaptureState m_probeCapture[2]; // 0 = editor viewport, 1 = game camera

    // GPU skinning: this frame's posed characters (filled by UpdateSkeletalAnimations)
    struct SkinJob {
        const Node* node{nullptr};
        std::string meshKey;      // key in m_cpuMeshes
        std::string gpuMeshName;  // node's custom mesh name (index buffer lookup)
        const LoadedMesh* loaded{nullptr};
        glm::vec4 centerScale{0.0f};
        glm::vec4 morphs{0.0f};
        std::vector<glm::mat4> bones;
    };
    std::vector<SkinJob> m_skinJobs;
    // Render target sizes that follow their views (see SyncViewSizes)
    TargetSizer m_editorViewSizer;
    TargetSizer m_gameViewSizer;
    std::map<std::string, const void*> m_skinSourceIds; // which vertex array each uploaded bind pose came from
    VulkanSkinning m_skinning;
    // Colour grading LUT shared by both viewports (loaded from SceneEnvironment::colorLutPath)
    std::shared_ptr<VulkanTexture> m_colorLut;
    std::string m_colorLutPath;
    VulkanPipeline m_pipeline;
    EditorUI m_editorUI;
    Gameplay::GameLoop m_gameLoop;
    GameConfig m_gameConfig;
};

int main(int argc, char* argv[]) {
    // Offline tools (no window): model optimizer
    {
        bool handled = false;
        int rc = Tools::RunModelOptimizerCommandLine(argc, argv, handled);
        if (handled) return rc;
        rc = Tools::RunWorldBuilderCommandLine(argc, argv, handled);
        if (handled) return rc;
    }
    try {
        LOG("main() started");
        EngineApp app;
        app.Run(argc, argv);
        LOG("app.Run() completed normally");
    } catch (const std::exception& e) {
        LOG("Fatal Exception caught in main(): " << e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG("Unknown fatal exception caught in main()");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#ifdef _WIN32
static LRESULT CALLBACK CustomWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (s_appInstance && !s_appInstance->IsStandalone()) {
        if (uMsg == WM_NCHITTEST) {
            LRESULT hit = s_appInstance->HandleNcHitTest(lParam);
            if (hit != HTCLIENT) {
                return hit;
            }
        } else if (uMsg == WM_GETMINMAXINFO) {
            HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (hMonitor) {
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfo(hMonitor, &mi)) {
                    MINMAXINFO* mmi = (MINMAXINFO*)lParam;
                    mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                    return 0;
                }
            }
        }
    }
    return CallWindowProc(s_originalWndProc, hwnd, uMsg, wParam, lParam);
}

LRESULT EngineApp::HandleNcHitTest(LPARAM lParam) {
    if (!m_window) return HTCLIENT;

    HWND hwnd = glfwGetWin32Window(m_window);
    if (!hwnd) return HTCLIENT;

    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ScreenToClient(hwnd, &pt);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    bool isMaximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0;

    // Border edge resizing (only active when not maximized)
    if (!isMaximized) {
        const int border = 6;
        bool left = (pt.x < border);
        bool right = (pt.x >= width - border);
        bool top = (pt.y < border);
        bool bottom = (pt.y >= height - border);

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    // Title bar region (top 32 pixels)
    if (pt.y >= 0 && pt.y < 32) {
        // Window control buttons area (Minimize, Maximize, Close): rightmost 140px
        if (pt.x >= width - 140) {
            return HTCLIENT; // Let ImGui handle button clicks and hover animations
        }

        // Menu bar items area (File, Edit, etc.) in Editor mode
        if (m_editorUI.IsMouseOverMenuBar(pt.x, pt.y)) {
            return HTCLIENT; // Let ImGui handle dropdown menus
        }

        // Draggable title bar region
        return HTCAPTION;
    }

    return HTCLIENT;
}
#endif
