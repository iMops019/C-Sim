// Verify the Precision Drive model's actual claims: Drive should produce
// real, growing harmonic saturation; Attack should measurably shift how
// much low end gets through (thick vs. tight); Bright should measurably
// boost the top end; and it should stay stable across its full range.

#include "../Source/dsp/PrecisionDriveStage.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
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

    // --- Test 1: Drive produces real, growing harmonic saturation ---
    std::printf("=== Drive: higher settings should add more harmonic content ===\n");
    {
        constexpr double testFreq = 220.0;

        auto distortionAt = [testFreq](float driveAmount)
        {
            PrecisionDriveStage stage(sampleRate);
            stage.setAttack(0.0f);
            stage.setDrive(driveAmount);
            stage.setBright(0.0f);

            auto input = sineAt(testFreq, 0.4f);
            std::vector<float> output(input.size());
            stage.processBlock(input.data(), output.data(), numSamples);

            auto fundamental = TestUtils::goertzelMagnitude(output, testFreq, sampleRate);
            auto second = TestUtils::goertzelMagnitude(output, testFreq * 2.0, sampleRate);
            auto third = TestUtils::goertzelMagnitude(output, testFreq * 3.0, sampleRate);
            return (second + third) / std::max(1.0e-6, fundamental);
        };

        auto low = distortionAt(0.0f);
        auto mid = distortionAt(0.4f);
        auto high = distortionAt(1.0f);

        std::printf("  drive=0.0: harmonic/fundamental = %.4f\n", low);
        std::printf("  drive=0.4: harmonic/fundamental = %.4f\n", mid);
        std::printf("  drive=1.0: harmonic/fundamental = %.4f\n", high);

        bool grows = mid > low && high > mid;
        std::printf("  %s: distortion grows with Drive\n\n", grows ? "OK" : "FAILED");
        allPassed &= grows;
    }

    // --- Test 2: Attack shifts how much low end gets through ---
    std::printf("=== Attack: thick (0) should pass more bass than tight (1) ===\n");
    {
        constexpr double bassFreq = 100.0;

        auto bassMagnitudeAt = [bassFreq](float attackAmount)
        {
            PrecisionDriveStage stage(sampleRate);
            stage.setAttack(attackAmount);
            stage.setDrive(0.0f); // isolate the filter, not the clipper
            stage.setBright(0.0f);

            auto input = sineAt(bassFreq, 0.3f);
            std::vector<float> output(input.size());
            stage.processBlock(input.data(), output.data(), numSamples);
            return TestUtils::goertzelMagnitude(output, bassFreq, sampleRate);
        };

        auto thick = bassMagnitudeAt(0.0f);
        auto tight = bassMagnitudeAt(1.0f);
        auto diffDb = TestUtils::toDb(thick) - TestUtils::toDb(tight);

        std::printf("  thick (Attack=0): magnitude at %.0fHz = %.4f\n", bassFreq, thick);
        std::printf("  tight (Attack=1): magnitude at %.0fHz = %.4f (diff %.2fdB)\n", bassFreq, tight, diffDb);

        bool shiftsBass = diffDb > 3.0;
        std::printf("  %s: Attack audibly changes low-end content\n\n", shiftsBass ? "OK" : "FAILED");
        allPassed &= shiftsBass;
    }

    // --- Test 3: Bright boosts the top end ---
    std::printf("=== Bright: higher settings should boost high frequencies ===\n");
    {
        constexpr double highFreq = 5000.0;

        auto highMagnitudeAt = [highFreq](float brightAmount)
        {
            PrecisionDriveStage stage(sampleRate);
            stage.setAttack(0.0f);
            stage.setDrive(0.0f);
            stage.setBright(brightAmount);

            auto input = sineAt(highFreq, 0.3f);
            std::vector<float> output(input.size());
            stage.processBlock(input.data(), output.data(), numSamples);
            return TestUtils::goertzelMagnitude(output, highFreq, sampleRate);
        };

        auto dark = highMagnitudeAt(0.0f);
        auto bright = highMagnitudeAt(1.0f);
        auto diffDb = TestUtils::toDb(bright) - TestUtils::toDb(dark);

        std::printf("  Bright=0: magnitude at %.0fHz = %.4f\n", highFreq, dark);
        std::printf("  Bright=1: magnitude at %.0fHz = %.4f (diff %+.2fdB)\n", highFreq, bright, diffDb);

        bool boosts = diffDb > 3.0;
        std::printf("  %s: Bright audibly boosts the top end\n\n", boosts ? "OK" : "FAILED");
        allPassed &= boosts;
    }

    // --- Test 4: stability across the full control range ---
    std::printf("=== Stability across the guitar range at extreme settings ===\n");
    {
        bool stable = true;
        for (double freq = 82.0; freq <= 5000.0; freq *= 1.7)
        {
            PrecisionDriveStage stage(sampleRate);
            stage.setAttack(1.0f);
            stage.setDrive(1.0f);
            stage.setBright(1.0f);

            auto input = sineAt(freq, 0.9f);
            std::vector<float> output(input.size());
            stage.processBlock(input.data(), output.data(), numSamples);

            for (auto y : output)
            {
                if (!std::isfinite(y) || std::abs(y) > 10.0f)
                {
                    std::printf("  FAILED at %.1fHz\n", freq);
                    stable = false;
                    break;
                }
            }
        }
        std::printf("  %s\n\n", stable ? "OK: stable across the range" : "see failures above");
        allPassed &= stable;
    }

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
