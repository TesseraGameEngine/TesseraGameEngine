// Particles drawn as camera-facing quads (VulkanPipeline::DrawParticles): sprites, and segments
// of trails and ribbons. One instance per quad (ParticleQuad in ParticleNode.h, also written by
// particle_sim.comp). The push block is the same 128 bytes as PushConstantData.
#ifndef PARTICLE_COMMON_GLSL
#define PARTICLE_COMMON_GLSL

layout(push_constant) uniform ParticleParams {
    vec4 sheet;  // x = flipbook columns, y = rows, z = frames, w = texture tiling along trails
    vec4 look;   // x = softness (m), y = glow intensity, z = camera fade (m), w = distortion strength
    ivec4 mode;  // x = blend (0 additive, 1 alpha, 2 distortion), y = flags (PARTICLE_*)
    vec4 extra;  // x = self glow (alpha blending: the sprite's bright parts glow)
    vec4 reserved[4];
} particle;

const int PARTICLE_TEXTURED = 1;     // albedoMap holds the sprite (sheet)
const int PARTICLE_BLEND_FRAMES = 2; // cross-fade between flipbook frames
const int PARTICLE_SOFT = 4;         // fade against the scene depth (this view has it)
const int PARTICLE_LIT = 8;          // lit by the sun, sky and lights (alpha blending)

#endif
