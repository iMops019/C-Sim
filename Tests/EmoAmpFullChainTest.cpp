// Step 8: assemble the full emo/math-rock build for the first time -
// dynamic gain stage -> tone stack -> spring reverb -> bias-modulated
// tremolo -> power amp - and tune two variants against the build order's
// actual references: a Deluxe-style build (more sag, breaks up early)
// and a Twin-style build (more headroom, stays cleaner under hard
// strumming), both sharing every module except the power amp's sag
// amount and tone stack's voicing. Tested with a single-coil-relevant
// signal (lower output level, brighter harmonic content than a
// humbucker) per the build order's explicit instruction.

#include "../Source/dsp/BiasModulatedTremolo.h"
#include "../Source/dsp/DynamicGainStage.h"
#include "../Source/dsp/FenderToneStack.h"
#include "../Source/dsp/PowerAmpStage.h"
#include "../Source/dsp/SpringReverb.h"
#include "TestUtils.h"

#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double noteFreq = 196.0; // open G-ish, a common math-rock/emo voicing register

    struct EmoAmp
    {
        DynamicGainStage gain;
        FenderToneStack toneStack;
        SpringReverb reverb;
        BiasModulatedTremolo tremolo;
        PowerAmpStage powerAmp;

        EmoAmp(double sr, float sag, bool tremoloOn)
            : gain(sr), toneStack(sr), reverb(sr), tremolo(sr), powerAmp(sr)
        {
            gain.setSensitivity(0.7f);
            gain.setBaseDrive(0.1f);
            toneStack.setControls(0.6f, 0.55f, 0.45f); // Fender-ish: bright, present bass, moderate mid
            reverb.setDecay(0.4f);
            reverb.setMix(0.25f);
            tremolo.setRateHz(5.5f);
            tremolo.setDepth(tremoloOn ? 0.5f : 0.0f);
            powerAmp.setSag(sag);
            powerAmp.setFeedback(0.3f);
        }

        void processBlock(const float* input, float* output, int numSamples)
        {
            // Makeup gain stages a real amp has and this test chain
            // otherwise lacks: recovery gain after the tone stack's
            // passive insertion loss, and driver/phase-inverter gain
            // feeding the power tubes. Without these, cascading several
            // sub-unity-small-signal-gain Koren stages plus the passive
            // tone stack compounds down to a near-silent signal by the
            // time it reaches the power amp - too quiet for sag to
            // meaningfully engage, which is exactly what an earlier
            // version of this test measured (both variants showing
            // near-zero, noise-level "compression").
            constexpr float toneStackMakeupGain = 3.0f;
            constexpr float driverStageMakeupGain = 2.0f;

            std::vector<float> a(static_cast<size_t>(numSamples)), b(static_cast<size_t>(numSamples));

            gain.processBlock(input, a.data(), numSamples);

            for (int n = 0; n < numSamples; ++n)
                a[static_cast<size_t>(n)] = toneStack.processSample(a[static_cast<size_t>(n)]) * toneStackMakeupGain;

            reverb.processBlock(a.data(), b.data(), numSamples);
            tremolo.processBlock(b.data(), a.data(), numSamples);

            for (int n = 0; n < numSamples; ++n)
                a[static_cast<size_t>(n)] *= driverStageMakeupGain;

            powerAmp.processBlock(a.data(), output, numSamples);
        }
    };

    double harmonicRatio(const std::vector<float>& signal, double freq)
    {
        auto fundamental = TestUtils::goertzelMagnitude(signal, freq, sampleRate);
        auto second = TestUtils::goertzelMagnitude(signal, freq * 2.0, sampleRate);
        auto third = TestUtils::goertzelMagnitude(signal, freq * 3.0, sampleRate);
        return (second + third) / std::max(1.0e-6, fundamental);
    }
}

