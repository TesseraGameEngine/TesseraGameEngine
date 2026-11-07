#pragma once

#include "Scene/Node.h"
#include "Scene/Mesh.h"
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

class Scene;
struct NodeInspectorState;

enum class TerrainBrushAction {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3
};

// What the terrain brush does in the viewport
enum class TerrainTool {
    Sculpt = 0,       // brushAction: raise, lower, smooth, flatten
    Paint = 1,        // paints paintLayer into the splat map
    Foliage = 2,      // scatters foliageTypes[activeFoliage]
    EraseFoliage = 3  // removes foliage (of the active type, or all with eraseAllFoliage)
};

// A paint layer: a texture (or a plain tinted colour with natural variation) tiled across the
// terrain wherever the splat map gives it weight
struct TerrainLayer {
    std::string name;
    std::string texturePath;
    glm::vec3 tint{1.0f};
    float tiling{4.0f};     // metres per texture repeat
    float roughness{0.85f};
};

// Something scattered over the terrain: a built-in plant or rock, or any model
struct FoliageType {
    std::string name;
    int builtIn{0};             // FoliageMesh: 0 grass, 1 flowers, 2 bush, 3 tree, 4 pine, 5 rock; -1 = model
    std::string modelMesh;      // custom mesh name (builtIn = -1)
    glm::vec4 color{1.0f};
    float minScale{0.8f};
    float maxScale{1.25f};
    float density{2.0f};        // instances per square metre the brush lays down
    float maxSlope{35.0f};      // degrees: steeper ground stays bare
    bool alignToSlope{true};    // lean with the ground (grass) or stand upright (trees)
    bool castShadows{true};
    bool wind{true};            // sways with the wind
    bool collision{false};      // solid in Play (trees, rocks)
    float drawDistance{150.0f}; // metres
};

struct FoliageInstance {
    glm::vec2 local{0.0f};  // terrain-local x, z (its height follows the ground)
    float rotation{0.0f};   // radians around the vertical
    float scale{1.0f};
    uint32_t type{0};
};

enum class TerrainPreset {
    FlatPlains = 0,
    RollingHills = 1,
    MountainPeaks = 2,
    CanyonsAndRavines = 3
};

class TerrainNode : public Node {
public:
    TerrainNode(const std::string& name = "Terrain", uint32_t parentId = 0);

    std::shared_ptr<Node> Clone() const override;
    void RenderInspectorProperties(Scene& scene, NodeInspectorState& state) override;

    // Grid Dimensions
    uint32_t gridResolution{64}; // 32, 64, 128, 256
    glm::vec2 terrainSize{100.0f, 100.0f}; // Width X and Length Z in meters
    float heightScale{20.0f}; // Max elevation in meters
    float uvTiling{16.0f};
    std::string heightmapPath{""};

    // Elevation data normalized to [0.0, 1.0] of size (gridResolution * gridResolution)
    std::vector<float> heightData;
    bool isMeshDirty{true};

    std::string GetTerrainMeshKey() const {
        return "Terrain_Mesh_" + std::to_string(id);
    }

    // Procedural Generation & Presets
    void GenerateProcedural(TerrainPreset preset);
    void SetResolution(uint32_t newRes);
    bool LoadFromHeightmap(const std::string& path);

    // Height & Normal Sampling (Bilinear Interpolation)
    float SampleHeight(float worldX, float worldZ, const Scene& scene) const;
    float SampleHeightLocal(float localX, float localZ) const;
    glm::vec3 SampleNormal(float worldX, float worldZ, const Scene& scene) const;

    // Paint layers (up to 4) and the splat map: RGBA8 weights of layers 0..3, splatResolution^2,
    // row by row from -z to +z
    static constexpr int kMaxLayers = 4;
    std::vector<TerrainLayer> layers;
    uint32_t splatResolution{256};
    std::vector<uint8_t> splatData;
    uint32_t splatVersion{1}; // changes whenever the splat map does (the renderer re-uploads it)
    std::string GetSplatTextureKey() const { return "@terrainsplat:" + std::to_string(id); }
    void InitSplat();
    // Paints layer into the splat map around a terrain-local point
    void PaintLayerLocal(float localX, float localZ, int layer, float dt);
    // Fills the splat map from the shape of the land: rock on steep slopes, the second layer
    // where it is high, the first elsewhere and the fourth along the lowest ground
    void AutoPaint();
    // Weights (0..1) of the four layers at a terrain-local point
    glm::vec4 SampleSplatLocal(float localX, float localZ) const;

    // Foliage
    std::vector<FoliageType> foliageTypes;
    std::vector<FoliageInstance> foliage;
    int activeFoliage{0};
    bool eraseAllFoliage{false};
    uint32_t foliageSeed{7u};
    // Scatters (or with erase, removes) foliage of the active type around a terrain-local point
    void PaintFoliageLocal(float localX, float localZ, float dt, bool erase);
    // World matrices of the foliage within each type's draw distance of cameraPos and inside the
    // view (visible: tests a world-space sphere), grouped by type
    void CollectFoliage(const Scene& scene, const glm::vec3& cameraPos, const std::function<bool(const glm::vec3&, float)>& visible,
                        std::vector<std::vector<glm::mat4>>& outPerType, bool shadowCastersOnly) const;
    // World matrices of all foliage of the types with collision (for physics)
    std::vector<std::pair<uint32_t, glm::mat4>> CollidingFoliage(const Scene& scene) const;

    // Where a world-space ray first meets the terrain's surface
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, const Scene& scene, glm::vec3& outHit) const;

    // The brush: active tool, used in the viewport while toolsEnabled
    TerrainTool tool{TerrainTool::Sculpt};
    int paintLayer{1};

    // Sculpting Tool
    bool sculptingEnabled{false};
    TerrainBrushAction brushAction{TerrainBrushAction::Raise};
    float brushRadius{10.0f}; // in world meters
    float brushStrength{1.5f};
    float flattenTargetHeight{0.25f}; // normalized [0, 1]

    // Applies the active tool at a world point (sculpt, paint or foliage)
    void ApplyBrush(float centerWorldX, float centerWorldZ, float dt, const Scene& scene);
    void ApplyBrushLocal(float localX, float localZ, float dt);
    void ApplyToolLocal(float localX, float localZ, float dt);

    // Mesh Data generation for Vulkan GPU buffer upload
    MeshData GenerateMeshData() const;

private:
    void InitGrid();
    // Foliage world matrices, rebuilt when the foliage, the ground or the node's transform change,
    // in 16 m cells for culling
    struct FoliageCell {
        glm::vec3 center{0.0f};
        float radius{0.0f};
        std::vector<uint32_t> instances;
    };
    mutable std::vector<glm::mat4> m_foliageMatrices;
    mutable std::vector<FoliageCell> m_foliageCells;
    mutable glm::mat4 m_foliageWorld{0.0f};
    mutable uint64_t m_foliageStamp{~0ull};
    uint64_t m_foliageVersion{0};
    uint64_t m_heightVersion{0};
    void RebuildFoliageCache(const Scene& scene) const;
public:
    // Call when the heights change (the renderer does, when it rebuilds the mesh)
    void NotifyHeightsChanged() { ++m_heightVersion; }
    void NotifyFoliageChanged() { ++m_foliageVersion; }
    uint64_t HeightVersion() const { return m_heightVersion; }
};

