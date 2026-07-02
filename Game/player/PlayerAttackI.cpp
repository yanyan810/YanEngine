#include "PlayerAttackI.h"

#include "Player.h"

#include <algorithm>
#include <array>
#include <cmath>

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
constexpr int kNeutralFinishDamage = 14;
constexpr int kUpRiseDamage = 12;
constexpr int kDownCounterSuccessDamage = 22;
const Vector3 kNeutralFinishHitboxHalfSize = { 0.75f, 0.85f, 0.55f };
const Vector3 kSideSlideHitboxHalfSize = { 1.25f, 0.85f, 0.65f };
const Vector3 kSideSlideChargedHitboxAdd = { 0.35f, 0.0f, 0.0f };
const Vector3 kUpRiseHitboxHalfSize = { 0.85f, 1.10f, 0.65f };
const Vector3 kDownCounterHitboxHalfSize = { 1.10f, 0.95f, 0.70f };
constexpr int kSpecialCancelLevelCount = 4;

struct SpecialCancelLevelTuning {
    float damageRate = 1.0f;
    Vector3 hitboxScale = { 1.0f, 1.0f, 1.0f };
    float attackSpeedRate = 1.0f;
    float moveSpeedRate = 1.0f;
    float moveDurationRate = 1.0f;
    float activeDurationRate = 1.0f;
    float hitStopRate = 1.0f;
    int effectLevel = 0;
    int cameraLevel = 0;
    int soundLevel = 0;
};

const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kNeutralSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.10f, 1.05f, 1.05f }, 1.05f, 1.00f, 1.00f, 1.10f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.22f, 1.10f, 1.10f }, 1.10f, 1.00f, 1.00f, 1.20f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.36f, 1.16f, 1.16f }, 1.18f, 1.00f, 1.00f, 1.35f, 1.45f, 3, 2, 3 },
} };

const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kSideSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.08f, 1.04f, 1.05f }, 1.04f, 1.08f, 1.04f, 1.06f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.18f, 1.08f, 1.10f }, 1.08f, 1.16f, 1.08f, 1.12f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.30f, 1.12f, 1.16f }, 1.12f, 1.26f, 1.12f, 1.20f, 1.45f, 3, 2, 3 },
} };

const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kUpSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.08f, 1.10f, 1.05f }, 1.04f, 1.08f, 1.04f, 1.08f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.14f, 1.22f, 1.10f }, 1.08f, 1.16f, 1.08f, 1.16f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.22f, 1.38f, 1.16f }, 1.12f, 1.24f, 1.12f, 1.28f, 1.45f, 3, 2, 3 },
} };

const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kDownSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.10f, 1.04f, 1.08f }, 1.04f, 1.00f, 1.00f, 1.10f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.22f, 1.08f, 1.15f }, 1.08f, 1.00f, 1.00f, 1.20f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.36f, 1.12f, 1.24f }, 1.12f, 1.00f, 1.00f, 1.35f, 1.45f, 3, 2, 3 },
} };

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

int ClampCancelLevel(int cancelCount) {
    return std::clamp(cancelCount, 0, kSpecialCancelLevelCount - 1);
}

const SpecialCancelLevelTuning& GetCancelTuning(Player::PlayerAttackType type, int cancelCount) {
    const int level = ClampCancelLevel(cancelCount);
    switch (type) {
    case Player::PlayerAttackType::NeutralSpecial:
        return kNeutralSpecialCancelTuning[level];
    case Player::PlayerAttackType::SideSpecial:
        return kSideSpecialCancelTuning[level];
    case Player::PlayerAttackType::UpSpecial:
        return kUpSpecialCancelTuning[level];
    case Player::PlayerAttackType::DownSpecial:
        return kDownSpecialCancelTuning[level];
    default:
        return kNeutralSpecialCancelTuning[0];
    }
}

const SpecialCancelLevelTuning& GetCurrentCancelTuning(const Player& player) {
    return GetCancelTuning(player.GetCurrentAttackType(), player.GetSpecialCancelCount());
}

float ScaledDuration(float baseSec, float durationRate) {
    return baseSec * std::max(0.0f, durationRate);
}

float ScaledByAttackSpeed(float baseSec, float attackSpeedRate) {
    return baseSec / std::max(0.01f, attackSpeedRate);
}

Vector3 ScaleVector3(const Vector3& value, const Vector3& scale) {
    return { value.x * scale.x, value.y * scale.y, value.z * scale.z };
}

int ApplyDamageRate(int baseDamage, float damageRate) {
    return static_cast<int>(std::lround(static_cast<float>(baseDamage) * damageRate));
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
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::NeutralSpecial, player.specialCancelCount_);
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iSpecialChargeSec_ = 0.0f;
    ChangeState(player, PlayerIAttackState::NeutralFinish_Active);
}

void PlayerIAttack::StartSideSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::SideSpecial, player.specialCancelCount_);
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
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
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::UpSpecial, player.specialCancelCount_);
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iSpecialChargeSec_ = 0.0f;
    player.onGround_ = false;
    ChangeState(player, PlayerIAttackState::UpRise_Windup);
}

