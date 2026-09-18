// Verify the seed FenderStyleAmp's two knobs actually do something real:
// Gain drives the (now 2-stage) cascade harder - measurably more total
// harmonic content - Volume scales output level linearly, and the stage
// stays stable across a guitar-range sweep at max settings.
//
// Uses combined (2nd+3rd)/fundamental rather than 2nd harmonic alone:
// with the real V1->V2 cascade (see FenderStyleAmp.h), a higher Gain
// setting shifts weight from even- to odd-order harmonics as the second
// stage's own saturation kicks in harder, so an isolated 2nd-harmonic
// ratio is not a monotonic proxy for "more driven" any more - the same
// "never compare differently-saturated nonlinear stages with a single
// harmonic in isolation" lesson already learned from this project's
// Fortin/Mesa and Diezel/Mesa comparisons.

#include "../Source/dsp/FenderStyleAmp.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 8192;
}

int main()
{
    bool allPassed = true;

    // --- Test 1: Gain should measurably increase total harmonic content. ---
    std::printf("=== Gain: higher Gain should add real harmonic distortion ===\n");
    {
        constexpr double freq = 220.0;

        auto harmonicRatio = [freq](float gainAmount)
        {
            FenderStyleAmp amp(sampleRate);
            amp.setGain(gainAmount);
            amp.setVolume(1.0f);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            auto fundamental = TestUtils::goertzelMagnitude(out, freq, sampleRate);
            auto h2 = TestUtils::goertzelMagnitude(out, freq * 2.0, sampleRate);
            auto h3 = TestUtils::goertzelMagnitude(out, freq * 3.0, sampleRate);
            return (h2 + h3) / std::max(1.0e-9, fundamental);
        };

        auto lowGainRatio = harmonicRatio(0.0f);
        auto highGainRatio = harmonicRatio(1.0f);
        std::printf("  Gain=0: harmonics/fundamental = %.5f, Gain=1: harmonics/fundamental = %.5f\n",
                     lowGainRatio, highGainRatio);

        bool gainAddsDistortion = highGainRatio > lowGainRatio * 1.5;
        std::printf("  %s: Gain measurably increases harmonic content\n\n",
                     gainAddsDistortion ? "OK" : "FAILED");
        allPassed &= gainAddsDistortion;
    }

    // --- Test 2: Volume should scale output level ~linearly. ---
    std::printf("=== Volume: output level should scale linearly ===\n");
    {
        constexpr double freq = 220.0;

        auto fundamentalAt = [freq](float volumeAmount)
        {
            FenderStyleAmp amp(sampleRate);
            amp.setGain(0.0f); // no tube nonlinearity in the way - isolate Volume's own effect
            amp.setVolume(volumeAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            return TestUtils::goertzelMagnitude(out, freq, sampleRate);
        };

        auto quarterVolume = fundamentalAt(0.25f);
        auto fullVolume = fundamentalAt(1.0f);
        auto ratio = fullVolume / std::max(1.0e-9, quarterVolume);
        std::printf("  Volume=0.25: %.5f, Volume=1.0: %.5f, ratio=%.3f (expect ~4.0)\n",
                     quarterVolume, fullVolume, ratio);

        bool volumeIsLinear = ratio > 3.5 && ratio < 4.5;
        std::printf("  %s: Volume scales output level linearly\n\n", volumeIsLinear ? "OK" : "FAILED");
        allPassed &= volumeIsLinear;
    }

    // --- Test 3: stability across a guitar-range sweep at max settings. ---
    std::printf("=== Stability across a guitar-range sweep, max Gain/Volume ===\n");
    bool rangeStable = true;

    for (double freq = 80.0; freq <= 1200.0; freq += 80.0)
    {
        FenderStyleAmp amp(sampleRate);
        amp.setGain(1.0f);
        amp.setVolume(1.0f);

        std::vector<float> sweepIn(4096), sweepOut(4096);
        for (int n = 0; n < 4096; ++n)
            sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.9 * std::sin(2.0 * M_PI * freq * n / sampleRate));

        amp.processBlock(sweepIn.data(), sweepOut.data(), 4096);

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
