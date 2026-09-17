// Verifies the Fender Bassman 5F6-A tone stack's real, sourced claims:
//  1. Passivity - a passive RC network can only attenuate, never gain.
//  2. Each knob's directional effect - treble raises relative highs, bass
//     raises relative lows, mid raises the mid band.
//  3. The "characteristic mid range scoop" Fenton's paper reports as
//     present "even with mid range parameter set to minimum" - i.e. this
//     circuit can never fully undo its own mid dip, unlike treble/bass.
//  4. Stability - no NaN/Inf across a sine sweep at extreme knob settings.

#include "../Source/dsp/BassmanToneStack.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 8192;

    double measureResponseAt(BassmanToneStack& stack, double freq)
    {
        std::vector<float> output(numSamples);
        for (int n = 0; n < numSamples; ++n)
        {
            auto x = static_cast<float>(0.5 * std::sin(2.0 * M_PI * freq * n / sampleRate));
            output[static_cast<size_t>(n)] = stack.processSample(x);
        }
        return TestUtils::goertzelMagnitude(output, freq, sampleRate) / 0.5; // relative to input amplitude
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: passivity across a grid of knob settings and frequencies ---
    std::printf("=== Passivity check (gain should never exceed ~1.0) ===\n");
    double worstGain = 0.0;

    for (float treble : { 0.0f, 0.5f, 1.0f })
    {
        for (float bass : { 0.0f, 0.5f, 1.0f })
        {
            for (float mid : { 0.0f, 0.5f, 1.0f })
            {
                for (double freq : { 80.0, 200.0, 800.0, 2000.0, 5000.0, 8000.0 })
                {
                    BassmanToneStack stack(sampleRate);
                    stack.setControls(treble, bass, mid);
                    auto gain = measureResponseAt(stack, freq);
                    worstGain = std::max(worstGain, gain);
                }
            }
        }
    }
    std::printf("  worst-case gain observed: %.4f (%.1f dB)\n", worstGain, TestUtils::toDb(worstGain));
    bool passivityOk = worstGain < 1.05; // small margin for the numerical solve
    std::printf("  %s\n\n", passivityOk ? "OK: stays passive" : "FAILED: gain exceeds 1.0 - not passive!");
    allPassed &= passivityOk;

    // --- Test 2: knob directionality ---
    std::printf("=== Knob directionality ===\n");

    BassmanToneStack trebleUp(sampleRate), trebleDown(sampleRate);
    trebleUp.setControls(1.0f, 0.5f, 0.5f);
    trebleDown.setControls(0.0f, 0.5f, 0.5f);
    auto highWithTrebleUp = measureResponseAt(trebleUp, 5000.0);
    auto highWithTrebleDown = measureResponseAt(trebleDown, 5000.0);
    std::printf("  5kHz: treble=1.0 -> %.4f,  treble=0.0 -> %.4f  %s\n",
                highWithTrebleUp, highWithTrebleDown,
                highWithTrebleUp > highWithTrebleDown ? "OK (more treble = more highs)" : "FAILED");
    allPassed &= (highWithTrebleUp > highWithTrebleDown);

    BassmanToneStack bassUp(sampleRate), bassDown(sampleRate);
    bassUp.setControls(0.5f, 1.0f, 0.5f);
    bassDown.setControls(0.5f, 0.0f, 0.5f);
    auto lowWithBassUp = measureResponseAt(bassUp, 100.0);
    auto lowWithBassDown = measureResponseAt(bassDown, 100.0);
    std::printf("  100Hz: bass=1.0 -> %.4f,  bass=0.0 -> %.4f  %s\n",
                lowWithBassUp, lowWithBassDown,
                lowWithBassUp > lowWithBassDown ? "OK (more bass = more lows)" : "FAILED");
    allPassed &= (lowWithBassUp > lowWithBassDown);

    BassmanToneStack midUp(sampleRate), midDown(sampleRate);
    midUp.setControls(0.5f, 0.5f, 1.0f);
    midDown.setControls(0.5f, 0.5f, 0.0f);
    auto midWithMidUp = measureResponseAt(midUp, 800.0);
    auto midWithMidDown = measureResponseAt(midDown, 800.0);
    std::printf("  800Hz: mid=1.0 -> %.4f,  mid=0.0 -> %.4f  %s\n",
                midWithMidUp, midWithMidDown,
                midWithMidUp > midWithMidDown ? "OK (more mid = more mids)" : "FAILED");
    allPassed &= (midWithMidUp > midWithMidDown);

    // --- Test 3: the real, sourced "mid scoop" - even at the setting that
    // passes the MOST mid, 800Hz should still sit below both the bass and
    // treble responses at their own best-case settings. Fenton's paper is
    // explicit that this scoop survives "even with mid range parameter set
    // to minimum" (i.e. whichever knob direction that is) - it's a real
    // property of the passive network, not a tunable side effect. ---
    std::printf("=== Mid scoop: a real Bassman can never fully undo its own mid dip ===\n");
    {
        BassmanToneStack allUp(sampleRate);
        allUp.setControls(1.0f, 1.0f, 1.0f); // brightest, boomiest, and most-mid setting simultaneously
        auto bassRegion = measureResponseAt(allUp, 100.0);
        auto midRegion = measureResponseAt(allUp, 800.0);
        auto trebleRegion = measureResponseAt(allUp, 5000.0);
        std::printf("  all knobs maxed: 100Hz=%.4f, 800Hz=%.4f, 5kHz=%.4f\n", bassRegion, midRegion, trebleRegion);

        bool scoopSurvives = midRegion < bassRegion && midRegion < trebleRegion;
        std::printf("  %s: 800Hz sits below both 100Hz and 5kHz even at max settings\n\n",
                     scoopSurvives ? "OK" : "FAILED");
        allPassed &= scoopSurvives;
    }

    std::printf("\n=== Stability sweep (extreme settings, sine sweep 40Hz-10kHz) ===\n");
    for (float treble : { 0.0f, 1.0f })
    {
        for (float bass : { 0.0f, 1.0f })
        {
            for (float mid : { 0.0f, 1.0f })
            {
                BassmanToneStack stack(sampleRate);
                stack.setControls(treble, bass, mid);
                bool sawNonFinite = false;

                for (int n = 0; n < numSamples; ++n)
                {
                    auto freq = 40.0 + (10000.0 - 40.0) * (static_cast<double>(n) / numSamples);
                    auto x = static_cast<float>(0.8 * std::sin(2.0 * M_PI * freq * n / sampleRate));
                    auto y = stack.processSample(x);
                    if (! std::isfinite(y))
                        sawNonFinite = true;
                }

                if (sawNonFinite)
                {
                    std::printf("  FAILED: NaN/Inf at treble=%.0f bass=%.0f mid=%.0f\n", treble, bass, mid);
                    allPassed = false;
                }
            }
        }
    }
    std::printf("  %s\n", allPassed ? "OK: no instability found" : "see failures above");

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
