// Shared scene uniform block. Must match UniformBufferObject in src/Renderer/VulkanBuffer.h
#ifndef COMMON_UBO_GLSL
#define COMMON_UBO_GLSL

struct LightData {
    vec4 position;            // xyz = position (or -dir), w: 0.0 = Directional, 1.0 = Point, 2.0 = Spot
    vec4 color;               // rgb = color, a = intensity
    vec4 spotDirAndRange;     // xyz = spot forward dir, w = range
    vec4 spotAnglesAndShadow; // x = cosInner, y = cosOuter, z = castShadows, w = shadowBias
};

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    int numLights;
    int enableACES;
    int enableShadows;
    int enableGI;
    vec4 giSkyColor;          // rgb = sky GI irradiance, a = GI intensity
    vec4 fogColorAndDensity;  // rgb = fog color, a = exponential height fog density
    vec4 postProcessParams;   // x = bloomStrength, y = ssaoStrength, z = ssrStrength, w = volumetricScattering
    vec4 timeParams;          // x = elapsed seconds, y = HDR exposure, z = 1 for a mirrored (reflection) view, w = frame counter (temporal noise)
    mat4 shadowMatrices[4];   // world -> shadow map clip space ([0,1] depth), one per cascade
    vec4 shadowCascadeTexel;  // world size of one shadow texel in each cascade (normal offset)
    vec4 shadowParams;        // x = enabled, y = shadowed light index, z = 1/resolution, w = cascade count
    vec4 skyParams;           // xyz = direction toward the sun, w = sky intensity (0 = procedural sky off)
    // Point / spot light shadows (layers of the local shadow array, binding 11)
    mat4 localShadowMatrices[12]; // world -> light clip space ([0,1] depth), one per layer
    vec4 localShadowLights[4];    // x = light index (-1 = unused), y = first layer, z = 1 point (6 layers) / 0 spot, w = world texel size per metre of distance
    vec4 localShadowParams;       // x = entries used, y = 1/resolution
    mat4 prevViewProj;            // previous frame's unjittered view-projection (motion vectors)
    mat4 unjitteredViewProj;      // this frame's view-projection without the TAA jitter
    vec4 envParams;               // x = 1 when an HDRI image is the environment, y = HDRI intensity, z = HDRI rotation (radians)
    vec4 probePosRadius[4];       // reflection probes: xyz = position, w = radius of influence
    vec4 probeParams;             // x = probes ready
    vec4 probeBoxExtents[4];      // box-projected probes: xyz = half size (box centred on the probe), w = 1 when enabled
    vec4 volumetricParams;        // volumetric fog: x = density (0 = off), y = height falloff, z = anisotropy, w = base height
    vec4 giVolumeMin;             // baked GI volume: xyz = first probe's position, w = 1 when active
    vec4 giVolumeSize;            // xyz = distance from the first probe to the last, w = intensity
    vec4 giVolumeDims;            // xyz = probe counts
    vec4 lightmapParams;          // baked lightmaps: x = 1 when shown, y = intensity
    vec4 waterParams;             // ocean: x = camera under water (0 no, 1 yes, 2 half: see waterline), y = water level, z = clarity (m), w = 1 when the waterline's +y side is under water
    vec4 waterColor;              // ocean: rgb = deep colour, a = 1 when the scene has an ocean
    vec4 waterBounds;             // ocean area: xy = minimum x / z, zw = maximum x / z
    vec4 moonParams;              // xyz = direction toward the moon, w = brightness (0 = no moon)
    vec4 cloudParams;             // x = coverage, y = opacity (0 = no clouds), z = height (m), w = shape size (m)
    vec4 cloudWind;               // xy = wind velocity (m/s, x / z), z = ground shadow strength
    vec4 skyClockParams;          // x = star rotation (radians), y = latitude (radians), z = north angle (radians), w = 1 with the clock on
    vec4 weatherParams;           // x = rain, y = snow, z = wetness, w = snow cover (all 0..1)
    vec4 lightningParams;         // x = flash (0..1), y = bolt azimuth (radians), z = bolt seed, w = 1 while the bolt shows
    mat4 shelterMatrix;           // world -> shelter map clip space (top-down, [0,1] depth)
    vec4 shelterParams;           // shelter map: x = 1 when rendered, y = top height (m), z = depth range (m), w = texel size (m)
    vec4 weatherParams2;          // x = snow depth (m)
    vec4 atmosphereParams;        // physical atmosphere: x = 1 when on, y = horizon elevation (radians), z = camera height (km)
    vec4 sunTransmittance;        // rgb = sunlight through the atmosphere to the camera, w = the same for the noon sun (brightness)
    vec4 volumetricCloudParams;   // x = 1 when this view draws volumetric clouds, y = thickness (m), z = detail erosion
    vec4 waterline[4];            // camera half in the water: the surface's height on screen (NDC y) at 16 points across (NDC x -1..1)
    vec4 vxgiParams;              // VXGI: x = 1 when active, y = intensity, z = volume world extent (m), w = max mip level
    vec4 vxgiCenter;              // VXGI: xyz = volume min corner in world space, w = base voxel size (m)
    vec4 rtParams;                // ray tracing: x = shadows, y = reflections, z = ambient occlusion (1 = on), w = reflection roughness cutoff
    vec4 rtParams2;               // x = sun disc angular radius (radians), y = AO radius (m), z = max reflection distance (m), w = max shadowed local light distance (m)
    // Lights live in a storage buffer with per-cluster lists (lights_common.glsl); numLights counts them
} ubo;

#endif
