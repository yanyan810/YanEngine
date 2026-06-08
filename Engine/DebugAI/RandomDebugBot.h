#pragma once

#include "DebugTypes.h"

#include <random>

class RandomDebugBot {
public:
    explicit RandomDebugBot(unsigned int seed = std::random_device{}());

    bool ChooseAction(const DebugGameState& state, DebugAction& outAction);

private:
    std::mt19937 random_;
};
