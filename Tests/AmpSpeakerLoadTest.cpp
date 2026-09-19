// Verify AmpSpeakerLoad against the physics it claims to model:
//  - the digital filter IS the bilinear transform of the analog closed-loop
//    response H = A*d / (1 + A*d*beta), divided by its own neutral
//    response (to numerical precision), so the 5-state construction and
//    the neutral inverse are right; Presence = Resonance = 0.5 is exactly
//    flat;
//  - the speaker's impedance has a resonance peak near fs, sits near Re in
//    the midrange, and climbs with frequency (voice-coil inductance);
//  - Presence changes the treble and leaves the bass alone; Resonance
//    changes the bass and leaves the treble alone;
//  - the speaker's impedance only shows through when feedback is LOW: with
//    heavy feedback the load drops out (a voltage source), with none the
//    output follows the load's divider (a current-source-like amp) -
//    the damping-factor story that is the whole reason for these controls;
//  - it is stable, including while the knobs move.

#include "../Source/dsp/AmpSpeakerLoad.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    // Steady-state gain of the actual digital filter at one frequency, by
    // running a sine through it (RMS out / RMS in over whole seconds).
    double measuredGainDb(float presence, float resonance, double freq, const AmpSpeakerLoad::Speaker& sp = {},
                          const AmpSpeakerLoad::Amp& amp = {})
    {
        AmpSpeakerLoad::Filter filter(sp, amp);
        filter.prepare(sampleRate);
        filter.setControls(presence, resonance);

        auto settle = static_cast<int>(1.0 * sampleRate);
        auto measure = static_cast<int>(2.0 * sampleRate);
        double inSq = 0.0, outSq = 0.0;
        for (int n = 0; n < settle + measure; ++n)
        {
            auto x = static_cast<float>(0.1 * std::sin(2.0 * pi * freq * n / sampleRate));
            auto y = filter.processSample(x);
            if (n >= settle)
            {
                inSq += static_cast<double>(x) * x;
                outSq += static_cast<double>(y) * y;
            }
        }
        return 10.0 * std::log10(outSq / inSq);
    }

    // What the bilinear transform of the analog response says the digital
    // filter's gain at `freq` must be: the analog closed loop over its own
    // neutral, both evaluated at the pre-warped frequency.
    double bilinearPredictionDb(float presence, float resonance, double freq, const AmpSpeakerLoad::Speaker& sp = {},
                                const AmpSpeakerLoad::Amp& amp = {})
    {
        auto warped = 2.0 * sampleRate * std::tan(pi * freq / sampleRate);
        auto h = std::abs(AmpSpeakerLoad::analogResponse(sp, amp, presence, resonance, warped));
        auto neutral = std::abs(AmpSpeakerLoad::analogResponse(sp, amp, 0.5, 0.5, warped));
        return 20.0 * std::log10(h / neutral);
    }

    // The physics view: the closed loop's own shape (not flat, even at neutral).
    double shape(double presence, double resonance, double freq, const AmpSpeakerLoad::Speaker& sp = {},
                 const AmpSpeakerLoad::Amp& amp = {})
    {
        return AmpSpeakerLoad::shapeDb(sp, amp, presence, resonance, freq);
    }

    // What the knobs do to the signal: the shape relative to the neutral shape.
    double deviation(double presence, double resonance, double freq, const AmpSpeakerLoad::Speaker& sp = {},
                     const AmpSpeakerLoad::Amp& amp = {})
    {
        return AmpSpeakerLoad::deviationDb(sp, amp, presence, resonance, freq);
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    AmpSpeakerLoad::Speaker speaker;
    AmpSpeakerLoad::Amp amp;

    std::printf("=== The speaker's impedance curve ===\n");
    {
        double peak = 0.0, peakFreq = 0.0;
        for (double f = 20.0; f <= 400.0; f *= 1.01)
        {
            auto z = std::abs(AmpSpeakerLoad::speakerImpedance(speaker, f));
            if (z > peak) { peak = z; peakFreq = f; }
        }
        auto mid = std::abs(AmpSpeakerLoad::speakerImpedance(speaker, 500.0));
        auto high = std::abs(AmpSpeakerLoad::speakerImpedance(speaker, 8000.0));
        std::printf("  peak %.0f ohms at %.0f Hz | 500 Hz: %.1f ohms | 8 kHz: %.1f ohms (Re = %.1f)\n",
                     peak, peakFreq, mid, high, speaker.re);
        check(peakFreq > 70.0 && peakFreq < 100.0, "the impedance peaks at the cone's resonance (guitar speakers: ~75-100 Hz)");
        check(peak > 25.0 && peak < 60.0, "and the peak is several times the nominal impedance (~30-50 ohms)");
        check(mid > speaker.re && mid < speaker.re * 1.6, "in the midrange it sits just above the voice coil's DC resistance");
        check(high > 25.0, "and it climbs at high frequencies (voice-coil inductance)");
    }

    std::printf("\n=== The digital filter IS the bilinear transform of the closed loop over its neutral ===\n");
    {
        double worstExact = 0.0, worstUnwarped = 0.0;
        const float settings[][2] = { { 0.5f, 0.5f }, { 1.0f, 0.5f }, { 0.0f, 0.5f }, { 0.5f, 1.0f }, { 0.5f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f } };
        for (auto& s : settings)
            for (double f : { 40.0, 60.0, 85.0, 120.0, 200.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
            {
                auto measured = measuredGainDb(s[0], s[1], f);
                worstExact = std::max(worstExact, std::abs(measured - bilinearPredictionDb(s[0], s[1], f)));
                worstUnwarped = std::max(worstUnwarped, std::abs(measured - deviation(s[0], s[1], f)));
            }
        std::printf("  worst deviation from the exact bilinear prediction: %.4f dB | from the un-warped analog formula: %.2f dB\n",
                     worstExact, worstUnwarped);
        check(worstExact < 0.02, "matches the bilinear transform of H(p,r)/H(neutral) to numerical precision");
        // The gap to the UN-warped analog formula is the bilinear transform's
        // frequency warping (exactly predicted by the line above), largest
        // where the response is steepest - here about 0.7 dB at 8 kHz at
        // 48 kHz. It is inherent to the method, not an error.
        check(worstUnwarped < 1.0, "and stays within 1 dB of the plain analog formula up to 8 kHz (the bilinear transform's warping)");

        double worstNeutral = 0.0;
        for (double f = 30.0; f <= 12000.0; f *= 1.15)
            worstNeutral = std::max(worstNeutral, std::abs(measuredGainDb(0.5f, 0.5f, f)));
        std::printf("  neutral (both knobs 0.5): largest deviation from 0 dB across 30 Hz - 12 kHz: %.5f dB\n", worstNeutral);
        check(worstNeutral < 0.01, "the neutral setting is EXACTLY flat (the default changes nothing)");
    }

    std::printf("\n=== Presence is a treble control, Resonance a bass control ===\n");
    {
        std::printf("  4 kHz by Presence 0/25/50/75/100%%:");
        double previous = -1.0e9;
        bool rises = true;
        for (double p : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            auto g = deviation(p, 0.5, 4000.0);
            std::printf(" %+.1f", g);
            rises &= g > previous;
            previous = g;
        }
        auto span = deviation(1.0, 0.5, 4000.0) - deviation(0.0, 0.5, 4000.0);
        auto bassMove = std::abs(deviation(1.0, 0.5, 100.0) - deviation(0.0, 0.5, 100.0));
        std::printf(" dB vs neutral (span %.1f dB; the same sweep moves 100 Hz by %.2f dB)\n", span, bassMove);
        check(deviation(0.0, 0.5, 4000.0) < 0.0 && deviation(1.0, 0.5, 4000.0) > 0.0,
              "below 50 it tightens the treble, above 50 it opens it up");
        check(rises && span > 8.0, "Presence raises the treble at every step, over a range of more than 8 dB");
        check(bassMove < 1.5, "and barely touches the bass");

        std::printf("  85 Hz by Resonance 0/25/50/75/100%%:");
        previous = -1.0e9;
        rises = true;
        for (double r : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            auto g = deviation(0.5, r, 85.0);
            std::printf(" %+.1f", g);
            rises &= g > previous;
            previous = g;
        }
        auto bassSpan = deviation(0.5, 1.0, 85.0) - deviation(0.5, 0.0, 85.0);
        auto trebleMove = std::abs(deviation(0.5, 1.0, 4000.0) - deviation(0.5, 0.0, 4000.0));
        std::printf(" dB vs neutral (span %.1f dB; the same sweep moves 4 kHz by %.2f dB)\n", bassSpan, trebleMove);
        check(deviation(0.5, 0.0, 85.0) < 0.0 && deviation(0.5, 1.0, 85.0) > 0.0,
              "below 50 it tightens the bass, above 50 it lets it bloom");
        check(rises && bassSpan > 7.0, "Resonance raises the bass at every step, over a range of more than 7 dB");
        check(trebleMove < 1.0, "and barely touches the treble");
    }

    std::printf("\n=== The speaker's impedance only shows through when feedback is low ===\n");
    {
        AmpSpeakerLoad::Speaker resistor;
        resistor.resistive = true; // a flat 8 ohm load: same amp, no impedance curve

        // How much the real speaker changes the response compared with a plain
        // 8 ohm resistor, AT THE FREQUENCY ITSELF: |H_speaker| / |H_resistor|
        // in dB, same amp and knobs. (No reference frequency: a reference
        // would be confounded by the knobs' shelves reaching it.) At the
        // speaker's impedance peak (85 Hz) and in its inductive treble rise (5 kHz).
        auto interaction = [&](const AmpSpeakerLoad::Amp& a, double presence, double resonance, double freq)
        {
            auto omega = 2.0 * pi * freq;
            auto real = std::abs(AmpSpeakerLoad::analogResponse(speaker, a, presence, resonance, omega));
            auto flat = std::abs(AmpSpeakerLoad::analogResponse(resistor, a, presence, resonance, omega));
            return 20.0 * std::log10(real / flat);
        };

        // Overall feedback depth is the one physically clean variable: scale
        // beta and nothing else changes (the loop gain keeps its phase). A
        // single KNOB is not - the presence shelf adds phase lag to the loop,
        // so |1 + L| does not track 1 + |L| and "knob at max" is not
        // guaranteed to beat "knob at neutral" in every band.
        std::printf("  what the speaker's impedance adds over a flat 8 ohm load, by amount of feedback (x the amp's own):\n");
        std::printf("      feedback      at 85 Hz (impedance peak)   at 5 kHz (inductive rise)\n");
        double previousPeak = 1.0e9, previousTreble = 1.0e9;
        bool peakFalls = true, trebleFalls = true;
        double noFeedbackPeak = 0.0, noFeedbackTreble = 0.0, heavyPeak = 0.0, heavyTreble = 0.0;
        for (double scale : { 0.0, 0.25, 1.0, 4.0, 20.0 })
        {
            AmpSpeakerLoad::Amp a = amp;
            a.beta0 = amp.beta0 * scale;
            auto peak = interaction(a, 0.5, 0.5, 85.0), treble = interaction(a, 0.5, 0.5, 5000.0);
            std::printf("      x%-6.2f       %+6.2f dB                   %+6.2f dB%s\n", scale, peak, treble,
                         scale == 0.0 ? "   (no feedback: a current-source-like amp)"
                                      : (scale == 20.0 ? "   (very heavy: a stiff voltage source)" : ""));
            peakFalls &= peak < previousPeak;
            trebleFalls &= treble < previousTreble;
            previousPeak = peak;
            previousTreble = treble;
            if (scale == 0.0) { noFeedbackPeak = peak; noFeedbackTreble = treble; }
            if (scale == 20.0) { heavyPeak = peak; heavyTreble = treble; }
        }
        check(noFeedbackPeak > 4.0 && noFeedbackTreble > 4.0,
              "with no feedback the speaker's peak and inductive rise come through strongly (>4 dB)");
        check(peakFalls && trebleFalls, "every step up in feedback reduces it, at both frequencies");
        check(std::abs(heavyPeak) < 0.3 && std::abs(heavyTreble) < 0.3,
              "with very heavy feedback the load drops out (a voltage source: the speaker's curve does almost nothing)");

        // At the amp's actual settings the interaction is present but small.
        auto neutralPeak = interaction(amp, 0.5, 0.5, 85.0), neutralTreble = interaction(amp, 0.5, 0.5, 5000.0);
        std::printf("  at the neutral setting: %+.2f dB at 85 Hz, %+.2f dB at 5 kHz\n", neutralPeak, neutralTreble);
        check(neutralPeak > 0.2 && neutralPeak < 2.0 && neutralTreble > 0.2 && neutralTreble < 2.0,
              "a real amp's feedback keeps it to a dB or so (audible only as a colour)");
    }

    std::printf("\n=== Stability ===\n");
    {
        bool decays = true, finite = true;
        for (float p : { 0.0f, 1.0f })
            for (float r : { 0.0f, 1.0f })
            {
                AmpSpeakerLoad::Filter filter;
                filter.prepare(sampleRate);
                filter.setControls(p, r);
                double peak = 0.0, tail = 0.0;
                for (int n = 0; n < static_cast<int>(3.0 * sampleRate); ++n)
                {
                    auto y = std::abs(static_cast<double>(filter.processSample(n == 0 ? 1.0f : 0.0f)));
                    finite &= std::isfinite(y);
                    if (n < 4800) peak = std::max(peak, y);
                    if (n > static_cast<int>(2.5 * sampleRate)) tail = std::max(tail, y);
                }
                decays &= tail < peak * 1.0e-4;
            }
        check(decays && finite, "the impulse response dies away at all four knob corners");

        // Sweep both knobs continuously while a note plays.
        AmpSpeakerLoad::Filter filter;
        filter.prepare(sampleRate);
        double maxOut = 0.0;
        bool bounded = true;
        for (int n = 0; n < 96000; ++n)
        {
            if (n % 100 == 0)
            {
                auto phase = static_cast<float>(n) / 96000.0f * 6.0f * static_cast<float>(pi);
                filter.setControls(0.5f + 0.5f * std::sin(phase), 0.5f + 0.5f * std::cos(phase * 0.7f));
            }
            auto y = filter.processSample(static_cast<float>(std::sin(2.0 * pi * 110.0 * n / sampleRate)));
            bounded &= std::isfinite(y);
            maxOut = std::max(maxOut, std::abs(static_cast<double>(y)));
        }
        std::printf("  peak output while sweeping both knobs under a 110 Hz note: %.2f (input peak 1.0)\n", maxOut);
        check(bounded && maxOut < 6.0, "moving Presence and Resonance while playing stays finite and bounded");

        AmpSpeakerLoad::Filter wild;
        wild.prepare(sampleRate);
        bool wildFinite = true;
        for (int n = 0; n < 20000; ++n)
            wildFinite &= std::isfinite(wild.processSample((n % 2 == 0) ? 100.0f : -100.0f));
        check(wildFinite, "finite for a huge alternating input");
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
