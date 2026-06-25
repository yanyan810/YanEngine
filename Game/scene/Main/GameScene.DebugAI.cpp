#include "GameScene.h"

#include "DebugAI/DebugAIManager.h"
#include "GameApp.h"
#include "GameSceneDebugAdapter.h"
#include "GameSceneDebugProfile.h"

#include <algorithm>
#include <cstdlib>
#include <dinput.h>
#include <string>

GameSceneDebugAdapter::GameSceneDebugAdapter(GameScene& scene)
    : scene_(scene) {
}

DebugGameState GameSceneDebugAdapter::CaptureDebugState() const {
    return scene_.CaptureDebugState();
}

bool GameSceneDebugAdapter::RestoreDebugState(const DebugGameState& state) {
    return scene_.RestoreDebugState(state);
}

void GameSceneDebugAdapter::SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) {
    scene_.SetReplaySpawnOverrides(overrides);
}

void GameSceneDebugAdapter::ExecuteDebugAction(const DebugAction& action) {
    scene_.ExecuteDebugAction(action);
}

void GameScene::SetupDebugAI_(GameApp& app) {
    debugFrameNumber_ = 0;
    debugAIEnabled_ = false;
    debugManualRecordingActive_ = false;
    debugAdapter_ = std::make_unique<GameSceneDebugAdapter>(*this);

    if (app.DebugAI()) {
        app.DebugAI()->SetAdapter(debugAdapter_.get());
        app.DebugAI()->SetEnabled(false);
    }
}

void GameScene::ShutdownDebugAI_(GameApp& app) {
    if (app.DebugAI()) {
        app.DebugAI()->SetEnabled(false);
        app.DebugAI()->SetAdapter(nullptr);
    }
    debugAIEnabled_ = false;
    debugManualRecordingActive_ = false;
    debugAdapter_.reset();
}

void GameScene::SetDebugAIEnabled_(GameApp& app, bool enabled) {
    debugAIEnabled_ = enabled;
    if (app.DebugAI()) {
        app.DebugAI()->SetEnabled(enabled);
    }
}

DebugGameState GameScene::CaptureDebugState() const {
    DebugGameState state;
    state.sceneName = "Game";
    state.frameNumber = debugFrameNumber_;
    state.fps = debugMeasuredFps_;
    state.randomSeed = debugRandomSeed_;
    GameSceneDebugPhase debugPhase = GameSceneDebugPhase::Unknown;
    switch (phase_) {
    case Phase::IntroVideo:
        debugPhase = GameSceneDebugPhase::IntroVideo;
        break;
    case Phase::Battle:
        debugPhase = GameSceneDebugPhase::Battle;
        break;
    case Phase::OutroVideo:
        debugPhase = GameSceneDebugPhase::OutroVideo;
        break;
    default:
        break;
    }
    state.gamePhase = ToDebugPhaseName(debugPhase);

    if (player_) {
        state.playerHp = player_->GetHP();
        state.playerPosition = player_->GetPos3D();
    }

    int aliveEnemyCount = 0;
    int firstAliveEnemyHp = 0;
    for (const Enemy& enemy : enemyMgr_.GetEnemies()) {
        if (!enemy.IsAlive()) {
            continue;
        }
        ++aliveEnemyCount;
        if (firstAliveEnemyHp == 0) {
            firstAliveEnemyHp = enemy.GetHP();
        }
    }
    enemyMgr_.AppendDebugEntities(state.entities);

    if (const Enemy* boss = enemyMgr_.GetBoss()) {
        state.enemyHp = boss->GetHP();
    } else {
        state.enemyHp = firstAliveEnemyHp;
    }
    state.enemyCount = aliveEnemyCount;

    state.availableActions = BuildGameSceneDebugActions(debugPhase);
    state.mapBounds = BuildGameSceneDebugMapBounds();
    state.stableStateKey = BuildGameSceneStableStateKey(state);
    state.progressKey = BuildGameSceneProgressKey(state, debugPhase);

    return state;
}

bool GameScene::RestoreDebugState(const DebugGameState& state) {
    if (state.sceneName != "Game") {
        return false;
    }

    debugFrameNumber_ = state.frameNumber;
    debugRandomSeed_ = state.randomSeed;
    debugManualRecordingActive_ = false;
    if (debugRandomSeed_ != 0) {
        std::srand(debugRandomSeed_);
    }
    isPaused_ = false;
    pauseSel_ = PauseSel::Close;
    introTime_ = 0.0f;
    outroTime_ = 0.0f;

    if (state.gamePhase == "IntroVideo") {
        phase_ = Phase::IntroVideo;
    } else if (state.gamePhase == "OutroVideo") {
        phase_ = Phase::OutroVideo;
    } else {
        phase_ = Phase::Battle;
    }

    if (player_) {
        player_->SetSpawnPos(state.playerPosition);
        player_->SetHP(state.playerHp);
    }

    if (!state.entities.empty()) {
        enemyMgr_.RestoreDebugEntities(state.entities);
    } else if (Enemy* boss = enemyMgr_.GetBoss()) {
        boss->SetHP(state.enemyHp);
    } else {
        for (Enemy& enemy : enemyMgr_.GetEnemies()) {
            if (!enemy.IsAlive()) {
                continue;
            }
            enemy.SetHP(state.enemyHp);
            break;
        }
    }

    enemyMgr_.ClearBossDefeatedFlag();
    return true;
}

void GameScene::SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) {
    enemyMgr_.SetReplaySpawnOverrides(overrides);
}

