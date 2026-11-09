#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Utils/SimpleJson.h"

struct ImDrawList;
struct ImVec2;
class Scene;
struct NodeInspectorState;
struct UINodeRendererState;
namespace Gameplay { class GameLoop; }
namespace Resource {
    class MeshResource;
    class MaterialResource;
}
namespace Physics { class XPBDClothSimulator; }

enum class NodeType {
    Mesh,
    Character,
    Camera,
    Light,
    Collider,
    Empty,
    Spline,
    Decal,
    ParticleEmitter,
    AudioSource,
    Terrain,
    UI_Canvas,
    UI_Panel,
    UI_Text,
    UI_Button,
    UI_ProgressBar,
    UI_Spinner,
    UI_TextField,
    UI_PasswordField,
    UI_Slider,
    UI_Checkbox,
    UI_RadioButton,
    UI_Window,
    UI_Image,
    UI_Video,
    UI_ScrollPane,
    UI_Dropdown,
    UI_DatePicker,
    UI_SelectableList,
    UI_ColorPicker,
    UI_Table,
    UI_Tabs,
    UI_MenuBar,
    UI_Shape,
    UI_BoxContainer,
    UI_GridContainer
};

enum class UIShapeType {
    Rectangle,
    RoundedRect,
    Circle,
    Triangle
};

inline const char* UIShapeTypeToString(UIShapeType shape) {
    switch (shape) {
        case UIShapeType::Rectangle: return "Rectangle";
        case UIShapeType::RoundedRect: return "RoundedRect";
        case UIShapeType::Circle: return "Circle";
        case UIShapeType::Triangle: return "Triangle";
        default: return "Rectangle";
    }
}

inline UIShapeType StringToUIShapeType(const std::string& str) {
    if (str == "RoundedRect" || str == "Rounded Rectangle") return UIShapeType::RoundedRect;
    if (str == "Circle") return UIShapeType::Circle;
    if (str == "Triangle") return UIShapeType::Triangle;
    return UIShapeType::Rectangle;
}


enum class BoxOrientation {
    Horizontal,
    Vertical
};

inline const char* BoxOrientationToString(BoxOrientation o) {
    return o == BoxOrientation::Horizontal ? "Horizontal" : "Vertical";
}

inline BoxOrientation StringToBoxOrientation(const std::string& str) {
    return str == "Horizontal" ? BoxOrientation::Horizontal : BoxOrientation::Vertical;
}

enum class UIAnchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

inline const char* NodeTypeToString(NodeType type) {
    switch (type) {
        case NodeType::Mesh: return "Mesh";
        case NodeType::Character: return "Character";
        case NodeType::Camera: return "Camera";
        case NodeType::Light: return "Light";
        case NodeType::Collider: return "Collider";
        case NodeType::Empty: return "Empty";
        case NodeType::Spline: return "Spline";
        case NodeType::Decal: return "Decal";
        case NodeType::ParticleEmitter: return "ParticleEmitter";
        case NodeType::AudioSource: return "AudioSource";
        case NodeType::Terrain: return "Terrain";
        case NodeType::UI_Canvas: return "UI_Canvas";
        case NodeType::UI_Panel: return "UI_Panel";
        case NodeType::UI_Text: return "UI_Text";
        case NodeType::UI_Button: return "UI_Button";
        case NodeType::UI_ProgressBar: return "UI_ProgressBar";
        case NodeType::UI_Spinner: return "UI_Spinner";
        case NodeType::UI_TextField: return "UI_TextField";
        case NodeType::UI_PasswordField: return "UI_PasswordField";
        case NodeType::UI_Slider: return "UI_Slider";
        case NodeType::UI_Checkbox: return "UI_Checkbox";
        case NodeType::UI_RadioButton: return "UI_RadioButton";
        case NodeType::UI_Window: return "UI_Window";
        case NodeType::UI_Image: return "UI_Image";
        case NodeType::UI_Video: return "UI_Video";
        case NodeType::UI_ScrollPane: return "UI_ScrollPane";
        case NodeType::UI_Dropdown: return "UI_Dropdown";
        case NodeType::UI_DatePicker: return "UI_DatePicker";
        case NodeType::UI_SelectableList: return "UI_SelectableList";
        case NodeType::UI_ColorPicker: return "UI_ColorPicker";
        case NodeType::UI_Table: return "UI_Table";
        case NodeType::UI_Tabs: return "UI_Tabs";
        case NodeType::UI_MenuBar: return "UI_MenuBar";
        case NodeType::UI_Shape: return "UI_Shape";
        case NodeType::UI_BoxContainer: return "UI_BoxContainer";
        case NodeType::UI_GridContainer: return "UI_GridContainer";
        default: return "Unknown";
    }
}

