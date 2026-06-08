#include "GameScene.h"

#include "DebugAI/DebugAIManager.h"
#include "DebugAI/IGameDebugAdapter.h"
#include "GameApp.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

class GameSceneDebugAdapter : public IGameDebugAdapter {
public:
    explicit GameSceneDebugAdapter(GameScene& scene)
        : scene_(scene) {
    }

    DebugGameState CaptureDebugState() const override {
        return scene_.CaptureDebugState();
    }

    void ExecuteDebugAction(const DebugAction& action) override {
        scene_.ExecuteDebugAction(action);
    }

private:
    GameScene& scene_;
};

}

void GameScene::SetupDebugAI_(GameApp& app) {
    debugFrameNumber_ = 0;
    debugAIEnabled_ = false;
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
    state.fps = 60.0f;

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

    if (const Enemy* boss = enemyMgr_.GetBoss()) {
        state.enemyHp = boss->GetHP();
    } else {
        state.enemyHp = firstAliveEnemyHp;
    }
    state.enemyCount = aliveEnemyCount;

    state.availableActions = {
        { "MoveLeft" },
        { "MoveRight" },
        { "MoveForward" },
        { "MoveBack" },
        { "Jump" },
        { "AttackWeak" },
        { "AttackSpecial" },
        { "Guard" },
        { "Wait" },
    };

    if (phase_ == Phase::IntroVideo) {
        state.availableActions.push_back({ "SkipIntro" });
    }

    state.mapBounds.enabled = true;
    state.mapBounds.min = { -20.0f, -1.0f, -15.0f };
    state.mapBounds.max = { 20.0f, 12.0f, 20.0f };

    const Vector3 pos = state.playerPosition;
    state.stableStateKey =
        std::to_string(state.playerHp) + ":" +
        std::to_string(state.enemyHp) + ":" +
        std::to_string(state.enemyCount) + ":" +
        std::to_string(static_cast<int>(std::floor(pos.x))) + ":" +
        std::to_string(static_cast<int>(std::floor(pos.z)));

    state.progressKey =
        state.sceneName + ":" +
        std::to_string(static_cast<int>(phase_)) + ":" +
        std::to_string(state.enemyHp) + ":" +
        std::to_string(state.enemyCount);

    return state;
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

    if (action.name == "MoveLeft") {
        command.action = Player::PlayerAction::Move;
        command.horizontal = -1;
    } else if (action.name == "MoveRight") {
        command.action = Player::PlayerAction::Move;
        command.horizontal = +1;
    } else if (action.name == "MoveForward") {
        command.action = Player::PlayerAction::Move;
        command.depth = +1;
    } else if (action.name == "MoveBack") {
        command.action = Player::PlayerAction::Move;
        command.depth = -1;
    } else if (action.name == "Jump") {
        command.action = Player::PlayerAction::Jump;
        command.jumpTriggered = true;
    } else if (action.name == "AttackWeak") {
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::Weak;
        command.horizontal = player_->GetFacing();
    } else if (action.name == "AttackSpecial") {
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::SideSpecial;
        command.horizontal = player_->GetFacing();
    } else if (action.name == "Guard") {
        command.action = Player::PlayerAction::Guard;
        command.guard = true;
    } else {
        command.action = Player::PlayerAction::Idle;
    }

    player_->QueueDebugCommand(command);
}

