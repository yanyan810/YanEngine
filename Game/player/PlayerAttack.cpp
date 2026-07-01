#include "Player.h"

#include "EnemyManager.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kSideSpecialHitBounceSpeedX = 7.5f;
constexpr float kSideSpecialHitBounceSpeedY = 11.0f;
constexpr float kMinLockOnDirectionLength = 0.001f;
}

Player::PlayerAttackDefinition& Player::AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant) {
    return attackDefinitions_[static_cast<size_t>(group)][static_cast<size_t>(variant)];
}

const Player::PlayerAttackDefinition& Player::AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant) const {
    return attackDefinitions_[static_cast<size_t>(group)][static_cast<size_t>(variant)];
}

const char* Player::AttackGroupName(PlayerAttackGroup group) {
    switch (group) {
    case PlayerAttackGroup::Ground:
        return "Ground U";
    case PlayerAttackGroup::Smash:
        return "Smash U";
    case PlayerAttackGroup::Air:
        return "Air U";
    default:
        return "Unknown";
    }
}

const char* Player::AttackVariantName(PlayerAttackVariant variant) {
    switch (variant) {
    case PlayerAttackVariant::Neutral:
        return "Neutral";
    case PlayerAttackVariant::Side:
        return "Side";
    case PlayerAttackVariant::Up:
        return "Up";
    default:
        return "Unknown";
    }
}

void Player::StartAttackAction_(PlayerAttackType type, int horizontal, PlayerAttackGroup group, PlayerAttackVariant variant) {
    const bool hasSpecialCancelResource =
        IsIAttackType_(type) &&
        (hasSpecialCancelRight_ || hasSpecialChainCancelRight_) &&
        cancelGauge_ > 0 &&
        !specialCancelUsedThisAction_;
    const bool wasCancelableSpecial =
        hasSpecialCancelResource &&
        ((action_ == PlayerAction::Attack && actionTimer_ > 0.0f) || !onGround_);

    if (wasCancelableSpecial) {
        --cancelGauge_;
        hasSpecialCancelRight_ = false;
        hasSpecialChainCancelRight_ = false;
        specialChainCancelEligible_ = true;
        specialCancelUsedThisAction_ = false;
        specialCancelDebugFlashSec_ = 0.45f;
    } else {
        specialCancelUsedThisAction_ = false;
        hasSpecialChainCancelRight_ = false;
        specialChainCancelEligible_ = false;
        if (IsIAttackType_(type) && hasSpecialCancelRight_) {
            hasSpecialCancelRight_ = false;
        }
    }

    action_ = PlayerAction::Attack;
    attackType_ = type;
    activeAttackGroup_ = group;
    activeAttackVariant_ = variant;
    attackElapsedSec_ = 0.0f;
    currentAttackHit_ = false;
    specialHitDuringAction_ = false;
    sideSpecialHitBounceUsed_ = false;
    sideSpecialLockOnActive_ =
        wasCancelableSpecial &&
        type == PlayerAttackType::SideSpecial &&
        nextSideSpecialLockOn_;
    if (sideSpecialLockOnActive_) {
        Vector3 toTarget = {
            sideSpecialLockOnTarget_.x - pos_.x,
            sideSpecialLockOnTarget_.y - pos_.y,
            sideSpecialLockOnTarget_.z - pos_.z
        };
        const float length = std::sqrt(
            toTarget.x * toTarget.x +
            toTarget.y * toTarget.y +
            toTarget.z * toTarget.z);
        if (length > kMinLockOnDirectionLength) {
            sideSpecialLockOnDirection_ = {
                toTarget.x / length,
                toTarget.y / length,
                toTarget.z / length
            };
            facing_ = sideSpecialLockOnDirection_.x >= 0.0f ? 1 : -1;
        } else {
            sideSpecialLockOnDirection_ = { static_cast<float>(facing_), 0.0f, 0.0f };
        }
    } else {
        sideSpecialLockOnDirection_ = { static_cast<float>(facing_), 0.0f, 0.0f };
    }
    nextSideSpecialLockOn_ = false;
    ++attackSerial_;
    crouching_ = false;
    fastFalling_ = false;
    guarding_ = false;

    if (launched_) {
        ResetLaunchState_(PlayerAction::Attack);
    }

    if (horizontal != 0) {
        facing_ = horizontal;
    }

    if (IsUAttackType_(type)) {
        const bool continueCombo =
            uComboResetTimer_ > 0.0f ||
            (action_ == PlayerAction::Attack && IsUAttackType_(attackType_));
        uComboStage_ = continueCombo ? ((uComboStage_ + 1) % 3) : 0;
        lastUComboStage_ = uComboStage_;
        uComboResetTimer_ = 0.0f;
        uComboBufferTimer_ = 0.0f;
        if (continueCombo) {
            uComboDebugFlashSec_ = 0.35f;
        }
    } else {
        uComboResetTimer_ = 0.0f;
        uComboBufferTimer_ = 0.0f;
        uComboStage_ = 0;
        lastUComboStage_ = 0;
    }

    StartIAttack_(type);

    actionTimer_ = GetAttackActionSec_(type, group, variant);
    LockMove(actionTimer_);
}