inline NodeType StringToNodeType(const std::string& str) {
    if (str == "Character") return NodeType::Character;
    if (str == "Camera") return NodeType::Camera;
    if (str == "Light") return NodeType::Light;
    if (str == "Collider") return NodeType::Collider;
    if (str == "Empty") return NodeType::Empty;
    if (str == "Spline") return NodeType::Spline;
    if (str == "Decal") return NodeType::Decal;
    if (str == "ParticleEmitter") return NodeType::ParticleEmitter;
    if (str == "AudioSource") return NodeType::AudioSource;
    if (str == "Terrain") return NodeType::Terrain;
    if (str == "UI_Canvas") return NodeType::UI_Canvas;
    if (str == "UI_Panel") return NodeType::UI_Panel;
    if (str == "UI_Text") return NodeType::UI_Text;
    if (str == "UI_Button") return NodeType::UI_Button;
    if (str == "UI_ProgressBar") return NodeType::UI_ProgressBar;
    if (str == "UI_Spinner") return NodeType::UI_Spinner;
    if (str == "UI_TextField") return NodeType::UI_TextField;
    if (str == "UI_PasswordField") return NodeType::UI_PasswordField;
    if (str == "UI_Slider") return NodeType::UI_Slider;
    if (str == "UI_Checkbox") return NodeType::UI_Checkbox;
    if (str == "UI_RadioButton") return NodeType::UI_RadioButton;
    if (str == "UI_Window") return NodeType::UI_Window;
    if (str == "UI_Image") return NodeType::UI_Image;
    if (str == "UI_Video") return NodeType::UI_Video;
    if (str == "UI_ScrollPane") return NodeType::UI_ScrollPane;
    if (str == "UI_Dropdown") return NodeType::UI_Dropdown;
    if (str == "UI_DatePicker") return NodeType::UI_DatePicker;
    if (str == "UI_SelectableList") return NodeType::UI_SelectableList;
    if (str == "UI_ColorPicker") return NodeType::UI_ColorPicker;
    if (str == "UI_Table") return NodeType::UI_Table;
    if (str == "UI_Tabs") return NodeType::UI_Tabs;
    if (str == "UI_MenuBar") return NodeType::UI_MenuBar;
    if (str == "UI_Shape") return NodeType::UI_Shape;
    if (str == "UI_BoxContainer") return NodeType::UI_BoxContainer;
    if (str == "UI_GridContainer") return NodeType::UI_GridContainer;
    return NodeType::Mesh;
}

inline const char* UIAnchorToString(UIAnchor anchor) {
    switch (anchor) {
        case UIAnchor::TopLeft: return "Top-Left";
        case UIAnchor::TopCenter: return "Top-Center";
        case UIAnchor::TopRight: return "Top-Right";
        case UIAnchor::CenterLeft: return "Center-Left";
        case UIAnchor::Center: return "Center";
        case UIAnchor::CenterRight: return "Center-Right";
        case UIAnchor::BottomLeft: return "Bottom-Left";
        case UIAnchor::BottomCenter: return "Bottom-Center";
        case UIAnchor::BottomRight: return "Bottom-Right";
        default: return "Center";
    }
}

inline UIAnchor StringToUIAnchor(const std::string& str) {
    if (str == "Top-Left" || str == "TopLeft") return UIAnchor::TopLeft;
    if (str == "Top-Center" || str == "TopCenter") return UIAnchor::TopCenter;
    if (str == "Top-Right" || str == "TopRight") return UIAnchor::TopRight;
    if (str == "Center-Left" || str == "CenterLeft") return UIAnchor::CenterLeft;
    if (str == "Center-Right" || str == "CenterRight") return UIAnchor::CenterRight;
    if (str == "Bottom-Left" || str == "BottomLeft") return UIAnchor::BottomLeft;
    if (str == "Bottom-Center" || str == "BottomCenter") return UIAnchor::BottomCenter;
    if (str == "Bottom-Right" || str == "BottomRight") return UIAnchor::BottomRight;
    return UIAnchor::Center;
}

enum class ShapeType {
    Cube,
    Sphere,
    Cone,
    Cylinder,
    Plane,
    Quad,
    Custom
};

