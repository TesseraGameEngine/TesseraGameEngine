#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Utils/SimpleJson.h"
#include "Node.h"

struct SceneEnvironment {
    float hdrExposure{1.0f};
    bool enableACES{true};
    bool enableFXAA{true}; // legacy (scenes saved before antiAliasing existed)
    int antiAliasing{2};   // 0 = off, 1 = FXAA, 2 = TAA (temporal)
    bool enableShadows{true};
    float shadowDistance{40.0f}; // metres in front of the camera covered by the sun's shadow map
    bool enableSky{true};        // procedural sky background + sky-based ambient light and reflections
    float skyIntensity{1.0f};
    bool physicalAtmosphere{false}; // sky from a physically based atmosphere instead of the stylised colours

    // Time of day (SkyClock): the clock places the sun and moon and steers the first directional light
    bool timeOfDayEnabled{false};
    float timeOfDay{14.0f};          // hours, 0..24
    float dayLengthMinutes{20.0f};   // real minutes for a full day while the clock runs (0 = stopped)
    bool clockRunsInEditor{false};   // also run the clock while editing (it always runs in Play)
    float latitude{35.0f};           // degrees: how high the sun climbs (0 = straight overhead at noon)
    float northAngle{0.0f};          // degrees: turns the sun's path around the vertical axis
    float dayOfYear{81.0f};          // 1..365: the season (81 = spring equinox, 172 = midsummer in the north)
    bool advanceCalendar{true};      // the day moves on (and the moon through its phases) as the clock passes midnight
    bool moonEnabled{true};
    float moonPhase{0.5f};           // 0 = new, 0.25 = first quarter, 0.5 = full, 0.75 = last quarter
    float moonBrightness{1.0f};      // moonlight strength (times the light's intensity)

    // Clouds: a procedural layer drawn in the sky, with shadows on the ground
    bool cloudsEnabled{false};
    float cloudCoverage{0.45f};      // 0 = clear, 1 = overcast
    float cloudOpacity{0.85f};
    float cloudHeight{1500.0f};      // metres
    float cloudScale{1.0f};          // size of the cloud shapes
    float windSpeed{8.0f};           // m/s, moves the clouds
    float windDirection{45.0f};      // degrees, the direction the wind blows toward
    float cloudShadows{0.6f};        // how much the clouds shade the ground
    bool volumetricClouds{false};    // ray-marched 3D clouds (fly through them) instead of a flat layer
    float cloudThickness{1500.0f};   // metres from the cloud base to the tops (volumetric)
    float cloudDetail{1.0f};         // how much billowing detail erodes the edges (volumetric)

    // Weather (Weather::Update): presets blend the clouds, wind, fog, rain, snow and lightning
    // over weatherTransition seconds, and wet the ground / settle snow over time. With presets
    // off, rain, snow, wetness, snow cover and lightning are set directly.
    bool weatherEnabled{false};
    int weatherPreset{0};            // Weather::Preset
    float weatherTransition{20.0f};  // seconds to blend into a new preset
    float rainAmount{0.0f};          // 0..1
    float snowAmount{0.0f};          // 0..1
    float wetness{0.0f};             // 0..1: darker, glossier surfaces and puddles
    float snowCover{0.0f};           // 0..1: snow settled on surfaces facing up
    float lightningRate{0.0f};       // strikes per minute
    float snowDepth{0.3f};           // metres of snow at full cover (banks against walls, caps edges)
    float thunderVolume{0.8f};       // 0 = no thunder
    // Runtime only (not saved): the current lightning strike
    float lightningAge{-1.0f};       // seconds since the strike began (-1 = none)
    float lightningTimer{0.0f};      // seconds until the next strike
    float lightningFlash{0.0f};      // 0..1 brightness of the flash
    float boltAzimuth{0.0f};         // radians
    float boltSeed{0.0f};
    int strikeCount{0};
    // Thunder on its way from recent strikes (sound travels at 343 m/s): seconds until it
    // arrives (-1 = free slot) and how far away each strike was
    float thunderTimers[4]{-1.0f, -1.0f, -1.0f, -1.0f};
    float thunderDistances[4]{};
    float thunderDistance{0.0f};     // metres to the strike whose thunder is due
    bool thunderDue{false};          // set when thunder should start playing; the player clears it
    uint32_t weatherRandom{12345u};
    bool enableGI{true};
    glm::vec3 giSkyColor{0.35f, 0.48f, 0.68f};
    float giIntensity{1.0f};
    glm::vec3 fogColor{0.55f, 0.68f, 0.85f};
    float fogDensity{0.0f};
    float bloomIntensity{0.35f};
    float ssaoIntensity{1.0f};
    float ssaoRadius{0.5f}; // metres: how far around a point SSAO looks for occluders
    float ssrIntensity{0.35f};
    float volumetricScattering{0.0f};

