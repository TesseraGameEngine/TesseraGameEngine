#version 450
#extension GL_GOOGLE_include_directive : require

// Alpha-cutout shadow casters (leaves, fences): holes in the texture cast no shadow.
// Opaque casters use a depth-only pipeline without this shader.
#include "shadow_common.glsl"

layout(binding = 1) uniform sampler2D albedoMap; // the caster's material descriptor set

layout(location = 0) in vec2 inTexCoord;

void main() {
    float alpha = pc.color.a;
    if ((pc.textureFlags & 1) != 0) {
        alpha *= texture(albedoMap, inTexCoord).a;
    }
    if (alpha < pc.alphaCutoff) {
        discard;
    }
}