void GameScene::ExecuteDebugAction(const DebugAction& action) {
    if (action.name == "SkipIntro") {
        if (phase_ == Phase::IntroVideo) {
            phase_ = Phase::Battle;
        }
        return;
    }

    if (!player_ || phase_ != Phase::Battle || isPaused_) {
        return;
    }

    Player::PlayerInputCommand command{};

    if (action.name == "Move") {
        command.action = Player::PlayerAction::Move;
        command.horizontal = action.intParam;
        command.depth = static_cast<int>(action.floatParam);
    } else if (action.name == "Down" || action.name == "MoveBack") {
        command.action = player_->IsOnGround()
            ? Player::PlayerAction::Crouch
            : Player::PlayerAction::FastFall;
        command.down = true;
    } else if (action.name == "Jump") {
        command.action = Player::PlayerAction::Jump;
        command.jumpTriggered = true;
        command.horizontal = std::clamp(action.intParam, -1, 1);
    } else if (action.name == "AttackWeak") {
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::Weak;
        command.horizontal = std::clamp(action.intParam, -1, 1);
    } else if (action.name == "AttackTilt") {
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::Tilt;
        command.horizontal = action.intParam != 0 ? std::clamp(action.intParam, -1, 1) : player_->GetFacing();
    } else if (action.name == "AttackSmash") {
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::Smash;
        command.horizontal = action.intParam != 0 ? std::clamp(action.intParam, -1, 1) : player_->GetFacing();
    } else if (action.name == "AttackNeutralSpecial") {
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::NeutralSpecial;
        command.horizontal = 0;
    } else if (action.name == "AttackSideSpecial" || action.name == "AttackSpecial") {
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::SideSpecial;
        command.horizontal = action.intParam != 0 ? std::clamp(action.intParam, -1, 1) : player_->GetFacing();
    } else if (action.name == "Guard") {
        command.action = Player::PlayerAction::Guard;
        command.guard = true;
    } else {
        command.action = Player::PlayerAction::Idle;
    }

    player_->QueueDebugCommand(command);
}

void GameScene::FinalizeRecordedDebugAction_(DebugAction& action, unsigned int attackSerialBefore) const {
    if (!player_) {
        return;
    }

    const bool attackAction =
        action.name == "AttackWeak" ||
        action.name == "AttackSpecial" ||
        action.name == "AttackTilt" ||
        action.name == "AttackSmash" ||
        action.name == "AttackNeutralSpecial" ||
        action.name == "AttackSideSpecial";
    if (!attackAction) {
        return;
    }

    if (player_->GetAttackSerial() == attackSerialBefore ||
        player_->GetCurrentAction() != Player::PlayerAction::Attack) {
        action = { "Wait" };
        return;
    }

    action.targetId.clear();
    action.floatParam = 0.0f;

    switch (player_->GetCurrentAttackType()) {
    case Player::PlayerAttackType::Weak:
        action.name = "AttackWeak";
        action.intParam = 0;
        break;
    case Player::PlayerAttackType::Tilt:
        action.name = "AttackTilt";
        action.intParam = player_->GetFacing();
        break;
    case Player::PlayerAttackType::Smash:
        action.name = "AttackSmash";
        action.intParam = player_->GetFacing();
        break;
    case Player::PlayerAttackType::NeutralSpecial:
        action.name = "AttackNeutralSpecial";
        action.intParam = 0;
        break;
    case Player::PlayerAttackType::SideSpecial:
        action.name = "AttackSideSpecial";
        action.intParam = player_->GetFacing();
        break;
    case Player::PlayerAttackType::None:
    default:
        action = { "Wait" };
        break;
    }
}

bool GameScene::CaptureManualDebugAction_(DebugAction& outAction) const {
    if (!input_ || phase_ != Phase::Battle || isPaused_) {
        return false;
    }

    if (input_->IsKeyTrigger(DIK_U)) {
        outAction = { "AttackWeak" };
        return true;
    }
    if (input_->IsKeyTrigger(DIK_I)) {
        outAction = { "AttackSpecial" };
        return true;
    }
    if (input_->IsKeyTrigger(DIK_SPACE)) {
        outAction = { "Jump" };
        const bool left = input_->IsKeyPressed(DIK_LEFT) || input_->IsKeyPressed(DIK_A);
        const bool right = input_->IsKeyPressed(DIK_RIGHT) || input_->IsKeyPressed(DIK_D);
        if (left != right) {
            outAction.intParam = right ? +1 : -1;
        }
        return true;
    }
    if (input_->IsKeyPressed(DIK_H)) {
        outAction = { "Guard" };
        return true;
    }

    const bool left = input_->IsKeyPressed(DIK_LEFT) || input_->IsKeyPressed(DIK_A);
    const bool right = input_->IsKeyPressed(DIK_RIGHT) || input_->IsKeyPressed(DIK_D);
    const bool up = input_->IsKeyPressed(DIK_UP) || input_->IsKeyPressed(DIK_W);
    const bool down = input_->IsKeyPressed(DIK_DOWN) || input_->IsKeyPressed(DIK_S);

    if (left || right || up || down) {
        if (down) {
            outAction = { "Down" };
            return true;
        }
        outAction = { "Move" };
        if (left != right) {
            outAction.intParam = right ? +1 : -1;
        }
        if (up != down) {
            outAction.floatParam = up ? +1.0f : -1.0f;
        }
        return true;
    }

    outAction = { "Wait" };
    return true;
}

