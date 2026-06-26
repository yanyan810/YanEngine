#include "Player.h"
#include "Input.h"
#include "Object3d.h"

#include <algorithm>
#include <dinput.h>

void Player::UpdateSmashInputWindow_(const Input& input) {
    const bool leftTrigger = input.IsKeyTrigger(DIK_LEFT) || input.IsKeyTrigger(DIK_A);
    const bool rightTrigger = input.IsKeyTrigger(DIK_RIGHT) || input.IsKeyTrigger(DIK_D);

    if (leftTrigger != rightTrigger) {
        recentHorizontalDir_ = rightTrigger ? +1 : -1;
        recentHorizontalFrames_ = kSmashInputWindowFrames_;
        return;
    }

    if (recentHorizontalFrames_ > 0) {
        --recentHorizontalFrames_;
        if (recentHorizontalFrames_ == 0) {
            recentHorizontalDir_ = 0;
        }
    }
}

Player::PlayerInputCommand Player::ResolveInput_(const Input& input) {
    UpdateSmashInputWindow_(input);

    PlayerInputCommand command{};

    const bool left = input.IsKeyPressed(DIK_LEFT) || input.IsKeyPressed(DIK_A);
    const bool right = input.IsKeyPressed(DIK_RIGHT) || input.IsKeyPressed(DIK_D);
    const bool down = input.IsKeyPressed(DIK_DOWN) || input.IsKeyPressed(DIK_S);
    const bool up = input.IsKeyPressed(DIK_UP);

    if (left != right) {
        command.horizontal = right ? +1 : -1;
    }
    command.down = down;
    if (up != down) {
        command.depth = up ? +1 : -1;
    }
    command.jumpTriggered = input.IsKeyTrigger(DIK_SPACE) || input.IsKeyTrigger(DIK_W);
    command.guard = input.IsKeyPressed(DIK_H);

    const bool weakTriggered = input.IsKeyTrigger(DIK_U);
    const bool specialTriggered = input.IsKeyTrigger(DIK_I);

    if (command.guard) {
        command.action = PlayerAction::Guard;
        return command;
    }

    if (weakTriggered) {
        command.action = PlayerAction::Attack;

        const bool smashWindow =
            command.horizontal != 0 &&
            recentHorizontalFrames_ > 0 &&
            recentHorizontalDir_ == command.horizontal;
        if (command.horizontal != 0) {
            command.attackVariant = PlayerAttackVariant::Side;
        } else if (command.depth > 0) {
            command.attackVariant = PlayerAttackVariant::Up;
        } else {
            command.attackVariant = PlayerAttackVariant::Neutral;
        }

        if (!onGround_) {
            command.attackGroup = PlayerAttackGroup::Air;
            command.attackType = PlayerAttackType::Weak;
        } else if (smashWindow) {
            command.attackGroup = PlayerAttackGroup::Smash;
            command.attackType = PlayerAttackType::Smash;
        } else if (command.horizontal != 0) {
            command.attackGroup = PlayerAttackGroup::Ground;
            command.attackType = PlayerAttackType::Tilt;
        } else {
            command.attackGroup = PlayerAttackGroup::Ground;
            command.attackType = PlayerAttackType::Weak;
        }
        return command;
    }

    if (specialTriggered) {
        command.action = PlayerAction::Attack;
        command.attackType = (command.horizontal != 0)
            ? PlayerAttackType::SideSpecial
            : PlayerAttackType::NeutralSpecial;
        return command;
    }

    if (down) {
        command.action = onGround_ ? PlayerAction::Crouch : PlayerAction::FastFall;
        return command;
    }

    if (command.jumpTriggered && jumpCount_ < maxJumpCount_) {
        command.action = PlayerAction::Jump;
        return command;
    }

    command.action = (command.horizontal != 0) ? PlayerAction::Move : PlayerAction::Idle;
    return command;
}

void Player::StartAttackAction_(PlayerAttackType type, int horizontal, PlayerAttackGroup group, PlayerAttackVariant variant) {
    action_ = PlayerAction::Attack;
    attackType_ = type;
    activeAttackGroup_ = group;
    activeAttackVariant_ = variant;
    attackElapsedSec_ = 0.0f;
    ++attackSerial_;
    crouching_ = false;
    fastFalling_ = false;
    guarding_ = false;

    if (launched_) {
        launched_ = false;
        launchedTimer_ = 0.0f;
        launchedTotalTime_ = 0.0f;
        launchInitialSpeed_ = 0.0f;
        launchActionSpeedRatio_ = 0.0f;
        launchControlUnlocked_ = false;
    }

    if (horizontal != 0) {
        facing_ = horizontal;
    }

    if (type == PlayerAttackType::NeutralSpecial) {
        actionTimer_ = 0.45f;
    } else if (type == PlayerAttackType::SideSpecial) {
        actionTimer_ = 0.50f;
    } else if (type == PlayerAttackType::None) {
        actionTimer_ = 0.0f;
    } else {
        actionTimer_ = AttackDefinition(group, variant).actionSec;
    }

    LockMove(actionTimer_);
}

