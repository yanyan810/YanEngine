#pragma once
#include <memory>
#include <string>
#include <vector>

#include "Enemy.h"
#include "Bullet.h"
#include "Player.h"
#include "LightingParam.h"
#include "DebugAI/DebugTypes.h"

class Object3dCommon;
class DirectXCommon;
class Camera;

// Enemy group ownership and battle-side collision/projectile orchestration.
class EnemyManager {
public:

    struct AABB3 {
        float x = 0, y = 0, z = 0; // center
        float hx = 0.5f, hy = 0.5f, hz = 0.5f;
    };

    struct MeleeHitbox {
        AABB3 box;
        float life;
        int damage = 5;
        bool fromBoss = false;
        MeleeKind kind = MeleeKind::Normal;
        Vector3 attackerPos = {};
        int facing = 1;
    };

    struct BossHitTuning {
        float damagePercent = 10.0f;
        float baseKnockback = 10.0f;
        float knockbackScale = 0.08f;
        Vector3 knockbackDir = { 1.0f, 0.35f, 0.0f };
        float hitStunSec = 0.35f;
    };

    struct PlayerAttackHitEvent {
        std::string targetId;
        std::string targetType;
        unsigned int attackSerial = 0;
        int damage = 0;
        int hpBefore = 0;
        int hpAfter = 0;
        Vector3 playerPosition = {};
        Vector3 targetPosition = {};
    };

    struct BossAttackEffectEvent {
        MeleeKind kind = MeleeKind::Normal;
        Vector3 position = {};
    };

    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);

    void Clear();
    void Spawn(EnemyType type, const Vector3& posXY);

    	void Update(float dt, const Vector2& playerXY, float playerZ, Player& player, bool disablePendingSpawn = false);

    void Draw();

    std::vector<Enemy>& GetEnemies() { return enemies_; }
    const std::vector<Enemy>& GetEnemies() const { return enemies_; }

    std::vector<PlayerAttackHitEvent> ApplyPlayerAttack(Player& player);
    std::vector<BossAttackEffectEvent> ConsumeBossAttackEffectEvents();
    void AppendDebugEntities(std::vector<DebugEntityState>& outEntities) const;
    void RestoreDebugEntities(const std::vector<DebugEntityState>& entities);

    void SetDebugDrawMeleeHitbox(bool enable) { debugDrawMeleeHitbox_ = enable; }
    BossHitTuning& BossTuning(MeleeKind kind);
    const BossHitTuning& BossTuning(MeleeKind kind) const;

    void QueueSpawn(EnemyType type, float delaySec);
    void SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides);

    bool IsBossDefeated() const { return bossDefeated_; }
    void ClearBossDefeatedFlag() { bossDefeated_ = false; }

    void SetLighting(const LightingParam& p);

    Enemy* GetBoss() {
        for (auto& e : enemies_) {
            if (e.GetType() == EnemyType::Boss && e.IsAlive()) return &e;
        }
        return nullptr;
    }
    const Enemy* GetBoss() const {
        for (auto& e : enemies_) {
            if (e.GetType() == EnemyType::Boss && e.IsAlive()) return &e;
        }
        return nullptr;
    }


private:
    Object3dCommon* objCommon_ = nullptr;
    DirectXCommon* dx_ = nullptr;
    Camera* cam_ = nullptr;

    std::vector<MeleeHitbox> meleeHitboxes_;
    std::vector<BossAttackEffectEvent> bossAttackEffectEvents_;
    BossHitTuning bossNormalTuning_{ 5.0f, 4.0f, 0.03f, { 0.4f, 0.15f, 0.0f }, 0.20f };
    BossHitTuning bossLandTuning_{ 16.0f, 12.0f, 0.10f, { 0.8f, 0.60f, 0.0f }, 0.45f };
    BossHitTuning bossRushTuning_{ 12.0f, 10.0f, 0.08f, { 1.0f, 0.35f, 0.0f }, 0.35f };

    std::vector<Enemy> enemies_;

    bool debugDrawMeleeHitbox_ = true;
    std::unique_ptr<Object3d> debugHitboxCube_;

    BulletManager bullets_;

    struct HealDrop {
        Vector3 pos;
        float life = 10.0f;
        float radius = 0.6f;
        int amount = 100;
    };

    std::vector<HealDrop> healDrops_;

    float healDropChance_ = 0.0f;
    int healDropAmount_ = 10;

    float Rand01_();
    void TrySpawnHealDrop_(const Enemy& e);
    void UpdateHealDrops_(float dt, Player& player);
    void DrawHealDrops_();

    struct PendingSpawn {
        EnemyType type;
        float t;
    };
    std::vector<PendingSpawn> pendingSpawns_;
    std::vector<DebugSpawnOverride> replaySpawnOverrides_;

    float spawnInterval_ = 1.0f;
    float respawnDelay_ = 10.0f;

    size_t maxAlive_ = 6;

    Vector3 MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ);
    void UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ);
    float RandRange_(float a, float b);

    bool bossDefeated_ = false;

    LightingParam light_;


};
