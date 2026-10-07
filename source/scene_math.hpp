#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lab::math {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;
};

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	constexpr Vec3() = default;
	constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Vec4 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;

	constexpr Vec4() = default;
	constexpr Vec4(float x_, float y_, float z_, float w_)
		: x(x_), y(y_), z(z_), w(w_) {}
};

inline constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
inline constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }
inline constexpr Vec3 operator/(Vec3 v, float s) { return v * (1.0f / s); }
inline constexpr Vec4 operator*(Vec4 v, float s) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }

inline constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3 cross(Vec3 a, Vec3 b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline constexpr float lengthSquared(Vec3 v) { return dot(v, v); }
inline float length(Vec3 v) { return std::sqrt(lengthSquared(v)); }
inline Vec3 normalize(Vec3 v) {
	const float len = length(v);
	return len > 0.0f ? v / len : Vec3{};
}

// Column-major, column-vector matrices: p' = m * p.
struct Mat4 {
	std::array<float, 16> v{};

	constexpr float& operator()(std::size_t column, std::size_t row) { return v[column * 4 + row]; }
	constexpr float operator()(std::size_t column, std::size_t row) const { return v[column * 4 + row]; }
};

inline Mat4 identity() {
	Mat4 result{};
	result(0, 0) = result(1, 1) = result(2, 2) = result(3, 3) = 1.0f;
	return result;
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
	Mat4 result{};
	for (std::size_t column = 0; column < 4; ++column) {
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t k = 0; k < 4; ++k) {
				result(column, row) += a(k, row) * b(column, k);
			}
		}
	}
	return result;
}

inline Vec4 operator*(const Mat4& m, Vec4 p) {
	return {
		m(0, 0) * p.x + m(1, 0) * p.y + m(2, 0) * p.z + m(3, 0) * p.w,
		m(0, 1) * p.x + m(1, 1) * p.y + m(2, 1) * p.z + m(3, 1) * p.w,
		m(0, 2) * p.x + m(1, 2) * p.y + m(2, 2) * p.z + m(3, 2) * p.w,
		m(0, 3) * p.x + m(1, 3) * p.y + m(2, 3) * p.z + m(3, 3) * p.w,
	};
}

inline Mat4 translate(Vec3 t) {
	Mat4 result = identity();
	result(3, 0) = t.x;
	result(3, 1) = t.y;
	result(3, 2) = t.z;
	return result;
}

inline Mat4 scale(Vec3 s) {
	Mat4 result = identity();
	result(0, 0) = s.x;
	result(1, 1) = s.y;
	result(2, 2) = s.z;
	return result;
}

inline Mat4 rotateX(float angle) {
	Mat4 result = identity();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result(1, 1) = c;
	result(2, 1) = -s;
	result(1, 2) = s;
	result(2, 2) = c;
	return result;
}

inline Mat4 rotateY(float angle) {
	Mat4 result = identity();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result(0, 0) = c;
	result(2, 0) = s;
	result(0, 2) = -s;
	result(2, 2) = c;
	return result;
}

inline Mat4 rotateZ(float angle) {
	Mat4 result = identity();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result(0, 0) = c;
	result(1, 0) = -s;
	result(0, 1) = s;
	result(1, 1) = c;
	return result;
}

inline Mat4 eulerXYZ(Vec3 angles) {
	return rotateZ(angles.z) * rotateY(angles.y) * rotateX(angles.x);
}

inline Mat4 lookAtRH(Vec3 eye, Vec3 target, Vec3 up) {
	const Vec3 forward = normalize(target - eye);
	const Vec3 right = normalize(cross(forward, up));
	const Vec3 correctedUp = cross(right, forward);
	Mat4 result = identity();
	result(0, 0) = right.x;
	result(1, 0) = right.y;
	result(2, 0) = right.z;
	result(0, 1) = correctedUp.x;
	result(1, 1) = correctedUp.y;
	result(2, 1) = correctedUp.z;
	result(0, 2) = -forward.x;
	result(1, 2) = -forward.y;
	result(2, 2) = -forward.z;
	result(3, 0) = -dot(right, eye);
	result(3, 1) = -dot(correctedUp, eye);
	result(3, 2) = dot(forward, eye);
	return result;
}

inline Mat4 perspectiveRHZeroToOne(float verticalFov, float aspect, float nearPlane, float farPlane) {
	const float f = 1.0f / std::tan(verticalFov * 0.5f);
	Mat4 result{};
	result(0, 0) = f / aspect;
	// Vulkan's framebuffer Y axis points down; this produces the required Y flip.
	result(1, 1) = -f;
	result(2, 2) = farPlane / (nearPlane - farPlane);
	result(2, 3) = -1.0f;
	result(3, 2) = (farPlane * nearPlane) / (nearPlane - farPlane);
	return result;
}

inline Mat4 orthographicRHZeroToOne(float width, float height, float nearPlane, float farPlane) {
	Mat4 result = identity();
	result(0, 0) = 2.0f / width;
	result(1, 1) = -2.0f / height;
	result(2, 2) = 1.0f / (nearPlane - farPlane);
	result(3, 2) = nearPlane / (nearPlane - farPlane);
	return result;
}

inline Vec3 transformPoint(const Mat4& matrix, Vec3 point) {
	const Vec4 transformed = matrix * Vec4{point.x, point.y, point.z, 1.0f};
	return {transformed.x / transformed.w, transformed.y / transformed.w, transformed.z / transformed.w};
}

