#version 450

layout(set = 0, binding = 0) uniform GlobalUniforms {
    mat4 viewProjection;
} globalData;

layout(set = 1, binding = 0) uniform ObjectUniforms {
    mat4 model;
    vec4 tint;
} objectData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inLocalColor;
layout(location = 0) out vec3 vertexColor;
layout(location = 1) out vec3 worldPosition;

void main() {
    vec4 world = objectData.model * vec4(inPosition, 1.0);
    gl_Position = globalData.viewProjection * world;
    vertexColor = inLocalColor;
    worldPosition = world.xyz;
}
