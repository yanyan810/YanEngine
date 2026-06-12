#pragma once
#include <cstdint>
#include "Matrix4x4.h"

class Enemy;

class BossAI {
public:

    enum class Phase : uint8_t { P1, P2, P3 };
    enum class State : uint8_t {
        Wander,

        Drop_Windup,
        Drop_Fall,
        Drop_Land,

        Melee_Dash,
        Melee_Attack,
        Melee_Recover,

        Rush_ToRight,
        Rush_Charge,
        Rush_ExitLeft,
        Rush_Return,

        Super50,
        Super25,
    };

    void Reset(int maxHP);
    void Update(Enemy& e, float dt, const Vector2& playerXY, float playerZ);

    // Enemy側（見た目制御）用
    State GetState() const { return st_; }
    Phase GetPhase() const { return phase_; } // 必要なら
    float GetTime() const { return t_; }
    float GetStateTime() const { return stateTime_; }
    bool Did50() const { return did50_; }
    bool Did25() const { return did25_; }

    struct BossDebugState {
        State st = State::Wander;
        Phase phase = Phase::P1;
        float t = 0.0f;
        float stateTime = 0.0f;
        bool did50 = false;
        bool did25 = false;
        Vector3 wanderVel = {};
        float wanderChange = 0.0f;
        float moveMul = 1.0f;
        float dropStartY = 18.0f;
        float rushSpeed = 18.0f;
        float chaseSpeed = 1.0f;
        float rushZMin = -10.0f;
        float rushZMax = 20.0f;
    };

    BossDebugState GetDebugState() const {
        BossDebugState state;
        state.st = st_;
        state.phase = phase_;
        state.t = t_;
        state.stateTime = stateTime_;
        state.did50 = did50_;
        state.did25 = did25_;
        state.wanderVel = wanderVel_;
        state.wanderChange = wanderChange_;
        state.moveMul = moveMul_;
        state.dropStartY = dropStartY_;
        state.rushSpeed = rushSpeed_;
        state.chaseSpeed = chaseSpeed_;
        state.rushZMin = rushZMin_;
        state.rushZMax = rushZMax_;
        return state;
    }

    void RestoreDebugState(const BossDebugState& state) {
        st_ = state.st;
        phase_ = state.phase;
        t_ = state.t;
        stateTime_ = state.stateTime;
        did50_ = state.did50;
        did25_ = state.did25;
        wanderVel_ = state.wanderVel;
        wanderChange_ = state.wanderChange;
        moveMul_ = state.moveMul;
        dropStartY_ = state.dropStartY;
        rushSpeed_ = state.rushSpeed;
        chaseSpeed_ = state.chaseSpeed;
        rushZMin_ = state.rushZMin;
        rushZMax_ = state.rushZMax;
    }

private:
 

    int maxHP_ = 300;
    bool did50_ = false;
    bool did25_ = false;

    // タイマー
    float t_ = 0.0f;
    float stateTime_ = 0.0f;

    // Wander
    Vector3 wanderVel_{};
    float wanderChange_ = 0.0f;

    // マップ範囲（あなたの値に合わせて調整）
    float xMin_ = -18.0f;
    float xMax_ = 18.0f;
    float yMin_ = 0.0f;   // 地面
    float yMax_ = 8.0f;   // ふらつく高さ（必要なら）

    float moveMul_ = 1.0f;   // 移動速度倍率

    // Drop
    float dropStartY_ = 18.0f;

    // Rush
    float rushSpeed_ = 18.0f;

    float chaseSpeed_ = 1.0f;   // 追従の基本速度
    float chaseStopDist_ = 5.0f; // 近づきすぎない距離

    float rushZMin_ = -10.0f;
    float rushZMax_ = 20.0f;

	State st_ = State::Wander;
	Phase phase_ = Phase::P1;

private:
    void UpdatePhase_(Enemy& e);
    void ChangeState_(State s);

    // 行動
    void DoWander_(Enemy& e, float dt, const Vector2& playerXY, float PlayerZ);
    void DoDrop_(Enemy& e, float dt, const Vector2& playerXY);
    void DoMelee_(Enemy& e, float dt, const Vector2& playerXY);
    void DoRush_(Enemy& e, float dt);

    // 補助
    float Rand01_(); // 0..1
};
