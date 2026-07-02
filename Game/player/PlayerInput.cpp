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
    const bool specialHeld = input.IsKeyPressed(DIK_I);
    const bool specialReleased = input.IsKeyReleased(DIK_I);
    const bool specialTriggered = input.IsKeyTrigger(DIK_I);
    const bool up = input.IsKeyPressed(DIK_UP) || (specialHeld && input.IsKeyPressed(DIK_W));

    if (left != right) {
        command.horizontal = right ? +1 : -1;
    }
    command.down = down;
    if (up != down) {
        command.depth = up ? +1 : -1;
    }
    command.jumpTriggered = input.IsKeyTrigger(DIK_SPACE) || (!specialHeld && input.IsKeyTrigger(DIK_W));
    command.guard = input.IsKeyPressed(DIK_H);
    command.specialHeld = specialHeld;
    command.specialReleased = specialReleased;
    latestSpecialHeld_ = command.specialHeld;
    latestSpecialReleased_ = command.specialReleased;

    const bool weakTriggered = input.IsKeyTrigger(DIK_U);

    if (command.guard) {
        command.action = PlayerAction::Guard;
        return command;
    }

    if (weakTriggered) {
        BuildUAttackCommand_(command);
        return command;
    }

    if (specialTriggered) {
        BuildIAttackCommand_(command);
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

void Player::ApplyActionCommand_(const PlayerInputCommand& command) {
    guarding_ = command.action == PlayerAction::Guard;
    crouching_ = command.action == PlayerAction::Crouch;
    fastFalling_ = command.action == PlayerAction::FastFall;

    if (command.horizontal != 0 && command.action != PlayerAction::Attack) {
        facing_ = command.horizontal;
    }

    switch (command.action) {
    case PlayerAction::Attack:
        if (CanStartAttackCommand_(command)) {
            StartAttackAction_(command.attackType, command.horizontal, command.attackGroup, command.attackVariant);
        } else if (IsUAttackType_(command.attackType) &&
            action_ == PlayerAction::Attack &&
            IsUAttackType_(attackType_) &&
            lastUComboStage_ < 2) {
            constexpr float kUComboBufferSec = 0.18f;
            bufferedUComboCommand_ = command;
            uComboBufferTimer_ = kUComboBufferSec;
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
    if (uComboResetTimer_ > 0.0f && !(action_ == PlayerAction::Attack && IsUAttackType_(attackType_))) {
        const float previousResetTimer = uComboResetTimer_;
        uComboResetTimer_ = std::max(0.0f, uComboResetTimer_ - dt);
        if (previousResetTimer > 0.0f && uComboResetTimer_ <= 0.0f) {
            uComboStage_ = 0;
            lastUComboStage_ = 0;
            uComboBufferTimer_ = 0.0f;
        }
    }
    if (uComboBufferTimer_ > 0.0f) {
        uComboBufferTimer_ = std::max(0.0f, uComboBufferTimer_ - dt);
    }
    uComboDebugFlashSec_ = std::max(0.0f, uComboDebugFlashSec_ - dt);

    if (actionTimer_ <= 0.0f) return;

    if (action_ == PlayerAction::Attack) {
        attackElapsedSec_ += dt;
    }

    actionTimer_ -= dt;
    if (uComboBufferTimer_ > 0.0f && CanStartAttackCommand_(bufferedUComboCommand_)) {
        StartAttackAction_(
            bufferedUComboCommand_.attackType,
            bufferedUComboCommand_.horizontal,
            bufferedUComboCommand_.attackGroup,
            bufferedUComboCommand_.attackVariant);
        return;
    }
    if (actionTimer_ <= 0.0f) {
        const PlayerAttackType finishedType = attackType_;
        actionTimer_ = 0.0f;
        attackType_ = PlayerAttackType::None;
        attackElapsedSec_ = 0.0f;
        currentAttackHit_ = false;
        specialHitDuringAction_ = false;
        specialCancelUsedThisAction_ = false;
        hasSpecialChainCancelRight_ = false;
        specialChainCancelEligible_ = false;
        nextSideSpecialLockOn_ = false;
        sideSpecialLockOnActive_ = false;
        iSpecialChargeSec_ = 0.0f;
        iCounterSuccess_ = false;
        ChangeIAttackState_(PlayerIAttackState::None);
        if (IsUAttackType_(finishedType)) {
            constexpr float kUComboResetSec = 0.28f;
            uComboResetTimer_ = kUComboResetSec;
        }
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
        case PlayerAttackType::UpSpecial:
        case PlayerAttackType::DownSpecial:
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
