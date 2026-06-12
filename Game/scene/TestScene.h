#pragma once
#include "IScene.h"
#include <memory>
#include "WinApp.h"
#include "Matrix4x4.h"
#include "SpriteCommon.h"


#include "Sprite.h"

#include "Object3d.h"
#include <memory>
#include "LightingParam.h"
#include "LevelLoader.h"
#include <vector>



class Input;
class Camera;
class Player;

#include "EnemyManager.h"

class TestScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override; // オフスクリーン（ポストエフェクト対象）
    void Draw3D(GameApp& app) override;     // バックバッファへ直接描く3D
    void Draw2D(GameApp& app) override;     // 2D / Sprite
    void Draw(GameApp& app) override;       // その他（空でOK）
    void DrawImGui(GameApp& app) override;

private:
    Input* input_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Player> player_;
    EnemyManager enemyMgr_;

    bool prevEsc_ = false;

    std::unique_ptr<Sprite> playTxst_;

    bool reachedEdge_ = false; // TestScene メンバ

    bool prevAtRight_ = false;
    bool enableEdgeTransition_ = false;
    bool bossAIEnabled_ = false;
    bool applyBossHitImmediately_ = true;
    bool resetFightersRequested_ = false;
    bool outOfBoundsEnabled_ = true;
    bool resetDamageOnOutOfBounds_ = false;
    float outLeftX_ = -26.0f;
    float outRightX_ = 26.0f;
    float outBottomY_ = -8.0f;
    Vector3 bossSpawnPos_{ 0.0f, 0.0f, 5.0f };
    Vector3 playerSpawnPos_{ -12.0f, 0.0f, 5.0f };
    Vector3 dropRespawnPos_{ 0.0f, 12.0f, 5.0f };

    std::unique_ptr<Object3d> ground_;

    std::unique_ptr<Object3d> skyDome_;
    std::unique_ptr<Object3d> knockbackPreviewLine_;
    bool drawKnockbackPreview_ = true;
    bool freezeKnockbackPreviewWhileLaunched_ = true;
    bool previewLineWasLaunched_ = false;
    bool previewUsesPlayerPercent_ = true;
    int previewLineMode_ = 0; // 0: actual distance, 1: launch velocity
    int previewAttackKind_ = 2;
    float previewPercent_ = 80.0f;
    float previewLineScale_ = 1.0f;
    float previewLineThickness_ = 0.08f;

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

    // ===== LevelLoader =====
    std::vector<std::unique_ptr<Object3d>> levelObjects_;

};
