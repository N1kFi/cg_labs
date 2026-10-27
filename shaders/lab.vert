#version 450
layout(set=0, binding=0, std140) uniform SceneUniforms {
    mat4 viewProjection;
    vec4 cameraPosition, ambient, directionalDirection, directionalColor;
    uvec4 counts;
} scene;
layout(set=1, binding=0, std140) uniform ModelUniforms {
    mat4 model, normalMatrix;
    vec4 albedo, specular, parameters;
} objectData;
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec3 inColor;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec3 vertexColor;
void main() {
    vec4 world = objectData.model * vec4(inPosition, 1);
    gl_Position = scene.viewProjection * world;
    worldPosition = world.xyz;
    worldNormal = mat3(objectData.normalMatrix) * inNormal;
    vertexColor = inColor;
}
