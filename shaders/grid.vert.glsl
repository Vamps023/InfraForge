#version 450

// Grid pass vertex shader: world-XY line vertices to clip space through the
// shared editor camera push constant. The grid lies on the Z-up reference
// plane and therefore recedes naturally in perspective mode.

layout(push_constant) uniform PushBlock {
    layout(offset = 0) mat4 viewProj;
} push;

layout(location = 0) in vec2 inWorld;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = push.viewProj * vec4(inWorld, 0.0, 1.0);
    fragColor = inColor;
}
