// Step 8 (emo build): verify the dynamic gain stage actually behaves
// dynamically - quiet playing should stay clean-ish (low harmonic
// content), harder playing should show real breakup (much more harmonic
// content), which is the entire point of this stage over a fixed-curve
// distortion.

#include "../Source/dsp/DynamicGainStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 196.0; // G3-ish, a single-coil-relevant mid-range note
    constexpr int numSamples = 8192;

    double distortionAt(float amplitude)
    {
        DynamicGainStage stage(sampleRate);
        std::vector<float> input(numSamples), output(numSamples);
        for (int n = 0; n < numSamples; ++n)
            input[static_cast<size_t>(n)] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        stage.processBlock(input.data(), output.data(), numSamples);

        auto fundamental = TestUtils::goertzelMagnitude(output, testFreq, sampleRate);
        auto second = TestUtils::goertzelMagnitude(output, testFreq * 2.0, sampleRate);
        auto third = TestUtils::goertzelMagnitude(output, testFreq * 3.0, sampleRate);

        // THD-ish ratio: harmonic energy relative to the fundamental.
        return (second + third) / std::max(1.0e-6, fundamental);
    }
}

int main()
{
    bool allPassed = true;

    std::printf("=== Dynamic response: quiet vs. hard playing ===\n");

    auto quietDistortion = distortionAt(0.05f);
    auto mediumDistortion = distortionAt(0.3f);
    auto loudDistortion = distortionAt(0.9f);

    std::printf("  quiet  (amp=0.05): harmonic/fundamental ratio = %.4f\n", quietDistortion);
    std::printf("  medium (amp=0.30): harmonic/fundamental ratio = %.4f\n", mediumDistortion);
    std::printf("  loud   (amp=0.90): harmonic/fundamental ratio = %.4f\n", loudDistortion);

    bool staysCleanQuiet = quietDistortion < 0.15;
    bool growsWithLevel = mediumDistortion > quietDistortion && loudDistortion > mediumDistortion;
    std::printf("  %s: stays clean-ish at quiet playing\n", staysCleanQuiet ? "OK" : "FAILED");
    std::printf("  %s: distortion grows with playing dynamics (not a fixed curve)\n\n",
                growsWithLevel ? "OK" : "FAILED");
    allPassed &= staysCleanQuiet && growsWithLevel;

    // --- Stability sweep ---
    std::printf("=== Stability across guitar range, max sensitivity ===\n");
    bool stable = true;

    for (double freq = 82.0; freq <= 1200.0; freq *= 1.6)
    {
        DynamicGainStage stage(sampleRate);
        stage.setSensitivity(1.0f);
        stage.setBaseDrive(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        stage.processBlock(sweepIn.data(), sweepOut.data(), 4096);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y))
            {
                std::printf("  FAILED: NaN/Inf at %.1fHz\n", freq);
                stable = false;
                break;
            }
        }
    }
    std::printf("  %s\n", stable ? "OK: stable across the guitar range" : "see failures above");
    allPassed &= stable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
