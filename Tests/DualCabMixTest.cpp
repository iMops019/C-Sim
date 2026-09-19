// Verify DualCabMix's pan and polarity math: Spread's endpoints and level
// behavior, that two identical cabs are indistinguishable from one at any
// Spread (so switching dual mode on with the same cab is a true no-op),
// and that polarity inversion does what miking lore says it does.

#include "../Source/dsp/DualCabMix.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    std::printf("=== Spread endpoints and level ===\n");
    {
        auto hard = DualCabMix::weightsForSpread(1.0f);
        auto centered = DualCabMix::weightsForSpread(0.0f);
        check(hard.primary == 1.0f && hard.secondary == 0.0f, "Spread=1 is hard left/right (own cab only)");
        check(centered.primary == 0.5f && centered.secondary == 0.5f, "Spread=0 weights both cabs equally");

        bool sumsToOne = true;
        for (float s = 0.0f; s <= 1.0f; s += 0.05f)
        {
            auto w = DualCabMix::weightsForSpread(s);
            sumsToOne &= std::abs((w.primary + w.secondary) - 1.0f) < 1.0e-6f;
        }
        check(sumsToOne, "weights sum to 1 at every Spread (no level jump while sweeping)");

        auto clampedLow = DualCabMix::weightsForSpread(-3.0f);
        auto clampedHigh = DualCabMix::weightsForSpread(9.0f);
        check(clampedLow.primary == 0.5f && clampedHigh.primary == 1.0f, "out-of-range Spread is clamped");
    }

    std::printf("\n=== Identical cabs: dual mode is a no-op at any Spread ===\n");
    {
        bool identical = true;
        for (float s = 0.0f; s <= 1.0f; s += 0.1f)
        {
            auto w = DualCabMix::weightsForSpread(s);
            for (float x : { -0.7f, -0.1f, 0.0f, 0.3f, 0.9f })
                identical &= std::abs((w.primary * x + w.secondary * x) - x) < 1.0e-6f;
        }
        check(identical, "same signal on both sides comes out unchanged");
    }

    std::printf("\n=== Mic blend and polarity ===\n");
    {
        check(DualCabMix::mixMics(0.8f, 0.2f, 0.0f, false, false) == 0.8f, "Blend=0 is all Mic A");
        check(DualCabMix::mixMics(0.8f, 0.2f, 1.0f, false, false) == 0.2f, "Blend=1 is all Mic B");
        check(std::abs(DualCabMix::mixMics(0.8f, 0.2f, 0.5f, false, false) - 0.5f) < 1.0e-6f, "Blend=0.5 averages");

        // Two mics hearing the SAME thing, inverted against each other,
        // cancel completely at an even blend - the studio phase trick.
        check(std::abs(DualCabMix::mixMics(0.6f, 0.6f, 0.5f, false, true)) < 1.0e-6f,
              "identical mics, one inverted, cancel at an even blend");
        // ...but what the mics hear DIFFERENTLY survives.
        check(std::abs(DualCabMix::mixMics(0.9f, 0.1f, 0.5f, false, true) - 0.4f) < 1.0e-6f,
              "differing content survives the inversion");
        check(DualCabMix::mixMics(0.8f, 0.2f, 0.0f, true, false) == -0.8f, "inverting Mic A flips the signal");
    }

    std::printf("\n=== Mic Spread: Mic A left, Mic B right ===\n");
    {
        bool defaultIsBitIdentical = true;
        for (int channel : { 0, 1 })
            for (float a : { -0.7f, 0.0f, 0.3f, 0.9f })
                for (float b : { -0.4f, 0.1f, 0.6f })
                    for (float blend : { 0.0f, 0.3f, 1.0f })
                        defaultIsBitIdentical &= DualCabMix::mixMicsWithSpread(a, b, blend, false, true, 0.0f, channel)
                                                 == DualCabMix::mixMics(a, b, blend, false, true);
        check(defaultIsBitIdentical, "spread 0 is bit-identical to the plain blend, for every blend and polarity tried");

        check(DualCabMix::mixMicsWithSpread(0.8f, 0.2f, 0.3f, false, false, 1.0f, 0) == 0.8f, "spread 100: the left side is Mic A alone");
        check(DualCabMix::mixMicsWithSpread(0.8f, 0.2f, 0.3f, false, false, 1.0f, 1) == 0.2f, "spread 100: the right side is Mic B alone");
        check(DualCabMix::mixMicsWithSpread(0.8f, 0.2f, 0.0f, false, false, 1.0f, 1) == 0.2f,
              "and Blend no longer matters at spread 100 (Blend 0 still gives Mic B on the right)");

        // Halfway is halfway between the blended signal and the mic on that side.
        auto blended = DualCabMix::mixMics(0.8f, 0.2f, 0.3f, false, false);
        check(std::abs(DualCabMix::mixMicsWithSpread(0.8f, 0.2f, 0.3f, false, false, 0.5f, 0) - 0.5f * (blended + 0.8f)) < 1.0e-6f,
              "spread 50 is halfway between the blend and Mic A on the left");

        // Summed to mono, two panned mics are an even blend of the two mics -
        // whatever the spread and Blend knobs say.
        bool monoIsEvenBlend = true;
        for (float spread : { 0.5f, 1.0f })
            for (float blend : { 0.0f, 0.3f, 1.0f })
            {
                auto left = DualCabMix::mixMicsWithSpread(0.8f, 0.2f, blend, false, false, spread, 0);
                auto right = DualCabMix::mixMicsWithSpread(0.8f, 0.2f, blend, false, false, spread, 1);
                if (spread == 1.0f)
                    monoIsEvenBlend &= std::abs(0.5f * (left + right) - 0.5f * (0.8f + 0.2f)) < 1.0e-6f;
            }
        check(monoIsEvenBlend, "at spread 100 the mono sum is an even blend of the two mics, whatever Blend is");

        check(DualCabMix::mixMicsWithSpread(0.8f, 0.2f, 0.3f, true, false, 1.0f, 0) == -0.8f,
              "Polarity A still flips Mic A when it is panned left");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
