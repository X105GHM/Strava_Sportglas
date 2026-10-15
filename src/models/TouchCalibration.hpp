#pragma once
#include <cstdint>

struct TouchCalibration
{
    uint32_t schema{1};
    uint32_t revision{};
    uint32_t gpio{};
    uint32_t baseline{};

    bool validFor(uint32_t expectedRevision, uint32_t expectedGpio) const
    {
        return schema == 1 && revision == expectedRevision && gpio == expectedGpio && baseline > 0 && baseline < (1U << 22);
    }
};