bool Player::GetAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const {
    if (action_ != PlayerAction::Attack || attackType_ == PlayerAttackType::None) {
        return false;
    }

    switch (attackType_) {
    case PlayerAttackType::Weak:
    case PlayerAttackType::Tilt:
    case PlayerAttackType::Smash:
        return GetUAttackDebugHitBox_(outCenter, outHalfSize);
    case PlayerAttackType::NeutralSpecial:
    case PlayerAttackType::SideSpecial:
    case PlayerAttackType::UpSpecial:
    case PlayerAttackType::DownSpecial:
        return GetIAttackDebugHitBox_(outCenter, outHalfSize);
    case PlayerAttackType::None:
    default:
        return false;
    }
}

bool Player::GetAttackHitBox(AABB& outHitBox) const {
    Vector3 center{};
    Vector3 halfSize{};
    if (!GetAttackDebugHitBox_(center, halfSize)) {
        return false;
    }

    outHitBox.min = {
        center.x - halfSize.x,
        center.y - halfSize.y,
        center.z - halfSize.z,
    };
    outHitBox.max = {
        center.x + halfSize.x,
        center.y + halfSize.y,
        center.z + halfSize.z,
    };
    return true;
}

int Player::GetAttackDamage() const {
    switch (attackType_) {
    case PlayerAttackType::Weak:
    case PlayerAttackType::Tilt:
    case PlayerAttackType::Smash:
        return GetUAttackDamage_();
    case PlayerAttackType::NeutralSpecial:
    case PlayerAttackType::SideSpecial:
    case PlayerAttackType::UpSpecial:
    case PlayerAttackType::DownSpecial:
        return GetIAttackDamage_();
    case PlayerAttackType::None:
    default:
        return 0;
    }
}

float Player::GetAttackActionSec_(PlayerAttackType type, PlayerAttackGroup group, PlayerAttackVariant variant) const {
    switch (type) {
    case PlayerAttackType::Weak:
    case PlayerAttackType::Tilt:
    case PlayerAttackType::Smash:
        return AttackDefinition(group, variant).actionSec;
    case PlayerAttackType::NeutralSpecial:
        return 0.45f;
    case PlayerAttackType::SideSpecial:
        return 1.30f;
    case PlayerAttackType::UpSpecial:
        return 0.62f;
    case PlayerAttackType::DownSpecial:
        return 0.72f;
    case PlayerAttackType::None:
    default:
        return 0.0f;
    }
}

bool Player::IsUAttackType_(PlayerAttackType type) const {
    return type == PlayerAttackType::Weak ||
        type == PlayerAttackType::Tilt ||
        type == PlayerAttackType::Smash;
}

bool Player::IsIAttackType_(PlayerAttackType type) const {
    return type == PlayerAttackType::NeutralSpecial ||
        type == PlayerAttackType::SideSpecial ||
        type == PlayerAttackType::UpSpecial ||
        type == PlayerAttackType::DownSpecial;
}

bool Player::IsUComboAccepting_() const {
    if (action_ != PlayerAction::Attack || !IsUAttackType_(attackType_)) {
        return false;
    }
    constexpr float kUComboAcceptStartSec = 0.08f;
    constexpr float kUComboAcceptEndPadSec = 0.02f;
    constexpr float kUComboThirdAcceptEndPadSec = -0.23f;

    const float endPadSec =
        lastUComboStage_ == 2 ? kUComboThirdAcceptEndPadSec : kUComboAcceptEndPadSec;

    return attackElapsedSec_ >= kUComboAcceptStartSec &&
        actionTimer_ > endPadSec;
}

