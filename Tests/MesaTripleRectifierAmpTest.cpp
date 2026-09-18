// Verify MesaTripleRectifierAmp's real, sourced 3-stage cascade actually
// does what it claims: Gain drives the cascade harder (more harmonic
// content) and sweeps the real gain-dependent voicing filter corner
// (~656Hz-~1400Hz, traced from the schematic), Volume scales output
// level linearly, the cascade is stable across a guitar-range sweep, and
// a realistic quiet input is still clearly audible at default settings
// (a direct regression guard for a real bug once found here - see
// MesaTripleRectifierAmp.h's own header comment).
//
// One assumption tried and DISPROVEN during this cascade's development,
// worth remembering: "higher Gain should lower the crest factor (more
// compression)" seems intuitive but is FALSE for this specific circuit -
// measured directly, crest factor consistently INCREASES with Gain here,
// because the real, sourced gain-dependent voicing filter (656Hz-1400Hz)
// increasingly highpasses the fundamental/lower harmonics as Gain rises,
// which raises peak/RMS even while the cascade is genuinely saturating
// more (confirmed by the harmonic-content test below still passing) -
// not every real amp circuit's "more gain" reads as "more compression"
// in a naive crest-factor sense once a gain-dependent EQ shift is also
// in play. Don't re-add a crest-factor-decreases-with-gain assertion for
// this specific module without re-deriving why it would hold here.

#include "../Source/dsp/MesaTripleRectifierAmp.h"
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
            MesaTripleRectifierAmp amp(sampleRate);
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
            MesaTripleRectifierAmp amp(sampleRate);
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
        MesaTripleRectifierAmp amp(sampleRate);
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

    // --- Test 4: the real, sourced gain-dependent voicing filter corner
    // should sweep from ~656Hz up to ~1400Hz - verified directly against
    // the exposed corner rather than acoustically, since Gain also scales
    // drive into three cascaded nonlinear stages. ---
    std::printf("=== Gain: voicing filter corner should sweep from ~656Hz up to ~1400Hz ===\n");
    {
        MesaTripleRectifierAmp amp(sampleRate);

        amp.setGain(0.0f);
        auto lowGainCutoff = amp.getVoicingCutoffHz();

        amp.setGain(1.0f);
        auto highGainCutoff = amp.getVoicingCutoffHz();

        std::printf("  Gain=0.0: corner = %.1fHz, Gain=1.0: corner = %.1fHz\n", lowGainCutoff, highGainCutoff);

        bool sweepsUp = highGainCutoff > lowGainCutoff + 500.0f;
        bool matchesResearchedEndpoints = lowGainCutoff > 600.0f && lowGainCutoff < 700.0f
                                        && highGainCutoff > 1300.0f && highGainCutoff < 1500.0f;

        std::printf("  %s: corner sweeps up with Gain\n", sweepsUp ? "OK" : "FAILED");
        std::printf("  %s: matches the researched ~656Hz-~1400Hz range\n\n",
                     matchesResearchedEndpoints ? "OK" : "FAILED");
        allPassed &= sweepsUp && matchesResearchedEndpoints;
    }

    // --- Test 5: regression guard for the actual reported bug ("I don't
    // hear anything... have to crank everything to get a tiny anything").
    // A realistic, quiet guitar-level input (0.15 amplitude, well below
    // the test signals used elsewhere in this file) at the DEFAULT Gain
    // must still produce a healthy, clearly audible peak - not a
    // near-silent trickle that only cranking every knob can coax out. ---
    std::printf("=== Regression: realistic quiet input at default Gain should be clearly audible ===\n");
    {
        constexpr double freq = 110.0;

        MesaTripleRectifierAmp amp(sampleRate);
        amp.setGain(0.5f);   // default
        amp.setVolume(0.7f); // default

        std::vector<float> in(numSamples), out(numSamples);
        for (int n = 0; n < numSamples; ++n)
            in[static_cast<size_t>(n)] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * freq * n / sampleRate));
        amp.processBlock(in.data(), out.data(), numSamples);

        double peak = 0.0;
        for (auto y : out) peak = std::max(peak, static_cast<double>(std::abs(y)));
        std::printf("  input peak=0.150, default Gain/Volume -> output peak=%.5f\n", peak);

        bool audible = peak > 0.1;
        std::printf("  %s: quiet input isn't crushed to near-silence at default settings\n\n",
                     audible ? "OK" : "FAILED");
        allPassed &= audible;
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
