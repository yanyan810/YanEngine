#pragma once
#include <memory>
#include <vector>
#include <cstdint>

#include "Vector3.h" // ← Vector2 / Vector3 がここにある
#include "AABB.h"    // ← AABB{ Vector3 min,max }
#include "Object3d.h"

#include "Player.h"
#include "Bullet.h"

#include "BossAI.h" 

#include "LightingParam.h"

class Object3dCommon;
class DirectXCommon;
class Camera;

enum class EnemyType : uint8_t {
    Melee,
    Shooter,
    Boss
};

enum class MeleeKind : uint8_t {
    Normal,   // 通常近接（敵の前）
    Land,     // ボス落下着地（中心・広め）
    Rush,     // ボス突進（前方広め or 連続用）
};


struct EnemyHitResult {
    bool hit = false;
    bool killed = false;
};

class Enemy {
public:
    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam,
        EnemyType type, const Vector3& spawnXYZ);

    void Update(float dt, const Vector2& playerXY, float playerZ);
    void Draw();

    bool IsAlive() const { return alive_; }
    bool IsBoss()  const { return type_ == EnemyType::Boss; }

    Vector3 GetPos3D() const { return pos_; }
    AABB    GetBodyAABB() const { return body_; }

    // プレイヤー攻撃ヒット時
    EnemyHitResult ApplyHit2D(float knockVx, float launchVy, bool requestHitstun, int damage);

    // ★Enemy側は「撃ってね」を出すだけ。生成はManager側でやる。
    bool ConsumeShootRequest(Vector3& outPos, int& outDir) {
        if (!requestShoot_) return false;
        requestShoot_ = false;
        outPos = shootMuzzlePos_;
        outDir = shootDir_;
        return true;
    }

    bool ConsumeMeleeRequest(MeleeKind& outKind) {
        if (!requestMeleeAttack_) return false;
        requestMeleeAttack_ = false;
        outKind = meleeKind_;
        return true;
    }

    void RequestMelee(MeleeKind kind) {
        requestMeleeAttack_ = true;
        meleeKind_ = kind;
    }

    // 互換用：古い呼び出しを壊さないため
    void RequestMelee() {
        RequestMelee(MeleeKind::Normal);
    }


    void SetDebugDrawMeleeHitbox(bool enable) { debugDrawMeleeHitbox_ = enable; }

    enum class EnemyModelSet : uint8_t {
        HumanWalk,
        HumanSneakWalk,
        GltfWalkGlb,
    };

public:
    // === BossAI 用の最小API ===
    EnemyType GetType() const { return type_; }
    int GetHP() const { return hp_; }
    void AddHP(int delta) { hp_ += delta; if (hp_ < 0) hp_ = 0; if (hp_ == 0) alive_ = false; }

    Vector3 GetPos() const { return pos_; }
    Vector3 GetVel() const { return vel_; }
    void SetVel(const Vector3& v) { vel_ = v; }
	void SetPos(const Vector3& p) { pos_ = p; }

    int GetFacing() const { return facing_; }

    float GetShootTimer() const { return shootTimer_; }
    void SetShootTimer(float t) { shootTimer_ = t; }

    // 弾を撃ってね要求（Managerが弾生成）
    void RequestShoot(const Vector3& muzzlePos, int dir) {
        requestShoot_ = true;
        shootMuzzlePos_ = muzzlePos;
        shootDir_ = dir;
    }

 
    void SetFrozen(bool f) { frozen_ = f; }
    bool IsFrozen() const { return frozen_; }

    //無敵用
    void SetInvincible(bool v) { invincible_ = v; }
    bool IsInvincible() const { return invincible_; }
    void SetAIDisabled(bool v) { aiDisabled_ = v; }

    void SetLighting(const LightingParam& p);

    EnemyModelSet currentModelSet_ = EnemyModelSet::HumanWalk;

    static const char* GetEnemyModelPath_(EnemyModelSet set) {
        switch (set) {
        case EnemyModelSet::HumanWalk:      return "human/walk.gltf";
        case EnemyModelSet::HumanSneakWalk: return "human/walk.gltf";
        case EnemyModelSet::GltfWalkGlb:    return "gltf/walk.glb";
        default:                            return "human/walk.gltf";
        }
    }

    void ChangeModelSet_(EnemyModelSet set) {
        if (!model_) return;
        if (currentModelSet_ == set) return; // ★同じなら何もしない

        currentModelSet_ = set;
        model_->SetModel(GetEnemyModelPath_(set));
        model_->PlayAnimation("", true);
    }

    int GetMaxHP() const { return maxHp_; }