inline const char* ShapeTypeToString(ShapeType type) {
    switch (type) {
        case ShapeType::Cube: return "Cube";
        case ShapeType::Sphere: return "Sphere";
        case ShapeType::Cone: return "Cone";
        case ShapeType::Cylinder: return "Cylinder";
        case ShapeType::Plane: return "Plane";
        case ShapeType::Quad: return "Quad";
        case ShapeType::Custom: return "Custom";
        default: return "Cube";
    }
}

inline ShapeType StringToShapeType(const std::string& str) {
    if (str == "Sphere") return ShapeType::Sphere;
    if (str == "Cone") return ShapeType::Cone;
    if (str == "Cylinder") return ShapeType::Cylinder;
    if (str == "Plane") return ShapeType::Plane;
    if (str == "Quad") return ShapeType::Quad;
    if (str == "Custom") return ShapeType::Custom;
    return ShapeType::Cube;
}

enum class LightType {
    Directional,
    Point,
    Spot,
    AreaRect
};

inline const char* LightTypeToString(LightType type) {
    switch (type) {
        case LightType::Directional: return "Directional";
        case LightType::Point: return "Point";
        case LightType::Spot: return "Spot";
        case LightType::AreaRect: return "AreaRect";
        default: return "Point";
    }
}

inline LightType StringToLightType(const std::string& str) {
    if (str == "Directional") return LightType::Directional;
    if (str == "Spot") return LightType::Spot;
    if (str == "AreaRect" || str == "Area" || str == "Area (Rect)") return LightType::AreaRect;
    return LightType::Point;
}

enum class ColliderType {
    Box,
    Sphere,
    Capsule,
    Mesh
};

inline const char* ColliderTypeToString(ColliderType type) {
    switch (type) {
        case ColliderType::Box: return "Box";
        case ColliderType::Sphere: return "Sphere";
        case ColliderType::Capsule: return "Capsule";
        case ColliderType::Mesh: return "Mesh";
        default: return "Box";
    }
}

inline ColliderType StringToColliderType(const std::string& str) {
    if (str == "Sphere") return ColliderType::Sphere;
    if (str == "Capsule") return ColliderType::Capsule;
    if (str == "Mesh") return ColliderType::Mesh;
    return ColliderType::Box;
}

enum class RigidBodyType {
    None,
    Static,
    Kinematic,
    Dynamic
};

inline const char* RigidBodyTypeToString(RigidBodyType type) {
    switch (type) {
        case RigidBodyType::None: return "None";
        case RigidBodyType::Static: return "Static";
        case RigidBodyType::Kinematic: return "Kinematic";
        case RigidBodyType::Dynamic: return "Dynamic";
        default: return "None";
    }
}

inline RigidBodyType StringToRigidBodyType(const std::string& str) {
    if (str == "Static") return RigidBodyType::Static;
    if (str == "Kinematic") return RigidBodyType::Kinematic;
    if (str == "Dynamic") return RigidBodyType::Dynamic;
    return RigidBodyType::None;
}

class Scene;

struct LODLevel {
    std::string meshKey{""};
    float distance{25.0f};
    float reductionRatio{0.5f};
    uint32_t triangleCount{0};
};


// Replication Modes & Rules for Declarative Multiplayer Property Sync
enum class ReplicationMode {
    None = 0,
    Always = 1,
    OnChange = 2
};

inline const char* ReplicationModeToString(ReplicationMode m) {
    switch (m) {
        case ReplicationMode::Always: return "Always";
        case ReplicationMode::OnChange: return "OnChange";
        default: return "None";
    }
}

inline ReplicationMode StringToReplicationMode(const std::string& str) {
    if (str == "Always") return ReplicationMode::Always;
    if (str == "OnChange") return ReplicationMode::OnChange;
    return ReplicationMode::None;
}

struct PropertyReplicationRule {
    std::string propertyName{"position"};
    ReplicationMode mode{ReplicationMode::OnChange};
    float syncRateHz{20.0f};
    float lastSyncTime{0.0f};
    std::string lastSerializedValue{""};
};

struct ReplicationConfig {
    bool enabled{false};
    std::vector<PropertyReplicationRule> rules;

    void AddRule(const std::string& prop, ReplicationMode mode = ReplicationMode::OnChange, float hz = 20.0f) {
        for (auto& r : rules) {
            if (r.propertyName == prop) {
                r.mode = mode;
                r.syncRateHz = hz;
                return;
            }
        }
        rules.push_back({prop, mode, hz, 0.0f, ""});
    }

    bool HasRule(const std::string& prop) const {
        for (const auto& r : rules) {
            if (r.propertyName == prop && r.mode != ReplicationMode::None) return true;
        }
        return false;
    }

    void RemoveRule(const std::string& prop) {
        rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const PropertyReplicationRule& r) {
            return r.propertyName == prop;
        }), rules.end());
    }
};

