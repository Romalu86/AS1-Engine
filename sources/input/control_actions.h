#pragma once

#include "core/configuration.h"
#include "core/types.h"

namespace as1::input
{
    void ApplyControlConfiguration(const as1::core::ControlConfiguration& config);

    void applyStartupControlProfileBlock(const as1::core::ControlConfiguration& config);
}
