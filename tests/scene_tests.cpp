#include "scene_math.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <utility>

namespace {

using lab::math::Vec3;

bool near(float a, float b, float epsilon = 1.0e-4f) {
	return std::abs(a - b) <= epsilon;
}

float ndcZ(const lab::math::Mat4& projection, float viewSpaceZ) {
	const lab::math::Vec4 clip = projection * lab::math::Vec4{0.0f, 0.0f, viewSpaceZ, 1.0f};
	return clip.z / clip.w;
}

void testIcosahedronTopology() {
	const lab::scene::Mesh mesh = lab::scene::makeIcosahedron();
	assert(mesh.vertices.size() == 12);
	assert(mesh.indices.size() == 60);

	std::map<std::pair<std::uint32_t, std::uint32_t>, int> edgeUse;
	// Raw vertex coordinates have length sqrt(1 + phi^2); after normalization
	// to unit circumradius the edge length squared is 4 / (1 + phi^2).
	const float expectedEdgeSquared = 4.0f / (1.0f + 1.6180339887498948482f * 1.6180339887498948482f);
	for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
		const std::uint32_t ia = mesh.indices[i];
		const std::uint32_t ib = mesh.indices[i + 1];
		const std::uint32_t ic = mesh.indices[i + 2];
		assert(ia < 12 && ib < 12 && ic < 12);
		assert(ia != ib && ib != ic && ia != ic);
		const Vec3 a = mesh.vertices[ia].position;
		const Vec3 b = mesh.vertices[ib].position;
		const Vec3 c = mesh.vertices[ic].position;
		assert(near(lab::math::lengthSquared(b - a), expectedEdgeSquared));
		assert(near(lab::math::lengthSquared(c - b), expectedEdgeSquared));
		assert(near(lab::math::lengthSquared(a - c), expectedEdgeSquared));
		assert(lab::math::dot(lab::math::cross(b - a, c - a), a + b + c) > 0.0f);
		const auto addEdge = [&edgeUse](std::uint32_t x, std::uint32_t y) {
			if (x > y) std::swap(x, y);
			++edgeUse[{x, y}];
		};
		addEdge(ia, ib);
		addEdge(ib, ic);
		addEdge(ic, ia);
	}
	assert(edgeUse.size() == 30);
	for (const auto& [edge, use] : edgeUse) {
		(void)edge;
		assert(use == 2);
	}
	// V - E + F = 2 for a closed orientable sphere.
	assert(12 - static_cast<int>(edgeUse.size()) + 20 == 2);
}

void testProjection() {
	constexpr float nearPlane = 0.1f;
	constexpr float farPlane = 40.0f;
	const auto projection = lab::math::perspectiveRHZeroToOne(
		lab::math::degreesToRadians(60.0f), 16.0f / 9.0f, nearPlane, farPlane);
	assert(near(ndcZ(projection, -nearPlane), 0.0f, 2.0e-5f));
	assert(near(ndcZ(projection, -farPlane), 1.0f, 2.0e-5f));
	const lab::math::Vec4 top = projection * lab::math::Vec4{0.0f, 1.0f, -1.0f, 1.0f};
	assert(top.y / top.w < 0.0f); // Vulkan framebuffer Y flip.
	const auto model = lab::math::translate({2.0f, 3.0f, -4.0f}) * lab::math::scale({2.0f, 2.0f, 2.0f});
	const Vec3 transformed = lab::math::transformPoint(model, {1.0f, 1.0f, 1.0f});
	assert(near(transformed.x, 4.0f) && near(transformed.y, 5.0f) && near(transformed.z, -2.0f));
}

void testAnimationClock() {
	lab::scene::AnimationClock clock;
	clock.reset(10.0);
	clock.update(11.0);
	assert(near(clock.time(), 0.0f));
	clock.playing = true;
	clock.speed = 2.0f;
	clock.update(11.5);
	assert(near(clock.time(), 0.2f));
	clock.update(100.0); // Large stalls are capped and remain deterministic.
	assert(near(clock.time(), 0.4f));
	clock.playing = false;
	clock.update(101.0);
	assert(near(clock.time(), 0.4f));
	const Vec3 first = lab::scene::trajectoryPosition(lab::scene::TrajectoryMode::Lissajous, clock.time(), 1.0f, 0.5f, 1.0f, 0.0f);
	const Vec3 second = lab::scene::trajectoryPosition(lab::scene::TrajectoryMode::Lissajous, clock.time(), 1.0f, 0.5f, 1.0f, 0.0f);
	assert(near(first.x, second.x) && near(first.y, second.y) && near(first.z, second.z));
}

} // namespace

int main() {
	testIcosahedronTopology();
	testProjection();
	testAnimationClock();
	std::cout << "scene_tests: all checks passed\n";
	return 0;
}
