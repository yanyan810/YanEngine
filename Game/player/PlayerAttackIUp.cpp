#include "PlayerAttackIInternal.h"

using namespace PlayerIAttackInternal;

// ===== 上必殺技 =====
void PlayerIUpSpecial::StartUpSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::UpSpecial, player.specialCancelCount_);
    player.iSpecialVariant_ = VariantFromCancelCount(player.specialCancelCount_);
    player.iSpecialPulseIndex_ = 0;
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iSpecialChargeSec_ = 0.0f;
    player.onGround_ = false;
    PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Windup);
}

void PlayerIUpSpecial::UpdateUpSpecial(Player& player, float dt) {
    switch (player.iSpecialVariant_) {
    case PlayerISpecialVariant::Lv1:
        UpdateUpSpecialLv1(player, dt);
        break;
    case PlayerISpecialVariant::Lv2:
        UpdateUpSpecialLv2(player, dt);
        break;
    case PlayerISpecialVariant::Lv3:
        UpdateUpSpecialLv3(player, dt);
        break;
    case PlayerISpecialVariant::Lv0:
    default:
        UpdateUpSpecialLv0(player, dt);
        break;
    }
}

void PlayerIUpSpecial::UpdateUpSpecialLv0(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseWindupSec, tuning.attackSpeedRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        player.onGround_ = false;
        player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * tuning.moveSpeedRate;
        player.vel_.y = kUpRiseSpeedY * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kUpRiseMoveSec, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerIUpSpecial::UpdateUpSpecialLv1(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseWindupSec * 0.8f, tuning.attackSpeedRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        player.onGround_ = false;
        player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * 2.0f * tuning.moveSpeedRate;
        player.vel_.y = kUpRiseSpeedY * 0.92f * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kUpRiseMoveSec * 1.05f, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec * 0.9f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerIUpSpecial::UpdateUpSpecialLv2(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.y = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kUpLv2HoverSec) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        player.onGround_ = false;
        if (player.iAttackStateTime_ >= kUpLv2SecondRiseSec && player.iSpecialPulseIndex_ == 0) {
            ++player.attackSerial_;
            player.iSpecialPulseIndex_ = 1;
        }
        player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * (player.iSpecialPulseIndex_ == 0 ? 0.6f : 1.4f);
        player.vel_.y = kUpRiseSpeedY * (player.iSpecialPulseIndex_ == 0 ? 0.85f : 1.25f) * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kUpRiseMoveSec * 1.35f, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec * 0.85f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

void PlayerIUpSpecial::UpdateUpSpecialLv3(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.y = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kUpLv3ChargeSec) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        player.onGround_ = false;
        player.vel_.x = 0.0f;
        player.vel_.y = kUpRiseSpeedY * 1.75f * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kUpRiseMoveSec * 0.85f, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec * 0.75f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

bool PlayerIUpSpecial::GetUpSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kUpRiseHitboxHalfSize, tuning.hitboxScale);
    float offsetY = 1.20f;
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv2 && player.iSpecialPulseIndex_ > 0) {
        offsetY = 1.55f;
        outHalfSize.y *= 1.15f;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        offsetY = 1.75f;
        outHalfSize.x *= 1.20f;
        outHalfSize.y *= 1.35f;
    }
    outCenter = {
        player.pos_.x + 0.25f * static_cast<float>(player.facing_),
        player.pos_.y + offsetY,
        player.pos_.z
    };
    return true;
}

int PlayerIUpSpecial::GetUpSpecialDamage(const Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kUpRiseDamage, tuning.damageRate);
}