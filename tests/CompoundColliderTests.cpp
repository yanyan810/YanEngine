#include "StageLoader.h"
#include <cassert>
#include <iostream>
int main() {
    StageLoader level;
    assert(level.Load("project/resources/levels/compound/compound.json"));
    assert(level.ValidateAssets("project/resources"));
    assert(level.id=="compound" && level.collision.colliders.size()==3);
    StageHit hit;
    // Actual Blender export: (-x,z,-y) conversion, doorway remains traversable.
    assert(!level.collision.Raycast({-3,1.5f,2},{0,0,-1},5,hit));
    assert(level.collision.Raycast({-.5f,1.5f,2},{0,0,-1},5,hit));
    assert(level.collision.Raycast({-5.5f,1.5f,2},{0,0,-1},5,hit));
    assert(level.collision.Raycast({-3,3.5f,2},{0,0,-1},5,hit));
    const auto through=level.collision.Move({-3,0,2},{-3,0,-3});
    assert(StageLength(through-Vector3{-3,0,-3})<.001f);
    const auto blocked=level.collision.Move({-.5f,0,2},{-.5f,0,-3});
    assert(blocked.z>0);
    std::cout<<"Compound collider export passed: existing StageLoader/StageWorld, three boxes, doorway rays and player movement pass; both pillars and lintel block.\n";
}
