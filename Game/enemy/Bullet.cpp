#include "Bullet.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "Player.h"

#include <cmath>

void BulletManager::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    objCommon_ = objCommon;
    dx_ = dx;
    cam_ = cam;
    bullets_.clear();
}

void BulletManager::Spawn(const Vector3& pos, int dir, int damage) {
    SpawnDesc desc{};
    desc.position = pos;
    desc.velocity = { 6.0f * float(dir), 0.0f, 0.0f };
    desc.damage = damage;
    desc.lifeSec = 20.0f;
    Spawn(desc);
}

void BulletManager::Spawn(const SpawnDesc& desc) {
    Bullet b{};
    b.alive = true;
    b.pos = desc.position;
    b.vel = desc.velocity;
    b.damage = desc.damage;
    b.life = desc.lifeSec;
    b.halfSize = desc.halfSize;
    b.gravity = desc.gravity;
    b.homingStrength = desc.homingStrength;
    b.homing = desc.homing;
    b.damagePercent = desc.damagePercent;
    b.baseKnockback = desc.baseKnockback;
    b.knockbackScale = desc.knockbackScale;
    b.knockbackDir = desc.knockbackDir;
    b.hitStunSec = desc.hitStunSec;

    b.model = std::make_unique<Object3d>();
    b.model->Initialize(objCommon_, dx_);
    b.model->SetCamera(cam_);
    b.model->SetModel(desc.modelPath.empty() ? "enemy/shooter/bullet/bullet.obj" : desc.modelPath);
    b.model->SetUseEnvironmentMap(true);
    b.model->SetEnvironmentTexturePath("resources/skybox/skybox.dds");
    b.model->SetEnvironmentCoefficient(1.0f);

    // ★ここで初期見た目を確定（push_backの前！）
    b.model->SetTranslate(b.pos);
    b.model->SetScale(b.halfSize);
    b.model->Update(0.0f);

    UpdateBody_(b);

    bullets_.push_back(std::move(b)); // ←最後に入れる
}



void BulletManager::SpawnDebug_(const Vector3& pos, const Vector3& vel, int damage, float life) {
    Bullet b{};
    b.alive = true;
    b.pos = pos;
    b.vel = vel;
    b.damage = damage;
    b.life = life;

    b.model = std::make_unique<Object3d>();
    b.model->Initialize(objCommon_, dx_);
    b.model->SetCamera(cam_);
    b.model->SetModel("enemy/shooter/bullet/bullet.obj");
    b.model->SetUseEnvironmentMap(true);
    b.model->SetEnvironmentTexturePath("resources/skybox/skybox.dds");
    b.model->SetEnvironmentCoefficient(1.0f);

    const float s = 0.25f;
    b.model->SetTranslate(b.pos);
    b.model->SetScale({ s, s, s });
    b.model->Update(0.0f);

    UpdateBody_(b);
    bullets_.push_back(std::move(b));
}

void BulletManager::AppendDebugEntities(std::vector<DebugEntityState>& outEntities) const {
    int index = 0;
    for (const Bullet& bullet : bullets_) {
        if (!bullet.alive) {
            continue;
        }

        DebugEntityState state;
        state.id = "bullet_" + std::to_string(index++);
        state.category = "Bullet";
        state.type = "EnemyBullet";
        state.position = bullet.pos;
        state.velocity = bullet.vel;
        state.damage = bullet.damage;
        state.life = bullet.life;
        state.alive = true;
        outEntities.push_back(state);
    }
}

void BulletManager::RestoreDebugEntities(const std::vector<DebugEntityState>& entities) {
    bullets_.clear();

    for (const DebugEntityState& entity : entities) {
        if (entity.category != "Bullet" || !entity.alive) {
            continue;
        }

        SpawnDebug_(entity.position, entity.velocity, entity.damage, entity.life);
    }
}

void BulletManager::Update(float dt, Player& player) {
    // 1) 弾の更新

 

    for (auto& b : bullets_) {
        if (!b.alive) continue;

      //  b.model->Update(dt);

        b.life -= dt;
        if (b.life <= 0.0f) {
            b.alive = false;
            continue;
        }

        if (b.homing) {
            const Vector3 target = player.GetPos3D();
            Vector3 desired{ target.x - b.pos.x, target.y - b.pos.y, target.z - b.pos.z };
            const float desiredLength = std::sqrt(desired.x * desired.x + desired.y * desired.y + desired.z * desired.z);
            const float speed = std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y + b.vel.z * b.vel.z);
            if (desiredLength > 1.0e-5f && speed > 1.0e-5f) {
                desired.x = desired.x / desiredLength * speed;
                desired.y = desired.y / desiredLength * speed;
                desired.z = desired.z / desiredLength * speed;
                const float blend = std::clamp(b.homingStrength * dt, 0.0f, 1.0f);
                b.vel.x += (desired.x - b.vel.x) * blend;
                b.vel.y += (desired.y - b.vel.y) * blend;
                b.vel.z += (desired.z - b.vel.z) * blend;
            }
        }
        b.vel.y -= b.gravity * dt;

        // 移動
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.pos.z += b.vel.z * dt;

        UpdateBody_(b);

        if (b.model) {
            b.model->SetTranslate(b.pos);
            b.model->SetScale(b.halfSize);
            b.model->Update(dt);
        }

        // 2) プレイヤーに当たったら消す
        // Player 側に GetBodyAABB() がある前提（Enemy.cppでも使ってるのでOK）
        if (IntersectAABB_(b.body, player.GetBodyAABB())) {
            player.TriggerHitFlash(0.25f);
            player.Damage(b.damage);
            Vector3 dir = b.knockbackDir;
            // The projectile may move in Z, but its knockback stays in X/Y.
            dir.z = 0.0f;
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 1.0e-5f) {
                dir.x /= len;
                dir.y /= len;
                dir.z /= len;
                const float power = b.baseKnockback + player.GetDamagePercent() * b.knockbackScale;
                player.ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, b.hitStunSec);
            }
            b.alive = false;
        }
    }

    // 3) 死んだ弾を削除
    bullets_.erase(
        std::remove_if(bullets_.begin(), bullets_.end(),
            [](const Bullet& b) { return !b.alive; }),
        bullets_.end()
    );
}

void BulletManager::Draw() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        if (b.model) b.model->Draw();
    }
}


void BulletManager::UpdateBody_(Bullet& b) {
    // 足元基準ではなく弾の中心基準でAABB作る
    b.body.min = { b.pos.x - b.halfSize.x, b.pos.y - b.halfSize.y, b.pos.z - b.halfSize.z };
    b.body.max = { b.pos.x + b.halfSize.x, b.pos.y + b.halfSize.y, b.pos.z + b.halfSize.z };
}

bool BulletManager::IntersectAABB_(const AABB& a, const AABB& b) const {
    // 3D AABB
    if (a.max.x < b.min.x || a.min.x > b.max.x) return false;
    if (a.max.y < b.min.y || a.min.y > b.max.y) return false;
    if (a.max.z < b.min.z || a.min.z > b.max.z) return false;
    return true;
}