// Signal Connection for Decoupled Event Messaging
struct SignalConnection {
    uint64_t id{0};
    std::string signalName{""};
    std::function<void(const std::vector<std::string>&)> callback;
};

struct Node {
    virtual ~Node() = default;

    uint32_t id{0};
    uint32_t parentId{0}; // 0 = Root level in scene
    std::vector<uint32_t> childrenIds;

    std::string name{"Node"};
    NodeType nodeType{NodeType::Mesh};
    ShapeType type{ShapeType::Cube};

    // Custom imported mesh info
    std::string customMeshName{""};
    glm::vec3 boundsMin{-1.0f};
    glm::vec3 boundsMax{1.0f};
    uint32_t vertexCount{0};
    uint32_t triangleCount{0};

    // Level of Detail (LOD) & Draw Distance Culling
    bool enableLOD{false};
    float maxDrawDistance{0.0f}; // 0 = disabled (infinite)
    std::vector<LODLevel> lodLevels;
    int forcedLOD{-1}; // -1 = Automatic by distance, 0 = LOD0, 1 = LOD1, 2 = LOD2...
    int currentActiveLOD{0}; // Read-only queryable at runtime

    std::string GetActiveLODMeshKey(float distanceToCamera) const {
        if (!enableLOD || lodLevels.empty()) {
            const_cast<Node*>(this)->currentActiveLOD = 0;
            return customMeshName;
        }
        if (forcedLOD >= 0) {
            if (forcedLOD == 0) {
                const_cast<Node*>(this)->currentActiveLOD = 0;
                return customMeshName;
            }
            size_t idx = static_cast<size_t>(forcedLOD - 1);
            if (idx < lodLevels.size() && !lodLevels[idx].meshKey.empty()) {
                const_cast<Node*>(this)->currentActiveLOD = forcedLOD;
                return lodLevels[idx].meshKey;
            }
            const_cast<Node*>(this)->currentActiveLOD = 0;
            return customMeshName;
        }

        // Search from furthest LOD down to closest
        for (int i = static_cast<int>(lodLevels.size()) - 1; i >= 0; --i) {
            if (distanceToCamera >= lodLevels[i].distance && !lodLevels[i].meshKey.empty()) {
                const_cast<Node*>(this)->currentActiveLOD = i + 1;
                return lodLevels[i].meshKey;
            }
        }
        const_cast<Node*>(this)->currentActiveLOD = 0;
        return customMeshName;
    }

    bool IsCulledByDistance(float distanceToCamera) const {
        if (maxDrawDistance > 0.001f && distanceToCamera > maxDrawDistance) {
            return true;
        }
        return false;
    }

    // Local Transform components
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles in degrees
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::vec4 color{0.8f, 0.45f, 0.2f, 1.0f};

    // Material & Texture properties
    std::string texturePath{""};
    std::string normalMapPath{""};
    std::string metallicMapPath{""};
    std::string roughnessMapPath{""};
    std::string aoMapPath{""};
    std::string emissiveMapPath{""};
    std::string heightMapPath{""};
    std::string shaderGraphPath{""};
    std::string materialName{""};
    float roughness{0.5f};
    float metallic{0.0f};
    float aoStrength{1.0f};
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    float emissiveIntensity{1.0f};
    float heightScale{0.04f};
    float alphaCutoff{0.0f}; // 0 = off; pixels with alpha below this are cut out (foliage, fences, hair)
    bool doubleSided{true};  // false: back faces are not drawn
    float refractionIOR{1.5f}; // transparent materials: 1.0 = no refraction, 1.33 = water, 1.5 = glass
    bool isMirror{false};      // planar mirror: shows the scene reflected across this surface

