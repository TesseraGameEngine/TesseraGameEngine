#version 450
#extension GL_GOOGLE_include_directive : require

#include "post_common.glsl"

// Fast approximate anti-aliasing on the tone-mapped image. Finds luma edges, works out
// which way each edge runs and how far its ends are, then resamples across the edge so
// stair-stepped pixels blend smoothly.
layout(binding = 0) uniform sampler2D colorTexture; // linear sampler, tone-mapped LDR

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const float kEdgeThresholdMin = 0.0312; // ignore contrast below this (dark noise)
const float kEdgeThresholdRel = 0.125;  // ...or below this fraction of the local maximum
const float kSubpixelAmount = 0.75;
const int kSearchSteps = 10;
const float kSearchStride[kSearchSteps] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0);

float Luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float LumaAt(vec2 uv) {
    return Luma(textureLod(colorTexture, uv, 0.0).rgb);
}

void main() {
    vec2 texel = pc.params1.xy;
    vec3 centerColor = textureLod(colorTexture, inUV, 0.0).rgb;
    float lumaC = Luma(centerColor);

    float lumaN = LumaAt(inUV + vec2(0.0, -texel.y));
    float lumaS = LumaAt(inUV + vec2(0.0,  texel.y));
    float lumaW = LumaAt(inUV + vec2(-texel.x, 0.0));
    float lumaE = LumaAt(inUV + vec2( texel.x, 0.0));

    float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float contrast = lumaMax - lumaMin;
    if (contrast < max(kEdgeThresholdMin, lumaMax * kEdgeThresholdRel)) {
        outColor = vec4(centerColor, 1.0);
        return;
    }

    float lumaNW = LumaAt(inUV + vec2(-texel.x, -texel.y));
    float lumaNE = LumaAt(inUV + vec2( texel.x, -texel.y));
    float lumaSW = LumaAt(inUV + vec2(-texel.x,  texel.y));
    float lumaSE = LumaAt(inUV + vec2( texel.x,  texel.y));

    // Sub-pixel blend: how much the centre differs from its neighbourhood average
    float neighbourhood = (2.0 * (lumaN + lumaS + lumaW + lumaE) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subpixel = clamp(abs(neighbourhood - lumaC) / contrast, 0.0, 1.0);
    subpixel = smoothstep(0.0, 1.0, subpixel);
    subpixel = subpixel * subpixel * kSubpixelAmount;

    // Edge orientation from second differences across rows and columns
    float horizontal = abs(lumaNW + lumaNE - 2.0 * lumaN) + 2.0 * abs(lumaW + lumaE - 2.0 * lumaC) + abs(lumaSW + lumaSE - 2.0 * lumaS);
    float vertical   = abs(lumaNW + lumaSW - 2.0 * lumaW) + 2.0 * abs(lumaN + lumaS - 2.0 * lumaC) + abs(lumaNE + lumaSE - 2.0 * lumaE);
    bool isHorizontal = horizontal >= vertical;

    // Step across the edge, toward whichever side differs more from the centre
    float lumaNeg = isHorizontal ? lumaN : lumaW;
    float lumaPos = isHorizontal ? lumaS : lumaE;
    float gradNeg = abs(lumaNeg - lumaC);
    float gradPos = abs(lumaPos - lumaC);
    float acrossStep = isHorizontal ? texel.y : texel.x;
    float edgeLuma;
    float gradient;
    if (gradNeg >= gradPos) {
        acrossStep = -acrossStep;
        edgeLuma = 0.5 * (lumaC + lumaNeg);
        gradient = gradNeg;
    } else {
        edgeLuma = 0.5 * (lumaC + lumaPos);
        gradient = gradPos;
    }

    // Walk along the edge in both directions until the luma leaves the edge's range
    vec2 edgeUV = inUV;
    vec2 alongStep;
    if (isHorizontal) {
        edgeUV.y += acrossStep * 0.5;
        alongStep = vec2(texel.x, 0.0);
    } else {
        edgeUV.x += acrossStep * 0.5;
        alongStep = vec2(0.0, texel.y);
    }
    float threshold = gradient * 0.25;

    vec2 uvNeg = edgeUV - alongStep;
    vec2 uvPos = edgeUV + alongStep;
    float deltaNeg = LumaAt(uvNeg) - edgeLuma;
    float deltaPos = LumaAt(uvPos) - edgeLuma;
    bool doneNeg = abs(deltaNeg) >= threshold;
    bool donePos = abs(deltaPos) >= threshold;
    for (int i = 1; i < kSearchSteps && !(doneNeg && donePos); ++i) {
        if (!doneNeg) {
            uvNeg -= alongStep * kSearchStride[i];
            deltaNeg = LumaAt(uvNeg) - edgeLuma;
            doneNeg = abs(deltaNeg) >= threshold;
        }
        if (!donePos) {
            uvPos += alongStep * kSearchStride[i];
            deltaPos = LumaAt(uvPos) - edgeLuma;
            donePos = abs(deltaPos) >= threshold;
        }
    }

    float distNeg = isHorizontal ? (inUV.x - uvNeg.x) : (inUV.y - uvNeg.y);
    float distPos = isHorizontal ? (uvPos.x - inUV.x) : (uvPos.y - inUV.y);
    bool nearerIsNeg = distNeg < distPos;
    float nearestDist = min(distNeg, distPos);
    float edgeLength = distNeg + distPos;

    // Only shift when the nearer end's luma moves away from the centre's side of the edge;
    // otherwise this pixel isn't on the stair-step that needs smoothing
    bool centerBelowEdge = lumaC < edgeLuma;
    bool endBelowEdge = (nearerIsNeg ? deltaNeg : deltaPos) < 0.0;
    float edgeOffset = (centerBelowEdge != endBelowEdge) ? (0.5 - nearestDist / edgeLength) : 0.0;

    float offset = max(edgeOffset, subpixel);
    vec2 finalUV = inUV;
    if (isHorizontal) {
        finalUV.y += offset * acrossStep;
    } else {
        finalUV.x += offset * acrossStep;
    }
    outColor = vec4(textureLod(colorTexture, finalUV, 0.0).rgb, 1.0);
}
