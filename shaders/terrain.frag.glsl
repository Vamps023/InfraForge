#version 450

// Terrain shading: multi-mode shading with directional hillshading, ambient,
// hypsometric elevation tint, elevation contour lines, and UV coordinates.
layout(location = 0) in vec3 inNormal;
layout(location = 1) in float inHeight;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    float heightScale;
    uint renderMode;
    float minHeight;
    float maxHeight;
} pc;

layout(location = 0) out vec4 outColor;

// Hypsometric tint palette: valleys, lowlands, plateaus, mountains, and snow peaks
vec3 hypsometricColor(float t) {
    vec3 c0 = vec3(0.18, 0.32, 0.20); // valley green
    vec3 c1 = vec3(0.38, 0.48, 0.28); // upland green
    vec3 c2 = vec3(0.65, 0.58, 0.38); // plateau ochre/sand
    vec3 c3 = vec3(0.50, 0.42, 0.38); // mountain rock
    vec3 c4 = vec3(0.85, 0.88, 0.92); // snow peak

    if (t < 0.25) return mix(c0, c1, t / 0.25);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) / 0.25);
    return mix(c3, c4, (t - 0.75) / 0.25);
}

void main() {
    vec3 normal = normalize(inNormal);
    // Directional light from high north-west for natural relief shading
    vec3 lightDir = normalize(vec3(-0.4, -0.5, 0.75));
    float lambert = max(dot(normal, lightDir), 0.0);
    float ambient = 0.38;

    // Normalized height factor
    float hMin = pc.minHeight;
    float hMax = pc.maxHeight;
    float range = max(1.0, hMax - hMin);
    float t = clamp((inHeight - hMin) / range, 0.0, 1.0);

    vec3 albedo = hypsometricColor(t);

    // Mode 2: Elevation contour lines every 50m
    if (pc.renderMode == 2u) {
        float contourInterval = 50.0;
        float lineDist = abs(fract(inHeight / contourInterval - 0.5) - 0.5) / fwidth(inHeight / contourInterval);
        float lineIntensity = 1.0 - clamp(lineDist, 0.0, 1.0);
        albedo = mix(albedo, vec3(0.10, 0.10, 0.10), lineIntensity * 0.5);
    }

    vec3 litColor = albedo * (ambient + (1.0 - ambient) * lambert);
    outColor = vec4(litColor, 1.0);
}