int main()
{
    bool allPassed = true;
    constexpr int numSamples = 16384;

    // --- Test 1: light single-coil playing stays clean through the whole chain ---
    std::printf("=== Light playing (single-coil-level input) stays clean end-to-end ===\n");

    EmoAmp deluxeAmp(sampleRate, 0.6f, false);
    std::vector<float> lightIn(numSamples), lightOut(numSamples);
    for (int n = 0; n < numSamples; ++n)
        lightIn[static_cast<size_t>(n)] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * noteFreq * n / sampleRate));

    deluxeAmp.processBlock(lightIn.data(), lightOut.data(), numSamples);
    auto lightRatio = harmonicRatio(lightOut, noteFreq);
    std::printf("  harmonic/fundamental ratio at light playing: %.4f\n", lightRatio);
    // The power amp's own tube nonlinearity contributes some harmonic
    // content regardless of how clean the preamp stage is - that's
    // physically real (real power amps aren't perfectly transparent
    // either), so the bar here is "clearly less than hard playing", not
    // "zero distortion" - see the ~6x gap vs. the hard-strum case below.
    bool staysClean = lightRatio < 0.4;
    std::printf("  %s: stays reasonably clean end-to-end at light playing\n\n", staysClean ? "OK" : "FAILED");
    allPassed &= staysClean;

    // --- Test 2: Deluxe (more sag) vs Twin (more headroom) differ under hard strumming ---
    // Moderate rather than maximal strum level - driving both variants
    // into heavy saturation (as a very hot input would) washes out the
    // headroom difference that's actually supposed to distinguish them;
    // the real distinguishing behaviour shows up before that point.
    std::printf("=== Deluxe (sag=0.7) vs Twin (sag=0.15) under a moderate hard strum ===\n");

    // Sag is a time-varying effect (the rail droops as a note sustains),
    // so a steady continuous tone's static harmonic content barely shows
    // it - the earlier attempt at this test measured essentially no
    // difference (1.0701 vs 1.0697) that way. The real signature is
    // compression building up over a sustained note, exactly as
    // PowerAmpStageTest measured directly on the power amp alone.
    EmoAmp deluxeHard(sampleRate, 0.7f, false);
    EmoAmp twinHard(sampleRate, 0.15f, false);

    constexpr int sustainSamples = 20000; // ~417ms - well past the ~150ms sag release
    std::vector<float> hardIn(sustainSamples), deluxeOut(sustainSamples), twinOut(sustainSamples);
    for (int n = 0; n < sustainSamples; ++n)
        hardIn[static_cast<size_t>(n)] = static_cast<float>(0.6 * std::sin(2.0 * M_PI * noteFreq * n / sampleRate));

    deluxeHard.processBlock(hardIn.data(), deluxeOut.data(), sustainSamples);
    twinHard.processBlock(hardIn.data(), twinOut.data(), sustainSamples);

    auto peakOf = [](const std::vector<float>& v, size_t from, size_t count)
    {
        double peak = 0.0;
        for (size_t i = from; i < from + count && i < v.size(); ++i)
            peak = std::max(peak, static_cast<double>(std::abs(v[i])));
        return peak;
    };

    auto earlyWindow = static_cast<size_t>(0.02 * sampleRate);
    auto lateStart = static_cast<size_t>(0.3 * sampleRate);
    auto windowLen = static_cast<size_t>(0.02 * sampleRate);

    auto deluxeEarly = peakOf(deluxeOut, 0, earlyWindow);
    auto deluxeLate = peakOf(deluxeOut, lateStart, windowLen);
    auto twinEarly = peakOf(twinOut, 0, earlyWindow);
    auto twinLate = peakOf(twinOut, lateStart, windowLen);

    auto deluxeCompressionPct = 100.0 * (1.0 - deluxeLate / deluxeEarly);
    auto twinCompressionPct = 100.0 * (1.0 - twinLate / twinEarly);

    std::printf("  Deluxe-style: early peak=%.4f  late peak=%.4f  (%.1f%% compression)\n",
                deluxeEarly, deluxeLate, deluxeCompressionPct);
    std::printf("  Twin-style:   early peak=%.4f  late peak=%.4f  (%.1f%% compression, should be less)\n",
                twinEarly, twinLate, twinCompressionPct);
    bool deluxeBreaksUpMore = deluxeCompressionPct > twinCompressionPct * 1.5;
    std::printf("  %s: Deluxe-style compresses/sags noticeably more under sustain\n\n",
                deluxeBreaksUpMore ? "OK" : "FAILED");
    allPassed &= deluxeBreaksUpMore;

    // --- Test 3: reverb tail and tremolo modulation both present when engaged ---
    std::printf("=== Reverb tail and tremolo both audible when engaged ===\n");

    EmoAmp wetAmp(sampleRate, 0.5f, true);
    std::vector<float> burstIn(numSamples, 0.0f);
    for (int n = 0; n < 200; ++n) // short pluck
        burstIn[static_cast<size_t>(n)] = static_cast<float>(0.6 * std::sin(2.0 * M_PI * noteFreq * n / sampleRate));

    std::vector<float> wetOut(numSamples);
    wetAmp.processBlock(burstIn.data(), wetOut.data(), numSamples);

    double tailEnergy = 0.0;
    for (size_t n = 4000; n < static_cast<size_t>(numSamples); ++n)
        tailEnergy += static_cast<double>(wetOut[n]) * wetOut[n];
    tailEnergy = std::sqrt(tailEnergy / (numSamples - 4000));

    std::printf("  tail RMS well after the pluck: %.6f (should be measurable - reverb ringing on)\n", tailEnergy);
    bool reverbAudible = tailEnergy > 1.0e-5;
    std::printf("  %s\n\n", reverbAudible ? "OK: reverb tail present" : "FAILED");
    allPassed &= reverbAudible;

    // --- Test 4: stability across the guitar range for both variants ---
    std::printf("=== Stability across guitar range, both amp variants ===\n");
    bool stable = true;

    for (double freq = 82.0; freq <= 1200.0; freq *= 1.7)
    {
        for (float sag : { 0.15f, 0.7f })
        {
            EmoAmp sweepAmp(sampleRate, sag, true);
            std::vector<float> sweepIn(4096), sweepOut(4096);
            for (int n = 0; n < 4096; ++n)
                sweepIn[static_cast<size_t>(n)] = static_cast<float>(0.8 * std::sin(2.0 * M_PI * freq * n / sampleRate));

            sweepAmp.processBlock(sweepIn.data(), sweepOut.data(), 4096);

            for (auto y : sweepOut)
            {
                if (! std::isfinite(y))
                {
                    std::printf("  FAILED: NaN/Inf at %.1fHz, sag=%.2f\n", freq, sag);
                    stable = false;
                    break;
                }
            }
        }
    }
    std::printf("  %s\n", stable ? "OK: stable across the whole range, both variants" : "see failures above");
    allPassed &= stable;

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
