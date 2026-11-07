#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// 3x3 tent upsample of the next-smaller bloom level, added onto the current level
// by additive blending in the pipeline.
layout(binding = 0) uniform sampler2D srcTexture;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 t = pc.params0.xy * pc.params0.w;

    vec3 result  = texture(srcTexture, inUV + vec2(-t.x,  t.y)).rgb;
    result += texture(srcTexture, inUV + vec2( 0.0,  t.y)).rgb * 2.0;
    result += texture(srcTexture, inUV + vec2( t.x,  t.y)).rgb;
    result += texture(srcTexture, inUV + vec2(-t.x,  0.0)).rgb * 2.0;
    result += texture(srcTexture, inUV).rgb * 4.0;
    result += texture(srcTexture, inUV + vec2( t.x,  0.0)).rgb * 2.0;
    result += texture(srcTexture, inUV + vec2(-t.x, -t.y)).rgb;
    result += texture(srcTexture, inUV + vec2( 0.0, -t.y)).rgb * 2.0;
    result += texture(srcTexture, inUV + vec2( t.x, -t.y)).rgb;

    outColor = vec4(result / 16.0, 1.0);
}
