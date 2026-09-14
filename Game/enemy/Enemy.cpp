#include "Enemy.h"
void Enemy::Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera) {
    object_.Initialize(common, dx);
    object_.SetCamera(camera);
    object_.SetModel("enemy/boss/boss.gltf");
    object_.StopAnimation();
    object_.SetRotate({0.0f, 1.5707963f, 0.0f});
    object_.SetScale({2.0f, 2.0f, 2.0f});
    object_.SetTranslate({3.0f, 0.0f, 2.0f});
    object_.SetEnableLighting(1);
    object_.SetDirection({0.3f, -1.0f, 0.5f});
    object_.SetIntensity(1.0f);
    object_.SetPointLightIntensity(0.0f);
    object_.SetSpotLightIntensity(0.0f);
}

