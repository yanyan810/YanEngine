#include "PlayerAttackI.h"

#include "PlayerAttackIInternal.h"

#include "Player.h"

using namespace PlayerIAttackInternal;

// ===== 必殺技（Special）の初期化・コマンド構築 =====
void Player::InitializeIAttackDefinitions_() {
}

bool Player::BuildIAttackCommand_(PlayerInputCommand& command) const {
    command.action = PlayerAction::Attack;
    if (command.horizontal != 0) {
        command.attackType = PlayerAttackType::SideSpecial;
    } else if (command.depth > 0) {
        command.attackType = PlayerAttackType::UpSpecial;
    } else if (command.down || command.depth < 0) {
        command.attackType = PlayerAttackType::DownSpecial;
    } else {
        command.attackType = PlayerAttackType::NeutralSpecial;
    }
    return true;
}

// ===== 必殺技（Special）のヒットボックス・ダメージ・ヒットストップクエリ =====
bool Player::GetIAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const {
    if (attackType_ == PlayerAttackType::NeutralSpecial) {
        return PlayerIAttack::GetNeutralSpecialHitBox(*this, outCenter, outHalfSize);
    }

    if (attackType_ == PlayerAttackType::SideSpecial) {
        return PlayerIAttack::GetSideSpecialHitBox(*this, outCenter, outHalfSize);
    }
    if (attackType_ == PlayerAttackType::UpSpecial) {
        return PlayerIAttack::GetUpSpecialHitBox(*this, outCenter, outHalfSize);
    }
    if (attackType_ == PlayerAttackType::DownSpecial) {
        return PlayerIAttack::GetDownSpecialHitBox(*this, outCenter, outHalfSize);
    }

    return false;
}

int Player::GetIAttackDamage_() const {
    if (attackType_ == PlayerAttackType::NeutralSpecial) {
        return PlayerIAttack::GetNeutralSpecialDamage(*this);
    }
    if (attackType_ == PlayerAttackType::SideSpecial) {
        return PlayerIAttack::GetSideSpecialDamage(*this);
    }
    if (attackType_ == PlayerAttackType::UpSpecial) {
        return PlayerIAttack::GetUpSpecialDamage(*this);
    }
    if (attackType_ == PlayerAttackType::DownSpecial) {
        return PlayerIAttack::GetDownSpecialDamage(*this);
    }
    return 0;
}

float Player::GetCurrentSpecialHitStopRate() const {
    if (!IsIAttackType_(attackType_)) {
        return 1.0f;
    }
    return GetCurrentCancelTuning(*this).hitStopRate;
}

bool Player::IsSideSpecialLv3AttackActive() const {
    return attackType_ == PlayerAttackType::SideSpecial &&
        iSpecialVariant_ == PlayerISpecialVariant::Lv3 &&
        iAttackState_ == PlayerIAttackState::SideSlide_Move &&
        iAttackHitActive_;
}

// ===== 必殺技（Special）の開始・更新・ステート管理（Player本体側ラッパー） =====
void Player::StartIAttack_(PlayerAttackType type) {
    if (type == PlayerAttackType::NeutralSpecial) {
        PlayerIAttack::StartNeutralSpecial(*this);
        return;
    }
    if (type == PlayerAttackType::SideSpecial) {
        PlayerIAttack::StartSideSpecial(*this);
        return;
    }
    if (type == PlayerAttackType::UpSpecial) {
        PlayerIAttack::StartUpSpecial(*this);
        return;
    }
    if (type == PlayerAttackType::DownSpecial) {
        PlayerIAttack::StartDownSpecial(*this);
        return;
    }

    ChangeIAttackState_(PlayerIAttackState::None);
}

void Player::UpdateIAttack_(float dt) {
    if (attackType_ == PlayerAttackType::NeutralSpecial) {
        PlayerIAttack::UpdateNeutralSpecial(*this, dt);
        return;
    }
    if (attackType_ == PlayerAttackType::SideSpecial) {
        PlayerIAttack::UpdateSideSpecial(*this, dt);
        return;
    }
    if (attackType_ == PlayerAttackType::UpSpecial) {
        PlayerIAttack::UpdateUpSpecial(*this, dt);
        return;
    }
    if (attackType_ == PlayerAttackType::DownSpecial) {
        PlayerIAttack::UpdateDownSpecial(*this, dt);
    }
}

void Player::ChangeIAttackState_(PlayerIAttackState state) {
    PlayerIAttack::ChangeState(*this, state);
}

void PlayerIAttack::StartNeutralSpecial(Player& player) {
    PlayerINeutralSpecial::StartNeutralSpecial(player);
}

void PlayerIAttack::StartSideSpecial(Player& player) {
    PlayerISideSpecial::StartSideSpecial(player);
}

void PlayerIAttack::StartUpSpecial(Player& player) {
    PlayerIUpSpecial::StartUpSpecial(player);
}

void PlayerIAttack::StartDownSpecial(Player& player) {
    PlayerIDownSpecial::StartDownSpecial(player);
}

void PlayerIAttack::UpdateNeutralSpecial(Player& player, float dt) {
    PlayerINeutralSpecial::UpdateNeutralSpecial(player, dt);
}

void PlayerIAttack::UpdateSideSpecial(Player& player, float dt) {
    PlayerISideSpecial::UpdateSideSpecial(player, dt);
}

void PlayerIAttack::UpdateUpSpecial(Player& player, float dt) {
    PlayerIUpSpecial::UpdateUpSpecial(player, dt);
}

void PlayerIAttack::UpdateDownSpecial(Player& player, float dt) {
    PlayerIDownSpecial::UpdateDownSpecial(player, dt);
}

bool PlayerIAttack::GetNeutralSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    return PlayerINeutralSpecial::GetNeutralSpecialHitBox(player, outCenter, outHalfSize);
}

bool PlayerIAttack::GetSideSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    return PlayerISideSpecial::GetSideSpecialHitBox(player, outCenter, outHalfSize);
}

bool PlayerIAttack::GetUpSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    return PlayerIUpSpecial::GetUpSpecialHitBox(player, outCenter, outHalfSize);
}

bool PlayerIAttack::GetDownSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    return PlayerIDownSpecial::GetDownSpecialHitBox(player, outCenter, outHalfSize);
}

int PlayerIAttack::GetNeutralSpecialDamage(const Player& player) {
    return PlayerINeutralSpecial::GetNeutralSpecialDamage(player);
}

int PlayerIAttack::GetSideSpecialDamage(const Player& player) {
    return PlayerISideSpecial::GetSideSpecialDamage(player);
}

int PlayerIAttack::GetUpSpecialDamage(const Player& player) {
    return PlayerIUpSpecial::GetUpSpecialDamage(player);
}

int PlayerIAttack::GetDownSpecialDamage(const Player& player) {
    return PlayerIDownSpecial::GetDownSpecialDamage(player);
}

// ===== 必殺技ステートの遷移処理 =====
void PlayerIAttack::ChangeState(Player& player, PlayerIAttackState state) {
    player.iAttackState_ = state;
    player.iAttackStateTime_ = 0.0f;
    player.iAttackHitActive_ = state == PlayerIAttackState::SideSlide_Move;
    if (state == PlayerIAttackState::None) {
        player.iSpecialVariant_ = PlayerISpecialVariant::Lv0;
        player.iSpecialPulseIndex_ = 0;
    }
}