bool Player::CanStartAttackCommand_(const PlayerInputCommand& command) const {
    if (command.action != PlayerAction::Attack) {
        return false;
    }
    if (IsIAttackType_(command.attackType) &&
        !onGround_ &&
        (hasSpecialCancelRight_ || hasSpecialChainCancelRight_) &&
        cancelGauge_ > 0 &&
        !specialCancelUsedThisAction_) {
        return true;
    }
    if (actionTimer_ <= 0.0f) {
        return true;
    }
    if (IsUAttackType_(command.attackType)) {
        return IsUComboAccepting_();
    }
    if (!IsIAttackType_(command.attackType)) {
        return false;
    }
    if (IsIAttackType_(attackType_)) {
        return hasSpecialChainCancelRight_ &&
            cancelGauge_ > 0 &&
            !specialCancelUsedThisAction_;
    }
    return hasSpecialCancelRight_ &&
        cancelGauge_ > 0 &&
        !specialCancelUsedThisAction_;
}

bool Player::CanSpecialCancelNow() const {
    return ((action_ == PlayerAction::Attack && actionTimer_ > 0.0f) || !onGround_) &&
        (hasSpecialCancelRight_ || hasSpecialChainCancelRight_) &&
        cancelGauge_ > 0 &&
        !specialCancelUsedThisAction_;
}

void Player::NotifyAttackHit() {
    currentAttackHit_ = true;
    if (IsUAttackType_(attackType_)) {
        hasSpecialCancelRight_ = true;
    }
    if (IsIAttackType_(attackType_)) {
        specialHitDuringAction_ = true;
        if (specialChainCancelEligible_) {
            hasSpecialChainCancelRight_ = true;
        }
        if (attackType_ == PlayerAttackType::SideSpecial &&
            iAttackState_ == PlayerIAttackState::SideSlide_Move &&
            !sideSpecialLockOnActive_ &&
            !sideSpecialHitBounceUsed_) {
            sideSpecialHitBounceUsed_ = true;
            onGround_ = false;
            vel_.x = -static_cast<float>(facing_) * kSideSpecialHitBounceSpeedX;
            vel_.y = std::max(vel_.y, kSideSpecialHitBounceSpeedY);
            vel_.z = 0.0f;
            ChangeIAttackState_(PlayerIAttackState::SideSlide_Recover);
        }
    }
}

void Player::PrepareSpecialCommandTarget_(const PlayerInputCommand& command, const EnemyManager& enemyMgr) {
    nextSideSpecialLockOn_ = false;
    if (command.action != PlayerAction::Attack ||
        command.attackType != PlayerAttackType::SideSpecial ||
        !CanStartAttackCommand_(command)) {
        return;
    }

    float bestDistanceSq = std::numeric_limits<float>::max();
    Vector3 bestTarget{};
    bool found = false;
    for (const Enemy& enemy : enemyMgr.GetEnemies()) {
        if (!enemy.IsAlive()) {
            continue;
        }
        const AABB body = enemy.GetBodyAABB();
        const Vector3 center = {
            (body.min.x + body.max.x) * 0.5f,
            (body.min.y + body.max.y) * 0.5f,
            (body.min.z + body.max.z) * 0.5f
        };
        const float dx = center.x - pos_.x;
        const float dy = center.y - pos_.y;
        const float dz = center.z - pos_.z;
        const float distanceSq = dx * dx + dy * dy + dz * dz;
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestTarget = center;
            found = true;
        }
    }

    if (found) {
        nextSideSpecialLockOn_ = true;
        sideSpecialLockOnTarget_ = bestTarget;
    }
}

bool Player::IsCounterActive() const {
    return attackType_ == PlayerAttackType::DownSpecial &&
        iAttackState_ == PlayerIAttackState::DownCounter_Active;
}

void Player::NotifyCounterSuccess() {
    if (!IsCounterActive()) {
        return;
    }
    iCounterSuccess_ = true;
    PlayerIAttack::ChangeState(*this, PlayerIAttackState::DownCounter_Success);
}
