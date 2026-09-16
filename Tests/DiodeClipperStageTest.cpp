// Verifies DiodeClipperStage in isolation before it's blended into
// MetalPreampChain: symmetric clipping (unlike the tube stage's
// asymmetric curve - antiparallel diodes clip both polarities the same
// way), and stability of the per-sample Newton-Raphson solve.

#include "../Source/dsp/DiodeClipperStage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main()
{
    bool allPassed = true;

    DiodeClipperStage stage;

    std::printf("=== Static transfer curve (input -> output) ===\n");
    std::printf("Look for: a hard, roughly SYMMETRIC knee (unlike the tube's\n");
    std::printf("asymmetric cutoff-vs-compression curve).\n\n");

    for (int i = -10; i <= 10; ++i)
    {
        DiodeClipperStage fresh; // avoid solver history bias between sweep points
        float x = static_cast<float>(i) / 10.0f;
        float y = fresh.processSample(x);
        std::printf("  in=%6.2f  out=%9.5f\n", x, y);
    }

    DiodeClipperStage posProbe, negProbe;
    auto posOut = posProbe.processSample(1.0f);
    auto negOut = negProbe.processSample(-1.0f);
    auto symmetryRatio = std::abs(posOut) / std::max(1.0e-6f, std::abs(negOut));
    std::printf("\nout(+1)=%.5f  out(-1)=%.5f  symmetry ratio=%.3f (should be close to 1.0)\n",
                posOut, negOut, symmetryRatio);
    bool symmetric = symmetryRatio > 0.9f && symmetryRatio < 1.1f;
    std::printf("%s\n\n", symmetric ? "OK: clips symmetrically" : "FAILED: not symmetric");
    allPassed &= symmetric;

    // Sine sweep at increasing drive: check for NaN/Inf and convergence.
    constexpr double sampleRate = 48000.0;
    constexpr double freq = 220.0;
    constexpr int numSamples = 4800;

    bool sawNonFinite = false;
    for (float amplitude : { 0.1f, 0.5f, 1.0f, 3.0f, 10.0f })
    {
        DiodeClipperStage sweepStage;
        double peakOut = 0.0;

        for (int n = 0; n < numSamples; ++n)
        {
            auto x = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * n / sampleRate));
            auto y = sweepStage.processSample(x);

            if (! std::isfinite(y))
                sawNonFinite = true;

            peakOut = std::max(peakOut, static_cast<double>(std::abs(y)));
        }

        std::printf("  amplitude=%.1f -> peak out=%.4f\n", amplitude, peakOut);
    }

    std::printf("\n%s: no NaN/Inf across drive levels 0.1 to 10.0\n",
                sawNonFinite ? "FAILED" : "OK");
    allPassed &= ! sawNonFinite;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
