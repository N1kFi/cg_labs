#pragma once

#include "scene_math.hpp"
#include <cstddef>

namespace lab::lighting {
using namespace lab::math;

// vec4 fields match std140/std430 offsets without compiler-specific padding.
struct alignas(16) SceneUniforms {
	Mat4 viewProjection;
	Vec4 cameraPosition, ambient, directionalDirection, directionalColor;
	std::array<std::uint32_t, 4> counts{};
};
struct alignas(16) ModelUniforms {
	Mat4 model, normalMatrix;
	Vec4 albedo, specular, parameters; // parameters: shininess, procedural weight
};
struct alignas(16) PointLight { Vec4 position, color; }; // color.w = intensity
struct alignas(16) SpotLight {
	Vec4 position, color;
	Vec4 direction; // xyz direction, w cos(inner half-angle)
	Vec4 cone; // x cos(outer half-angle)
};
static_assert(sizeof(SceneUniforms) == 144 && offsetof(SceneUniforms, counts) == 128);
static_assert(sizeof(ModelUniforms) == 176 && offsetof(ModelUniforms, albedo) == 128);
static_assert(sizeof(PointLight) == 32 && offsetof(PointLight, color) == 16);
static_assert(sizeof(SpotLight) == 64 && offsetof(SpotLight, direction) == 32);

struct LitVertex {
	Vec3 position, normal;
	Vec3 color{1, 1, 1};
};
struct Geometry {
	std::vector<LitVertex> vertices;
	std::vector<std::uint32_t> indices;
};
inline Geometry makeLitIcosahedron(bool smooth = false) {
	const auto source = lab::scene::makeIcosahedron();
	Geometry result;
	for (std::size_t i = 0; i < source.indices.size(); i += 3) {
		const auto& a = source.vertices[source.indices[i]];
		const auto& b = source.vertices[source.indices[i + 1]];
		const auto& c = source.vertices[source.indices[i + 2]];
		const Vec3 face = normalize(cross(b.position - a.position, c.position - a.position));
		for (const auto* vertex : {&a, &b, &c}) {
			result.indices.push_back(static_cast<std::uint32_t>(result.vertices.size()));
			result.vertices.push_back({vertex->position, smooth ? normalize(vertex->position) : face, vertex->localColor});
		}
	}
	return result;
}
inline Geometry makeFloor() {
	return {{{{-7, 0, -7}, {0, 1, 0}}, {{7, 0, -7}, {0, 1, 0}},
	         {{7, 0, 7}, {0, 1, 0}}, {{-7, 0, 7}, {0, 1, 0}}}, {0, 2, 1, 0, 3, 2}};
}
// Inverse-transpose of T*R*S has linear part R*inverse(S).
inline Mat4 normalTransform(Vec3 rotation, Vec3 scaling) {
	return eulerXYZ(rotation) * scale({1 / scaling.x, 1 / scaling.y, 1 / scaling.z});
}
struct Camera {
	Vec3 position{4.8f, 3.2f, 8.5f};
	Vec3 rotation{-18, 29, 0}; // pitch, yaw, roll, degrees
	float fov = 52, speed = 4;
	Mat4 orientation() const {
		return rotateY(degreesToRadians(rotation.y)) * rotateX(degreesToRadians(rotation.x)) * rotateZ(degreesToRadians(rotation.z));
	}
	Vec3 axis(Vec3 a) const {
		const auto v = orientation() * Vec4{a.x, a.y, a.z, 0};
		return {v.x, v.y, v.z};
	}
	Mat4 view() const { return lookAtRH(position, position + axis({0, 0, -1}), axis({0, 1, 0})); }
};
inline Vec3 safeDirection(Vec3 d) { return lengthSquared(d) > 1e-8f ? normalize(d) : Vec3{0, -1, 0}; }
inline float inverseSquare(float d2) { return 1 / std::max(d2, 0.01f); }
inline float spotWeight(float cosine, float inner, float outer) {
	if (inner - outer < 1e-6f) return cosine >= outer ? 1.0f : 0.0f;
	const float t = std::clamp((cosine - outer) / (inner - outer), 0.0f, 1.0f);
	return t * t * (3 - 2 * t);
}
inline void appendLine(std::vector<LitVertex>& vertices, Vec3 a, Vec3 b, Vec3 color) {
	vertices.push_back({a, {}, color});
	vertices.push_back({b, {}, color});
}
inline void appendMarker(std::vector<LitVertex>& vertices, Vec3 center, Vec3 color) {
	// A small unlit cube marks the exact light position.
	constexpr Vec3 corners[] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
	                            {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
	constexpr std::uint32_t indices[] = {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
	                                    3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
	for (auto index : indices) vertices.push_back({center + corners[index] * 0.1f, {}, color});
}
inline void appendCone(std::vector<LitVertex>& lines, Vec3 origin, Vec3 direction,
                       float halfAngleDegrees, float distance, Vec3 color) {
	const Vec3 d = safeDirection(direction);
	const Vec3 u = normalize(cross(d, std::abs(d.y) < 0.95f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
	const Vec3 v = cross(d, u);
	const Vec3 center = origin + d * distance;
	const float radius = distance * std::tan(degreesToRadians(halfAngleDegrees));
	constexpr int segments = 48;
	for (int i = 0; i < segments; ++i) {
		const float a = 2 * pi * static_cast<float>(i) / segments;
		const float b = 2 * pi * static_cast<float>(i + 1) / segments;
		const Vec3 p = center + (u * std::cos(a) + v * std::sin(a)) * radius;
		const Vec3 q = center + (u * std::cos(b) + v * std::sin(b)) * radius;
		appendLine(lines, p, q, color);
		if (i % 12 == 0) appendLine(lines, origin, p, color);
	}
	appendLine(lines, origin, center, color);
}
} // namespace lab::lighting
