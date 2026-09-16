// Standalone diagnostic for KorenTriodeStage - step 1 of the amp-modeling
// build order: verify one gain stage in isolation before chaining anything.
// Run it and read the printed curve/numbers; this is a console tool, not
// an automated pass/fail test suite.

#include "../Source/dsp/KorenTriodeStage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main()
{
    KorenTriodeStage stage;

    std::printf("=== Static transfer curve (input -> output) ===\n");
    std::printf("Look for: smooth curve, NOT symmetric about 0 (positive and\n");
    std::printf("negative sides should clip differently).\n\n");

    for (int i = -10; i <= 10; ++i)
    {
        float x = static_cast<float>(i) / 10.0f;
        float y = stage.processSample(x);
        std::printf("  in=%6.2f  out=%9.5f\n", x, y);
    }

    float posOut = stage.processSample(1.0f);
    float negOut = stage.processSample(-1.0f);
    auto asymmetryRatio = std::abs(posOut) / std::max(1.0e-6f, std::abs(negOut));

    std::printf("\nout(+1)=%.5f  out(-1)=%.5f  asymmetry ratio=%.3f", posOut, negOut, asymmetryRatio);
    std::printf(" (1.0 would mean symmetric - should NOT be close to 1.0)\n");

    constexpr double sampleRate = 48000.0;
    constexpr double freq = 110.0; // A2 - a dropped-tuning-ish low note
    constexpr int numSamples = 4800;

    double peakIn = 0.0, peakOut = 0.0, sumSq = 0.0;
    bool sawNonFinite = false;

    for (int n = 0; n < numSamples; ++n)
    {
        auto x = static_cast<float>(0.8 * std::sin(2.0 * M_PI * freq * static_cast<double>(n) / sampleRate));
        float y = stage.processSample(x);

        if (! std::isfinite(y))
            sawNonFinite = true;

        peakIn = std::max(peakIn, static_cast<double>(std::abs(x)));
        peakOut = std::max(peakOut, static_cast<double>(std::abs(y)));
        sumSq += static_cast<double>(y) * static_cast<double>(y);
    }

    auto rms = std::sqrt(sumSq / numSamples);

    std::printf("\n=== 110Hz sine test (0.8 amplitude, %d samples @ %.0fHz) ===\n", numSamples, sampleRate);
    std::printf("  peak in=%.4f  peak out=%.4f  rms out=%.4f  non-finite samples: %s\n",
                peakIn, peakOut, rms, sawNonFinite ? "YES <-- FAIL" : "no");

    if (sawNonFinite)
    {
        std::printf("\nFAILED: stage produced NaN/Inf.\n");
        return 1;
    }

    std::printf("\nOK: no NaN/Inf. Read the curve above to judge the clipping shape.\n");
    return 0;
}
