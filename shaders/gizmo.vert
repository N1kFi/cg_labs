#version 450
layout(set=0, binding=0, std140) uniform SceneUniforms { mat4 viewProjection; } scene;
layout(location=0) in vec3 inPosition;
layout(location=2) in vec3 inColor;
layout(location=0) out vec3 color;
void main() {
    gl_Position = scene.viewProjection * vec4(inPosition,1);
    color = inColor;
}