bool Player::GetAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const {
    if (action_ != PlayerAction::Attack || attackType_ == PlayerAttackType::None) {
        return false;
    }

    if (attackType_ == PlayerAttackType::NeutralSpecial) {
        outHalfSize = { 0.75f, 0.85f, 0.55f };
        outCenter = {
            pos_.x + 1.00f * static_cast<float>(facing_),
            pos_.y + outHalfSize.y,
            pos_.z
        };
        return true;
    }
    if (attackType_ == PlayerAttackType::SideSpecial) {
        outHalfSize = { 1.25f, 0.85f, 0.65f };
        outCenter = {
            pos_.x + 1.55f * static_cast<float>(facing_),
            pos_.y + outHalfSize.y,
            pos_.z
        };
        return true;
    }

    const PlayerAttackDefinition& attack = AttackDefinition(activeAttackGroup_, activeAttackVariant_);
    if (attackElapsedSec_ < attack.startDelaySec ||
        attackElapsedSec_ > attack.startDelaySec + attack.activeSec) {
        return false;
    }

    outHalfSize = attack.halfSize;
    outCenter = {
        pos_.x + attack.offset.x * static_cast<float>(facing_),
        pos_.y + attack.offset.y,
        pos_.z + attack.offset.z
    };
    return true;
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
    if (attackType_ == PlayerAttackType::NeutralSpecial) {
        return 14;
    }
    if (attackType_ == PlayerAttackType::SideSpecial) {
        return 18;
    }
    if (attackType_ == PlayerAttackType::None) {
        return 0;
    }
    return AttackDefinition(activeAttackGroup_, activeAttackVariant_).damage;
}

void Player::ApplyActionCommand_(const PlayerInputCommand& command) {
    guarding_ = command.action == PlayerAction::Guard;
    crouching_ = command.action == PlayerAction::Crouch;
    fastFalling_ = command.action == PlayerAction::FastFall;

    if (command.horizontal != 0 && command.action != PlayerAction::Attack) {
        facing_ = command.horizontal;
    }

    switch (command.action) {
    case PlayerAction::Attack:
        if (actionTimer_ <= 0.0f) {
            StartAttackAction_(command.attackType, command.horizontal, command.attackGroup, command.attackVariant);
        }
        break;

    case PlayerAction::Guard:
        action_ = PlayerAction::Guard;
        attackType_ = PlayerAttackType::None;
        vel_.x = 0.0f;
        vel_.z = 0.0f;
        break;

    case PlayerAction::Crouch:
        action_ = PlayerAction::Crouch;
        attackType_ = PlayerAttackType::None;
        vel_.x = 0.0f;
        break;

    case PlayerAction::FastFall:
        action_ = PlayerAction::FastFall;
        attackType_ = PlayerAttackType::None;
        vel_.y = std::min(vel_.y, -fastFallSpeed_);
        break;

    case PlayerAction::Jump:
        action_ = PlayerAction::Jump;
        attackType_ = PlayerAttackType::None;
        break;

    case PlayerAction::Launched:
        action_ = PlayerAction::Launched;
        attackType_ = PlayerAttackType::None;
        guarding_ = false;
        crouching_ = false;
        fastFalling_ = false;
        break;

    case PlayerAction::Move:
        if (actionTimer_ <= 0.0f) {
            action_ = PlayerAction::Move;
            attackType_ = PlayerAttackType::None;
        }
        break;

    case PlayerAction::Idle:
    default:
        if (actionTimer_ <= 0.0f) {
            action_ = PlayerAction::Idle;
            attackType_ = PlayerAttackType::None;
        }
        break;
    }
}

void Player::UpdateActionTimer_(float dt) {
    if (actionTimer_ <= 0.0f) return;

    if (action_ == PlayerAction::Attack) {
        attackElapsedSec_ += dt;
    }

    actionTimer_ -= dt;
    if (actionTimer_ <= 0.0f) {
        actionTimer_ = 0.0f;
        attackType_ = PlayerAttackType::None;
        attackElapsedSec_ = 0.0f;
        if (action_ == PlayerAction::Attack) {
            action_ = isMoving ? PlayerAction::Move : PlayerAction::Idle;
        }
    }
}

void Player::PlayActionAnimation_(const PlayerInputCommand& command) {
    if (!model_) return;

    auto playIfChanged = [&](const char* anim, bool loop) {
        if (curAnim_ == anim) return;
        model_->CrossFadeTo(anim, 0.10f, loop);
        curAnim_ = anim;
    };

    if (action_ == PlayerAction::Attack) {
        switch (attackType_) {
        case PlayerAttackType::Weak:
        case PlayerAttackType::Tilt:
        case PlayerAttackType::Smash:
            playIfChanged("Attak_I", false);
            break;
        case PlayerAttackType::NeutralSpecial:
        case PlayerAttackType::SideSpecial:
            playIfChanged("Attak_O", false);
            break;
        case PlayerAttackType::None:
        default:
            break;
        }
        return;
    }

    if (action_ == PlayerAction::Guard || action_ == PlayerAction::Crouch) {
        playIfChanged("Idle", true);
        return;
    }

    if (action_ == PlayerAction::Launched) {
        playIfChanged("Idle", true);
        return;
    }

    if (isMoving || command.horizontal != 0) {
        playIfChanged("Walk", true);
    } else {
        playIfChanged("Idle", true);
    }
}
