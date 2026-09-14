#version 450

// Terrain heightfield vertices: positions are RENDER-LOCAL float
// coordinates (the double-precision origin-relative conversion happened on
// the CPU at buffer build time through the shared RenderLocalFrame
// boundary), normals are per-vertex world-aligned.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out float outHeight;

void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    outNormal = inNormal;
    outHeight = inPosition.z;
}
