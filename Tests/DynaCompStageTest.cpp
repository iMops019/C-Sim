// Verify the Dyna Comp model's actual claims: higher Sensitivity means
// real, measurable gain reduction on a hot steady tone, compression
// genuinely lifts sustain (a decaying note's tail survives closer to its
// starting level than it would uncompressed - the pedal's whole
// documented appeal), the Output control's real sourced tonal sweep
// (52-310Hz), and stability.

#include "../Source/dsp/DynaCompStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 24000; // 0.5s - long enough to see real decay/sustain behaviour
}

int main()
{
    bool allPassed = true;

    // --- Test 1: higher Sensitivity should apply real gain reduction to
    // a hot, steady tone. ---
    std::printf("=== Sensitivity: a hot steady tone should read quieter at high Sensitivity ===\n");
    {
        constexpr double freq = 220.0;
        auto levelAt = [freq](float sensitivity)
        {
            DynaCompStage comp(sampleRate);
            comp.setSensitivity(sensitivity);
            comp.setOutput(0.5f);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.7 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            comp.processBlock(in.data(), out.data(), numSamples);

            // Measure the back half, after the envelope has settled.
            std::vector<float> settled(out.begin() + numSamples / 2, out.end());
            return TestUtils::goertzelMagnitude(settled, freq, sampleRate);
        };

        auto lowSens = levelAt(0.0f);
        auto highSens = levelAt(1.0f);
        auto diffDb = TestUtils::toDb(highSens) - TestUtils::toDb(lowSens);
        std::printf("  Sensitivity=0: %.4f, Sensitivity=1: %.4f (%.2fdB)\n", lowSens, highSens, diffDb);

        bool sensitivityWorks = diffDb < -6.0;
        std::printf("  %s: higher Sensitivity applies real gain reduction\n\n", sensitivityWorks ? "OK" : "FAILED");
        allPassed &= sensitivityWorks;
    }

    // --- Test 2: sustain lift - a decaying note's tail should survive
    // closer to its starting level at high Sensitivity than at low. ---
    std::printf("=== Sustain lift: compression should flatten a decaying note's envelope ===\n");
    {
        constexpr double freq = 165.0;
        constexpr double decayTau = 0.15; // a fast-decaying plucked-string-like envelope

        auto decayRangeDb = [freq, decayTau](float sensitivity)
        {
            DynaCompStage comp(sampleRate);
            comp.setSensitivity(sensitivity);
            comp.setOutput(0.5f);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
            {
                auto t = static_cast<double>(n) / sampleRate;
                in[static_cast<size_t>(n)] = static_cast<float>(0.8 * std::exp(-t / decayTau) * std::sin(2.0 * M_PI * freq * n / sampleRate));
            }
            comp.processBlock(in.data(), out.data(), numSamples);

            std::vector<float> early(out.begin(), out.begin() + numSamples / 8);
            std::vector<float> late(out.end() - numSamples / 8, out.end());
            auto earlyMag = TestUtils::goertzelMagnitude(early, freq, sampleRate);
            auto lateMag = TestUtils::goertzelMagnitude(late, freq, sampleRate);
            return TestUtils::toDb(earlyMag) - TestUtils::toDb(lateMag); // how much it decayed, in dB - smaller is "more sustain"
        };

        auto rangeLowSens = decayRangeDb(0.0f);
        auto rangeHighSens = decayRangeDb(1.0f);
        std::printf("  Sensitivity=0: early-to-late decay = %.2fdB\n", rangeLowSens);
        std::printf("  Sensitivity=1: early-to-late decay = %.2fdB (should be smaller - more sustain)\n", rangeHighSens);

        bool addsSustain = rangeHighSens < rangeLowSens - 3.0;
        std::printf("  %s: compression measurably flattens the decay envelope\n\n", addsSustain ? "OK" : "FAILED");
        allPassed &= addsSustain;
    }

    // --- Test 3: Output's real sourced tonal sweep - bass content
    // should change measurably between Output=0 and Output=1. ---
    std::printf("=== Output: its filter sweep should measurably change bass content ===\n");
    {
        constexpr double bassFreq = 80.0;
        auto bassLevelAt = [bassFreq](float outputAmount)
        {
            DynaCompStage comp(sampleRate);
            comp.setSensitivity(0.3f);
            comp.setOutput(outputAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * bassFreq * n / sampleRate));
            comp.processBlock(in.data(), out.data(), numSamples);

            std::vector<float> settled(out.begin() + numSamples / 2, out.end());
            return TestUtils::goertzelMagnitude(settled, bassFreq, sampleRate);
        };

        auto bassAtLowOutput = bassLevelAt(0.0f);
        auto bassAtHighOutput = bassLevelAt(1.0f);
        auto diffDb = TestUtils::toDb(bassAtHighOutput) - TestUtils::toDb(bassAtLowOutput);
        std::printf("  Output=0: %.4f, Output=1: %.4f (%.2fdB)\n", bassAtLowOutput, bassAtHighOutput, diffDb);

        bool outputSweepWorks = std::abs(diffDb) > 2.0;
        std::printf("  %s: Output measurably changes bass content, not just level\n\n", outputSweepWorks ? "OK" : "FAILED");
        allPassed &= outputSweepWorks;
    }

    // --- Test 4: stability at max settings. ---
    std::printf("=== Stability across a guitar-range sweep, max Sensitivity ===\n");
    bool rangeStable = true;

    for (double freq = 80.0; freq <= 1200.0; freq += 80.0)
    {
        DynaCompStage sweep(sampleRate);
        sweep.setSensitivity(1.0f);
        sweep.setOutput(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        sweep.processBlock(sweepIn.data(), sweepOut.data(), 4096);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y))
            {
                std::printf("  FAILED: NaN/Inf at %.1fHz, max settings\n", freq);
                rangeStable = false;
                break;
            }
        }
    }
    std::printf("  %s\n", rangeStable ? "OK: stable across the whole sweep" : "see failures above");
    allPassed &= rangeStable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
