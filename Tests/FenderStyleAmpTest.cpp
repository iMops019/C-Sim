// Verify the seed FenderStyleAmp's two knobs actually do something real:
// Gain drives the tube stage harder (measurably more 2nd-harmonic content,
// since the Koren triode curve is asymmetric), Volume scales output level
// linearly, and the stage stays stable across a guitar-range sweep at max
// settings.

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

    // --- Test 1: Gain should measurably increase 2nd-harmonic content. ---
    std::printf("=== Gain: higher Gain should add real 2nd-harmonic distortion ===\n");
    {
        constexpr double freq = 220.0;

        auto secondHarmonicRatio = [freq](float gainAmount)
        {
            FenderStyleAmp amp(sampleRate);
            amp.setGain(gainAmount);
            amp.setVolume(1.0f);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            amp.processBlock(in.data(), out.data(), numSamples);

            auto fundamental = TestUtils::goertzelMagnitude(out, freq, sampleRate);
            auto secondHarmonic = TestUtils::goertzelMagnitude(out, freq * 2.0, sampleRate);
            return secondHarmonic / std::max(1.0e-9, fundamental);
        };

        auto lowGainRatio = secondHarmonicRatio(0.0f);
        auto highGainRatio = secondHarmonicRatio(1.0f);
        std::printf("  Gain=0: 2nd/1st = %.5f, Gain=1: 2nd/1st = %.5f\n", lowGainRatio, highGainRatio);

        bool gainAddsDistortion = highGainRatio > lowGainRatio * 1.5;
        std::printf("  %s: Gain measurably increases 2nd-harmonic content\n\n",
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
