#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include "Scene/Mesh.h"

// Most lights a view can use (directional, point and spot together)
constexpr int kMaxSceneLights = 1024;

struct LightData {
    glm::vec4 position{0.0f, 2.0f, 2.0f, 1.0f};            // xyz = position (or -dir), w: 0.0 = Directional, 1.0 = Point, 2.0 = Spot
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};               // rgb = color, a = intensity
    glm::vec4 spotDirAndRange{0.0f, -1.0f, 0.0f, 25.0f};   // xyz = spot forward dir, w = range
    glm::vec4 spotAnglesAndShadow{0.906f, 0.707f, 1.0f, 0.005f}; // x = cosInner, y = cosOuter, z = castShadows, w = shadowBias
};

struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPos;
    int numLights{0};
    int enableACES{1};
    int enableShadows{1};
    int enableGI{1};
    glm::vec4 giSkyColor{0.16f, 0.22f, 0.32f, 0.35f};         // rgb = sky GI irradiance, a = GI intensity
    glm::vec4 fogColorAndDensity{0.55f, 0.65f, 0.80f, 0.0f};  // rgb = fog color, a = exponential height fog density
    glm::vec4 postProcessParams{0.25f, 0.35f, 0.25f, 0.0f};   // x = bloomStrength, y = ssaoStrength, z = ssrStrength, w = volumetricScattering
    glm::vec4 timeParams{0.0f, 1.0f, 0.0f, 0.0f};             // x = elapsed seconds, y = HDR exposure, z = 1 for a mirrored (reflection) view, w = frame counter (temporal noise)
    glm::mat4 shadowMatrices[4]{glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)}; // world -> shadow clip space ([0,1] depth), per cascade
    glm::vec4 shadowCascadeTexel{0.0f};                        // world size of one shadow texel in each cascade (normal offset)
    glm::vec4 shadowParams{0.0f, -1.0f, 0.0f, 0.0f};           // x = enabled, y = shadowed light index, z = 1/resolution, w = cascade count
    glm::vec4 skyParams{0.3f, 0.8f, 0.4f, 0.0f};              // xyz = direction toward the sun, w = sky intensity (0 = procedural sky off)
    // Point / spot light shadows (layers of the local shadow array, binding 11)
    glm::mat4 localShadowMatrices[12]{};                      // world -> light clip space ([0,1] depth), one per layer
    glm::vec4 localShadowLights[4]{glm::vec4(-1.0f), glm::vec4(-1.0f), glm::vec4(-1.0f), glm::vec4(-1.0f)}; // x = light index, y = first layer, z = 1 point / 0 spot, w = texel size per metre
    glm::vec4 localShadowParams{0.0f};                        // x = entries used, y = 1/resolution
    glm::mat4 prevViewProj{1.0f};                             // previous frame's unjittered view-projection (motion vectors)
    glm::mat4 unjitteredViewProj{1.0f};                       // this frame's view-projection without the TAA jitter
    glm::vec4 envParams{0.0f, 1.0f, 0.0f, 0.0f};              // x = 1 when an HDRI image is the environment, y = HDRI intensity, z = HDRI rotation (radians)
    glm::vec4 probePosRadius[4]{};                            // reflection probes: xyz = position, w = radius of influence
    glm::vec4 probeParams{0.0f};                              // x = probes ready
    glm::vec4 probeBoxExtents[4]{};                           // box-projected probes: xyz = half size (box centred on the probe), w = 1 when enabled
    glm::vec4 volumetricParams{0.0f, 0.1f, 0.6f, 0.0f};       // volumetric fog: x = density (0 = off), y = height falloff, z = anisotropy, w = base height
    glm::vec4 giVolumeMin{0.0f};                              // baked GI volume: xyz = first probe's position, w = 1 when active
    glm::vec4 giVolumeSize{1.0f};                             // xyz = distance from the first probe to the last, w = intensity
    glm::vec4 giVolumeDims{1.0f};                             // xyz = probe counts
    glm::vec4 lightmapParams{0.0f};                           // baked lightmaps: x = 1 when shown, y = intensity
    glm::vec4 waterParams{0.0f};                              // ocean: x = camera under water (0 no, 1 yes, 2 half: see waterline), y = water level, z = clarity (m), w = 1 when the waterline's +y side is under water
    glm::vec4 waterColor{0.0f};                               // ocean: rgb = deep colour, a = 1 when the scene has an ocean
    glm::vec4 waterBounds{0.0f};                              // ocean area: xy = minimum x / z, zw = maximum x / z
    glm::vec4 moonParams{0.0f};                               // xyz = direction toward the moon, w = brightness (0 = no moon)
    glm::vec4 cloudParams{0.0f};                              // x = coverage, y = opacity (0 = no clouds), z = height (m), w = shape size (m)
    glm::vec4 cloudWind{0.0f};                                // xy = wind velocity (m/s, x / z), z = ground shadow strength
    glm::vec4 skyClockParams{0.0f};                           // x = star rotation (radians), y = latitude (radians), z = north angle (radians), w = 1 with the clock on
    glm::vec4 weatherParams{0.0f};                            // x = rain, y = snow, z = wetness, w = snow cover (all 0..1)
    glm::vec4 lightningParams{0.0f};                          // x = flash (0..1), y = bolt azimuth (radians), z = bolt seed, w = 1 while the bolt shows
    glm::mat4 shelterMatrix{1.0f};                            // world -> shelter map clip space (top-down, [0,1] depth)
    glm::vec4 shelterParams{0.0f};                            // shelter map: x = 1 when rendered, y = top height (m), z = depth range (m), w = texel size (m)
    glm::vec4 weatherParams2{0.0f};                           // x = snow depth (m)
    glm::vec4 atmosphereParams{0.0f};                         // physical atmosphere: x = 1 when on, y = horizon elevation (radians), z = camera height (km)
    glm::vec4 sunTransmittance{1.0f};                         // rgb = sunlight through the atmosphere to the camera, w = the same for the noon sun (brightness)
    glm::vec4 volumetricCloudParams{0.0f};                    // x = 1 when this view draws volumetric clouds, y = thickness (m), z = detail erosion
    glm::vec4 waterline[4]{};                                 // camera half in the water: the surface's height on screen (NDC y) at 16 points across (NDC x -1..1)
    glm::vec4 vxgiParams{0.0f};                               // VXGI: x = 1 when active, y = intensity, z = volume world extent (m), w = max mip level
    glm::vec4 vxgiCenter{0.0f};                               // VXGI: xyz = volume min corner in world space, w = base voxel size (m)
    // Hardware ray tracing (shaders/rt_common.glsl, only read by the ray tracing shader variant)
    glm::vec4 rtParams{0.0f};                                 // x = RT shadows, y = RT reflections, z = RT ambient occlusion (1 = on), w = reflection roughness cutoff
    glm::vec4 rtParams2{0.0f};                                // x = sun disc angular radius (radians, soft shadows), y = AO radius (m), z = max reflection distance (m), w = max shadowed local light distance (m)
    // Lights are uploaded separately (VulkanPipeline::SetViewLights) with no fixed limit
};

