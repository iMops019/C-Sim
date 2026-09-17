// Verify the pitch shifter's actual claim: setting Semitones should move
// the output's dominant frequency by the requested ratio (not just filter
// or distort it), Mix should blend cleanly to dry, and it should stay
// stable (finite, bounded) across its full range.

#include "../Source/dsp/PitchShifter.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double testFreq = 220.0; // A3
    constexpr int numSamples = 48000;  // 1s
    constexpr int steadyStateStart = 8000; // skip startup/grain-fill transient

    std::vector<float> sineAt(double freq, float amplitude, int n)
    {
        std::vector<float> s(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            s[static_cast<size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * freq * i / sampleRate));
        return s;
    }

    // Goertzel magnitude over just the steady-state tail, so startup
    // transients from the grain crossfade don't skew the measurement.
    double steadyStateMagnitudeAt(const std::vector<float>& signal, double freq)
    {
        std::vector<float> tail(signal.begin() + steadyStateStart, signal.end());
        return TestUtils::goertzelMagnitude(tail, freq, sampleRate);
    }

    std::vector<float> runShifter(float semitones, float mix)
    {
        PitchShifter shifter(sampleRate);
        shifter.setSemitones(semitones);
        shifter.setMix(mix);

        auto input = sineAt(testFreq, 0.6f, numSamples);
        std::vector<float> output(input.size());
        shifter.processBlock(input.data(), output.data(), numSamples);
        return output;
    }

    // Reference level for "this frequency at this amplitude, measured the
    // same way" - a dry sine run through the same steady-state Goertzel
    // window, so comparisons aren't thrown off by the window's own
    // scalloping loss at an off-bin frequency.
    double dryReferenceAt(double freq, float amplitude)
    {
        return steadyStateMagnitudeAt(sineAt(freq, amplitude, numSamples), freq);
    }
}

int main()
{
    bool allPassed = true;

    // --- Test 1: Semitones=0 preserves the original pitch ---
    // At ratio=1 the two crossfaded taps read a fixed, unmoving delay
    // (no grain-rate modulation at all), so this should lose little
    // beyond the window's own scalloping loss - checked against the dry
    // reference rather than a hand-picked absolute number.
    std::printf("=== Semitones=0: output pitch should match the input ===\n");
    {
        auto out = runShifter(0.0f, 1.0f);
        auto atFundamental = steadyStateMagnitudeAt(out, testFreq);
        auto dryRef = dryReferenceAt(testFreq, 0.6f);
        auto ratio = atFundamental / dryRef;
        std::printf("  magnitude at %.0fHz: %.4f (dry reference %.4f, ratio %.2f)\n", testFreq, atFundamental, dryRef, ratio);
        bool preserved = ratio > 0.5;
        std::printf("  %s\n\n", preserved ? "OK" : "FAILED");
        allPassed &= preserved;
    }

    // --- Test 2: Semitones=-12 (one octave down) halves the frequency ---
    // Away from ratio=1 the tap delay wraps continuously (grain-rate
    // amplitude modulation), which is this technique's known trade-off -
    // it spreads some energy into sidebands around the shifted pitch, so
    // the shifted peak legitimately reads lower than a plain dry tone.
    // What must hold is that the shifted frequency clearly dominates the
    // untouched original frequency, which is the actual "did it
    // transpose" claim.
    std::printf("=== Semitones=-12: output should shift to half the input frequency ===\n");
    {
        auto out = runShifter(-12.0f, 1.0f);
        auto atHalf = steadyStateMagnitudeAt(out, testFreq / 2.0);
        auto atOriginal = steadyStateMagnitudeAt(out, testFreq);
        auto dryRefHalf = dryReferenceAt(testFreq / 2.0, 0.6f);
        std::printf("  magnitude at %.0fHz (shifted): %.4f (dry reference %.4f)\n", testFreq / 2.0, atHalf, dryRefHalf);
        std::printf("  magnitude at %.0fHz (original): %.4f (should be much smaller)\n", testFreq, atOriginal);
        bool shiftedDown = atHalf > dryRefHalf * 0.15 && atHalf > atOriginal * 20.0;
        std::printf("  %s\n\n", shiftedDown ? "OK" : "FAILED");
        allPassed &= shiftedDown;
    }

    // --- Test 3: Semitones=+12 (one octave up) doubles the frequency ---
    std::printf("=== Semitones=+12: output should shift to double the input frequency ===\n");
    {
        auto out = runShifter(12.0f, 1.0f);
        auto atDouble = steadyStateMagnitudeAt(out, testFreq * 2.0);
        auto atOriginal = steadyStateMagnitudeAt(out, testFreq);
        auto dryRefDouble = dryReferenceAt(testFreq * 2.0, 0.6f);
        std::printf("  magnitude at %.0fHz (shifted): %.4f (dry reference %.4f)\n", testFreq * 2.0, atDouble, dryRefDouble);
        std::printf("  magnitude at %.0fHz (original): %.4f (should be much smaller)\n", testFreq, atOriginal);
        bool shiftedUp = atDouble > dryRefDouble * 0.15 && atDouble > atOriginal * 20.0;
        std::printf("  %s\n\n", shiftedUp ? "OK" : "FAILED");
        allPassed &= shiftedUp;
    }

    // --- Test 4: Mix=0 is exact dry passthrough regardless of Semitones ---
    std::printf("=== Mix=0: should pass the dry signal through unaffected ===\n");
    {
        PitchShifter shifter(sampleRate);
        shifter.setSemitones(-7.0f);
        shifter.setMix(0.0f);

        auto input = sineAt(testFreq, 0.5f, 2000);
        std::vector<float> output(input.size());
        shifter.processBlock(input.data(), output.data(), 2000);

        bool exact = true;
        for (size_t n = 0; n < input.size(); ++n)
            if (std::abs(input[n] - output[n]) > 1.0e-6f) exact = false;

        std::printf("  %s: mix=0 output matches input exactly\n\n", exact ? "OK" : "FAILED");
        allPassed &= exact;
    }

    // --- Test 5: stability across the full range ---
    std::printf("=== Stability across the full Semitones range ===\n");
    {
        bool stable = true;
        for (float st = -12.0f; st <= 12.0f; st += 3.0f)
        {
            auto out = runShifter(st, 1.0f);
            for (auto y : out)
            {
                if (!std::isfinite(y) || std::abs(y) > 5.0f)
                {
                    std::printf("  FAILED at semitones=%.0f\n", st);
                    stable = false;
                    break;
                }
            }
        }
        std::printf("  %s\n\n", stable ? "OK: finite, bounded output across the range" : "see failures above");
        allPassed &= stable;
    }

    std::printf("%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
