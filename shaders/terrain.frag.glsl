#version 450

// Terrain shading: fixed-direction lambert with a subtle height tint so
// elevation structure stays readable in the initial editor pass. NoData
// regions never reach this shader — they are dropped at geometry build.
layout(location = 0) in vec3 inNormal;
layout(location = 1) in float inHeight;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(inNormal);
    // Light from the upper-left "south", independent of view.
    vec3 lightDir = normalize(vec3(-0.4, -0.5, 0.75));
    float lambert = max(dot(normal, lightDir), 0.0);
    float ambient = 0.35;

    // Terrain palette: low ground greenish, high ground pale.
    float heightTint = clamp(inHeight / 400.0, 0.0, 1.0);
    vec3 low = vec3(0.30, 0.36, 0.24);
    vec3 high = vec3(0.52, 0.50, 0.42);
    vec3 albedo = mix(low, high, heightTint);

    outColor = vec4(albedo * (ambient + (1.0 - ambient) * lambert), 1.0);
}
