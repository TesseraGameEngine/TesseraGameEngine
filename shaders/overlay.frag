#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_material.glsl"

// Editor overlay lines: flat colour in display space (no lighting, shadows or tone mapping)
layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
