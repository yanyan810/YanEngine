#pragma once
#include <memory>
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
    };

    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);

    void Clear();
    void Spawn(EnemyType type, const Vector3& posXY);

    void Update(float dt, const Vector2& playerXY, float playerZ, Player& player);

    void Draw();

    std::vector<Enemy>& GetEnemies() { return enemies_; }
    const std::vector<Enemy>& GetEnemies() const { return enemies_; }

    void ApplyPlayerAttack(Player& player);
    void AppendDebugEnemyStates(std::vector<DebugEnemyState>& outStates) const;
    void RestoreDebugEnemyStates(const std::vector<DebugEnemyState>& states);

    void SetDebugDrawMeleeHitbox(bool enable) { debugDrawMeleeHitbox_ = enable; }

    void QueueSpawn(EnemyType type, float delaySec);

    bool IsBossDefeated() const { return bossDefeated_; }
    void ClearBossDefeatedFlag() { bossDefeated_ = false; } // 莉ｻ諢・

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

    std::vector<Enemy> enemies_;

    bool debugDrawMeleeHitbox_ = true;
    std::unique_ptr<Object3d> debugHitboxCube_;

    BulletManager bullets_;

    //蝗槫ｾｩ
    struct HealDrop {
        Vector3 pos;
        float life = 10.0f;     // 豸医∴繧九∪縺ｧ縺ｮ譎る俣・育ｧ抵ｼ・0莉･荳九〒豸域ｻ・
        float radius = 0.6f;    // 諡ｾ縺・愛螳壹・蜊雁ｾ・ｼ・Y・・
        int amount = 100;         // 蝗槫ｾｩ驥・
    };

    std::vector<HealDrop> healDrops_;

    // 隱ｿ謨ｴ逕ｨ
    float healDropChance_ = 0.0f; // 35%縺ｧ關ｽ縺｡繧・
    int healDropAmount_ = 10;

    // 荵ｱ謨ｰ
    float Rand01_();
    void TrySpawnHealDrop_(const Enemy& e);
    void UpdateHealDrops_(float dt, Player& player);
    void DrawHealDrops_();

    struct PendingSpawn {
        EnemyType type;
        float t; // 谿九ｊ遘・
    };
    std::vector<PendingSpawn> pendingSpawns_;

    // 隱ｿ謨ｴ
    float spawnInterval_ = 1.0f;   // 1菴薙★縺､貉ｧ縺城俣髫費ｼ亥・譛滓ｹｧ縺咲畑縺ｫ菴ｿ縺・↑繧会ｼ・
    float respawnDelay_ = 10.0f;  // 蛟偵＆繧後◆繧我ｽ慕ｧ貞ｾ後↓霑ｽ蜉縺吶ｋ縺・

    size_t maxAlive_ = 6;

    // 蜀・Κ
    Vector3 MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ);
    void UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ);
    float RandRange_(float a, float b);

    bool bossDefeated_ = false;

    LightingParam light_;


};
