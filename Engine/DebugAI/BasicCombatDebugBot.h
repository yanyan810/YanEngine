#pragma once

#include "DebugTypes.h"
#include "IDebugBot.h"

class BasicCombatDebugBot : public IDebugBot {
public:
    bool ChooseAction(const DebugGameState& state, DebugAction& outAction) override;
    const char* Name() const override { return "BasicCombatDebugBot"; }

private:
    bool HasAction_(const DebugGameState& state, const char* actionName) const;
    bool TryChooseWallEscape_(const DebugGameState& state, DebugAction& outAction) const;
    bool TryChooseEnemyAction_(const DebugGameState& state, DebugAction& outAction) const;
};
