#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Object3d.h"
#include "Player.h"
#include "Enemy.h"
#include "EnemySpawnSystem.h"
#include "Sprite.h"

class GameScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw(GameApp&) override {}
    void DrawImGui(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
private:
    EnemyPartType lastHitPart_ = EnemyPartType::None;
    unsigned long long shotCount_ = 0;
    unsigned long long hitCount_ = 0;
    float lastDamage_ = 0.0f;
    static constexpr float kShotDamage = 25.0f;
    static constexpr float kShotRange = 100.0f;
    bool initialCapturePending_ = true;
    int savedMouseFlags_ = 0;
    Camera camera_;
    Player player_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    EnemySpawnSystem spawnSystem_;
    uint64_t nextEnemyId_ = 0;
    bool showSpawnDebug_ = true;
    int selectedEnemy_ = 0;
    int lastHitEnemy_ = -1;
    unsigned long long enemyAttackCount_ = 0;
    float playerDamagedFlash_ = 0;
    float lastEnemyDamage_ = 0;
    Object3d ground_;
    Sprite crosshairHorizontal_;
    Sprite crosshairVertical_;
};