    // HDRI environment: an equirectangular .hdr image replaces the procedural sky and lights the scene
    bool useHDRI{false};
    std::string hdriPath;
    float hdriIntensity{1.0f};
    float hdriRotation{0.0f}; // degrees around the vertical axis

    // Camera exposure and colour grading
    bool autoExposure{false};
    float autoExposureMin{0.1f};
    float autoExposureMax{8.0f};
    float autoExposureSpeed{1.5f};
    float contrast{1.0f};
    float saturation{1.0f};
    float temperature{0.0f}; // -1 cool .. +1 warm
    float tint{0.0f};        // -1 green .. +1 magenta
    float vignette{0.0f};
    float filmGrain{0.0f};
    float sharpen{0.25f};    // with TAA

    // Lens
    float lensFlare{0.0f};
    bool depthOfField{false};
    float focusDistance{10.0f}; // metres
    float aperture{0.5f};       // 0..1 blur strength
    float dofMaxBlur{12.0f};    // pixels
    float motionBlur{0.0f};     // shutter: 0 = off, 0.5 = typical film
    float chromaticAberration{0.0f};

    // Colour LUT (.cube, or an N*N x N image strip) applied after grading
    std::string colorLutPath;
    float lutStrength{1.0f};

    // Screen-space reflections (strength is ssrIntensity above)
    float ssrMaxDistance{20.0f}; // metres
    float ssrThickness{0.3f};    // metres

    // Volumetric fog (on when volumetricScattering above is > 0)
    float volumetricFogDensity{0.02f};
    float volumetricHeightFalloff{0.1f}; // how fast the fog thins with height (per metre)
    float volumetricBaseHeight{0.0f};    // fog is densest below this height
    float volumetricAnisotropy{0.6f};    // 0 = even glow, toward 1 = bright shafts looking toward the light

    // Skip objects hidden behind others (hardware occlusion queries, results two frames old)
    bool occlusionCulling{false};

    // Baked lighting: an irradiance volume (grid of light probes) baked with bounce light
    bool bakedGIEnabled{true};
    float bakedGIIntensity{1.0f};
    int bakedGIBounces{2};
    float bakedGISpacing{1.0f};   // metres between probes
    std::string bakedGIPath;      // baked data file (written by the bake)
    bool bakeLightmaps{true};     // the bake also makes lightmaps for Lightmap Static meshes
    float lightmapDensity{8.0f};  // lightmap texels per metre (lowered if the atlas would be too big)
    int lightmapSamples{128};     // rays per texel per bounce
    std::string lightmapPath;     // baked lightmap file (written by the bake)

    // Real-time Voxel Global Illumination (VXGI): dynamic 3D voxel radiance & cone tracing
    bool vxgiEnabled{false};
    float vxgiIntensity{1.0f};
    float vxgiRange{24.0f};       // metres across the cubic voxel volume
    float vxgiBounce{0.6f};       // multi-bounce feedback strength
};

#include "Animation/Cutscene.h"

