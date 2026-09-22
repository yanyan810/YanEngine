#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Object3d.h"
#include "Player.h"
#include "Enemy.h"
#include "EnemySpawnSystem.h"
#include "EnemyProjectile.h"
#include "StageLoader.h"
#include "StageProjectile.h"
#include "Sprite.h"
#include "StageProgress.h"
#include "StageClearOverlay.h"
#include "WeaponHUD.h"

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
    void UpdateCombat(GameApp& app, float dt, bool wasCaptured);
    void OnStageClear(GameApp& app);

    void UpdateADS(const Input& input, float dt);

    StageLoader level_;
    bool stageLoaded_=false;
    bool showStageColliders_=false;
    StageProgress stage_;
    StageClearOverlay clearOverlay_;
    bool showGoalDebug_ = true;
    EnemyPartType lastHitPart_ = EnemyPartType::None;
    unsigned long long shotCount_ = 0;
    unsigned long long hitCount_ = 0;
    float lastDamage_ = 0.0f;
    WeaponSystem weapons_;
    WeaponHUD weaponHUD_;
    std::vector<std::unique_ptr<Object3d>> weaponVisuals_;
    std::mt19937 pelletRandom_{std::random_device{}()};
    inline static std::optional<WeaponRandomSettings> weaponSeedOverride_;
    bool initialCapturePending_ = true;
    int savedMouseFlags_ = 0;
    Camera camera_;
    Player player_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    EnemyDefinitions enemyDefinitions_;
    EnemyProjectileSystem enemyProjectiles_;
    std::vector<std::unique_ptr<Object3d>> projectileVisuals_;
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

    float adsBlend_ = 0.0f;

    // SceneをクリックしてFPS操作へ戻した時、
    // そのクリック長押しでFullAutoが始まるのを防止
    bool suppressFireUntilRelease_ = false;

};

