#include "PlayerAttackIInternal.h"

using namespace PlayerIAttackInternal;

namespace {
constexpr float kUpLv1PulseTimes[] = { 0.0f, 0.25f, 0.50f };
constexpr float kUpLv1PulseActiveSec = 0.08f;
constexpr float kUpLv1MoveSec = 0.62f;
constexpr float kUpLv2BeamActiveSec = 0.32f;
constexpr float kUpLv2BeamThicknessX = 0.75f;
constexpr float kUpLv2BeamThicknessY = 0.75f;
constexpr float kUpLv2BeamThicknessZ = 0.55f;
constexpr float kUpLv2BeamMinLength = 4.0f;
constexpr float kUpLv3ApproachSec = 0.18f;
constexpr float kUpLv3PierceSec = 0.18f;
constexpr float kUpLv3BeamSec = 0.24f;
constexpr float kUpLv3ApproachSpeed = 28.0f;
constexpr float kUpLv3PierceSpeed = 30.0f;
constexpr float kUpLv3PierceRiseSpeed = 18.0f;
constexpr float kUpLv3BeamThicknessX = 0.90f;
constexpr float kUpLv3BeamThicknessY = 0.90f;
constexpr float kUpLv3BeamThicknessZ = 0.65f;

Vector3 UpSpecialTargetOrFallback(
    const Vector3& playerPos,
    int facing,
    bool hasTarget,
    const Vector3& target) {
    if (hasTarget) {
        return target;
    }
    return {
        playerPos.x + static_cast<float>(facing) * 6.0f,
        playerPos.y + 2.0f,
        playerPos.z
    };
}

Vector3 NormalizedOrFacing(const Vector3& value, int facing) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.001f) {
        return { static_cast<float>(facing), 0.0f, 0.0f };
    }
    return { value.x / length, value.y / length, value.z / length };
}

void BuildBeamBox(
    const Vector3& playerPos,
    int facing,
    const Vector3& target,
    float minLength,
    float thicknessX,
    float thicknessY,
    float thicknessZ,
    Vector3& outCenter,
    Vector3& outHalfSize) {
    const Vector3 start = {
        playerPos.x,
        playerPos.y + 1.0f,
        playerPos.z
    };
    Vector3 end = target;
    end.y = std::max(end.y, start.y);

    const Vector3 toTarget = {
        end.x - start.x,
        end.y - start.y,
        end.z - start.z
    };
    const Vector3 dir = NormalizedOrFacing(toTarget, facing);
    const float length = std::max(
        minLength,
        std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z));
    end = {
        start.x + dir.x * length,
        start.y + dir.y * length,
        start.z + dir.z * length
    };

    outCenter = {
        (start.x + end.x) * 0.5f,
        (start.y + end.y) * 0.5f,
        (start.z + end.z) * 0.5f
    };
    outHalfSize = {
        std::max(thicknessX, std::abs(end.x - start.x) * 0.5f + thicknessX),
        std::max(thicknessY, std::abs(end.y - start.y) * 0.5f + thicknessY),
        std::max(thicknessZ, std::abs(end.z - start.z) * 0.5f + thicknessZ)
    };
}
}

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

