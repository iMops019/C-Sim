// Verify the Warden model's actual claims: Ratio genuinely changes
// compression amount independently of Sustain, a slow Attack lets a
// transient's initial peak through while a fast Attack clamps it down
// quickly (the real, practical reason this pedal suits tapping/mathrock
// articulation - EarthQuaker's own documented Attack behaviour), Release
// changes recovery speed, the Tone tilt cuts/boosts treble on either
// side of centre, and stability.

#include "../Source/dsp/WardenStage.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 24000;
}

int main()
{
    bool allPassed = true;

    // --- Test 1: Ratio should change gain reduction independently of
    // Sustain, on the same hot input. ---
    std::printf("=== Ratio: higher Ratio should compress more at the same Sustain ===\n");
    {
        constexpr double freq = 220.0;
        auto levelAt = [freq](float ratioAmount)
        {
            WardenStage warden(sampleRate);
            warden.setSustain(0.5f);
            warden.setRatio(ratioAmount);
            warden.setAttack(0.1f);
            warden.setRelease(0.3f);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.7 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            warden.processBlock(in.data(), out.data(), numSamples);

            std::vector<float> settled(out.begin() + numSamples / 2, out.end());
            return TestUtils::goertzelMagnitude(settled, freq, sampleRate);
        };

        auto lowRatio = levelAt(0.0f);
        auto highRatio = levelAt(1.0f);
        auto diffDb = TestUtils::toDb(highRatio) - TestUtils::toDb(lowRatio);
        std::printf("  Ratio=0: %.4f, Ratio=1: %.4f (%.2fdB)\n", lowRatio, highRatio, diffDb);

        bool ratioWorks = diffDb < -4.0;
        std::printf("  %s: Ratio applies real, independent gain reduction\n\n", ratioWorks ? "OK" : "FAILED");
        allPassed &= ratioWorks;
    }

    // --- Test 2: Attack - a slow Attack should let an initial transient
    // spike through at closer to its true peak than a fast Attack does,
    // the real practical reason this control matters for tapping/legato
    // articulation. ---
    std::printf("=== Attack: slow Attack should preserve an initial transient peak better than fast ===\n");
    {
        // A sharp transient (a fast-decaying burst) riding on top of a
        // steady tone - like a pick attack or a tapped note.
        auto transientPeakRatio = [](float attackAmount)
        {
            WardenStage warden(sampleRate);
            warden.setSustain(0.7f);
            warden.setRatio(0.8f);
            warden.setAttack(attackAmount);
            warden.setRelease(0.3f);

            constexpr double freq = 330.0;
            constexpr int transientStart = 4000;
            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
            {
                auto t = static_cast<double>(n) / sampleRate;
                auto steady = 0.3 * std::sin(2.0 * M_PI * freq * n / sampleRate);
                double burst = 0.0;
                if (n >= transientStart)
                {
                    auto tt = static_cast<double>(n - transientStart) / sampleRate;
                    burst = 0.6 * std::exp(-tt / 0.003) * std::sin(2.0 * M_PI * freq * n / sampleRate);
                }
                in[static_cast<size_t>(n)] = static_cast<float>(steady + burst);
            }
            warden.processBlock(in.data(), out.data(), numSamples);

            // Peak right at the transient vs. the steady level just
            // before it - a bigger ratio means more of the transient
            // survived uncompressed.
            float preTransientPeak = 0.0f;
            for (int n = transientStart - 200; n < transientStart; ++n)
                preTransientPeak = std::max(preTransientPeak, std::abs(out[static_cast<size_t>(n)]));

            float transientPeak = 0.0f;
            for (int n = transientStart; n < transientStart + 100; ++n)
                transientPeak = std::max(transientPeak, std::abs(out[static_cast<size_t>(n)]));

            return transientPeak / std::max(preTransientPeak, 1.0e-6f);
        };

        auto fastAttackRatio = transientPeakRatio(0.0f);
        auto slowAttackRatio = transientPeakRatio(1.0f);
        std::printf("  Fast Attack: transient/pre-transient peak ratio = %.3f\n", fastAttackRatio);
        std::printf("  Slow Attack: transient/pre-transient peak ratio = %.3f (should be higher)\n", slowAttackRatio);

        bool attackWorks = slowAttackRatio > fastAttackRatio * 1.1f;
        std::printf("  %s: slow Attack preserves more of the transient peak\n\n", attackWorks ? "OK" : "FAILED");
        allPassed &= attackWorks;
    }

    // --- Test 3: Release - slower Release should keep gain reduction
    // engaged longer after a loud transient than fast Release does. ---
    std::printf("=== Release: slow Release should recover gain more slowly after a loud transient ===\n");
    {
        auto recoveredLevel = [](float releaseAmount)
        {
            WardenStage warden(sampleRate);
            warden.setSustain(0.8f);
            warden.setRatio(0.9f);
            warden.setAttack(0.0f);
            warden.setRelease(releaseAmount);

            constexpr double freq = 220.0;
            constexpr int loudSamples = 4000;
            constexpr int quietStart = loudSamples;
            constexpr int measureAt = quietStart + 1000; // shortly after dropping to quiet

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
            {
                auto amplitude = (n < loudSamples) ? 0.8 : 0.15;
                in[static_cast<size_t>(n)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * n / sampleRate));
            }
            warden.processBlock(in.data(), out.data(), numSamples);

            std::vector<float> window(out.begin() + measureAt, out.begin() + measureAt + 500);
            return TestUtils::goertzelMagnitude(window, freq, sampleRate);
        };

        auto fastRelease = recoveredLevel(0.0f);
        auto slowRelease = recoveredLevel(1.0f);
        auto diffDb = TestUtils::toDb(slowRelease) - TestUtils::toDb(fastRelease);
        std::printf("  Fast Release: %.4f, Slow Release: %.4f (%.2fdB)\n", fastRelease, slowRelease, diffDb);

        // Slow release should still be holding MORE gain reduction shortly
        // after the drop - i.e. reading QUIETER than the fast-release case,
        // which has already recovered its gain back up.
        bool releaseWorks = diffDb < -1.5;
        std::printf("  %s: slow Release keeps gain reduction engaged longer\n\n", releaseWorks ? "OK" : "FAILED");
        allPassed &= releaseWorks;
    }

    // --- Test 4: Tone tilt - should cut treble below centre and boost
    // it above centre. ---
    std::printf("=== Tone: should cut treble below 0.5 and boost it above 0.5 ===\n");
    {
        constexpr double trebleFreq = 4000.0;
        auto trebleLevelAt = [trebleFreq](float toneAmount)
        {
            WardenStage warden(sampleRate);
            warden.setSustain(0.3f);
            warden.setRatio(0.2f);
            warden.setTone(toneAmount);

            std::vector<float> in(numSamples), out(numSamples);
            for (int n = 0; n < numSamples; ++n)
                in[static_cast<size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * trebleFreq * n / sampleRate));
            warden.processBlock(in.data(), out.data(), numSamples);

            std::vector<float> settled(out.begin() + numSamples / 2, out.end());
            return TestUtils::goertzelMagnitude(settled, trebleFreq, sampleRate);
        };

        auto toneCut = trebleLevelAt(0.0f);
        auto toneFlat = trebleLevelAt(0.5f);
        auto toneBoost = trebleLevelAt(1.0f);
        std::printf("  Tone=0 (cut): %.4f, Tone=0.5 (flat): %.4f, Tone=1 (boost): %.4f\n", toneCut, toneFlat, toneBoost);

        bool toneWorks = toneCut < toneFlat && toneBoost > toneFlat;
        std::printf("  %s: Tone tilts treble both directions around centre\n\n", toneWorks ? "OK" : "FAILED");
        allPassed &= toneWorks;
    }

    // --- Test 5: stability at max settings. ---
    std::printf("=== Stability across a guitar-range sweep, max settings ===\n");
    bool rangeStable = true;

    for (double freq = 80.0; freq <= 1200.0; freq += 80.0)
    {
        WardenStage sweep(sampleRate);
        sweep.setSustain(1.0f);
        sweep.setRatio(1.0f);
        sweep.setAttack(0.0f);
        sweep.setRelease(1.0f);
        sweep.setTone(1.0f);
        sweep.setLevel(1.0f);

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