class Scene {
public:
    Scene();

    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    const std::string& GetFilePath() const { return m_filePath; }
    void SetFilePath(const std::string& path) { m_filePath = path; }

    std::shared_ptr<Node> AddNode(ShapeType type, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddCharacterNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddCameraNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddLightNode(LightType type = LightType::Point, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddColliderNode(ColliderType colType = ColliderType::Box, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddEmptyNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddSplineNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddDecalNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddParticleNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddAudioNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddTerrainNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUICanvasNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIPanelNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUITextNode(const std::string& text = "Text Label", const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIButtonNode(const std::string& label = "Button", const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIProgressBarNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUISpinnerNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUITextFieldNode(const std::string& placeholder = "Enter text...", const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIPasswordFieldNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUISliderNode(float minVal = 0.0f, float maxVal = 100.0f, float curVal = 50.0f, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUICheckboxNode(const std::string& label = "Checkbox", bool checked = false, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIRadioButtonNode(const std::string& label = "Radio Option", const std::string& group = "Group1", bool checked = false, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIWindowNode(const std::string& title = "Floating Window", const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIImageNode(const std::string& texturePath = "", const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIVideoNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIScrollPaneNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIDropdownNode(const std::vector<std::string>& items = {"Option 1", "Option 2", "Option 3"}, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIDatePickerNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUISelectableListNode(const std::vector<std::string>& items = {"Item 1", "Item 2", "Item 3", "Item 4", "Item 5"}, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIColorPickerNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUITableNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUITabsNode(const std::vector<std::string>& tabs = {"Tab 1", "Tab 2", "Tab 3"}, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIMenuBarNode(const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIShapeNode(UIShapeType shape = UIShapeType::Rectangle, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIBoxContainerNode(BoxOrientation orientation = BoxOrientation::Vertical, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> AddUIGridContainerNode(int columns = 4, const std::string& name = "", uint32_t parentId = 0);
    std::shared_ptr<Node> InstantiateScene(const std::string& sceneFilePath, uint32_t parentId = 0, const glm::vec3& positionOffset = glm::vec3(0.0f));
    bool ReloadSceneInstance(uint32_t instanceRootId);

    std::shared_ptr<Node> AddCustomMeshNode(const std::string& meshName,
                                            const glm::vec3& bMin,
                                            const glm::vec3& bMax,
                                            uint32_t vertCount,
                                            uint32_t triCount,
                                            const std::string& nodeName = "",
                                            uint32_t parentId = 0);

    bool DeleteNode(uint32_t id);
    bool DeleteSelected();

    // Copy, Paste & Duplicate (Subtree-aware)
    bool CopyNode(uint32_t id);
    bool CopySelected();
    std::shared_ptr<Node> PasteNode(uint32_t targetParentId = 0, const glm::vec3& positionOffset = glm::vec3(0.5f, 0.0f, 0.5f));
    std::shared_ptr<Node> DuplicateNode(uint32_t id, const glm::vec3& positionOffset = glm::vec3(0.5f, 0.0f, 0.5f));
    bool HasClipboardData() const { return !m_clipboardNodes.empty(); }

    // Hierarchy management
    bool SetParent(uint32_t childId, uint32_t newParentId);
    bool Unparent(uint32_t childId);
    bool IsDescendantOf(uint32_t potentialDescendantId, uint32_t ancestorId) const;
    std::vector<std::shared_ptr<Node>> GetRootNodes() const;
    std::vector<std::shared_ptr<Node>> GetChildren(uint32_t parentId) const;

    // Reordering within hierarchy
    bool MoveNodeUp(uint32_t nodeId);
    bool MoveNodeDown(uint32_t nodeId);
    bool MoveNodeToTop(uint32_t nodeId);
    bool MoveNodeToBottom(uint32_t nodeId);
    bool ReorderNodeBefore(uint32_t sourceNodeId, uint32_t targetNodeId);
    bool ReorderNodeAfter(uint32_t sourceNodeId, uint32_t targetNodeId);

    std::shared_ptr<Node> GetNodeById(uint32_t id) const;
    std::shared_ptr<Node> GetNodeByName(const std::string& name) const;
    std::shared_ptr<Node> GetSelectedNode() const;

    void SelectNode(int32_t id) { m_selectedId = id; }
    void ClearSelection() { m_selectedId = -1; }
    int32_t GetSelectedId() const { return m_selectedId; }

    const std::vector<std::shared_ptr<Node>>& GetNodes() const { return m_nodes; }
    // A fresh node id (for nodes created elsewhere and attached with AttachNode)
    uint32_t AllocateNodeId() { return m_nextId++; }
    // Ids handed out from now on are at least `next` (keeps two scenes' new ids apart)
    void EnsureNextIdAtLeast(uint32_t next) { if (m_nextId < next) m_nextId = next; }
    uint32_t PeekNextId() const { return m_nextId; }
    void AttachNode(std::shared_ptr<Node> node) {
        if (!node) return;
        for (const auto& existing : m_nodes) {
            if (existing && existing->id == node->id) return;
        }
        m_nodes.push_back(node);
    }
    bool DetachNode(uint32_t id) {
        auto it = std::remove_if(m_nodes.begin(), m_nodes.end(), [id](const std::shared_ptr<Node>& n) {
            return n && n->id == id;
        });
        if (it != m_nodes.end()) {
            m_nodes.erase(it, m_nodes.end());
            return true;
        }
        return false;
    }
    std::vector<std::shared_ptr<Node>> GetCameras() const;
    std::vector<std::shared_ptr<Node>> GetLights() const;
    std::vector<std::shared_ptr<Node>> GetColliders() const;
    std::vector<std::shared_ptr<Node>> GetUINodes() const;

    std::shared_ptr<Node> GetActiveCamera() const;
    void SetActiveCamera(uint32_t id) { m_activeCameraId = id; }

    SceneEnvironment& GetEnvironment() { return m_environment; }
    const SceneEnvironment& GetEnvironment() const { return m_environment; }

    // Cutscene & Cinematic Sequencer
    std::vector<Animation::CutsceneSequence>& GetCutscenes() { return m_cutscenes; }
    const std::vector<Animation::CutsceneSequence>& GetCutscenes() const { return m_cutscenes; }
    Animation::CutsceneSequence* FindCutscene(const std::string& name);
    const Animation::CutsceneSequence* FindCutscene(const std::string& name) const;
    Animation::CutsceneSequence& AddCutscene(const Animation::CutsceneSequence& seq);
    bool RemoveCutscene(const std::string& name);

    void Clear();
    // Removes every node but keeps the environment
    void ClearNodes();
    void ResetToDefault();

    // Serialization & Multi-Scene management
    JsonValue Serialize() const;
    bool Deserialize(const JsonValue& json, std::function<bool(const std::string&, const std::string&)> loadMeshCallback = nullptr);
    bool SaveToFile(const std::string& filePath) const;
    bool LoadFromFile(const std::string& filePath, std::function<bool(const std::string&, const std::string&)> loadMeshCallback = nullptr);

    // Deep Snapshot & Restore for Game Loop (Play / Stop)
    std::shared_ptr<Scene> Clone() const;
    void CopyFrom(const Scene& other);

private:
    void CollectSubtreeIds(uint32_t nodeId, std::vector<uint32_t>& outIds) const;

    std::string m_name{"Main Scene"};
    std::string m_filePath{""};
    std::vector<std::shared_ptr<Node>> m_nodes;
    std::vector<std::shared_ptr<Node>> m_clipboardNodes;
    int32_t m_selectedId{-1};
    uint32_t m_nextId{1};
    uint32_t m_activeCameraId{0};
    SceneEnvironment m_environment{};
    std::vector<Animation::CutsceneSequence> m_cutscenes;
};
