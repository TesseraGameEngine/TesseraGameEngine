#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "fluid_common.glsl"

// Foam and spray of the simulated fluid (VulkanPipeline::DrawFluidFoam): every particle with foam
// (velocity.w) is a soft white blob, and spray (radius < 0) a small droplet, facing the camera
layout(location = 0) in vec4 inParticle; // xyz = position, w = radius (< 0 spray, 0 unused slot)
layout(location = 1) in vec4 inVelocity; // xyz = velocity, w = foam

layout(location = 0) out vec2 outCorner;
layout(location = 1) out float outFoam;      // 0..1; spray is 1
layout(location = 2) out float outDistance;  // view distance of the particle
layout(location = 3) out float outSpray;     // 1 for spray

void main() {
    const vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                   vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 corner = corners[gl_VertexIndex];
    bool spray = inParticle.w < 0.0;
    float foam = spray ? 1.0 : inVelocity.w;
    if (inParticle.w == 0.0 || foam < 0.05) {
        gl_Position = vec4(0.0, 0.0, -1.0, 1.0); // nothing to draw
        outCorner = vec2(0.0);
        outFoam = 0.0;
        outDistance = 0.0;
        outSpray = 0.0;
        return;
    }
    float radius = abs(inParticle.w);
    // Foam blobs spread a little wider than their particle; spray droplets are smaller
    float size = spray ? radius * 0.5 : radius * (1.0 + 0.4 * foam);
    vec3 centerView = (ubo.view * vec4(inParticle.xyz, 1.0)).xyz;
    outCorner = corner;
    outFoam = foam;
    outDistance = -centerView.z;
    outSpray = spray ? 1.0 : 0.0;
    gl_Position = ubo.proj * vec4(centerView + vec3(corner * size, 0.0), 1.0);
}
