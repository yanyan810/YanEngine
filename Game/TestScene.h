#pragma once
#include "IScene.h"
#include <memory>
#include "WinApp.h"
#include "Matrix4x4.h"
#include "SpriteCommon.h"

#include "PlayerCombo.h"

#include "Sprite.h"

#include "Object3d.h"
#include <memory>
#include "LightingParam.h"



class Input;
class Camera;
class Player;

#include "Enemy.h" // ← EnemyManager を使うなら

class TestScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;

private:
    Input* input_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Player> player_;
    EnemyManager enemyMgr_;

    bool prevEsc_ = false;

    std::unique_ptr<Sprite> playTxst_;

    bool reachedEdge_ = false; // TestScene メンバ

    bool prevAtRight_ = false;

    std::unique_ptr<Object3d> ground_;

    std::unique_ptr<Object3d> skyDome_;

    // ===== Lighting params =====
    LightingParam light_;

    LightingParam groundLight_{};
    bool groundPointOnly_ = true;
    std::unique_ptr<Object3d> pointMarker_;   // ★ポイントライト位置の目印
    float pointMarkerScale_ = 0.25f;          // ★見た目サイズ
    bool drawPointMarker_ = false;             // ★表示ON/OFF

    // 追加：スポットライト用 UI
    bool groundSpotOnly_ = true;

    // Spot マーカー（ライト位置の表示）
    std::unique_ptr<Object3d> spotMarker_;
    float spotMarkerScale_ = 0.25f;
    bool drawSpotMarker_ = false;

};