void PlayerIAttack::StartDownSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::DownSpecial, player.specialCancelCount_);
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iCounterSuccess_ = false;
    player.iSpecialChargeSec_ = 0.0f;
    player.vel_.x = 0.0f;
    player.vel_.z = 0.0f;
    ChangeState(player, PlayerIAttackState::DownCounter_Active);
}

void PlayerIAttack::UpdateNeutralSpecial(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kNeutralFinishActiveSec, tuning.activeDurationRate)) {
            ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralFinishRecoverSec, tuning.attackSpeedRate)) {
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
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;

    switch (player.iAttackState_) {
    case PlayerIAttackState::SideSlide_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        player.iSpecialChargeSec_ = std::min(kSideSlideMaxChargeSec, player.iSpecialChargeSec_ + dt);
        if ((!player.latestSpecialHeld_ && player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideMinChargeSec, tuning.attackSpeedRate)) ||
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
            const float lockOnSpeed = kSideLockOnPierceSpeed * tuning.moveSpeedRate;
            player.vel_.x = player.sideSpecialLockOnDirection_.x * lockOnSpeed;
            player.vel_.y = player.sideSpecialLockOnDirection_.y * lockOnSpeed;
            player.vel_.z = player.sideSpecialLockOnDirection_.z * lockOnSpeed;
            player.iAttackHitActive_ = true;
            if (player.iAttackStateTime_ >= ScaledDuration(kSideLockOnPierceSec, tuning.moveDurationRate)) {
                ChangeState(player, PlayerIAttackState::SideSlide_Recover);
            }
            break;
        }
        const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
        player.vel_.x = static_cast<float>(player.facing_) *
            (kSideSlideSpeed + kSideSlideChargedSpeedAdd * charge) * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.vel_.y = std::max(player.vel_.y, 1.5f);
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kSideSlideMoveSec, tuning.moveDurationRate)) {
            ChangeState(player, PlayerIAttackState::SideSlide_Recover);
        }
    } break;

    case PlayerIAttackState::SideSlide_Recover:
        if (!player.sideSpecialHitBounceUsed_) {
            player.vel_.x = 0.0f;
            player.vel_.y = std::min(player.vel_.y, 0.0f);
            player.vel_.z = 0.0f;
        }
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideRecoverSec, tuning.attackSpeedRate)) {
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
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseWindupSec, tuning.attackSpeedRate)) {
            ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        player.onGround_ = false;
        player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * tuning.moveSpeedRate;
        player.vel_.y = kUpRiseSpeedY * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kUpRiseMoveSec, tuning.moveDurationRate)) {
            ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec, tuning.attackSpeedRate)) {
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
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    player.vel_.x = 0.0f;
    player.vel_.z = 0.0f;

    switch (player.iAttackState_) {
    case PlayerIAttackState::DownCounter_Active:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterActiveSec, tuning.activeDurationRate)) {
            ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Success:
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterSuccessSec, tuning.activeDurationRate)) {
            ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kDownCounterRecoverSec, tuning.attackSpeedRate)) {
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
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kNeutralFinishHitboxHalfSize, tuning.hitboxScale);
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
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    const Vector3 chargedHalfSize = {
        kSideSlideHitboxHalfSize.x + kSideSlideChargedHitboxAdd.x * charge,
        kSideSlideHitboxHalfSize.y + kSideSlideChargedHitboxAdd.y * charge,
        kSideSlideHitboxHalfSize.z + kSideSlideChargedHitboxAdd.z * charge,
    };
    outHalfSize = ScaleVector3(chargedHalfSize, tuning.hitboxScale);
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
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kUpRiseHitboxHalfSize, tuning.hitboxScale);
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
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kDownCounterHitboxHalfSize, tuning.hitboxScale);
    outCenter = {
        player.pos_.x + 0.75f * static_cast<float>(player.facing_),
        player.pos_.y + 0.95f,
        player.pos_.z
    };
    return true;
}

int PlayerIAttack::GetNeutralSpecialDamage(const Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kNeutralFinishDamage, tuning.damageRate);
}

int PlayerIAttack::GetSideSpecialDamage(const Player& player) {
    const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
    const int baseDamage = kSideSlideBaseDamage +
        static_cast<int>(kSideSlideChargedDamageAdd * charge);
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(baseDamage, tuning.damageRate);
}

int PlayerIAttack::GetUpSpecialDamage(const Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kUpRiseDamage, tuning.damageRate);
}

int PlayerIAttack::GetDownSpecialDamage(const Player& player) {
    if (!player.iCounterSuccess_) {
        return 0;
    }
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kDownCounterSuccessDamage, tuning.damageRate);
}

void PlayerIAttack::ChangeState(Player& player, PlayerIAttackState state) {
    player.iAttackState_ = state;
    player.iAttackStateTime_ = 0.0f;
    player.iAttackHitActive_ = state == PlayerIAttackState::SideSlide_Move;
}