    // In-Game Video Screen (TV set, Cinema screen, Smartphone, CCTV monitor, Billboard, or UI_Video)
    bool isVideoScreen{false};
    int videoChannelPreset{1};        // 0 = External File/GIF/Atlas/Sequence, 1 = Cinema Reel, 2 = TV News, 3 = CCTV, 4 = Smartphone, 5 = CRT Bars, 6 = Sci-Fi Radar
    std::string videoSourcePath{""};  // Path to animated GIF, sprite-sheet atlas, frame folder, or video file
    bool videoPlaying{true};          // Play/pause state
    bool videoLoop{true};             // Loop video when reaching duration
    float videoSpeed{1.0f};           // Playback speed multiplier
    float videoCurrentTime{0.0f};     // Current playhead time (seconds)
    float videoDuration{10.0f};       // Video duration (seconds)
    float videoFps{24.0f};            // Frame rate for frame/atlas decoding
    int videoAtlasCols{4};            // Columns when videoSourcePath is a sprite-sheet video atlas
    int videoAtlasRows{4};            // Rows when videoSourcePath is a sprite-sheet video atlas
    float videoScreenBrightness{1.6f};// Emissive HDR glow (drives emissiveIntensity & emissiveMapPath so screens glow in dark rooms & VXGI)
    float videoScanlineStrength{0.15f}; // CRT/LCD scanline & subpixel grille effect (0 = off, 1 = strong CRT)
    std::string videoAudioPath{""};   // Optional synced .wav audio track
    float videoAudioVolume{0.8f};     // Volume of synced audio
    bool videoSpatialAudio{true};     // True = 3D positional audio from the TV/phone/cinema position

