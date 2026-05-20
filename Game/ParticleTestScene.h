#pragma once

#include "IScene.h"

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
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Object3d> ground_;
    std::unique_ptr<Particle> editorParticle_;

    float reloadCooldown_ = 0.0f;
};
