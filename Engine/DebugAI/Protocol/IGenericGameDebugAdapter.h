#pragma once

#include "DebugGenericTypes.h"

class IGenericGameDebugAdapter {
public:
    virtual ~IGenericGameDebugAdapter() = default;

    virtual DebugObservation CaptureDebugObservation() const = 0;
    virtual bool ExecuteGenericDebugAction(const DebugGenericAction& action) = 0;
    virtual bool SetDebugSimulationPaused(bool) { return false; }
};
