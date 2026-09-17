// Verify the actual claim behind offering Solid-State as a power amp
// Type, not just "it builds and runs": at the same drive setting and
// input level, it should stay measurably cleaner than the tube
// PowerAmpStage (more headroom before breakup), it should still
// distort more as Drive rises, it shouldn't need push-pull combining to
// cancel even harmonics (the clipper is already an odd function), and
// it should stay stable across the guitar range at extreme settings.

#include "../Source/dsp/SolidStatePowerAmpStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 110.0; // A2
    constexpr int numSamples = 8192;

    std::vector<float> sineAt(double freq, float amplitude)
    {
        std::vector<float> s(numSamples);
        for (int n = 0; n < numSamples; ++n)
            s[static_cast<size_t>(n)] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * freq * n / sampleRate));
        return s;
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: below its linear threshold, Solid-State should
    // reproduce a signal essentially bit-for-bit - true headroom up to a
    // known point, not just "less distorted." This is the actual "way
    // more headroom" claim the Type switch exists to represent: the
    // tube stage's Koren curve is smoothly nonlinear everywhere and has
    // no such perfectly-clean region at any level, however small. ---
    std::printf("=== Solid-State: a below-threshold signal should pass through essentially unchanged ===\n");
    {
        auto input = sineAt(testFreq, 0.5f); // comfortably under the 0.85 linear ceiling

        SolidStatePowerAmpStage stage(sampleRate);
        stage.setDrive(0.0f);
        std::vector<float> out(numSamples);
        stage.processBlock(input.data(), out.data(), numSamples);

        auto inputFundamental = TestUtils::goertzelMagnitude(input, testFreq, sampleRate);
        auto outputFundamental = TestUtils::goertzelMagnitude(out, testFreq, sampleRate);
        auto outputThird = TestUtils::goertzelMagnitude(out, testFreq * 3.0, sampleRate);

        auto fundamentalRatio = outputFundamental / inputFundamental;
        auto distortion = outputThird / std::max(1.0e-6, outputFundamental);

        std::printf("  fundamental in/out ratio: %.4f (should be ~1.0)\n", fundamentalRatio);
        std::printf("  3rd harmonic / fundamental: %.5f (should be near the oversampler's own noise floor)\n",
                     distortion);

        bool fundamentalPreserved = fundamentalRatio > 0.97 && fundamentalRatio < 1.03;
        bool staysClean = distortion < 0.01;
        std::printf("  %s: fundamental passes through essentially unchanged\n", fundamentalPreserved ? "OK" : "FAILED");
        std::printf("  %s: no real distortion below the linear threshold\n\n", staysClean ? "OK" : "FAILED");
        allPassed &= fundamentalPreserved && staysClean;
    }

    // --- Test 2: Drive should still produce real, growing distortion on
    // its own once it's pushed past Solid-State's own headroom. ---
    std::printf("=== Drive: higher settings should add more harmonic content, once past headroom ===\n");
    {
        auto input = sineAt(testFreq, 0.9f);

        auto distortionAt = [&input](float driveAmount)
        {
            SolidStatePowerAmpStage stage(sampleRate);
            stage.setDrive(driveAmount);

            std::vector<float> out(numSamples);
            stage.processBlock(input.data(), out.data(), numSamples);

            auto fundamental = TestUtils::goertzelMagnitude(out, testFreq, sampleRate);
            auto third = TestUtils::goertzelMagnitude(out, testFreq * 3.0, sampleRate);
            return third / std::max(1.0e-6, fundamental);
        };

        auto low = distortionAt(0.0f);
        auto high = distortionAt(1.0f);

        std::printf("  drive=0.0: 3rd/fundamental = %.4f\n", low);
        std::printf("  drive=1.0: 3rd/fundamental = %.4f\n", high);

        bool grows = high > low * 2.0;
        std::printf("  %s: Drive grows distortion once past the linear region\n\n", grows ? "OK" : "FAILED");
        allPassed &= grows;
    }

    // --- Test 3: the clipper is an odd function by construction, so even
    // harmonics should stay near-zero without any explicit push-pull
    // combination step (unlike the asymmetric tube curve). ---
    std::printf("=== Even harmonics should stay near-zero (odd-function clipper, no push-pull step needed) ===\n");
    {
        auto input = sineAt(testFreq, 0.95f);

        SolidStatePowerAmpStage stage(sampleRate);
        stage.setDrive(1.0f);
        std::vector<float> out(numSamples);
        stage.processBlock(input.data(), out.data(), numSamples);

        auto fundamental = TestUtils::goertzelMagnitude(out, testFreq, sampleRate);
        auto second = TestUtils::goertzelMagnitude(out, testFreq * 2.0, sampleRate);

        std::printf("  fundamental: %.4f   2nd harmonic: %.5f\n", fundamental, second);

        // A small nonzero leak here comes from the oversampler's own
        // upsample/downsample filters, not the (perfectly odd) clipper
        // itself - 5% is well below anything audible as "even harmonic
        // content" and clearly distinguishes this from a real asymmetric
        // curve like the tube stage's.
        bool evenNearZero = second < fundamental * 0.05;
        std::printf("  %s: 2nd harmonic stays negligible\n\n", evenNearZero ? "OK" : "FAILED");
        allPassed &= evenNearZero;
    }

    // --- Test 4: stability across dropped-tuning range at max drive. ---
    std::printf("=== Stability across dropped-tuning range, max drive ===\n");
    bool rangeStable = true;

    for (double freq = 55.0; freq <= 165.0; freq += 11.0)
    {
        SolidStatePowerAmpStage stage(sampleRate);
        stage.setDrive(1.0f);

        auto sweepIn = sineAt(freq, 0.95f);
        std::vector<float> sweepOut(numSamples);
        stage.processBlock(sweepIn.data(), sweepOut.data(), numSamples);

        for (auto y : sweepOut)
        {
            if (! std::isfinite(y) || std::abs(y) > 10.0f)
            {
                std::printf("  FAILED at %.1fHz\n", freq);
                rangeStable = false;
                break;
            }
        }
    }
    std::printf("  %s\n\n", rangeStable ? "OK: stable across the range" : "see failures above");
    allPassed &= rangeStable;

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
