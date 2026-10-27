#version 450
layout(set=0, binding=0, std140) uniform SceneUniforms {
    mat4 viewProjection;
    vec4 cameraPosition, ambient, directionalDirection, directionalColor;
    uvec4 counts;
} scene;
struct PointLight { vec4 position; vec4 color; };
struct SpotLight { vec4 position; vec4 color; vec4 direction; vec4 cone; };
layout(set=0, binding=1, std430) readonly buffer PointLights { PointLight points[]; };
layout(set=0, binding=2, std430) readonly buffer SpotLights { SpotLight spots[]; };
layout(set=1, binding=0, std140) uniform ModelUniforms {
    mat4 model, normalMatrix;
    vec4 albedo, specular, parameters;
} objectData;
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec3 vertexColor;
layout(location=0) out vec4 outColor;
vec3 blinnPhong(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 albedo) {
    float ndotl = max(dot(n,l), 0);
    if (ndotl <= 0) return vec3(0);
    vec3 sum = l + v;
    vec3 h = sum * inversesqrt(max(dot(sum,sum), 1e-8));
    float specular = pow(max(dot(n,h), 0), objectData.parameters.x);
    return radiance * (albedo * ndotl + objectData.specular.rgb * specular);
}
void main() {
    vec3 n = normalize(worldNormal);
    vec3 deltaView = scene.cameraPosition.xyz - worldPosition;
    vec3 v = deltaView * inversesqrt(max(dot(deltaView,deltaView), 1e-8));
    vec3 albedo = objectData.albedo.rgb * mix(vec3(1), vertexColor, objectData.parameters.y);
    vec3 result = scene.ambient.rgb * scene.ambient.w * albedo;
    result += blinnPhong(n, v, -scene.directionalDirection.xyz,
                        scene.directionalColor.rgb * scene.directionalColor.w, albedo);
    for (uint i=0; i<scene.counts.x; ++i) {
        vec3 delta = points[i].position.xyz - worldPosition;
        float d2 = dot(delta,delta);
        vec3 l = delta * inversesqrt(max(d2,1e-8));
        vec3 radiance = points[i].color.rgb * points[i].color.w / max(d2,0.01);
        result += blinnPhong(n,v,l,radiance,albedo);
    }
    for (uint i=0; i<scene.counts.y; ++i) {
        vec3 delta = spots[i].position.xyz - worldPosition;
        float d2 = dot(delta,delta);
        vec3 l = delta * inversesqrt(max(d2,1e-8));
        float cosine = dot(-l,spots[i].direction.xyz);
        float inner = spots[i].direction.w, outer = spots[i].cone.x;
        // Equal angles give a hard edge, distinct angles a smooth penumbra.
        float cone = inner-outer > 1e-6 ? smoothstep(outer,inner,cosine) : step(outer,cosine);
        vec3 radiance = spots[i].color.rgb * spots[i].color.w * cone / max(d2,0.01);
        result += blinnPhong(n,v,l,radiance,albedo);
    }
    // Compress HDR highlights before the sRGB framebuffer conversion.
    outColor = vec4(vec3(1)-exp(-result), 1);
}