    // XPBD Cloth Simulation (Physically simulated fabric/curtain/flag)
    bool isCloth{false};
    uint32_t clothResX{10};
    uint32_t clothResY{10};
    float clothWidth{1.5f};
    float clothHeight{1.5f};
    glm::vec3 clothWind{2.0f, 0.0f, 1.0f};
    float clothMass{1.0f};
    float clothCompliance{0.00001f};
    bool clothPinTop{true};
    std::shared_ptr<Physics::XPBDClothSimulator> clothSim{nullptr}; // runtime only: each scene steps its own
    // The simulated surface's GPU mesh (runtime only, one per node instance: the edited scene and
    // its play clone have their own); the node's saved shape / model stay as they are
    std::string ClothMeshKey() const {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "@cloth:%p", static_cast<const void*>(this));
        return buf;
    }

    // Advanced material (see shaders/material_ext.glsl)
    int materialModel{0};                       // 0 standard, 1 skin, 2 hair, 3 eye
    glm::vec3 subsurfaceColor{0.9f, 0.35f, 0.25f}; // skin: colour light picks up under the surface
    float subsurfaceStrength{0.6f};             // skin: how far light wraps past the shadow edge
    float subsurfaceTranslucency{0.3f};         // skin: glow through thin parts (ears, nostrils) when backlit
    glm::vec3 hairHighlightTint{1.0f, 0.85f, 0.7f}; // hair: secondary highlight colour (times albedo)
    float hairHighlightShift{0.08f};            // hair: moves the highlights along the strand
    float irisRadius{0.15f};                    // eye: iris radius in UV units (iris centred at UV 0.5, 0.5)
    float irisDepth{0.04f};                     // eye: how far below the cornea the iris sits (UV units)
    float corneaIOR{1.336f};                    // eye: refraction of the cornea
    float clearCoat{0.0f};                      // glossy top layer (car paint, varnish), 0 = none
    float clearCoatRoughness{0.05f};
    bool triplanarMapping{false};               // project textures from world X/Y/Z instead of UVs
    float triplanarTileSize{2.0f};              // metres per texture repeat
    float triplanarSharpness{4.0f};             // higher = harder seams between projections
    std::string detailAlbedoPath{""};           // tiled close-up detail (0.5 grey = no change)
    std::string detailNormalPath{""};
    float detailTiling{8.0f};                   // detail repeats per base-texture repeat (or per triplanar tile)
    float detailStrength{1.0f};
    bool useVertexColors{false};                // multiply the base colour by the mesh's vertex colours
    bool lightmapStatic{true};                  // never moves: gets a baked lightmap (Baked Lighting)

    // Liquid: a mesh inside a container, cut off at its fill level (shader.frag); its surface
    // sloshes when the container accelerates (Liquids::Simulation)
    bool isLiquid{false};
    float liquidFill{0.6f};                      // 0 = empty, 1 = full (of the mesh's height)
    glm::vec4 liquidTopColor{0.85f, 0.95f, 1.0f, 0.55f}; // colour of the surface seen from above
    float liquidSlosh{1.0f};                     // how strongly it sloshes (0 = never)
    float liquidColorDepth{0.05f};               // metres of liquid that show its colour; thicker looks deeper
    // Runtime only (not saved): the surface plane (xyz = normal, w = distance) and slosh state
    glm::vec4 liquidPlane{0.0f, 1.0f, 0.0f, 1e9f};
    glm::vec2 liquidWobble{0.0f};
    glm::vec2 liquidWobbleVelocity{0.0f};
    glm::vec3 liquidLastPosition{0.0f};
    glm::vec3 liquidLastVelocity{0.0f};
    bool liquidTracked{false};
    glm::vec3 liquidWorldMin{0.0f};              // world box of the mesh (thickness in the shader)
    glm::vec3 liquidWorldMax{0.0f};
    glm::vec4 liquidRipple{0.0f};                // xyz = where a stream lands, w = ripple strength

    // Pour spout: liquid streams out along this node's forward axis (-Z) once that axis has
    // turned far enough from straight up (a jug's spout faces up when the jug is upright)
    bool isPourSpout{false};
    float pourFlow{-1.0f};                       // -1 = from the tilt, 0..1 = fixed (scripts)
    float pourStartAngle{60.0f};                 // degrees from straight up where pouring starts
    float pourStreamRadius{0.012f};              // stream radius at full flow (m)
    float pourSpeed{1.0f};                       // speed the liquid leaves the spout at (m/s)
    glm::vec4 pourColor{0.8f, 0.92f, 1.0f, 0.35f};
    uint32_t pourSourceId{0};                    // the liquid it drains (0 = never runs out)
    float pourCurrentFlow{0.0f};                 // runtime: this frame's flow

    // Fluid volume (empty nodes): real simulated fluid in a box centred on the node
    // (VulkanFluidSim: Position Based Fluids on the GPU), drawn as one smooth liquid surface
    bool isFluidVolume{false};
    glm::vec3 fluidSize{1.0f, 0.6f, 0.6f};       // the box the fluid stays in (m)
    float fluidFill{0.4f};                       // share of the box filled at the start
    int fluidStart{1};                           // 0 = a layer on the floor, 1 = a column on one side (dam break)
    float fluidSpacing{0.04f};                   // particle spacing (m): smaller = finer and slower
    float fluidViscosity{0.05f};                 // 0 = water, higher = syrupy
    glm::vec4 fluidColor{0.55f, 0.8f, 0.95f, 1.0f};
    float fluidColorDepth{0.5f};                 // metres of fluid that show its colour
    bool fluidResetRequested{false};             // runtime: start the fluid again

    // Ocean: drawn as a wavy water surface at this node's height instead of its mesh
    // (VulkanPipeline::DrawOcean, shaders/ocean_*.glsl). Rotation and scale don't apply.
    bool isOcean{false};
    glm::vec2 oceanSize{2000.0f, 2000.0f};      // metres covered (x, z), centred on the node
    float waveHeight{0.8f};                     // height of the biggest waves, trough to crest (m)
    float waveLength{24.0f};                    // length of the longest waves (m)
    float waveDirection{30.0f};                 // wind direction (degrees around the vertical axis)
    float waveChoppiness{0.6f};                 // 0 = rounded swell, 1 = sharp crests
    float waveSpeed{1.0f};                      // 1 = real-world speed for the wavelength
    glm::vec3 waterShallowColor{0.55f, 0.85f, 0.80f}; // tint of what's seen through shallow water
    glm::vec3 waterDeepColor{0.02f, 0.10f, 0.14f};    // colour of deep water
    float waterClarity{4.0f};                   // metres of water you can see through
    float waterRoughness{0.06f};                // surface roughness (blurs reflections)
    float whitecaps{0.4f};                      // foam on wave crests (0..1)
    float shoreFoamWidth{1.0f};                 // metres of foam where the water meets objects and shores
    float oceanSoundVolume{0.8f};               // surf and crashing waves (in Play); 0 = silent
    bool isReflectionProbe{false}; // captures the surroundings at this point for nearby reflections
    bool occludesSound{false};     // solid enough to block sound (acoustic ray tracing); colliders and physics bodies always do
    float probeRadius{10.0f};      // metres the probe affects
    bool probeBoxProjection{false}; // correct reflections for a box-shaped room (parallax)
    bool isGIVolume{false};          // marks the box the baked lighting covers (centred on the node)
    glm::vec3 giVolumeSize{20.0f, 6.0f, 20.0f};
    glm::vec3 probeBoxSize{10.0f, 4.0f, 10.0f}; // full size of that box, centred on the probe

    // Scripting component properties
    std::string scriptPath{""};

    // Skeletal Rigging, IK, and Morph Target properties
    bool hasSkeleton{false};
    std::string skeletonName{""};
    uint32_t boneCount{0};
    std::vector<std::string> boneNames;
    std::vector<std::string> animClipNames;
    std::string currentAnimState{"Idle"};
    bool playAnimation{false}; // off: model holds its rest pose
    float animationSpeed{1.0f};       // this character's playback speed (times the clip's own)
    float animationStartOffset{0.0f}; // seconds into the clip this character starts at (so crowds aren't in step)
    // Runtime only (not saved): this character's own clock, and the editor's preview play state
    float animationTime{0.0f};
    bool animationPreviewPlaying{false};
    bool enableIK{false};
    std::string ikTargetBone{""};
    glm::vec3 ikTargetPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 ikPoleVector{0.0f, 0.0f, 1.0f};
    float ikWeight{1.0f};
    std::map<std::string, float> morphWeights;

    // Camera component properties
    float cameraFov{60.0f};
    float cameraNear{0.1f};
    float cameraFar{1000.0f};

    // Light component properties
    LightType lightType{LightType::Point};
    glm::vec3 lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity{1.0f};
    float lightRange{10.0f};
    float spotInnerConeAngle{25.0f};
    float spotOuterConeAngle{35.0f};
    bool castShadows{true};
    float shadowBias{0.002f};
    glm::vec2 areaLightSize{2.0f, 1.0f}; // width, height (m) for LightType::AreaRect (LTC area light)
    bool areaLightTwoSided{false};       // whether rectangular area light emits from both faces

    // Collider & Rigid Body component properties
    ColliderType colliderType{ColliderType::Box};
    glm::vec3 colliderSize{1.0f, 1.0f, 1.0f};
    float colliderRadius{0.5f};
    float colliderHeight{1.0f};
    glm::vec3 colliderCenter{0.0f, 0.0f, 0.0f};
    bool isTrigger{false};
    float bounciness{0.0f};
    float friction{0.5f};
    RigidBodyType rigidBodyType{RigidBodyType::None};
    float mass{1.0f};
    bool useGravity{true};
    bool floatsOnWater{false};  // dynamic bodies: bob on an ocean's waves (OceanWaves)
    float buoyancy{1.4f};       // 1 = neutral, below 1 sinks, above 1 floats higher
    float linearDamping{0.05f};
    float angularDamping{0.05f};
    glm::vec3 linearVelocity{0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity{0.0f, 0.0f, 0.0f};
    int constraintType{0}; // 0=None, 1=Fixed, 2=Hinge, 3=Distance, 4=SwingTwist
    uint32_t constraintTargetNodeId{0};
    bool enableRagdoll{false};


    // Nested Scene Instancing (Scenes as Prefabs)
    std::string instanceScenePath{""};
    bool isSceneInstanceRoot{false};
    bool IsSceneInstance() const { return isSceneInstanceRoot || !instanceScenePath.empty(); }

    // Ref-Counted Resource Handles
    std::shared_ptr<Resource::MeshResource> meshResource;
    std::shared_ptr<Resource::MaterialResource> materialResource;

    // Precision Collision Layer & Mask Bitfields
    uint32_t collisionLayer{1}; // Bitmask of layers this body belongs to (Default: Layer 1 / bit 0)
    uint32_t collisionMask{1};  // Bitmask of layers this body collides with (Default: Layer 1 / bit 0)
    bool CanCollideWith(const Node& other) const {
        return ((collisionLayer & other.collisionMask) != 0) || ((other.collisionLayer & collisionMask) != 0);
    }

    // Declarative Multiplayer Property Sync
    ReplicationConfig replicationConfig;

    // Signals (Decoupled Event Messaging)
    std::map<std::string, std::vector<SignalConnection>> m_signals;
    uint64_t m_nextSignalId{1};

    uint64_t ConnectSignal(const std::string& signalName, std::function<void(const std::vector<std::string>&)> callback) {
        SignalConnection conn;
        conn.id = m_nextSignalId++;
        conn.signalName = signalName;
        conn.callback = callback;
        m_signals[signalName].push_back(conn);
        return conn.id;
    }

    bool DisconnectSignal(const std::string& signalName, uint64_t connectionId) {
        auto it = m_signals.find(signalName);
        if (it != m_signals.end()) {
            auto& conns = it->second;
            auto cIt = std::remove_if(conns.begin(), conns.end(), [connectionId](const SignalConnection& c) {
                return c.id == connectionId;
            });
            if (cIt != conns.end()) {
                conns.erase(cIt, conns.end());
                return true;
            }
        }
        return false;
    }

    void DisconnectAllSignals(const std::string& signalName = "") {
        if (signalName.empty()) {
            m_signals.clear();
        } else {
            m_signals.erase(signalName);
        }
    }

    void EmitSignal(const std::string& signalName, const std::vector<std::string>& args = {}) {
        auto it = m_signals.find(signalName);
        if (it != m_signals.end()) {
            auto conns = it->second;
            for (const auto& conn : conns) {
                if (conn.callback) {
                    conn.callback(args);
                }
            }
        }
    }

    bool HasSignal(const std::string& signalName) const {
        auto it = m_signals.find(signalName);
        return it != m_signals.end() && !it->second.empty();
    }

    size_t GetSignalConnectionCount(const std::string& signalName) const {
        auto it = m_signals.find(signalName);
        if (it != m_signals.end()) return it->second.size();
        return 0;
    }

    // World streaming (runtime only, never saved): the sector ("x,z") whose file this node was
    // streamed in from; empty for the scene's own nodes
    std::string streamSector{""};

    // Networking properties
    uint32_t networkId{0};
    uint32_t multiplayerAuthority{1}; // 1 = Server/Host (Default), 2+ = Specific Client, 0 = Shared/Any
    bool replicateTransform{false};
    float lastLocalActiveTime{0.0f};

    uint32_t GetMultiplayerAuthority() const { return multiplayerAuthority; }
    void SetMultiplayerAuthority(uint32_t peerId) { multiplayerAuthority = peerId; }
    bool HasMultiplayerAuthority(uint32_t localPeerId) const {
        return (multiplayerAuthority == 0) || (multiplayerAuthority == localPeerId);
    }

    void MarkLocallyActive() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        lastLocalActiveTime = std::chrono::duration<float>(now).count();
    }

    bool IsLocallyActive(float timeoutSec = 0.5f) const {
        if (lastLocalActiveTime <= 0.0f) return false;
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        float current = std::chrono::duration<float>(now).count();
        bool activeByTime = (current - lastLocalActiveTime) < timeoutSec;
        bool activeByMotion = ((current - lastLocalActiveTime) < 2.5f && glm::length(linearVelocity) > 0.15f);
        return activeByTime || activeByMotion;
    }

    // Attached Lua Scripts
    std::vector<std::string> scripts;
    std::map<std::string, std::string> scriptVariables;

    // 2D UI Components & Universal Attributes
    UIAnchor uiAnchor{UIAnchor::Center};
    glm::vec2 uiPosition{0.0f, 0.0f};
    glm::vec2 uiSize{100.0f, 40.0f};
    glm::vec4 uiColor{0.2f, 0.2f, 0.25f, 0.9f};
    glm::vec4 uiTextColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 uiBorderColor{0.4f, 0.4f, 0.5f, 1.0f};
    float uiBorderThickness{1.0f};
    float uiRounding{4.0f};
    glm::vec4 uiHoverColor{0.3f, 0.3f, 0.38f, 1.0f};
    glm::vec4 uiPressColor{0.15f, 0.15f, 0.2f, 1.0f};

    std::string uiText{"Button"};
    std::string uiLocKey{""};
    std::string uiPlaceholder{"Enter text..."};
    std::string uiFontPath{""};
    float uiFontSize{16.0f};
    float uiValue{50.0f};
    float uiMinVal{0.0f};
    float uiMaxVal{100.0f};
    bool uiChecked{false};
    float uiProgress{0.5f};
    int uiSelectedIndex{0};
    std::vector<std::string> uiItems;
    std::string uiRadioGroup{"Group1"};
    bool uiWindowIsOpen{true};
    bool uiWindowMovable{true};
    bool uiWindowClosable{true};
    UIShapeType uiShapeType{UIShapeType::Rectangle};
    bool uiVisible{true};
    bool uiEnabled{true};
    std::string uiOnClickAction{""};

    virtual bool IsUINode() const;
    std::string GetResolvedUIText() const;

    // Transform calculation
    glm::mat4 GetLocalTransformMatrix() const;
    glm::mat4 GetTransformMatrix() const { return GetLocalTransformMatrix(); }
    void SetTransformMatrix(const glm::mat4& matrix);

    glm::mat4 GetWorldTransformMatrix(const Scene& scene) const;
    void SetWorldTransformMatrix(const glm::mat4& matrix, const Scene& scene);
    glm::vec3 GetWorldPosition(const Scene& scene) const;

    glm::mat4 GetCameraViewMatrix(const Scene* scene = nullptr) const;
    glm::mat4 GetCameraProjectionMatrix(float aspectRatio) const;

    virtual std::shared_ptr<Node> Clone() const;
    virtual void RenderInspectorProperties(Scene& scene, NodeInspectorState& state);
    virtual void DrawUI(Scene& scene, ImDrawList* drawList, ImVec2 canvasPos, float canvasW, float canvasH,
                        bool isEditorMode, Gameplay::GameLoop* gameLoop, UINodeRendererState& state,
                        ImVec2 rectMin, ImVec2 rectMax, bool isHovered, bool isClicked);
};
