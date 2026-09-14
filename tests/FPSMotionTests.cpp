#include "FPSMotion.h"
#include "Matrix4x4.h"
#include <cassert>
#include <cstdio>

bool Near(float a, float b) { return std::abs(a-b) < 0.0002f; }
int main() {
    FPSMotion::Settings settings;
    for (int fps : {30, 60, 144}) {
        Transform t{{1,1,1}, {}, {0,0,0}};
        for (int i=0; i<fps; ++i) FPSMotion::Move(t, 0, 1, 1.0f/fps, settings);
        assert(Near(t.translate.z,5));
        assert(t.translate.y==0);
    }
    Transform straight{{1,1,1}, {}, {}}, diagonal=straight;
    FPSMotion::Move(straight,0,1,1,settings);
    FPSMotion::Move(diagonal,1,1,1,settings);
    assert(Near(std::hypot(diagonal.translate.x,diagonal.translate.z),straight.translate.z));
    for (float yaw : {0.0f, 0.5f, 1.5707963f, -1.2f}) {
        Transform t{{1,1,1}, {0,yaw,0}, {0,0,0}};
        const auto matrix = Matrix4x4::MakeAffineMatrix(t.scale,t.rotate,t.translate);
        FPSMotion::Move(t,0,1,1,settings);
        assert(Near(t.translate.x, matrix.m[2][0]*5));
        assert(Near(t.translate.z, matrix.m[2][2]*5));
    }
    Transform t{{1,1,1},{},{0,0,0}};
    FPSMotion::Look(t, 1000000,1000000,settings);
    assert(Near(t.rotate.x,89*std::numbers::pi_v<float>/180));
    assert(std::abs(t.rotate.y)<=std::numbers::pi_v<float>);
    FPSMotion::Move(t,0,1,1,settings);
    assert(t.translate.y==0 && Near(std::hypot(t.translate.x,t.translate.z),5));
    FPSMotion::Look(t,0,-1000000,settings);
    assert(Near(t.rotate.x,-89*std::numbers::pi_v<float>/180));
    Transform split{{1,1,1},{},{}}, whole=split;
    FPSMotion::Look(whole,200,100,settings);
    for(int i=0;i<10;++i) FPSMotion::Look(split,20,10,settings);
    assert(Near(split.rotate.x,whole.rotate.x) && Near(split.rotate.y,whole.rotate.y));
    const Vector3 before=t.translate;
    FPSMotion::Move(t,0,0,1,settings);
    assert(t.translate.x==before.x && t.translate.z==before.z);
    std::puts("FPS motion tests passed: timestep, diagonal speed, Camera yaw convention, pitch limits, horizontal motion, mouse delta.");
}