inline constexpr float pi = 3.14159265358979323846f;
inline constexpr float degreesToRadians(float degrees) { return degrees * pi / 180.0f; }

} // namespace lab::math

namespace lab::scene {

using lab::math::Vec3;
using lab::math::Vec4;

enum class TrajectoryMode : std::uint32_t {
	Lissajous = 0,
	Epicyclic = 1,
};

struct Vertex {
	Vec3 position;
	Vec3 localColor;
};

struct Mesh {
	std::array<Vertex, 12> vertices{};
	std::array<std::uint32_t, 60> indices{};
};

inline Mesh makeIcosahedron(float radius = 1.0f) {
	constexpr float phi = 1.6180339887498948482f;
	Mesh mesh{};
	mesh.vertices = {{
		{{0.0f, -1.0f, -phi}, {}}, {{0.0f, -1.0f, phi}, {}},
		{{0.0f, 1.0f, -phi}, {}}, {{0.0f, 1.0f, phi}, {}},
		{{-1.0f, -phi, 0.0f}, {}}, {{-1.0f, phi, 0.0f}, {}},
		{{1.0f, -phi, 0.0f}, {}}, {{1.0f, phi, 0.0f}, {}},
		{{-phi, 0.0f, -1.0f}, {}}, {{-phi, 0.0f, 1.0f}, {}},
		{{phi, 0.0f, -1.0f}, {}}, {{phi, 0.0f, 1.0f}, {}},
	}};

	// Every equilateral triple at the shortest distance is an icosahedron face.
	// Orienting by the centroid makes the winding outward and independent of a
	// hand-maintained face table.
	std::size_t indexCount = 0;
	for (std::uint32_t a = 0; a < mesh.vertices.size(); ++a) {
		for (std::uint32_t b = a + 1; b < mesh.vertices.size(); ++b) {
			for (std::uint32_t c = b + 1; c < mesh.vertices.size(); ++c) {
				const Vec3 ab = mesh.vertices[b].position - mesh.vertices[a].position;
				const Vec3 ac = mesh.vertices[c].position - mesh.vertices[a].position;
				const Vec3 bc = mesh.vertices[c].position - mesh.vertices[b].position;
				constexpr float edgeSquared = 4.0f;
				if (std::abs(lab::math::lengthSquared(ab) - edgeSquared) > 1.0e-4f ||
				    std::abs(lab::math::lengthSquared(ac) - edgeSquared) > 1.0e-4f ||
				    std::abs(lab::math::lengthSquared(bc) - edgeSquared) > 1.0e-4f) {
					continue;
				}
				if (indexCount + 3 > mesh.indices.size()) {
					return {};
				}
				const Vec3 normal = lab::math::cross(ab, ac);
				const Vec3 centroid = mesh.vertices[a].position + mesh.vertices[b].position + mesh.vertices[c].position;
				mesh.indices[indexCount++] = a;
				if (lab::math::dot(normal, centroid) > 0.0f) {
					mesh.indices[indexCount++] = b;
					mesh.indices[indexCount++] = c;
				} else {
					mesh.indices[indexCount++] = c;
					mesh.indices[indexCount++] = b;
				}
			}
		}
	}

	for (Vertex& vertex : mesh.vertices) {
		const Vec3 normal = lab::math::normalize(vertex.position);
		vertex.position = normal * radius; // unit circumradius
		vertex.localColor = {0.45f + 0.45f * normal.x, 0.45f + 0.45f * normal.y, 0.45f + 0.45f * normal.z};
	}
	return mesh;
}

inline Vec3 trajectoryPosition(TrajectoryMode mode, float time, float radius, float amplitude,
                               float frequency, float phase) {
	const float t = time * frequency + phase;
	if (mode == TrajectoryMode::Epicyclic) {
		const float radial = radius * (0.78f + 0.22f * std::cos(3.0f * t));
		return {radial * std::cos(t), amplitude * std::sin(2.0f * t), radial * std::sin(t)};
	}
	return {radius * std::sin(t), amplitude * std::sin(2.0f * t + 0.35f), radius * 0.70f * std::cos(t)};
}

inline Vec3 trajectoryRotation(float time, float speed, float phase) {
	return {speed * 0.43f * time + phase * 0.11f,
	        speed * 0.71f * time - phase * 0.07f,
	        speed * 0.29f * time + phase * 0.17f};
}

class AnimationClock {
public:
	bool playing = false;
	float speed = 1.0f;

	void reset(double wallTime = 0.0) {
		simulationTime_ = 0.0;
		lastWallTime_ = wallTime;
		hasLastWallTime_ = true;
	}

	void update(double absoluteWallTime) {
		if (!hasLastWallTime_) {
			lastWallTime_ = absoluteWallTime;
			hasLastWallTime_ = true;
			return;
		}
		const double delta = std::clamp(absoluteWallTime - lastWallTime_, 0.0, 0.1);
		lastWallTime_ = absoluteWallTime;
		if (playing) {
			simulationTime_ += static_cast<float>(delta) * speed;
		}
	}

	[[nodiscard]] float time() const { return simulationTime_; }

private:
	double lastWallTime_ = 0.0;
	float simulationTime_ = 0.0f;
	bool hasLastWallTime_ = false;
};

} // namespace lab::scene
