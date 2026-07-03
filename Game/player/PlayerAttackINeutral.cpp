#include "PlayerAttackIInternal.h"

using namespace PlayerIAttackInternal;

// ===== N必殺技 =====
void PlayerINeutralSpecial::StartNeutralSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::NeutralSpecial, player.specialCancelCount_);
    player.iSpecialVariant_ = VariantFromCancelCount(player.specialCancelCount_);
    player.iSpecialPulseIndex_ = 0;
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iSpecialChargeSec_ = 0.0f;
    PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Active);
}

void PlayerINeutralSpecial::UpdateNeutralSpecial(Player& player, float dt) {
    switch (player.iSpecialVariant_) {
    case PlayerISpecialVariant::Lv1:
        UpdateNeutralSpecialLv1(player, dt);
        break;
    case PlayerISpecialVariant::Lv2:
        UpdateNeutralSpecialLv2(player, dt);
        break;
    case PlayerISpecialVariant::Lv3:
        UpdateNeutralSpecialLv3(player, dt);
        break;
    case PlayerISpecialVariant::Lv0:
    default:
        UpdateNeutralSpecialLv0(player, dt);
        break;
    }
}

void PlayerINeutralSpecial::UpdateNeutralSpecialLv0(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kNeutralFinishActiveSec, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralFinishRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerINeutralSpecial::UpdateNeutralSpecialLv1(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.vel_.x = static_cast<float>(player.facing_) * 5.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kNeutralFinishActiveSec * 1.15f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralFinishRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerINeutralSpecial::UpdateNeutralSpecialLv2(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        if (player.iAttackStateTime_ < kNeutralLv2PulseGapSec) {
            player.iAttackHitActive_ = true;
        } else {
            if (player.iSpecialPulseIndex_ == 0) {
                ++player.attackSerial_;
                player.iSpecialPulseIndex_ = 1;
            }
            player.iAttackHitActive_ = true;
        }
        if (player.iAttackStateTime_ >= ScaledDuration(kNeutralFinishActiveSec * 1.8f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralFinishRecoverSec * 0.9f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerINeutralSpecial::UpdateNeutralSpecialLv3(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        if (player.iAttackStateTime_ < kNeutralLv3ChargeSec) {
            player.iAttackHitActive_ = false;
        } else {
            if (player.iSpecialPulseIndex_ == 0) {
                ++player.attackSerial_;
                player.iSpecialPulseIndex_ = 1;
            }
            player.iAttackHitActive_ = true;
        }
        if (player.iAttackStateTime_ >= ScaledDuration(kNeutralFinishActiveSec * 2.2f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralFinishRecoverSec * 0.8f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

bool PlayerINeutralSpecial::GetNeutralSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kNeutralFinishHitboxHalfSize, tuning.hitboxScale);
    float offsetX = 1.00f;
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv2) {
        offsetX = player.iSpecialPulseIndex_ == 0 ? 0.85f : 1.35f;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        outHalfSize.x *= 1.25f;
        outHalfSize.y *= 1.20f;
        offsetX = 0.65f;
    }
    outCenter = {
        player.pos_.x + offsetX * static_cast<float>(player.facing_),
        player.pos_.y + outHalfSize.y,
        player.pos_.z
    };
    return true;
}

int PlayerINeutralSpecial::GetNeutralSpecialDamage(const Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kNeutralFinishDamage, tuning.damageRate);
}