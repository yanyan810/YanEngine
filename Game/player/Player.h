#pragma once
#include "Object3d.h"
#include "FPSMotion.h"
class Input;

class Player {
public:
    void Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera);
    void Update(const Input& input, float dt);
    const Transform& GetTransform() const { return transform_; }
    FPSMotion::Settings& Settings() { return settings_; }
private:
    Transform transform_{{1.0f, 1.0f, 1.0f}, {}, {3.0f, 0.0f, -6.0f}};
    FPSMotion::Settings settings_;
    Camera* camera_ = nullptr;
    Object3d object_; // Retained as a position marker; not drawn in first person.
};
