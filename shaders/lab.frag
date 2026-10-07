#version 450

layout(set = 1, binding = 0) uniform ObjectUniforms {
    mat4 model;
    vec4 tint;
} objectData;

layout(location = 0) in vec3 vertexColor;
layout(location = 1) in vec3 worldPosition;
layout(location = 0) out vec4 outColor;

void main() {
    // Flat per-face normal from screen-space derivatives: every one of the 20
    // triangular facets reads as a distinct plane even though vertices are shared.
    vec3 faceNormal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));

    vec3 lightDirection = normalize(vec3(0.45, 0.75, 0.55));
    vec3 viewDirection = normalize(vec3(0.0, 0.02, 1.0));
    vec3 halfVector = normalize(lightDirection + viewDirection);

    float diffuse = max(dot(faceNormal, lightDirection), 0.0);
    float backLight = max(dot(-faceNormal, lightDirection), 0.0) * 0.18;
    float specular = pow(max(dot(faceNormal, halfVector), 0.0), 48.0) * 0.35;
    float ambient = 0.34;

    float shade = ambient + diffuse * 0.78 + backLight + specular;

    // Per-object tint multiplies the procedural vertex-local colour, then the
    // combined colour is lit so facets stay visually separated.
    vec3 base = vertexColor * objectData.tint.rgb;
    outColor = vec4(base * shade, objectData.tint.a);
}
