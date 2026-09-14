#version 450

// Road shading: fixed-direction lambert with a distinct asphalt palette
// so roads are visually distinguishable from terrain. The road surface
// uses a dark grey albedo with subtle height-based variation.
layout(location = 0) in vec3 inNormal;
layout(location = 1) in float outHeight;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(inNormal);
    // Light from the upper-left "south", matching terrain shading.
    vec3 lightDir = normalize(vec3(-0.4, -0.5, 0.75));
    float lambert = max(dot(normal, lightDir), 0.0);
    float ambient = 0.35;

    // Road palette: dark asphalt grey.
    vec3 albedo = vec3(0.28, 0.28, 0.30);

    outColor = vec4(albedo * (ambient + (1.0 - ambient) * lambert), 1.0);
}
