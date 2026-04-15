#include "BossAI.h"
#include "Enemy.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

float BossAI::Rand01_() {
    return float(std::rand()) / float(RAND_MAX);
}

void BossAI::Reset(int maxHP) {
    maxHP_ = std::max(1, maxHP);
    phase_ = Phase::P1;
    st_ = State::Wander;
    t_ = 0.0f;
    stateTime_ = 0.0f;
    did50_ = false;
    did25_ = false;

    moveMul_ = 1.0f;

    wanderChange_ = 0.0f;
    wanderVel_ = { 0,0,0 };
}

void BossAI::ChangeState_(State s) {
    st_ = s;
    stateTime_ = 0.0f;
}

void BossAI::UpdatePhase_(Enemy& e) {
    const float hp = float(e.GetHP());
    const float ratio = hp / float(maxHP_);

    // 50%, 25% 大技を一回だけ
    if (!did50_ && ratio <= 0.50f) {
        did50_ = true;
        ChangeState_(State::Super50);
        return;
    }
    if (!did25_ && ratio <= 0.25f) {
        did25_ = true;
        ChangeState_(State::Super25);
        return;
    }

    if (ratio <= 0.25f) phase_ = Phase::P3;
    else if (ratio <= 0.50f) phase_ = Phase::P2;
    else phase_ = Phase::P1;
}

void BossAI::Update(Enemy& e, float dt, const Vector2& playerXY, float playerZ) {
    (void)playerZ;
    t_ += dt;
    stateTime_ += dt;

    // 閾値大技割り込み
    UpdatePhase_(e);

    switch (st_) {
    case State::Wander:
        DoWander_(e, dt, playerXY,playerZ);
        // 攻撃選択（追従なし）
        if (stateTime_ > 1.8f) {
            // フェーズで攻撃頻度UP
            float pDrop = (phase_ == Phase::P1) ? 0.45f : 0.35f;
            float pMelee = (phase_ == Phase::P1) ? 0.35f : 0.40f;
            float r = Rand01_();

            if (r < pDrop) ChangeState_(State::Drop_Windup);
            else if (r < pDrop + pMelee) ChangeState_(State::Melee_Dash);
            else ChangeState_(State::Rush_ToRight);
        }
        break;

    case State::Drop_Windup:
    case State::Drop_Fall:
    case State::Drop_Land:
        DoDrop_(e, dt, playerXY);
        break;

    case State::Melee_Dash:
    case State::Melee_Attack:
    case State::Melee_Recover:
        DoMelee_(e, dt, playerXY);
        break;

    case State::Rush_ToRight:
    case State::Rush_Charge:
    case State::Rush_ExitLeft:
    case State::Rush_Return:
        DoRush_(e, dt);
        break;

    case State::Super50:
        // 例：落下攻撃×3（あとで派手に）
        // とりあえず短い演出時間だけ確保
        if (stateTime_ < 0.3f) {
            // “溜め”中は止める
            e.SetVel({ 0,0,0 });
        } else {
            ChangeState_(State::Drop_Windup);
        }
        break;

    case State::Super25:
        // 例：突進2連発にするなど
        if (stateTime_ < 0.3f) e.SetVel({ 0,0,0 });
        else ChangeState_(State::Rush_ToRight);
        break;
    }
}

void BossAI::DoWander_(Enemy& e, float dt, const Vector2& playerXY, float playerZ) {
    Vector3 p = e.GetPos();

    // XY追従
    float dx = playerXY.x - p.x;
    float dy = playerXY.y - p.y;

    float dist = std::sqrtf(dx * dx + dy * dy);
    if (dist < 1e-4f) dist = 1e-4f;

    Vector3 v{ 0,0,0 };

    if (dist > chaseStopDist_) {
        float nx = dx / dist;
        float ny = dy / dist;
        float spd = chaseSpeed_ * moveMul_;
        v.x = nx * spd;
        v.y = ny * spd;
    }

    // Z追従（滑らかに寄せる）
    float dz = playerZ - p.z;
    v.z = dz * 3.0f; // 係数はお好みで（大きいほど追従が速い）

    e.SetVel(v);
}