// Bits for PushConstantData::textureFlags (must match shader.frag)
enum TextureFlagBits : int {
    TEX_ALBEDO    = 1 << 0,
    TEX_NORMAL    = 1 << 1,
    TEX_METALLIC  = 1 << 2,
    TEX_ROUGHNESS = 1 << 3,
    TEX_AO        = 1 << 4,
    TEX_EMISSIVE  = 1 << 5,
    TEX_HEIGHT    = 1 << 6,
    // The metallic and roughness slots hold one glTF-style packed texture: roughness in G,
    // metallic in B (R is often ambient occlusion). Set when both slots name the same file.
    TEX_PACKED_MR = 1 << 7
};

// Bits for PushConstantData::materialFlags (must match shader.frag)
enum MaterialFlagBits : int {
    MAT_DOUBLE_SIDED = 1 << 0,
    MAT_TRANSPARENT  = 1 << 1, // drawn in the sorted transparent pass with premultiplied alpha
    MAT_MIRROR       = 1 << 2, // shows the planar reflection rendered for this view
    MAT_INSTANCED    = 1 << 3, // model matrix comes from the instance buffer (instanced draw)
    MAT_VERTEX_COLOR = 1 << 4, // multiply the base colour by the vertex colours
    MAT_LIGHTMAP     = 1 << 5, // indirect light comes from the baked lightmap atlas (lightmap UVs)
    MAT_WIND         = 1 << 6  // sways with the wind, more the higher above its origin (foliage)
    // bits 8 and up: advanced-material entry + 1 (VulkanPipeline::MaterialExtFlags)
};

// Kept at exactly 128 bytes: the minimum push constant size Vulkan guarantees
struct PushConstantData {
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 emissiveColorAndIntensity{0.0f, 0.0f, 0.0f, 0.0f};
    float roughness{0.5f};
    float metallic{0.0f};
    float aoStrength{1.0f};
    float heightScale{0.0f};
    int textureFlags{0};
    float alphaCutoff{0.0f}; // 0 = off; otherwise fragments with alpha below this are discarded
    int materialFlags{MAT_DOUBLE_SIDED};
    float ior{1.0f};         // index of refraction for transparent materials (1 = no refraction)
};
static_assert(sizeof(PushConstantData) == 128, "PushConstantData must stay 128 bytes");

struct GPUMesh {
    VkBuffer vertexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory vertexBufferMemory{VK_NULL_HANDLE};
    VkBuffer indexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory indexBufferMemory{VK_NULL_HANDLE};
    uint32_t indexCount{0};
    std::vector<SubmeshSection> submeshes;

    // Staging buffer for fast per-frame dynamic updates (e.g. skeletal skinning)
    VkBuffer dynamicStagingBuffer{VK_NULL_HANDLE};
    VkDeviceMemory dynamicStagingMemory{VK_NULL_HANDLE};
    void* mappedStagingData{nullptr};
    VkDeviceSize dynamicStagingSize{0};

    void Cleanup(VkDevice device) {
        if (dynamicStagingBuffer != VK_NULL_HANDLE) {
            if (mappedStagingData) {
                vkUnmapMemory(device, dynamicStagingMemory);
                mappedStagingData = nullptr;
            }
            vkDestroyBuffer(device, dynamicStagingBuffer, nullptr);
            dynamicStagingBuffer = VK_NULL_HANDLE;
        }
        if (dynamicStagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, dynamicStagingMemory, nullptr);
            dynamicStagingMemory = VK_NULL_HANDLE;
        }
        dynamicStagingSize = 0;

        if (indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, indexBuffer, nullptr);
            indexBuffer = VK_NULL_HANDLE;
        }
        if (indexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, indexBufferMemory, nullptr);
            indexBufferMemory = VK_NULL_HANDLE;
        }
        if (vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertexBuffer, nullptr);
            vertexBuffer = VK_NULL_HANDLE;
        }
        if (vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertexBufferMemory, nullptr);
            vertexBufferMemory = VK_NULL_HANDLE;
        }
        indexCount = 0;
    }
};

class VulkanBufferUtils {
public:
    static void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                             VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    static void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    static GPUMesh UploadMesh(const MeshData& meshData);
    static void UpdateVertexBuffer(GPUMesh& gpuMesh, const std::vector<Vertex>& vertices);
};
