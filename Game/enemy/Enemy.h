#pragma once
#include "Object3d.h"

// Uses only the Boss model resource; no legacy AI, attacks or animation playback.
class Enemy {
public:
    void Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera);
    void Update(float dt) { object_.Update(dt); }
    void Draw() { object_.Draw(); }
private:
    Object3d object_;
};
