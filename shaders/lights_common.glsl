// Scene lights and the clustered light lists (VulkanPipeline::BuildLightClusters). The view is
// split into 16 x 9 screen tiles x 24 depth slices; each cluster lists the point and spot lights
// that can reach it, so a pixel only evaluates nearby lights. Directional lights reach every
// pixel and are listed once. Requires common_ubo.glsl. Must match VulkanPipeline.h.
#ifndef LIGHTS_COMMON_GLSL
#define LIGHTS_COMMON_GLSL

const uint kClusterTilesX = 16u;
const uint kClusterTilesY = 9u;
const uint kClusterSlices = 24u;
const uint kClusterCount = kClusterTilesX * kClusterTilesY * kClusterSlices;
const uint kMaxDirectionalLights = 32u;

layout(std430, binding = 15) readonly buffer LightClusterBuffer {
    uvec4 lightCounts;     // x = lights, y = directional lights
    vec4 clusterParams;    // x = slice scale, y = slice bias (slice = log(depth) * x + y)
    uint directionalLights[kMaxDirectionalLights];
    uvec2 clusters[kClusterCount]; // x = first entry in lightIndices, y = count
    uint lightIndices[];
} lightClusters;

layout(std430, binding = 16) readonly buffer LightBuffer {
    LightData lights[];
} lightList;

// Cluster containing a world-space point, in this view
uint ClusterIndex(vec3 worldPos) {
    vec4 viewPos = ubo.view * vec4(worldPos, 1.0);
    vec4 clip = ubo.proj * viewPos;
    vec2 ndc = clip.xy / max(clip.w, 1e-4);
    uvec2 tile = uvec2(clamp((ndc * 0.5 + 0.5) * vec2(kClusterTilesX, kClusterTilesY), vec2(0.0),
                             vec2(kClusterTilesX - 1u, kClusterTilesY - 1u)));
    float depth = max(-viewPos.z, 1e-4);
    uint slice = uint(clamp(log(depth) * lightClusters.clusterParams.x + lightClusters.clusterParams.y,
                            0.0, float(kClusterSlices - 1u)));
    return (slice * kClusterTilesY + tile.y) * kClusterTilesX + tile.x;
}

// Lights affecting a point: directional lights first, then the cluster's list. Usage:
//   uvec2 cluster = lightClusters.clusters[ClusterIndex(p)];
//   int total = LightCountAt(cluster);
//   for (int k = 0; k < total; ++k) { int i = LightIndexAt(cluster, k); ... lightList.lights[i] ... }
int LightCountAt(uvec2 cluster) {
    return int(lightClusters.lightCounts.y + cluster.y);
}

int LightIndexAt(uvec2 cluster, int k) {
    uint directionalCount = lightClusters.lightCounts.y;
    return (uint(k) < directionalCount) ? int(lightClusters.directionalLights[k])
                                        : int(lightClusters.lightIndices[cluster.x + uint(k) - directionalCount]);
}

#endif
