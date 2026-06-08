#pragma once

#include "DebugTypes.h"

class IGameDebugAdapter {
public:
    virtual ~IGameDebugAdapter() = default;

    virtual DebugGameState CaptureDebugState() const = 0;
    virtual void ExecuteDebugAction(const DebugAction& action) = 0;
};

