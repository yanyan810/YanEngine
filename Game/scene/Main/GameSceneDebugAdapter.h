#pragma once

#include "DebugAI/IGameDebugAdapter.h"

class GameScene;

class GameSceneDebugAdapter : public IGameDebugAdapter {
public:
    explicit GameSceneDebugAdapter(GameScene& scene);

    DebugGameState CaptureDebugState() const override;
    bool RestoreDebugState(const DebugGameState& state) override;
    void SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) override;
    void ExecuteDebugAction(const DebugAction& action) override;

private:
    GameScene& scene_;
};
