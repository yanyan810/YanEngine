#include "Player.h"
#include "Input.h"

void Player::Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera) {
    hp_ = 100.0f;
    camera_ = camera;
    object_.Initialize(common, dx);
    object_.SetCamera(camera);
    object_.SetModel("cube/cube.obj");
    object_.SetScale({0.5f, 1.0f, 0.5f});
    object_.SetIsVisible(false);
}
void Player::Update(const Input& input, float dt) {
    if (input.IsCameraControlEnabled() && input.HasFocus()) {
        const POINT mouse = input.GetMouseDelta();
        FPSMotion::Look(transform_, static_cast<float>(mouse.x), static_cast<float>(mouse.y), settings_);
        const float forward = static_cast<float>(input.IsKeyPressed(DIK_W)) - static_cast<float>(input.IsKeyPressed(DIK_S));
        const float right = static_cast<float>(input.IsKeyPressed(DIK_D)) - static_cast<float>(input.IsKeyPressed(DIK_A));
        FPSMotion::Move(transform_, right, forward, dt, settings_);
    }
    object_.SetTranslate(transform_.translate + Vector3{0.0f, 1.0f, 0.0f});
    object_.SetRotate({0.0f, transform_.rotate.y, 0.0f});
    camera_->SetTranslate(transform_.translate + Vector3{0.0f, settings_.cameraHeight, 0.0f});
    camera_->SetRotate(transform_.rotate);
    camera_->Update();
    object_.Update(dt);
}
