#pragma once
#include "Matrix4x4.h"
#include <algorithm>
#include <cmath>
#include <dinput.h>
#include "Object3d.h"
#include <unordered_set>

class Input;
class EnemyManager;
class Enemy;

struct AABB2 {
    float x = 0, y = 0;     // center
    float hx = 0.5f, hy = 0.5f;
};
inline bool Intersect(const AABB2& a, const AABB2& b) {
    return (std::abs(a.x - b.x) <= (a.hx + b.hx)) &&
        (std::abs(a.y - b.y) <= (a.hy + b.hy));
}


struct AABB3 {
    float x = 0, y = 0, z = 0; // center
    float hx = 0.5f, hy = 0.5f, hz = 0.5f;
};
enum class AttackBtn { Weak, Strong };

enum class AttackType {
    None,
    I, // 速攻
    O, // 重攻
};

// 技データ（後でImGuiで調整しやすい形）
struct AttackData {
    float duration = 0.35f;
    float hitStart = 0.08f;
    float hitEnd = 0.18f;

    float chainOpen = 999.0f;
    float chainClose = -999.0f;

    float knockX = 6.0f;
    float launchY = 7.0f;

    float hbOffX = 0.9f;
    float hbOffY = 0.0f;
    float hbHalfX = 0.6f;
    float hbHalfY = 0.5f;

    int damage = 1;

    float hitZ = 0.5f; // ★Z方向の半幅（奥行き）
    bool airFloatOnHit = true; // 空中ヒット中浮遊
};

class PlayerCombo {
public:
    void Reset();

    void Start(AttackType type);

    // ★ Player::Update から呼ぶ（呼び出し側はそのまま enemyMgr を渡せる）
    void Update(float dt,
        const Input& in,
        Vector2& playerPos, Vector2& playerVel,
        bool onGround,
        int facing,                  // +1右 / -1左
        float playerZ,
        EnemyManager& enemyMgr);

    bool IsAttacking() const { return attacking_; }

    // ★現在の攻撃の全体時間（秒）を取得（I/Oの強制仕様にも追従）
    float GetCurrentAttackDuration() const {
        return GetData_(attackAir_, step_, curBtn_).duration;
    }

    // ★攻撃開始前のプレビュー（空中/段数/ボタンに応じた全体時間）
    float PreviewAttackDuration(bool airborne, int step, AttackBtn btn) const {
        return GetData_(airborne, step, btn).duration;
    }

    bool GetDebugHitBox(AABB2& out) const {
        if (!debugHbValid_) return false;
        out = debugHb_;
        return true;
    }

    bool GetDebugHitBox3(AABB3& out) const {
        if (!debugHb3Valid_) return false;
        out = debugHb3_;
        return true;
    }

    // ★アニメ用：いまの攻撃状態を外に出す
    struct AttackAnimState {
        bool attacking = false;
        AttackBtn btn = AttackBtn::Weak;
        int step = 0;
        float t = 0.0f;         // 攻撃経過時間
        AttackData data{};      // duration/hitStart/hitEnd など
    };

    bool GetAnimState(AttackAnimState& out) const {

        out = {};

        if (!attacking_) return false;
        out.attacking = true;
        out.btn = curBtn_;
        out.step = step_;
        out.t = t_;

        // ★Updateで実際に使ったデータを返す
        if (lastDataValid_) out.data = lastData_;
        else out.data = GetData_(attackAir_, step_, curBtn_); // 保険

        return true;
    }


#ifdef USE_IMGUI
    void DebugImGui();
#endif


private:
    // 入力バッファ
    struct BufItem { AttackBtn btn; float life; };
    std::vector<BufItem> buf_;
    float bufKeep_ = 0.25f;

    bool attacking_ = false;
    bool attackAir_ = false;
    int  step_ = 0;        // 0..2（3段）
    float t_ = 0.0f;
    AttackBtn curBtn_ = AttackBtn::Weak;

    // ★攻撃開始時に固定する方向（+1上 / 0横 / -1下）
    int startDirY_ = 0;

    // デバッグ可視化用（今フレームのヒットボックス）
    bool  debugHbValid_ = false;
    AABB2 debugHb_{};

    bool  debugHb3Valid_ = false;
    AABB3 debugHb3_{};                // ★AABB3 を保存する（こっちが本体）


    AttackType attackType_ = AttackType::None;

    AttackData lastData_{};
    bool lastDataValid_ = false;

    std::unordered_set<const Enemy*> hitSet_;

private:
    void Push_(AttackBtn b);
    bool Pop_(AttackBtn& out);
    void UpdateBuf_(float dt);

    int ReadDirY_(const Input& in) const;                 // ↑↔↓
    AttackData GetData_(bool airborne, int step, AttackBtn btn) const;
    AABB2 MakeHitBox_(const Vector2& p, int facing, const AttackData& a) const;

    // Enemy の AABB を 2D に変換
    AABB2 MakeEnemyBody2D_(const Enemy& e) const;

    void StartAttack_(bool airborne, AttackBtn btn, int dirY);
  //  void NextStep_(bool airborne, AttackBtn btn);

    // ===== O(Strong) 調整（ImGuiからいじる用）=====
    AttackData tuningO_{}; // O用の上書きデータ

    static AttackData DefaultO_() {
        AttackData a{};
        a.duration = 1.20f;
        a.hitStart = 0.40f;
        a.hitEnd = 1.0f;

        a.chainOpen = 999.0f;
        a.chainClose = -999.0f;

        a.hbHalfX = 1.9f;
        a.hbHalfY = 1.3f;
        a.hbOffX = 1.6f;
        a.hbOffY = 0.7f;

        a.hitZ = 2.5f;
        a.damage = 20;

        return a;
    }


};