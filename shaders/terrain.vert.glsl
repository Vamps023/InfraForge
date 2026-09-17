#version 450

// Terrain heightfield vertices: positions are RENDER-LOCAL float
// coordinates (the double-precision origin-relative conversion happened on
// the CPU at buffer build time through the shared RenderLocalFrame
// boundary), normals are per-vertex world-aligned.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    float heightScale;
    uint renderMode;
    float minHeight;
    float maxHeight;
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out float outHeight;
layout(location = 2) out vec2 outUV;

void main() {
    float scale = pc.heightScale > 0.0 ? pc.heightScale : 1.0;
    vec3 scaledPos = vec3(inPosition.x, inPosition.y, inPosition.z * scale);
    gl_Position = pc.viewProj * vec4(scaledPos, 1.0);
    outNormal = inNormal;
    outHeight = inPosition.z;
    outUV = inUV;
}