private:
    void UpdateAI_Melee_(float dt, const Vector2& playerXY, float playerZ);
    void UpdateAI_Shooter_(float dt, const Vector2& playerXY, float playerZ);
    void UpdateAI_Boss_(float dt, const Vector2& playerXY, float playerZ);


    void ApplyPhysics_(float dt);
    void UpdateBody_();
    void UpdateModel_(float dt);


    void ChangeAnimIfChanged_(const char* name, bool loop);
    void StartOneShot_(const char* name, float lengthSec);


private:




    void SetModelIfChanged_(Model* m) {
        if (!model_ || !m) return;
        if (currentModel_ == m) return;   // ★同じなら何もしない
        currentModel_ = m;
        model_->SetModel(m);
    }



private:

    EnemyType type_ = EnemyType::Melee;
    bool alive_ = true;

    BossAI bossAI_;

    // 位置/速度（内部は3Dで持つが、Zは見た目だけ）
    Vector3 pos_{ 0,0,15 };
    Vector3 vel_{ 0,0,0 };

    float hitRadiusZ_ = 0.5f; // ★Z方向の当たり半径

    bool onGround_ = true;
    bool airborne_ = false;

    // ひるみ
    bool hitstun_ = false;
    float hitstunTime_ = 0.0f;

    int hp_ = 20;

    int maxHp_ = 20;

    // 見た目
    std::unique_ptr<Object3d> model_;
    Model* currentModel_ = nullptr;
    float zView_ = 15.0f;

    // 当たり判定（3D AABB だが、判定はXYだけ使う想定）
    AABB body_{};

    // AIパラメータ
    int facing_ = -1;
    float moveSpeed_ = 2.6f;
    float gravity_ = 25.0f;
    float depthSpeed_ = 8.0f;
    float zFollowDeadZone_ = 0.25f;

    
    // AIパラメータ
    float meleeRangeX_ = 1.3f;   
    float meleeRangeZ_ = 0.8f;   

    float meleeCooldown_ = 0.8f;
    float meleeTimer_ = 0.0f;

    float shootCooldown_ = 1.2f;
    float shootTimer_ = 0.0f;

    int damageTaken_ = 1;

    //AI
    // --- 状態 ---
    enum class MeleeState : uint8_t { Approach, Windup, Attack, Cooldown };
    enum class ShooterState : uint8_t { Retreat, Aim, Windup, Cooldown };

    MeleeState meleeState_ = MeleeState::Approach;
    ShooterState shooterState_ = ShooterState::Retreat;

    // --- 追加：Melee（止まってから殴る） ---
    float meleeWindupTime_ = 0.0f;   // 止まってから攻撃するまで
    float meleeAttackTime_ = 0.05f;   // 攻撃が出てる時間（判定を出すなら）
    float meleeWindup_ = 0.0f;
    float meleeAttack_ = 0.0f;
    bool  requestMeleeAttack_ = false; // ★近接攻撃を出してね要求

    // --- 追加：Shooter（距離を取って狙って撃つ） ---
    float shooterDesiredDist_ = 6.0f;   // ここまで離れたい
    float shooterDistEps_ = 0.0f;   // 距離許容
    float shooterAlignYEps_ = 0.25f;  // Y一致許容（ジャンプ等を考慮）

    float shootWindupTime_ = 1.0f;     // 溜めてから撃つ
    float shootWindup_ = 0.0f;

    bool requestShoot_ = false;          // ★弾を撃ってね要求
    Vector3 shootMuzzlePos_{};           // 発射位置
    int shootDir_ = +1;                  // 発射方向（+1右, -1左）

    //ボス用
    MeleeKind meleeKind_ = MeleeKind::Normal;

    bool debugDrawMeleeHitbox_ = true;
    std::unique_ptr<Object3d> debugHitboxCube_;


    //テスト用
    bool frozen_ = false;
    bool invincible_ = false;
    bool aiDisabled_ = false;

	// ヒットフラッシュ
    float hitFlashSec_ = 0.0f;
    Vector4 normalColor_{ 1,1,1,1 };
    Vector4 hitColor_{ 1,0.2f,0.2f,1 };

    //シューター用

    // === Shooter walk (OBJ差し替え) ===
    float walkAnimTime_ = 0.0f;
    static constexpr int   kWalkFrameCount_ = 5;
    static constexpr float kWalkFps_ = 8.0f; // 好みで 5～12 くらい

    Model* shooterWalkModels_[kWalkFrameCount_] = {};
    Model* meleeWalkModels_[5] = {};

    // --- 追加：モデル参照 ---
    Model* meleeAttackModels_[3] = { nullptr, nullptr, nullptr };
    Model* meleeDamageModel_ = nullptr;
    Model* shooterDamageModel_ = nullptr;

    // --- 追加：攻撃アニメ用タイマー ---
    float meleeAtkAnimTime_ = 0.0f;

    float damageScaleMul_ = 1.0f; // damageモデル専用倍率

    static constexpr int kBossIdleFrameCount = 3;

    Model* bossIdleModels_[kBossIdleFrameCount]{};
    float bossIdleAnimTime_ = 0.0f;
    static constexpr float kBossIdleFps_ = 8.0f;  // 好きに調整

    LightingParam light_;

    int lastWalkFrame_ = -1;
    int lastBossFrame_ = -1;
    int lastAtkFrame_ = -1;

    bool lockMove_ = false;

    private:


        // --- Melee(Gltf) アニメ切替用 ---
        MeleeState prevMeleeState_ = MeleeState::Approach;

        const char* meleeAnimIdle_ = "Idle"; // ←必要なら差し替え
        const char* meleeAnimWalk_ = "Walk";
        const char* meleeAnimAttack_ = "Attak_1";
        const char* meleeAnimDamage_ = "Damage";

        std::string currentAnim_;
        bool currentAnimLoop_ = true;

        // 1回だけ再生したい用（攻撃/被弾）
        float oneShotTimer_ = 0.0f;
        float oneShotLength_ = 0.0f;
        bool  oneShotPlaying_ = false;

        // --- Melee Attack(Attak_1) の長さ合わせ用 ---
        static constexpr int   kMeleeAttackFrames_ = 40;   // Attak_1 が 40f
        static constexpr float kAnimFps_ = 60.0f;          // あなたのゲームの想定FPS




        private:

            // --- Shooter(Gltf) アニメ切替用 ---
            ShooterState prevShooterState_ = ShooterState::Retreat;

            // ※enemyShooter.gltf に Idle が無いので、たぶんこれが待機
            const char* shooterAnimIdle_ = "Idle";
            const char* shooterAnimWalk_ = "Walk";
            const char* shooterAnimCharge_ = "Charge";
            const char* shooterAnimFire_ = "Fire";
            const char* shooterAnimDamage_ = "Damage";



};



