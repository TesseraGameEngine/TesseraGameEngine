#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"

#include "common_material.glsl"
#include "instancing_common.glsl"
#include "wind_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4 inBoneWeights;
layout(location = 6) in vec4 inColor; // vertex colour (white unless the model has colours)
layout(location = 7) in vec2 inLightmapUV; // lightmap atlas position (lightmapped meshes only)

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragPosition;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragColor;
layout(location = 4) out flat int fragUseTexture;
layout(location = 5) out vec2 fragLightmapUV;

void main() {
    mat4 model = ((pc.materialFlags & MAT_INSTANCED) != 0) ? instances[gl_InstanceIndex].model : pc.model;
    vec4 worldPos = model * vec4(inPosition, 1.0);
    if ((pc.materialFlags & MAT_WIND) != 0) worldPos.xyz = WindSway(worldPos.xyz, model[3].xyz, inPosition.y);
    gl_Position = ubo.proj * ubo.view * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragPosition = worldPos.xyz;
    fragTexCoord = inTexCoord;
    fragColor = ((pc.materialFlags & MAT_VERTEX_COLOR) != 0) ? pc.color * inColor : pc.color;
    fragUseTexture = pc.textureFlags & 1;
    fragLightmapUV = inLightmapUV;
}
