#include "Raycast.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main() {
    const AABB box{{-1,0,4},{1,4,6}};
    float distance=-1;
    assert(RaycastAABB({0,1.6f,0},{0,0,1},box,100,distance) && distance==4);
    assert(RaycastAABB({0,1.6f,0},{0,0,5},box,100,distance) && distance==4);
    assert(!RaycastAABB({2,1.6f,0},{0,0,1},box,100,distance));
    assert(!RaycastAABB({0,1.6f,0},{0,0,-1},box,100,distance));
    assert(!RaycastAABB({0,1.6f,0},{0,0,1},box,3.99f,distance));
    assert(RaycastAABB({1,4,0},{0,0,1},box,4,distance) && distance==4);
    assert(RaycastAABB({0,2,5},{1,0,0},box,100,distance) && distance==0);
    assert(RaycastAABB({4,2,5},{-1,0,0},box,100,distance) && distance==3);
    assert(!RaycastAABB({0,5,0},{0,0,1},box,100,distance));
    assert(!RaycastAABB({0,2,0},{0,0,0},box,100,distance));
    assert(!RaycastAABB({0,2,0},{0,0,1},box,-1,distance));
    assert(!RaycastAABB({0,2,0},{0,0,std::numeric_limits<float>::quiet_NaN()},box,100,distance));
    distance=123;
    assert(!RaycastAABB({2,2,0},{0,0,1},box,100,distance) && distance==123);
    const auto world=Matrix4x4::MakeAffineMatrix({2,2,2},{0,1.5707963f,0},{3,0,2});
    const auto moved=TransformAABB({{0,1,2},{1,3,4}},world);
    assert(std::abs(moved.min.x-7)<0.001f && std::abs(moved.max.x-11)<0.001f);
    assert(std::abs(moved.min.y-2)<0.001f && std::abs(moved.max.y-6)<0.001f);
    assert(RaycastAABB({9,4,-5},{0,0,1},moved,100,distance) && std::abs(distance-5)<0.001f);
    assert(!RaycastAABB({3,4,-5},{0,0,1},moved,100,distance));
    std::puts("Raycast tests passed: hit/miss, parallel axes, behind, range, edge, inside, normalization, invalid input.");
}