void BossAI::DoDrop_(Enemy& e, float dt, const Vector2& playerXY) {
    Vector3 p = e.GetPos();
    Vector3 v = e.GetVel();

    switch (st_) {
    case State::Drop_Windup:
        // プレイヤーの上へワープして上空へ
        if (stateTime_ <= dt) { // 状態入った瞬間っぽくする（厳密じゃなくてOK）
            p.x = playerXY.x;
            p.y = dropStartY_;
            e.SetPos(p);   // SetPos がある前提（無ければ最初からこの攻撃は“現在位置から上に”でもOK）
            e.SetVel({ 0,0,0 });
        }
        if (stateTime_ > 0.35f) {
            ChangeState_(State::Drop_Fall);
        }
        break;

    case State::Drop_Fall:
        // 落下：重力は Enemy 側にもあるが、ここでは落下開始の初速だけ付与してもOK
        // ここでは強制的に下へ
        v.y = -18.0f;
        e.SetVel(v);

        // 地面に近づいたら着地へ（yMinを地面と仮定）
        if (p.y <= yMin_ + 0.05f) {
            ChangeState_(State::Drop_Land);
        }
        break;

    case State::Drop_Land:
        // 着地ダメージ判定を1回だけ出す
        e.SetVel({ 0,0,0 });

        if (stateTime_ <= dt) {
            // ★ 既存仕組みを流用：ConsumeMeleeRequest で大きい範囲にする（EnemyManager側でボスなら拡大）
            e.RequestMelee(MeleeKind::Land);
        }
        if (stateTime_ > 0.25f) ChangeState_(State::Wander);
        break;
    }
}

void BossAI::DoMelee_(Enemy& e, float dt, const Vector2& playerXY) {
    Vector3 p = e.GetPos();
    Vector3 v = e.GetVel();

    auto norm = [](float x, float y) {
        float l = std::sqrtf(x * x + y * y);
        if (l < 1e-4f) return std::pair{ 0.0f, 0.0f };
        return std::pair{ x / l, y / l };
        };

    switch (st_) {
    case State::Melee_Dash: {
        // 急接近（短時間だけ追う）
        auto [dx, dy] = norm(playerXY.x - p.x, playerXY.y - p.y);
        float spd = ((phase_ == Phase::P1) ? 10.0f : 13.0f) * moveMul_;

        v.x = dx * spd;
        v.y = dy * spd;
        e.SetVel(v);

        if (stateTime_ > 0.18f) ChangeState_(State::Melee_Attack);
    } break;

    case State::Melee_Attack:
        // 攻撃判定を1回
        e.SetVel({ 0,0,0 });
        if (stateTime_ <= dt) e.RequestMelee(MeleeKind::Normal);
        if (stateTime_ > 0.12f) ChangeState_(State::Melee_Recover);
        break;

    case State::Melee_Recover:
        // 硬直
        e.SetVel({ 0,0,0 });
        if (stateTime_ > 0.35f) ChangeState_(State::Wander);
        break;
    }
}

void BossAI::DoRush_(Enemy& e, float dt) {
    Vector3 p = e.GetPos();
    Vector3 v = e.GetVel();

    switch (st_) {
    case State::Rush_ToRight:
        // 右端へ移動
        v.x = +8.0f;
        v.y = 0.0f;
        e.SetVel(v);
        if (p.x >= xMax_ - 0.5f) ChangeState_(State::Rush_Charge);
        break;

    case State::Rush_Charge:
        // 溜め
        e.SetVel({ 0,0,0 });
        if (stateTime_ > 0.35f) ChangeState_(State::Rush_ExitLeft);
        break;

    case State::Rush_ExitLeft:
        // 左へ突進して画面外へ（xMinよりさらに外）
        v.x = -rushSpeed_;
        v.y = 0.0f;
        e.SetVel(v);

        // 突進中に当たったら痛い → ここも RequestMelee を短間隔で出す手もある
        if (stateTime_ > 0.05f && (int)(stateTime_ * 10) % 2 == 0) {
            e.RequestMelee(MeleeKind::Rush); // 当たり判定を連続で出す簡易版（life短めに）
        }

        if (p.x <= xMin_ - 6.0f) ChangeState_(State::Rush_Return);
        break;

    case State::Rush_Return:
        // 戻ってくる（左外→中央へ）
        v.x = +12.0f;
        v.y = 0.0f;
        e.SetVel(v);
        if (p.x >= 0.0f) ChangeState_(State::Wander);
        break;
    }
}