// 上昇中に当たり判定を3回出す。出始め、0.25秒後、0.5秒後。
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
        player.iAttackHitActive_ = false;
        for (int pulse = 0; pulse < 3; ++pulse) {
            const float pulseStart = kUpLv1PulseTimes[pulse];
            if (player.iAttackStateTime_ >= pulseStart &&
                player.iAttackStateTime_ < pulseStart + kUpLv1PulseActiveSec) {
                player.iAttackHitActive_ = true;
                if (player.iSpecialPulseIndex_ < pulse) {
                    ++player.attackSerial_;
                    player.iSpecialPulseIndex_ = pulse;
                }
                break;
            }
        }
        if (player.iAttackStateTime_ >= ScaledDuration(kUpLv1MoveSec, tuning.moveDurationRate)) {
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

// 今いる場所から敵に向かってビームのような判定を出す。
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
        if (player.iSpecialPulseIndex_ == 0) {
            ++player.attackSerial_;
            player.iSpecialPulseIndex_ = 1;
        }
        player.vel_.x = 0.0f;
        player.vel_.y = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kUpLv2BeamActiveSec, tuning.activeDurationRate)) {
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

// 敵の足元へ移動し、移動し終わったら敵へ斜め上に貫通し、その後に敵へビームを出す。
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
    {
        player.onGround_ = false;
        const Vector3 target = UpSpecialTargetOrFallback(
            player.pos_,
            player.facing_,
            player.sideSpecialLockOnActive_,
            player.sideSpecialLockOnTarget_);
        if (player.iAttackStateTime_ < kUpLv3ApproachSec) {
            const Vector3 footTarget = { target.x, player.pos_.y, target.z };
            const Vector3 dir = NormalizedOrFacing({
                footTarget.x - player.pos_.x,
                0.0f,
                footTarget.z - player.pos_.z
            }, player.facing_);
            player.vel_.x = dir.x * kUpLv3ApproachSpeed * tuning.moveSpeedRate;
            player.vel_.y = 0.0f;
            player.vel_.z = dir.z * kUpLv3ApproachSpeed * tuning.moveSpeedRate;
            player.iAttackHitActive_ = false;
        } else if (player.iAttackStateTime_ < kUpLv3ApproachSec + kUpLv3PierceSec) {
            if (player.iSpecialPulseIndex_ < 1) {
                ++player.attackSerial_;
                player.iSpecialPulseIndex_ = 1;
            }
            const Vector3 dir = NormalizedOrFacing({
                target.x - player.pos_.x,
                std::max(1.0f, target.y + 1.5f - player.pos_.y),
                target.z - player.pos_.z
            }, player.facing_);
            player.vel_.x = dir.x * kUpLv3PierceSpeed * tuning.moveSpeedRate;
            player.vel_.y = std::max(kUpLv3PierceRiseSpeed, dir.y * kUpLv3PierceSpeed) * tuning.moveSpeedRate;
            player.vel_.z = dir.z * kUpLv3PierceSpeed * tuning.moveSpeedRate;
            player.iAttackHitActive_ = true;
        } else {
            if (player.iSpecialPulseIndex_ < 2) {
                ++player.attackSerial_;
                player.iSpecialPulseIndex_ = 2;
            }
            player.vel_ = { 0.0f, 0.0f, 0.0f };
            player.iAttackHitActive_ = true;
        }
        if (player.iAttackStateTime_ >= ScaledDuration(kUpLv3ApproachSec + kUpLv3PierceSec + kUpLv3BeamSec, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
    } break;
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
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv1) {
        offsetY = 1.05f + 0.28f * static_cast<float>(player.iSpecialPulseIndex_);
        outHalfSize.y *= 0.90f;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv2) {
        BuildBeamBox(
            player.pos_,
            player.facing_,
            UpSpecialTargetOrFallback(
                player.pos_,
                player.facing_,
                player.sideSpecialLockOnActive_,
                player.sideSpecialLockOnTarget_),
            kUpLv2BeamMinLength,
            kUpLv2BeamThicknessX * tuning.hitboxScale.x,
            kUpLv2BeamThicknessY * tuning.hitboxScale.y,
            kUpLv2BeamThicknessZ * tuning.hitboxScale.z,
            outCenter,
            outHalfSize);
        return true;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        if (player.iSpecialPulseIndex_ >= 2) {
            BuildBeamBox(
                player.pos_,
                player.facing_,
                UpSpecialTargetOrFallback(
                    player.pos_,
                    player.facing_,
                    player.sideSpecialLockOnActive_,
                    player.sideSpecialLockOnTarget_),
                kUpLv2BeamMinLength,
                kUpLv3BeamThicknessX * tuning.hitboxScale.x,
                kUpLv3BeamThicknessY * tuning.hitboxScale.y,
                kUpLv3BeamThicknessZ * tuning.hitboxScale.z,
                outCenter,
                outHalfSize);
            return true;
        }
        offsetY = 1.55f;
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
