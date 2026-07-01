#include "PlayerAttackI.h"

#include "Player.h"

#include <algorithm>

namespace {
constexpr float kSideSlideMinChargeSec = 0.08f;
constexpr float kSideSlideMaxChargeSec = 0.75f;
constexpr float kSideSlideMoveSec = 0.18f;
constexpr float kSideSlideRecoverSec = 0.22f;
constexpr float kSideSlideSpeed = 24.0f;
constexpr float kSideSlideChargedSpeedAdd = 12.0f;
constexpr float kSideLockOnPierceSpeed = 34.0f;
constexpr float kSideLockOnPierceSec = 0.42f;
constexpr int kSideSlideBaseDamage = 18;
constexpr int kSideSlideChargedDamageAdd = 10;
constexpr float kUpRiseWindupSec = 0.06f;
constexpr float kUpRiseMoveSec = 0.32f;
constexpr float kUpRiseRecoverSec = 0.20f;
constexpr float kUpRiseSpeedY = 18.0f;
constexpr float kUpRiseSpeedX = 4.0f;
constexpr float kDownCounterActiveSec = 0.35f;
constexpr float kDownCounterSuccessSec = 0.20f;
constexpr float kDownCounterRecoverSec = 0.32f;
constexpr float kNeutralFinishActiveSec = 0.14f;
constexpr float kNeutralFinishRecoverSec = 0.24f;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

}

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
        return PlayerIAttack::GetNeutralSpecialDamage();
    }
    if (attackType_ == PlayerAttackType::SideSpecial) {
        return PlayerIAttack::GetSideSpecialDamage(*this);
    }
    if (attackType_ == PlayerAttackType::UpSpecial) {
        return PlayerIAttack::GetUpSpecialDamage();
    }
    if (attackType_ == PlayerAttackType::DownSpecial) {
        return PlayerIAttack::GetDownSpecialDamage(*this);
    }
    return 0;
}

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
    player.iSpecialChargeSec_ = 0.0f;
    ChangeState(player, PlayerIAttackState::NeutralFinish_Active);
}

void PlayerIAttack::StartSideSpecial(Player& player) {
    player.iSpecialChargeSec_ = 0.0f;
    if (player.sideSpecialLockOnActive_) {
        player.onGround_ = false;
        player.iSpecialChargeSec_ = kSideSlideMaxChargeSec;
        ChangeState(player, PlayerIAttackState::SideSlide_Move);
        return;
    }
    ChangeState(player, PlayerIAttackState::SideSlide_Windup);
}

void PlayerIAttack::StartUpSpecial(Player& player) {
    player.iSpecialChargeSec_ = 0.0f;
    player.onGround_ = false;
    ChangeState(player, PlayerIAttackState::UpRise_Windup);
}

void PlayerIAttack::StartDownSpecial(Player& player) {
    player.iCounterSuccess_ = false;
    player.iSpecialChargeSec_ = 0.0f;
    player.vel_.x = 0.0f;
    player.vel_.z = 0.0f;
    ChangeState(player, PlayerIAttackState::DownCounter_Active);
}

