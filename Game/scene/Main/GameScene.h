#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Object3d.h"
#include "Player.h"
#include "Enemy.h"

class GameScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw(GameApp&) override {}
    void DrawImGui(GameApp& app) override;
private:
    bool initialCapturePending_ = true;
    int savedMouseFlags_ = 0;
    Camera camera_;
    Player player_;
    Enemy enemy_;
    Object3d ground_;
};

