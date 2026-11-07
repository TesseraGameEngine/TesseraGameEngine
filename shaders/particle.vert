#version 450
#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "particle_common.glsl"

// One quad per instance: a sprite facing the camera (turned by its rotation, or stretched along
// its motion), or a trail / ribbon segment from a to b, turned to face the camera about its length
layout(location = 0) in vec4 inA;      // xyz = position (segment: start), w = half size (segment: half width at the start)
layout(location = 1) in vec4 inB;      // sprite: xyz = velocity x stretch, w = rotation; segment: xyz = end, w = half width at the end
layout(location = 2) in vec4 inColorA;
layout(location = 3) in vec4 inColorB; // segment: colour at the end
layout(location = 4) in vec4 inParams; // sprite: x = flipbook frame, y = seed, z = -1; segment: z = v at the start, w = v at the end

layout(location = 0) out vec2 outUV;       // across the quad, 0..1
layout(location = 1) out vec4 outColor;
layout(location = 2) flat out vec2 outFrame; // flipbook frame, seed
layout(location = 3) out vec3 outWorldPos;
layout(location = 4) flat out float outSegment; // 1 for trail / ribbon segments
layout(location = 5) out float outAlong;        // segment: texture v along it

void main() {
    const vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                   vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 corner = corners[gl_VertexIndex];
    // The camera's axes in the world (rows of the view rotation)
    vec3 right = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 up = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
    vec3 forward = -vec3(ubo.view[0][2], ubo.view[1][2], ubo.view[2][2]);
    vec3 world;
    bool segment = inParams.z > -0.5;
    if (!segment) {
        float halfSize = inA.w;
        vec3 stretch = inB.xyz;
        stretch -= forward * dot(stretch, forward);
        float stretchLength = length(stretch);
        if (stretchLength > 1e-4) {
            // Long along the motion (screen-space), trailing behind the particle
            vec3 axisY = stretch / stretchLength;
            vec3 axisX = normalize(cross(axisY, forward));
            world = inA.xyz - stretch * 0.5 + axisX * corner.x * halfSize + axisY * corner.y * (halfSize + stretchLength * 0.5);
        } else {
            float c = cos(inB.w), s = sin(inB.w);
            vec2 turned = vec2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
            world = inA.xyz + (right * turned.x + up * turned.y) * halfSize;
        }
        outColor = inColorA;
        outAlong = 0.0;
    } else {
        // Segment: across it, facing the camera
        float t = corner.y * 0.5 + 0.5;
        vec3 p = mix(inA.xyz, inB.xyz, t);
        vec3 dir = inB.xyz - inA.xyz;
        vec3 toCamera = ubo.cameraPos.xyz - p;
        vec3 side = cross(dir, toCamera);
        float sideLength = length(side);
        side = sideLength > 1e-8 ? side / sideLength : right;
        world = p + side * corner.x * mix(inA.w, inB.w, t);
        outColor = mix(inColorA, inColorB, t);
        outAlong = mix(inParams.z, inParams.w, t);
    }
    outUV = vec2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    outFrame = inParams.xy;
    outWorldPos = world;
    outSegment = segment ? 1.0 : 0.0;
    gl_Position = ubo.proj * ubo.view * vec4(world, 1.0);
}