// 管理
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

    void SetDebugDrawMeleeHitbox(bool enable) { debugDrawMeleeHitbox_ = enable; }

    void QueueSpawn(EnemyType type, float delaySec);

    bool IsBossDefeated() const { return bossDefeated_; }
    void ClearBossDefeatedFlag() { bossDefeated_ = false; } // 任意

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

    //回復
    struct HealDrop {
        Vector3 pos;
        float life = 10.0f;     // 消えるまでの時間（秒） 0以下で消滅
        float radius = 0.6f;    // 拾う判定の半径（XY）
        int amount = 100;         // 回復量
    };

    std::vector<HealDrop> healDrops_;

    // 調整用
    float healDropChance_ = 0.0f; // 35%で落ちる
    int healDropAmount_ = 10;

    // 乱数
    float Rand01_();
    void TrySpawnHealDrop_(const Enemy& e);
    void UpdateHealDrops_(float dt, Player& player);
    void DrawHealDrops_();

    struct PendingSpawn {
        EnemyType type;
        float t; // 残り秒
    };
    std::vector<PendingSpawn> pendingSpawns_;

    // 調整
    float spawnInterval_ = 1.0f;   // 1体ずつ湧く間隔（初期湧き用に使うなら）
    float respawnDelay_ = 10.0f;  // 倒されたら何秒後に追加するか

    size_t maxAlive_ = 6;

    // 内部
    Vector3 MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ);
    void UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ);
    float RandRange_(float a, float b);

    bool bossDefeated_ = false;

    LightingParam light_;


};
