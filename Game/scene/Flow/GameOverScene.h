#pragma once
#include "IScene.h"
#include "Sprite.h"

class GameOverScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void Draw(GameApp&) override {}
private:
    Sprite background_;
    Sprite logo_;
    Sprite pressSpace_;
    Matrix4x4 uiView_{};
    Matrix4x4 uiProjection_{};
};
