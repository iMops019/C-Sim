// Verify SpeakerCompression against the physics it claims to model:
//  - Push = 0 is an EXACT bypass, and the effect fades in continuously
//    (no jump between "off" and "barely on");
//  - quiet playing passes through untouched;
//  - cone excursion limiting is LEVEL-dependent and LOW-band only: the
//    bass is squashed and makes harmonics (3rd growing fastest) as the
//    drive rises, while a 1 kHz tone gets no harmonics at all;
//  - a speaker with less headroom (25 W Greenback) distorts more than a
//    60 W V30 at the same drive, in proportion to sqrt(power);
//  - power compression is slow gain reduction that only starts above a
//    threshold, grows with level and Push, and is bounded;
//  - the envelope's time constants are what the header says.
//
// Every gain is measured RELATIVE to a Push = 0 (bypass) run of the same
// signal through the same analyser. The project's Goertzel helper
// deliberately offsets its bin by half a step, which reads about -3.9 dB
// low for a bin-aligned tone; comparing against the dry signal measured
// the same way cancels that exactly (a known pitfall - see TestUtils.h).

#include "../Source/dsp/SpeakerCompression.h"
#include "TestUtils.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 48000;

    // The settled second half of a sine through the processor.
    std::vector<float> run(float push, float headroom, double freq, double amplitude)
    {
        SpeakerCompression::Processor p;
        p.prepare(sampleRate);
        p.setPush(push);
        p.setHeadroom(headroom);
        std::vector<float> out(static_cast<size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
            out[static_cast<size_t>(i)] = p.processSample(static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * i / sampleRate)));
        return std::vector<float>(out.begin() + numSamples / 2, out.end());
    }

    double fundamental(const std::vector<float>& x, double f) { return TestUtils::goertzelMagnitude(x, f, sampleRate); }

    // Gain of the fundamental relative to the bypassed signal, in dB.
    double gainDb(float push, float headroom, double freq, double amplitude)
    {
        return TestUtils::toDb(fundamental(run(push, headroom, freq, amplitude), freq)
                               / fundamental(run(0.0f, headroom, freq, amplitude), freq));
    }

    double harmonic(float push, float headroom, double freq, double amplitude, int order)
    {
        auto out = run(push, headroom, freq, amplitude);
        return fundamental(out, freq * order) / fundamental(out, freq);
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    std::printf("=== Push = 0 is an exact bypass; the effect fades in continuously ===\n");
    {
        SpeakerCompression::Processor p;
        p.prepare(sampleRate);
        p.setPush(0.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
        bool exact = true;
        for (int i = 0; i < 20000; ++i)
        {
            auto x = noise(rng);
            exact &= p.processSample(x) == x;
        }
        check(exact, "output is bit-identical to the input at Push = 0 (hot noise, 20000 samples)");

        // A whisper of Push must sound like almost nothing, not like a jump.
        auto jump = std::abs(gainDb(0.01f, 1.0f, 100.0, 0.9));
        std::printf("  Push 1%% on a hot 100Hz tone: %.2f dB from bypass\n", jump);
        check(jump < 0.3, "no jump between off and barely on");
    }

    std::printf("\n=== Quiet playing passes through untouched ===\n");
    {
        for (double freq : { 100.0, 1000.0 })
        {
            auto gain = gainDb(1.0f, 1.0f, freq, 0.02);
            auto h2 = harmonic(1.0f, 1.0f, freq, 0.02, 2), h3 = harmonic(1.0f, 1.0f, freq, 0.02, 3);
            std::printf("  %4.0fHz at 0.02, Push 100%%: gain %+.3f dB, 2nd %.4f, 3rd %.5f\n", freq, gain, h2, h3);
            check(std::abs(gain) < 0.15 && h2 < 0.03 && h3 < 0.003, "unity gain and (nearly) no distortion at low level");
        }
    }

    std::printf("\n=== Cone excursion: level-dependent, and mostly odd harmonics ===\n");
    {
        std::printf("  100Hz, Push 100%%   level   gain     2nd     3rd\n");
        double previousThird = -1.0, previousRatio = 0.0;
        bool thirdRises = true, ratioRises = true;
        double gainAt06 = 0.0, thirdAt06 = 0.0, secondAt09 = 0.0, thirdAt09 = 0.0;
        for (double amp : { 0.05, 0.2, 0.4, 0.6, 0.9 })
        {
            auto gain = gainDb(1.0f, 1.0f, 100.0, amp);
            auto h2 = harmonic(1.0f, 1.0f, 100.0, amp, 2), h3 = harmonic(1.0f, 1.0f, 100.0, amp, 3);
            std::printf("                     %.2f   %+6.2f   %.4f  %.4f\n", amp, gain, h2, h3);
            thirdRises &= h3 > previousThird;
            ratioRises &= (h3 / h2) > previousRatio;
            previousThird = h3;
            previousRatio = h3 / h2;
            if (amp == 0.6) { gainAt06 = gain; thirdAt06 = h3; }
            if (amp == 0.9) { secondAt09 = h2; thirdAt09 = h3; }
        }
        check(thirdRises, "the 3rd harmonic grows with drive level at every step");
        check(gainAt06 < -2.5 && thirdAt06 > 0.05, "hard-driven bass is squashed (>2.5 dB) and gains a clear 3rd harmonic (>5%)");
        check(ratioRises && thirdAt09 > secondAt09, "the 3rd grows faster than the 2nd and overtakes it: odd-dominated");
    }

    std::printf("\n=== More Push = more of it ===\n");
    {
        double previousThird = -1.0, previousGain = 1.0;
        bool thirdRises = true, gainFalls = true;
        std::printf("  100Hz at 0.6:");
        for (float push : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto h3 = harmonic(push, 1.0f, 100.0, 0.6, 3);
            auto gain = gainDb(push, 1.0f, 100.0, 0.6);
            std::printf("  Push %.0f%%: 3rd %.3f / %+.1f dB", push * 100.0f, h3, gain);
            thirdRises &= h3 > previousThird;
            gainFalls &= gain < previousGain;
            previousThird = h3;
            previousGain = gain;
        }
        std::printf("\n");
        check(thirdRises && gainFalls, "distortion rises and bass level falls as Push rises");
    }

    std::printf("\n=== The excursion stage only touches the bass ===\n");
    {
        auto gain = gainDb(1.0f, 1.0f, 1000.0, 0.3);
        auto h3 = harmonic(1.0f, 1.0f, 1000.0, 0.3, 3);
        auto h3Hot = harmonic(1.0f, 1.0f, 1000.0, 0.9, 3);
        std::printf("  1kHz at 0.3, Push 100%%: gain %+.3f dB, 3rd %.5f | at 0.9: 3rd %.4f\n", gain, h3, h3Hot);
        check(std::abs(gain) < 0.15 && h3 < 0.001, "a 1kHz tone below the compression threshold is untouched");
        check(h3Hot < 0.01, "even hot, a 1kHz tone gets almost no harmonics (compression is gain-only)");
    }

    std::printf("\n=== Less headroom = more distortion (sqrt of power rating) ===\n");
    {
        auto v30 = SpeakerCompression::headroomForWatts(SpeakerCompression::ratedWattsForCab(0));
        auto greenback = SpeakerCompression::headroomForWatts(SpeakerCompression::ratedWattsForCab(1));
        std::printf("  headroom: V30 %.3f (60W), Greenback %.3f (25W = sqrt(25/60) = %.3f)\n", v30, greenback, std::sqrt(25.0 / 60.0));
        check(std::abs(v30 - 1.0f) < 1.0e-6f && std::abs(greenback - std::sqrt(25.0f / 60.0f)) < 1.0e-6f,
              "headroom is the square root of the power ratio, V30 = 1");

        auto v30Third = harmonic(0.5f, v30, 100.0, 0.5, 3), greenbackThird = harmonic(0.5f, greenback, 100.0, 0.5, 3);
        auto v30Gain = gainDb(0.5f, v30, 100.0, 0.5), greenbackGain = gainDb(0.5f, greenback, 100.0, 0.5);
        std::printf("  same drive (100Hz at 0.5, Push 50%%): V30 3rd %.4f / %+.2f dB | Greenback 3rd %.4f / %+.2f dB\n",
                     v30Third, v30Gain, greenbackThird, greenbackGain);
        check(greenbackThird > v30Third * 1.5 && greenbackGain < v30Gain - 0.5,
              "the 25 W Greenback distorts and squashes clearly more than the 60 W V30");
    }

    std::printf("\n=== Power compression: threshold, growth, bounded ===\n");
    {
        std::printf("  1kHz gain by level (Push 25%% / 50%% / 100%%):\n");
        double at09[3] = {};
        for (double amp : { 0.05, 0.3, 0.6, 0.9 })
        {
            std::printf("    %.2f:", amp);
            int i = 0;
            for (float push : { 0.25f, 0.5f, 1.0f })
            {
                auto g = gainDb(push, 1.0f, 1000.0, amp);
                std::printf("  %+5.2f dB", g);
                if (amp == 0.9) at09[i] = g;
                ++i;
            }
            std::printf("\n");
        }
        check(std::abs(gainDb(1.0f, 1.0f, 1000.0, 0.05)) < 0.1 && std::abs(gainDb(1.0f, 1.0f, 1000.0, 0.3)) < 0.1,
              "nothing happens below the threshold, even at full Push");
        check(gainDb(1.0f, 1.0f, 1000.0, 0.9) < gainDb(1.0f, 1.0f, 1000.0, 0.6) && gainDb(1.0f, 1.0f, 1000.0, 0.6) < -0.5,
              "more level = more gain reduction");
        check(at09[0] > at09[1] && at09[1] > at09[2], "more Push = more gain reduction");
        check(at09[2] < -2.0 && at09[2] > -6.0, "and it is bounded: between -2 and -6 dB at the extreme");
    }

    std::printf("\n=== The envelope is slow, as the header says ===\n");
    {
        SpeakerCompression::Processor p;
        p.prepare(sampleRate);
        p.setPush(1.0f);

        // Attack: a step of 1.0 should reach ~63% of its final value after one time constant (10 ms).
        for (int i = 0; i < static_cast<int>(0.010 * sampleRate); ++i)
            p.processSample(1.0f);
        auto atAttackTau = p.getEnvelope();

        // Settle, then release: after one time constant (200 ms) of silence, ~37% should remain.
        for (int i = 0; i < static_cast<int>(1.0 * sampleRate); ++i)
            p.processSample(1.0f);
        auto settled = p.getEnvelope();
        for (int i = 0; i < static_cast<int>(0.200 * sampleRate); ++i)
            p.processSample(0.0f);
        auto atReleaseTau = p.getEnvelope();

        std::printf("  attack: %.2f of final after 10 ms (expect 0.63) | release: %.2f left after 200 ms (expect 0.37)\n",
                     atAttackTau / settled, atReleaseTau / settled);
        check(std::abs(atAttackTau / settled - 0.632) < 0.08, "attack time constant is ~10 ms");
        check(std::abs(atReleaseTau / settled - 0.368) < 0.06, "release time constant is ~200 ms");
    }

    std::printf("\n=== Stability ===\n");
    {
        bool finite = true;
        for (float push : { 0.0f, 0.3f, 1.0f })
            for (float headroom : { 0.2f, 1.0f, 1.5f })
            {
                SpeakerCompression::Processor p;
                p.prepare(sampleRate);
                p.setPush(push);
                p.setHeadroom(headroom);
                std::mt19937 rng(3);
                std::uniform_real_distribution<float> noise(-4.0f, 4.0f); // far beyond full scale
                for (int i = 0; i < 20000; ++i)
                    finite &= std::isfinite(p.processSample(noise(rng)));
            }
        check(finite, "finite output for wild input at every Push and headroom");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
