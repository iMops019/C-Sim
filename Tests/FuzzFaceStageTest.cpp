// Verify the Fuzz Face model's actual claims: real harmonic content at
// moderate drive, a genuine even-order harmonic from the asymmetric
// clipper (same proof technique as MorningGloryStageTest - a symmetric
// clipper driven by a pure sine mathematically cannot produce one), the
// Fuzz knob genuinely increasing saturation, the documented touch/volume
// sensitivity (quiet input should read measurably cleaner - relative to
// its own level - than loud input at the SAME Fuzz setting, not just
// proportionally so), and stability.

#include "../Source/dsp/FuzzFaceStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 220.0; // A3
    constexpr int numSamples = 8192;

    double harmonicRatioAt(float fuzz, double amplitude)
    {
        FuzzFaceStage stage(sampleRate);
        stage.setFuzz(fuzz);

        std::vector<float> input(numSamples), output(numSamples);
        for (int n = 0; n < numSamples; ++n)
            input[static_cast<size_t>(n)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        // Run a silent warm-up block first so the envelope follower settles
        // to this amplitude rather than reading the previous test's state.
        std::vector<float> warmup(numSamples), warmupOut(numSamples);
        for (int n = 0; n < numSamples; ++n)
            warmup[static_cast<size_t>(n)] = input[static_cast<size_t>(n)];
        stage.processBlock(warmup.data(), warmupOut.data(), numSamples);

        stage.processBlock(input.data(), output.data(), numSamples);

        auto fundamental = TestUtils::goertzelMagnitude(output, testFreq, sampleRate);
        auto h2 = TestUtils::goertzelMagnitude(output, testFreq * 2.0, sampleRate);
        auto h3 = TestUtils::goertzelMagnitude(output, testFreq * 3.0, sampleRate);
        return (h2 + h3) / std::max(fundamental, 1.0e-6);
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: fundamental survival and real harmonic generation. ---
    std::printf("=== Sustained %.2fHz tone, Fuzz=0.5, moderate amplitude ===\n", testFreq);
    {
        FuzzFaceStage stage(sampleRate);
        stage.setFuzz(0.5f);

        std::vector<float> input(numSamples), output(numSamples);
        for (int n = 0; n < numSamples; ++n)
            input[static_cast<size_t>(n)] = static_cast<float>(0.4 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));

        stage.processBlock(input.data(), output.data(), numSamples);

        auto fundamental = TestUtils::goertzelMagnitude(output, testFreq, sampleRate);
        auto h2 = TestUtils::goertzelMagnitude(output, testFreq * 2.0, sampleRate);
        auto h3 = TestUtils::goertzelMagnitude(output, testFreq * 3.0, sampleRate);

        std::printf("  fundamental: %.5f, 2nd: %.5f, 3rd: %.5f\n", fundamental, h2, h3);
        bool fundamentalSurvives = fundamental > 0.05;
        bool distorting = (h2 + h3) > 0.02;
        std::printf("  %s: fundamental survives\n", fundamentalSurvives ? "OK" : "FAILED");
        std::printf("  %s: stage generates real harmonic content\n\n", distorting ? "OK" : "FAILED");
        allPassed &= fundamentalSurvives && distorting;
    }

    // --- Test 2: even-harmonic signature. ---
    std::printf("=== Even-order harmonic: asymmetric clipping should produce a real 2nd harmonic ===\n");
    {
        FuzzFaceStage stage(sampleRate);
        stage.setFuzz(0.6f);

        std::vector<float> input(numSamples), output(numSamples);
        for (int n = 0; n < numSamples; ++n)
            input[static_cast<size_t>(n)] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * testFreq * n / sampleRate));
        stage.processBlock(input.data(), output.data(), numSamples);

        auto h2 = TestUtils::goertzelMagnitude(output, testFreq * 2.0, sampleRate);
        std::printf("  2nd harmonic magnitude: %.5f (threshold 0.01)\n", h2);
        bool hasEvenHarmonic = h2 > 0.01;
        std::printf("  %s: a real, non-negligible even-order harmonic is present\n\n", hasEvenHarmonic ? "OK" : "FAILED");
        allPassed &= hasEvenHarmonic;
    }

    // --- Test 3: Fuzz knob genuinely increases saturation at a fixed,
    // loud input level (well above the touch-sensitivity band so both
    // runs are fully "opened up"). ---
    std::printf("=== Fuzz knob: higher Fuzz should distort more at the same (loud) input level ===\n");
    {
        auto lowFuzz = harmonicRatioAt(0.1f, 0.6);
        auto highFuzz = harmonicRatioAt(1.0f, 0.6);
        std::printf("  Fuzz=0.1: harmonics/fundamental = %.4f\n", lowFuzz);
        std::printf("  Fuzz=1.0: harmonics/fundamental = %.4f (should be well above Fuzz=0.1)\n", highFuzz);

        bool fuzzWorks = highFuzz > lowFuzz * 1.3;
        std::printf("  %s: Fuzz knob genuinely increases saturation\n\n", fuzzWorks ? "OK" : "FAILED");
        allPassed &= fuzzWorks;
    }

    // --- Test 4: the real, documented touch-sensitivity claim - at the
    // SAME Fuzz setting, a quiet input should read measurably cleaner
    // (lower harmonics/fundamental) than a loud one, by a wide margin -
    // not just the mild "small signal is less clipped" effect any static
    // clipper already has. ---
    std::printf("=== Touch sensitivity: quiet input should be dramatically cleaner than loud, same Fuzz ===\n");
    {
        constexpr float fuzzSetting = 0.7f;
        auto quietRatio = harmonicRatioAt(fuzzSetting, 0.03);
        auto loudRatio = harmonicRatioAt(fuzzSetting, 0.5);
        std::printf("  quiet (0.03 amplitude): harmonics/fundamental = %.4f\n", quietRatio);
        std::printf("  loud  (0.50 amplitude): harmonics/fundamental = %.4f\n", loudRatio);

        bool quietIsClean = quietRatio < 0.15;
        bool bigJump = loudRatio > quietRatio * 3.0;
        std::printf("  %s: quiet input stays clean\n", quietIsClean ? "OK" : "FAILED");
        std::printf("  %s: loud input is dramatically more distorted, not just proportionally\n\n",
                     bigJump ? "OK" : "FAILED");
        allPassed &= quietIsClean && bigJump;
    }

    // --- Test 5: stability sweep at max Fuzz. ---
    std::printf("=== Stability across a guitar-range sweep, max Fuzz ===\n");
    bool rangeStable = true;
    for (double freq = 80.0; freq <= 1200.0; freq += 80.0)
    {
        FuzzFaceStage sweep(sampleRate);
        sweep.setFuzz(1.0f);

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