void PlayerIAttack::UpdateNeutralSpecial(Player& player, float dt) {
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= kNeutralFinishActiveSec) {
            ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kNeutralFinishRecoverSec) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerIAttack::UpdateSideSpecial(Player& player, float dt) {
    player.iAttackStateTime_ += dt;

    switch (player.iAttackState_) {
    case PlayerIAttackState::SideSlide_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        player.iSpecialChargeSec_ = std::min(kSideSlideMaxChargeSec, player.iSpecialChargeSec_ + dt);
        if ((!player.latestSpecialHeld_ && player.iAttackStateTime_ >= kSideSlideMinChargeSec) ||
            player.latestSpecialReleased_ ||
            player.iSpecialChargeSec_ >= kSideSlideMaxChargeSec) {
            player.onGround_ = false;
            ChangeState(player, PlayerIAttackState::SideSlide_Move);
        }
        break;

    case PlayerIAttackState::SideSlide_Move:
    {
        if (player.sideSpecialLockOnActive_) {
            player.onGround_ = false;
            player.vel_.x = player.sideSpecialLockOnDirection_.x * kSideLockOnPierceSpeed;
            player.vel_.y = player.sideSpecialLockOnDirection_.y * kSideLockOnPierceSpeed;
            player.vel_.z = player.sideSpecialLockOnDirection_.z * kSideLockOnPierceSpeed;
            player.iAttackHitActive_ = true;
            if (player.iAttackStateTime_ >= kSideLockOnPierceSec) {
                ChangeState(player, PlayerIAttackState::SideSlide_Recover);
            }
            break;
        }
        const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
        player.vel_.x = static_cast<float>(player.facing_) *
            (kSideSlideSpeed + kSideSlideChargedSpeedAdd * charge);
        player.vel_.z = 0.0f;
        player.vel_.y = std::max(player.vel_.y, 1.5f);
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= kSideSlideMoveSec) {
            ChangeState(player, PlayerIAttackState::SideSlide_Recover);
        }
    } break;

    case PlayerIAttackState::SideSlide_Recover:
        if (!player.sideSpecialHitBounceUsed_ && !player.sideSpecialLockOnActive_) {
            player.vel_.x = 0.0f;
            player.vel_.z = 0.0f;
        }
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kSideSlideRecoverSec) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.sideSpecialLockOnActive_ = false;
            player.action_ = Player::PlayerAction::Jump;
            ChangeState(player, PlayerIAttackState::None);
        }
        break;

    case PlayerIAttackState::None:
    default:
        player.iAttackHitActive_ = false;
        break;
    }
}

void PlayerIAttack::UpdateUpSpecial(Player& player, float dt) {
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kUpRiseWindupSec) {
            ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        player.onGround_ = false;
        player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX;
        player.vel_.y = kUpRiseSpeedY;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= kUpRiseMoveSec) {
            ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kUpRiseRecoverSec) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerIAttack::UpdateDownSpecial(Player& player, float dt) {
    player.iAttackStateTime_ += dt;
    player.vel_.x = 0.0f;
    player.vel_.z = 0.0f;

    switch (player.iAttackState_) {
    case PlayerIAttackState::DownCounter_Active:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kDownCounterActiveSec) {
            ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Success:
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= kDownCounterSuccessSec) {
            ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kDownCounterRecoverSec) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            player.iCounterSuccess_ = false;
            ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

bool PlayerIAttack::GetNeutralSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    outHalfSize = { 0.75f, 0.85f, 0.55f };
    outCenter = {
        player.pos_.x + 1.00f * static_cast<float>(player.facing_),
        player.pos_.y + outHalfSize.y,
        player.pos_.z
    };
    return true;
}

bool PlayerIAttack::GetSideSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }

    const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
    outHalfSize = { 1.25f + 0.35f * charge, 0.85f, 0.65f };
    outCenter = {
        player.pos_.x + 1.55f * static_cast<float>(player.facing_),
        player.pos_.y + outHalfSize.y,
        player.pos_.z
    };
    return true;
}

bool PlayerIAttack::GetUpSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    outHalfSize = { 0.85f, 1.10f, 0.65f };
    outCenter = {
        player.pos_.x + 0.25f * static_cast<float>(player.facing_),
        player.pos_.y + 1.20f,
        player.pos_.z
    };
    return true;
}

bool PlayerIAttack::GetDownSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    outHalfSize = { 1.10f, 0.95f, 0.70f };
    outCenter = {
        player.pos_.x + 0.75f * static_cast<float>(player.facing_),
        player.pos_.y + 0.95f,
        player.pos_.z
    };
    return true;
}

int PlayerIAttack::GetNeutralSpecialDamage() {
    return 14;
}

int PlayerIAttack::GetSideSpecialDamage(const Player& player) {
    const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
    return kSideSlideBaseDamage +
        static_cast<int>(kSideSlideChargedDamageAdd * charge);
}

int PlayerIAttack::GetUpSpecialDamage() {
    return 12;
}

int PlayerIAttack::GetDownSpecialDamage(const Player& player) {
    return player.iCounterSuccess_ ? 22 : 0;
}

void PlayerIAttack::ChangeState(Player& player, PlayerIAttackState state) {
    player.iAttackState_ = state;
    player.iAttackStateTime_ = 0.0f;
    player.iAttackHitActive_ = state == PlayerIAttackState::SideSlide_Move;
}
