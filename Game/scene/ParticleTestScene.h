#pragma once

#include "IScene.h"
#include "Vector3.h"

#include <memory>

class Camera;
class Object3d;
class Particle;

class ParticleTestScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw3D(GameApp& app) override;
    void Draw2D(GameApp& app) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    void EnsureHitEffectGroup_();
    void ReloadParticleJson_();
    void SpawnHitEffectPreview_();

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Object3d> ground_;
    std::unique_ptr<Particle> editorParticle_;

    char hitEffectGroupName_[64] = "HitEffect";
    int hitEffectSpawnCount_ = 24;
    Vector3 hitEffectSpawnPosition_{ 0.0f, 1.0f, 0.0f };
    float reloadCooldown_ = 0.0f;
};
