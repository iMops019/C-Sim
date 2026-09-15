#include "PitchDetector.h"

#include <cmath>
#include <vector>

namespace PitchDetector
{

double detectPitchYin(const float* samples, int numSamples, double sampleRate)
{
    constexpr float threshold = 0.15f;
    const int maxTau = numSamples / 2;

    if (maxTau < 2)
        return 0.0;

    std::vector<float> diff(static_cast<size_t>(maxTau), 0.0f);

    // Step 1: difference function.
    for (int tau = 1; tau < maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < maxTau; ++j)
        {
            auto delta = samples[j] - samples[j + tau];
            sum += delta * delta;
        }
        diff[static_cast<size_t>(tau)] = sum;
    }

    // Step 2: cumulative mean normalized difference function.
    std::vector<float> cmnd(static_cast<size_t>(maxTau), 1.0f);
    float runningSum = 0.0f;
    for (int tau = 1; tau < maxTau; ++tau)
    {
        runningSum += diff[static_cast<size_t>(tau)];
        cmnd[static_cast<size_t>(tau)] = runningSum > 0.0f
                                              ? diff[static_cast<size_t>(tau)] * static_cast<float>(tau) / runningSum
                                              : 1.0f;
    }

    // Step 3: absolute threshold - first dip below threshold, refined to
    // the following local minimum.
    int tauEstimate = -1;
    for (int tau = 2; tau < maxTau - 1; ++tau)
    {
        if (cmnd[static_cast<size_t>(tau)] < threshold)
        {
            while (tau + 1 < maxTau && cmnd[static_cast<size_t>(tau + 1)] < cmnd[static_cast<size_t>(tau)])
                ++tau;

            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate == -1)
        return 0.0;

    // Step 4: parabolic interpolation for sub-sample accuracy.
    auto betterTau = static_cast<float>(tauEstimate);
    auto s0 = cmnd[static_cast<size_t>(tauEstimate - 1)];
    auto s1 = cmnd[static_cast<size_t>(tauEstimate)];
    auto s2 = cmnd[static_cast<size_t>(tauEstimate + 1)];
    auto denom = 2.0f * (2.0f * s1 - s2 - s0);

    if (std::abs(denom) > 1.0e-9f)
    {
        auto adjustment = (s2 - s0) / denom;
        if (std::isfinite(adjustment))
            betterTau += adjustment;
    }

    if (betterTau <= 0.0f)
        return 0.0;

    return sampleRate / static_cast<double>(betterTau);
}

}
