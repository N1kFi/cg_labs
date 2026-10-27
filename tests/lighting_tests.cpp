#include "lighting_scene.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace lab::math;
using namespace lab::lighting;
namespace {
bool near(float a, float b, float e=1e-4f) { return std::abs(a-b)<e; }
Vec3 direction(const Mat4& m, Vec3 a) {
    const auto v=m*Vec4{a.x,a.y,a.z,0}; return {v.x,v.y,v.z};
}
void normalsAndGeometry() {
    for (bool smooth : {false,true}) {
        const auto mesh=makeLitIcosahedron(smooth);
        assert(mesh.vertices.size()==60 && mesh.indices.size()==60);
        for (const auto& v : mesh.vertices) {
            assert(near(length(v.normal),1)); assert(dot(v.position,v.normal)>0);
        }
        if (!smooth) for (std::size_t i=0;i<60;i+=3) {
            const auto& a=mesh.vertices[i]; const auto& b=mesh.vertices[i+1]; const auto& c=mesh.vertices[i+2];
            assert(length(a.normal-b.normal)<1e-5f && length(a.normal-c.normal)<1e-5f);
            assert(std::abs(dot(a.normal,b.position-a.position))<1e-5f);
        }
    }
    const auto floor=makeFloor();
    for (std::size_t i=0;i<floor.indices.size();i+=3) {
        const auto a=floor.vertices[floor.indices[i]],b=floor.vertices[floor.indices[i+1]],c=floor.vertices[floor.indices[i+2]];
        assert(dot(cross(b.position-a.position,c.position-a.position),a.normal)>0);
    }
    // A normal must remain perpendicular to both transformed tangents after nonuniform scale.
    const Vec3 tangentA{1,-1,0}, tangentB{1,0,-1}, n=normalize(cross(tangentA,tangentB));
    const Vec3 angles{0.3f,-0.7f,0.9f}, scaling{2,0.3f,1.4f};
    const Mat4 model=eulerXYZ(angles)*scale(scaling);
    const Vec3 transformedN=normalize(direction(normalTransform(angles,scaling),n));
    assert(std::abs(dot(transformedN,direction(model,tangentA)))<1e-5f);
    assert(std::abs(dot(transformedN,direction(model,tangentB)))<1e-5f);
}
void cameraAndAttenuation() {
    Camera c;
    for (auto rotation : {Vec3{0,0,0},Vec3{-35,75,18},Vec3{89,-140,85}}) {
        c.rotation=rotation;
        const auto origin=c.view()*Vec4{c.position.x,c.position.y,c.position.z,1};
        assert(near(origin.x,0)&&near(origin.y,0)&&near(origin.z,0));
        const auto ahead=c.position+c.axis({0,0,-1});
        const auto point=c.view()*Vec4{ahead.x,ahead.y,ahead.z,1};
        assert(near(point.x,0)&&near(point.y,0)&&near(point.z,-1));
    }
    assert(near(inverseSquare(4)/inverseSquare(16),4));
    assert(std::isfinite(inverseSquare(0)));
    const float inner=std::cos(degreesToRadians(20)),outer=std::cos(degreesToRadians(30));
    assert(near(spotWeight(1,inner,outer),1));
    assert(near(spotWeight(outer-0.01f,inner,outer),0));
    assert(near(spotWeight((inner+outer)*0.5f,inner,outer),0.5f));
    assert(near(spotWeight(outer,outer,outer),1));
    assert(near(spotWeight(outer-0.01f,outer,outer),0));
    float last=0;
    for (int i=0;i<=100;++i) {
        const auto weight=spotWeight(outer+(inner-outer)*i/100,inner,outer);
        assert(weight>=last-1e-5f && std::isfinite(weight)); last=weight;
    }
}
void coneVisualization() {
    for (const Vec3 d : {Vec3{0,-1,0},Vec3{0,1,0},Vec3{},Vec3{1,2,3}}) {
        std::vector<LitVertex> lines;
        appendCone(lines,{0,0,0},d,30,3,{1,1,1});
        assert(lines.size()==106);
        for (const auto& v : lines) assert(std::isfinite(v.position.x+v.position.y+v.position.z));
        const Vec3 end=lines.back().position;
        assert(near(length(end),3));
        // The ring lies 3 units down the axis with the requested half-angle.
        const Vec3 ring=lines[0].position;
        assert(near(dot(normalize(ring),safeDirection(d)),std::cos(degreesToRadians(30))));
    }
}
}
int main() {
    normalsAndGeometry(); cameraAndAttenuation(); coneVisualization();
    std::cout << "lighting_tests: geometry, inverse-transpose normals, camera, attenuation and cones passed\n";
}
