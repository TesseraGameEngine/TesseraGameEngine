// Ray-marched volumetric fog shared by shader.frag and sky.frag: light scattered toward the camera
// by height fog between the camera and a surface (or out to a fixed distance for the sky), with
// light shafts from the sun's shadow cascades and from shadowed point / spot lights.
// Requires common_ubo.glsl and lights_common.glsl, and the caller must declare shadowMap (binding 7) and
// localShadowMap (binding 11).
#ifndef VOLUMETRIC_COMMON_GLSL
#define VOLUMETRIC_COMMON_GLSL

const int kVolumetricSteps = 16;
const int kVolumetricMaxLights = 32; // lights evaluated per sample (those in range)
const float kVolumetricSkyDistance = 150.0; // how far the sky's fog reaches (m)

bool VolumetricActive() {
    return ubo.volumetricParams.x > 0.0 && ubo.postProcessParams.w > 0.0;
}

// Interleaved gradient noise, shifted every frame so TAA averages the banding away
float VolumetricNoise(vec2 fragCoord) {
    fragCoord += 5.588238 * mod(ubo.timeParams.w, 64.0);
    return fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
}

// Henyey-Greenstein phase: g > 0 scatters forward (bright halo looking toward the light)
float VolumetricPhase(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

// Single-tap sun visibility from the finest cascade covering the point (1 = lit)
float VolumetricSunVisibility(vec3 p) {
    int cascadeCount = int(ubo.shadowParams.w + 0.5);
    for (int c = 0; c < cascadeCount; ++c) {
        vec4 clip = ubo.shadowMatrices[c] * vec4(p, 1.0);
        vec3 proj = clip.xyz / clip.w;
        if (max(abs(proj.x), abs(proj.y)) >= 1.0 || proj.z <= 0.0 || proj.z >= 1.0) continue;
        return texture(shadowMap, vec4(proj.xy * 0.5 + 0.5, float(c), proj.z - 0.002));
    }
    return 1.0;
}

// Single-tap point / spot visibility (1 = lit, or the light has no shadow layers)
float VolumetricLocalVisibility(int lightIndex, vec3 p, vec3 lightPos) {
    int used = int(ubo.localShadowParams.x + 0.5);
    for (int e = 0; e < used; ++e) {
        vec4 info = ubo.localShadowLights[e];
        if (int(info.x + 0.5) != lightIndex) continue;
        int layer = int(info.y + 0.5);
        vec3 fromLight = p - lightPos;
        if (info.z > 0.5) {
            vec3 a = abs(fromLight);
            if (a.x >= a.y && a.x >= a.z)      layer += (fromLight.x > 0.0) ? 0 : 1;
            else if (a.y >= a.z)               layer += (fromLight.y > 0.0) ? 2 : 3;
            else                               layer += (fromLight.z > 0.0) ? 4 : 5;
        }
        vec4 clip = ubo.localShadowMatrices[layer] * vec4(p, 1.0);
        if (clip.w <= 0.0) return 1.0;
        vec3 proj = clip.xyz / clip.w;
        if (proj.z <= 0.0 || proj.z >= 1.0 || max(abs(proj.x), abs(proj.y)) >= 1.0) return 1.0;
        return texture(localShadowMap, vec4(proj.xy * 0.5 + 0.5, float(layer), proj.z - 0.002));
    }
    return 1.0;
}

// Fog between rayStart and rayEnd. Returns rgb = light scattered toward the camera,
// a = transmittance (how much of the surface behind still shows). Apply as color * a + rgb.
vec4 VolumetricFog(vec3 rayStart, vec3 rayEnd, vec2 fragCoord) {
    vec3 ray = rayEnd - rayStart;
    float rayLength = length(ray);
    if (rayLength < 1e-4) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 dir = ray / rayLength;

    float density = ubo.volumetricParams.x;
    float falloff = ubo.volumetricParams.y;
    float anisotropy = clamp(ubo.volumetricParams.z, -0.95, 0.95);
    float baseHeight = ubo.volumetricParams.w;
    float lightStrength = ubo.postProcessParams.w;

    float stepLength = rayLength / float(kVolumetricSteps);
    float t = stepLength * VolumetricNoise(fragCoord);
    int sunIndex = (ubo.shadowParams.x > 0.5) ? int(ubo.shadowParams.y + 0.5) : -1;

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    for (int s = 0; s < kVolumetricSteps; ++s) {
        vec3 p = rayStart + dir * t;
        float localDensity = density * exp(-max(p.y - baseHeight, 0.0) * falloff);

        // Ambient light in the fog takes the fog colour
        vec3 inScatter = ubo.fogColorAndDensity.rgb * 0.08;
        // Directional lights, then this sample's cluster. Only lights that reach the sample count
        // toward the per-sample cap, so busy clusters don't drop lights unevenly (visible tile edges).
        uvec2 cluster = lightClusters.clusters[ClusterIndex(p)];
        int lightCount = LightCountAt(cluster);
        int evaluated = 0;
        for (int k = 0; k < lightCount && evaluated < kVolumetricMaxLights; ++k) {
            int i = LightIndexAt(cluster, k);
            vec3 lightColor = lightList.lights[i].color.rgb * lightList.lights[i].color.a;
            float type = lightList.lights[i].position.w;
            if (type < 0.5) {
                vec3 L = normalize(lightList.lights[i].position.xyz);
                float visibility = (i == sunIndex) ? VolumetricSunVisibility(p) : 1.0;
                inScatter += lightColor * VolumetricPhase(dot(dir, L), anisotropy) * visibility;
                ++evaluated;
            } else {
                vec3 lightPos = lightList.lights[i].position.xyz;
                vec3 toLight = lightPos - p;
                float dist = length(toLight);
                float range = lightList.lights[i].spotDirAndRange.w;
                if (dist >= range) continue;
                vec3 L = toLight / dist;
                // Same falloff as the surface lighting in shader.frag
                float rangeFactor = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
                float attenuation = (rangeFactor * rangeFactor) / (1.0 + 0.09 * dist + 0.032 * dist * dist);
                if (type > 1.5) {
                    float cosAngle = dot(-L, normalize(lightList.lights[i].spotDirAndRange.xyz));
                    float cosInner = lightList.lights[i].spotAnglesAndShadow.x;
                    float cosOuter = lightList.lights[i].spotAnglesAndShadow.y;
                    float spotFactor = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
                    attenuation *= spotFactor * spotFactor;
                }
                if (attenuation <= 0.0) continue;
                float visibility = (lightList.lights[i].spotAnglesAndShadow.z > 0.5) ? VolumetricLocalVisibility(i, p, lightPos) : 1.0;
                inScatter += lightColor * attenuation * VolumetricPhase(dot(dir, L), anisotropy) * visibility;
                ++evaluated;
            }
        }

        // Energy-conserving integration over the step (Frostbite)
        float stepTransmittance = exp(-localDensity * stepLength);
        vec3 stepScatter = inScatter * lightStrength * (1.0 - stepTransmittance);
        scattered += transmittance * stepScatter;
        transmittance *= stepTransmittance;
        t += stepLength;
    }
    return vec4(scattered, transmittance);
}

#endif
