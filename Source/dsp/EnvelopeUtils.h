#pragma once

#include <cmath>

// Shared one-pole envelope-follower coefficient math for the dsp toolkit.
namespace EnvelopeUtils
{
    // Coefficient for a one-pole follower y += coeff*(target-y) that
    // reaches ~63% of a step change in `timeSeconds`.
    inline double timeToCoeff(double timeSeconds, double sampleRate) noexcept
    {
        return 1.0 - std::exp(-1.0 / (timeSeconds * sampleRate));
    }
}
