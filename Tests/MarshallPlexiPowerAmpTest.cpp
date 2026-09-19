// Verify MarshallPlexiPowerAmp - the 1959HW's phase inverter, four EL34s, output
// transformer, speaker and global feedback loop, traced from the July 1970
// Unicord drawing.
//
// There is no printed test voltage to calibrate against, so the checks are the
// physical behaviours the circuit must have, each measured rather than assumed:
//
//  - it idles where its bias says it should, and stays there;
//  - the feedback loop is STABLE WITH MARGIN. This is the check that matters most
//    and the one that found a real problem while building it: the loop gain is
//    measured directly (open the loop, inject a tone at the feedback input, read
//    the speaker terminals; then apply the feedback filter and the one-sample
//    delay the closed loop adds) and the gain and phase margins are computed from
//    the resulting Nyquist data, at several Presence settings, transformer taps
//    and cabinets;
//  - the feedback does what Marshall's manual says: it is taken from the speaker
//    output, so a lower impedance tap gets less of it ("the lower the impedance
//    setting, the lower the damping factor"), which loosens the speaker's
//    resonance;
//  - Presence removes feedback at high frequencies and opens the top end;
//  - the feedback flattens the response and cuts distortion;
//  - power and distortion vs level: about 100W (the spec) at the usual 5% THD, a
//    push-pull stage's odd-order character, class AB (the tubes leave class A
//    overlap as the level rises), the drift into hard clipping;
//  - the supply sags under sustained drive and recovers; the driver grids'
//    blocking (a hard-driven Plexi squashing itself) shifts the bias and recovers;
//  - it is well behaved: finite and bounded on noise with every control moving,
//    and reset() gives a replay-identical amp.
//
// The amp runs at 192kHz here (4x a 48kHz host), as it does in the app. Spectra
// are Hann-windowed lock-ins (raw Goertzel would read leakage as distortion).

#include "../Source/dsp/MarshallPlexiPowerAmp.h"
#include "../Source/dsp/MarshallToneStack.h"

#include <complex>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    using Cx = std::complex<double>;
    constexpr double fs = 192000.0;

    // The tone stack feeding the power amp, exactly as the amp will wire them:
    // the stack's output node is the phase inverter's grid, which may draw current.
    struct Rig
    {
        MarshallToneStack stack { fs };
        MarshallPlexiPowerAmp amp { fs };
        double grid = 0.0, speaker = 0.0;

        Rig() { stack.setControls(0.5f, 0.5f, 0.5f); }

        void step(double volts)
        {
            auto free = stack.beginSample(volts);
            auto ohms = stack.outputOhms();
            auto drawn = amp.process(free, ohms);
            grid = stack.finishSample(drawn);
            speaker = amp.speakerVolts();
        }
    };

    // Hann-windowed complex amplitude of x at f.
    Cx tone(const std::vector<double>& x, double f)
    {
        Cx sum = 0.0;
        auto n = static_cast<double>(x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            auto w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / (n - 1.0));
            sum += w * x[i] * std::polar(1.0, -2.0 * M_PI * f * static_cast<double>(i) / fs);
        }
        return sum * 2.0 / (n * 0.5);
    }

    struct Measured
    {
        double speaker = 0.0, grid = 0.0;          // fundamental amplitudes (peak volts)
        double thd = 0.0, h2 = 0.0, h3 = 0.0;      // relative to the fundamental
        double minAmpsA = 1.0e9, minAmpsB = 1.0e9; // smallest per-tube current over the window
        double minSupply = 1.0e9;
        double meanShift = 0.0;                    // mean driver-grid shift from idle
        double peak = 0.0;
    };

    Measured measure(Rig& r, double f, double sourceVolts, double seconds, double settle)
    {
        Measured m;
        std::vector<double> spk, grd;
        auto ns = static_cast<int>(settle * fs), n = static_cast<int>(seconds * fs);
        double shift = 0.0;
        for (int i = 0; i < ns + n; ++i)
        {
            r.step(sourceVolts * std::sin(2.0 * M_PI * f * i / fs));
            if (i < ns)
                continue;
            spk.push_back(r.speaker);
            grd.push_back(r.grid);
            m.minAmpsA = std::min(m.minAmpsA, r.amp.tubeAmpsA() / 2.0);
            m.minAmpsB = std::min(m.minAmpsB, r.amp.tubeAmpsB() / 2.0);
            m.minSupply = std::min(m.minSupply, r.amp.supplyVolts());
            m.peak = std::max(m.peak, std::abs(r.speaker));
            shift += r.amp.biasShiftVolts();
        }
        auto h1 = std::abs(tone(spk, f));
        m.speaker = h1;
        m.grid = std::abs(tone(grd, f));
        double sum = 0.0;
        for (int k = 2; k <= 9; ++k)
        {
            auto a = std::abs(tone(spk, f * k));
            sum += a * a;
            if (k == 2) m.h2 = a / h1;
            if (k == 3) m.h3 = a / h1;
        }
        m.thd = std::sqrt(sum) / h1;
        m.meanShift = shift / static_cast<double>(n);
        return m;
    }

    double watts(double peakVolts, double ohms = 16.0) { return peakVolts * peakVolts / 2.0 / ohms; }
    double db(double x) { return 20.0 * std::log10(std::max(x, 1.0e-12)); }

    // ---- Loop gain, measured. ----
    // With the loop open, a tone injected at the feedback input (the junction's
    // 47k) comes out at the speaker terminals as H(f). The closed loop applies the
    // feedback band-limit (two poles) and a one-sample delay to that path, so
    //     L(f) = -H(f) * LP(f)^2 * z^-1 .
    struct Loop
    {
        std::vector<double> freqs;
        std::vector<Cx> l;
    };

    template <typename Configure>
    Loop measureLoop(Configure&& configure)
    {
        Loop loop;
        auto alpha = 1.0 - std::exp(-2.0 * M_PI * MarshallPlexiPowerAmp::feedbackBandwidthHz / fs);
        for (double f = 150.0; f < 90000.0; f *= 1.16)
        {
            Rig r;
            configure(r);
            r.amp.setFeedbackEnabled(false);
            auto n = static_cast<int>(0.09 * fs);
            std::vector<double> in, out;
            for (int i = 0; i < n; ++i)
            {
                auto v = 0.05 * std::sin(2.0 * M_PI * f * i / fs);
                r.amp.setInjectedFeedback(v);
                r.step(0.0);
                if (i > n / 2) { in.push_back(v); out.push_back(r.speaker); }
            }
            auto h = tone(out, f) / tone(in, f);
            auto z = std::polar(1.0, -2.0 * M_PI * f / fs);
            auto lp = alpha / (1.0 - (1.0 - alpha) * z);
            loop.freqs.push_back(f);
            loop.l.push_back(-h * lp * lp * z);
        }
        return loop;
    }

    struct Margins { double gainMarginDb = 1.0e9, phaseMarginDeg = 1.0e9; double gainAtHz = 0.0, phaseAtHz = 0.0; };

    // From the Nyquist data: the gain margin is how far below 0dB |L| is where its
    // (unwrapped) phase first reaches -180 degrees; the phase margin is how far
    // above -180 the phase is where |L| falls through 0dB.
    Margins margins(const Loop& loop)
    {
        Margins m;
        double unwrapped = 0.0, prevRaw = 0.0, prevPhase = 0.0, prevMag = 0.0;
        for (size_t k = 0; k < loop.l.size(); ++k)
        {
            auto raw = std::arg(loop.l[k]) * 180.0 / M_PI;
            if (k == 0) unwrapped = raw;
            else
            {
                auto d = raw - prevRaw;
                while (d > 180.0) d -= 360.0;
                while (d < -180.0) d += 360.0;
                unwrapped += d;
            }
            auto mag = db(std::abs(loop.l[k]));
            if (k > 0)
            {
                if (prevPhase > -180.0 && unwrapped <= -180.0)
                {
                    auto worse = std::max(mag, prevMag);
                    if (-worse < m.gainMarginDb) { m.gainMarginDb = -worse; m.gainAtHz = loop.freqs[k]; }
                }
                if (prevMag > 0.0 && mag <= 0.0)
                {
                    auto pm = 180.0 + std::min(unwrapped, prevPhase);
                    if (pm < m.phaseMarginDeg) { m.phaseMarginDeg = pm; m.phaseAtHz = loop.freqs[k]; }
                }
            }
            prevRaw = raw; prevPhase = unwrapped; prevMag = mag;
        }
        return m;
    }

    Cx loopAt(const Loop& loop, double f)
    {
        size_t best = 0;
        for (size_t k = 0; k < loop.freqs.size(); ++k)
            if (std::abs(std::log(loop.freqs[k] / f)) < std::abs(std::log(loop.freqs[best] / f)))
                best = k;
        return loop.l[best];
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // --- 1. Operating point. ---
    std::printf("=== Idle ===\n");
    {
        Rig r;
        auto& o = r.amp.operating();
        std::printf("  EL34 idle %.1f mA each, fixed bias %.1f V, B+ %.0f V;  phase inverter: %.2f mA tail, cathode %.1f V, plates %.0f V (82k) / %.0f V (100k)\n",
                     o.tubeAmps * 1e3, o.biasVolts, o.supplyVolts, o.piTailAmps * 1e3, o.piCathodeVolts, o.piPlate1Volts, o.piPlate2Volts);
        check(std::abs(o.tubeAmps - 0.035) < 0.0005, "the bias gives the intended 35mA per EL34");
        check(o.biasVolts < -35.0 && o.biasVolts > -60.0, "at a fixed bias of a few tens of volts negative (a plexi runs about -50V)");
        check(o.piTailAmps > 1.5e-3 && o.piTailAmps < 4.0e-3 && o.piCathodeVolts > 20.0 && o.piCathodeVolts < 50.0,
              "the phase inverter's tail carries a couple of mA and its cathode sits a few tens of volts up");
        check(o.piPlate1Volts != o.piPlate2Volts, "the 82k and 100k plate loads leave the two plates at different voltages (deliberately unbalanced)");

        double peak = 0.0;
        for (int i = 0; i < static_cast<int>(0.15 * fs); ++i) { r.step(0.0); peak = std::max(peak, std::abs(r.speaker)); }
        std::printf("  after 150ms of silence: tubes %.2f / %.2f mA, speaker DC/peak %.3f V, B+ %.2f V\n",
                     r.amp.tubeAmpsA() * 500.0, r.amp.tubeAmpsB() * 500.0, peak, r.amp.supplyVolts());
        check(std::abs(r.amp.tubeAmpsA() * 500.0 - 35.0) < 1.5 && std::abs(r.amp.tubeAmpsB() * 500.0 - 35.0) < 1.5, "idles where it was biased (both sides within 1.5mA)");
        check(peak < 0.5, "and puts nothing out (< 0.5V at the speaker)");
        check(std::abs(r.amp.supplyVolts() - 460.0) < 1.0, "with B+ steady at +460V");
    }
    std::printf("\n");

    // --- 2. The feedback loop is stable, with margin. ---
    std::printf("=== Feedback loop stability (loop gain measured by injection) ===\n");
    struct Case { const char* name; float presence; double tap; double cab; };
    Loop loop16Presence0;
    for (auto c : { Case { "Presence 0, 16 ohm tap, 16 ohm cab", 0.0f, 16.0, 16.0 },
                    Case { "Presence 0.5, 16 ohm tap, 16 ohm cab", 0.5f, 16.0, 16.0 },
                    Case { "Presence 1, 16 ohm tap, 16 ohm cab", 1.0f, 16.0, 16.0 },
                    Case { "Presence 0, 16 ohm tap, 4 ohm cab (mismatched)", 0.0f, 16.0, 4.0 },
                    Case { "Presence 0, 4 ohm tap, 16 ohm cab (mismatched)", 0.0f, 4.0, 16.0 },
                    Case { "Presence 0, 8 ohm tap, 8 ohm cab", 0.0f, 8.0, 8.0 } })
    {
        auto loop = measureLoop([&](Rig& r) {
            r.amp.setPresence(c.presence);
            r.amp.setSpeaker(AmpSpeakerLoad::Speaker {}, c.cab);
            r.amp.setImpedanceTap(c.tap);
        });
        auto m = margins(loop);
        if (m.phaseMarginDeg > 1.0e8)
            std::printf("  %-48s gain margin %5.1f dB (at %5.0f Hz), loop gain never reaches 0dB above the first point\n", c.name, m.gainMarginDb, m.gainAtHz);
        else
            std::printf("  %-48s gain margin %5.1f dB (at %5.0f Hz), phase margin %5.1f deg (at %5.0f Hz)\n", c.name, m.gainMarginDb, m.gainAtHz, m.phaseMarginDeg, m.phaseAtHz);
        check(m.gainMarginDb > 4.0 && (m.phaseMarginDeg > 20.0 || m.phaseMarginDeg > 1.0e8), "stable with at least 4dB gain margin and 20 degrees of phase margin");
        if (c.presence == 0.0f && c.tap == 16.0 && c.cab == 16.0)
            loop16Presence0 = loop;
    }
    {
        auto l1k = loopAt(loop16Presence0, 1000.0);
        auto depth = db(std::abs(1.0 + l1k));
        std::printf("  at 1kHz, Presence 0: loop gain %.1f dB, so %.1f dB of negative feedback\n", db(std::abs(l1k)), depth);
        check(depth > 6.0 && depth < 16.0, "a moderate amount of feedback at midband (6-16dB - a Plexi is usually put around 10)");
    }
    std::printf("\n");

    // --- 3. The feedback comes from the speaker output: the tap matters. ---
    std::printf("=== Manual, Tonal Note 3: the lower the impedance tap, the less the damping ===\n");
    {
        auto loopAtTap = [&](double tap) {
            return measureLoop([&](Rig& r) { r.amp.setPresence(0.0f); r.amp.setSpeaker(AmpSpeakerLoad::Speaker {}, tap); r.amp.setImpedanceTap(tap); });
        };
        auto l16 = loopAtTap(16.0), l4 = loopAtTap(4.0);
        auto ratio = db(std::abs(loopAt(l4, 1000.0))) - db(std::abs(loopAt(l16, 1000.0)));
        std::printf("  loop gain at 1kHz, each tap on its own matched cabinet: the 4 ohm tap is %+.1f dB against the 16 ohm tap (the tap's voltage is 1/2 as big: -6dB)\n", ratio);
        check(std::abs(ratio + 6.0) < 2.0, "feedback taken at the selected tap: the 4 ohm tap has ~6dB less loop gain");

        // The speaker's resonance shows through more when the loop is weaker (the
        // manual's example: a 4x12 that offers 16 and 4 ohm inputs - each a matched
        // load, the 4 ohm input on the 4 ohm tap).
        auto bump = [&](double tap) {
            Rig a, b;
            for (auto* r : { &a, &b })
            {
                r->amp.setPresence(0.0f);
                r->amp.setSpeaker(AmpSpeakerLoad::Speaker {}, tap);
                r->amp.setImpedanceTap(tap);
            }
            auto low = measure(a, 85.0, 0.4, 0.5, 0.25), mid = measure(b, 500.0, 0.4, 0.15, 0.1);
            return db(low.speaker / low.grid) - db(mid.speaker / mid.grid);
        };
        auto tight = bump(16.0), loose = bump(4.0);
        std::printf("  the speaker's 85Hz resonance vs 500Hz, closed loop: %+.2f dB at the 16 ohm tap, %+.2f dB at the 4 ohm tap\n", tight, loose);
        check(loose > tight + 0.3, "a lower tap gives a bigger resonant bump - a looser low end, as the manual says");
    }
    std::printf("\n");

    // --- 4. Presence and the loop's flattening. ---
    std::printf("=== Presence and the feedback's effect on the response ===\n");
    {
        auto response = [&](float presence, bool feedback, double f) {
            Rig r;
            r.amp.setPresence(presence);
            r.amp.setFeedbackEnabled(feedback);
            auto m = measure(r, f, 0.3, f < 300.0 ? 0.4 : 0.15, f < 300.0 ? 0.25 : 0.1);
            return db(m.speaker / m.grid);
        };
        double mid0 = response(0.0f, true, 500.0), mid1 = response(1.0f, true, 500.0);
        double top0 = response(0.0f, true, 4000.0), top1 = response(1.0f, true, 4000.0), top5 = response(0.5f, true, 4000.0);
        std::printf("  4kHz relative to 500Hz: Presence 0 %+.1f dB, 0.5 %+.1f dB, 1 %+.1f dB\n", top0 - mid0, top5 - response(0.5f, true, 500.0), top1 - mid1);
        std::printf("  Presence 0 -> 1 lifts 4kHz by %.1f dB and 500Hz by %.1f dB\n", top1 - top0, mid1 - mid0);
        check((top1 - top0) - (mid1 - mid0) > 4.0, "turning Presence up opens the top end (>4dB more at 4kHz than at 500Hz)");
        check(top5 > top0 + 1.0 && top1 > top5 + 1.0, "and monotonically: each step up brings more 4kHz");
        check(top0 - mid0 > -3.0 && top0 - mid0 < 3.0, "with Presence 0 the feedback holds 4kHz within 3dB of 500Hz");

        // Flattening: the spread of gain over 100Hz-5kHz with the loop closed vs open.
        auto spread = [&](bool feedback) {
            double lo = 1.0e9, hi = -1.0e9;
            for (double f : { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0 })
            {
                auto g = response(0.0f, feedback, f);
                lo = std::min(lo, g); hi = std::max(hi, g);
            }
            return hi - lo;
        };
        auto closed = spread(true), open = spread(false);
        std::printf("  response spread over 100Hz-3.2kHz (max - min): %.1f dB open loop, %.1f dB closed loop\n", open, closed);
        check(closed < open - 3.0, "the feedback flattens the response (>3dB less spread)");
    }
    std::printf("\n");

    // --- 5. Power and distortion vs level. ---
    std::printf("=== Power and distortion at 1kHz into 16 ohms ===\n");
    double fivePercentWatts = 0.0;
    {
        double prevThd = 0.0, prevWatts = 0.0;
        bool monotonic = true, oddDominant = true, classAb = false, classAOverlap = true;
        double maxWatts = 0.0;
        for (double v : { 0.5, 2.0, 4.0, 8.0, 12.0, 16.0, 22.0, 32.0, 45.0, 64.0 })
        {
            Rig r;
            auto m = measure(r, 1000.0, v, 0.12, 0.06);
            auto w = watts(m.speaker);
            std::printf("  %5.1fV in: PI grid %5.2fV, %6.1f W, THD %5.1f%% (H2 %4.1f%%, H3 %4.1f%%), lowest tube current %5.1f mA\n",
                         v, m.grid, w, m.thd * 100.0, m.h2 * 100.0, m.h3 * 100.0, std::min(m.minAmpsA, m.minAmpsB) * 1e3);
            monotonic &= m.thd >= prevThd - 0.002;
            if (m.thd > 0.05 && prevThd <= 0.05 && fivePercentWatts == 0.0)
                fivePercentWatts = prevWatts + (w - prevWatts) * (0.05 - prevThd) / (m.thd - prevThd);
            if (m.thd < 0.10 && m.thd > 0.005) oddDominant &= m.h3 > 3.0 * m.h2;
            if (v == 0.5) classAOverlap = std::min(m.minAmpsA, m.minAmpsB) > 0.030;
            if (std::min(m.minAmpsA, m.minAmpsB) < 0.002) classAb = true;
            maxWatts = std::max(maxWatts, w);
            prevThd = m.thd; prevWatts = w;
        }
        std::printf("  power at 5%% THD (interpolated): %.0f W; most power seen: %.0f W\n", fivePercentWatts, maxWatts);
        check(fivePercentWatts > 60.0 && fivePercentWatts < 140.0, "about 100W at 5% THD (the 1959HW is a 100W amp: 60-140W accepted)");
        check(maxWatts > 100.0, "more than 100W available before it runs out of swing");
        check(monotonic, "distortion rises with level all the way up");
        check(oddDominant, "push-pull: odd harmonics dominate (H3 > 3x H2) through the useful range");
        check(classAOverlap, "at small signals both sides conduct throughout (class A overlap)");
        check(classAb, "at large signals each side cuts off during its half of the cycle (class AB)");
    }
    std::printf("\n");

    // --- 6. The feedback lowers distortion. ---
    std::printf("=== Feedback vs distortion at the same output level (~25W) ===\n");
    {
        auto atLevel = [&](bool feedback) {
            double v = 6.0;
            Measured m;
            for (int i = 0; i < 6; ++i)
            {
                Rig r;
                r.amp.setFeedbackEnabled(feedback);
                m = measure(r, 1000.0, v, 0.1, 0.05);
                v *= 28.0 / m.speaker;   // aim for 28V peak = 24.5W
            }
            return m;
        };
        auto closed = atLevel(true), open = atLevel(false);
        std::printf("  THD at ~%.0f W: %.2f%% with feedback, %.2f%% without (%.1f dB less)\n", watts(closed.speaker), closed.thd * 100.0, open.thd * 100.0, db(open.thd / closed.thd));
        check(std::abs(closed.speaker - open.speaker) < 2.0, "measured at matched output levels");
        check(open.thd > 2.0 * closed.thd, "feedback at least halves the distortion (6dB)");
    }
    std::printf("\n");

    // --- 7. Sag and blocking. ---
    std::printf("=== Supply sag and driver-grid blocking ===\n");
    {
        Rig r;
        std::vector<double> early, late;
        auto n = static_cast<int>(0.30 * fs);
        double minSupply = 1.0e9;
        for (int i = 0; i < n; ++i)
        {
            r.step(38.0 * std::sin(2.0 * M_PI * 1000.0 * i / fs));
            minSupply = std::min(minSupply, r.amp.supplyVolts());
            if (i >= static_cast<int>(0.004 * fs) && i < static_cast<int>(0.010 * fs)) early.push_back(r.speaker);   // the first few cycles, before B+ has fallen
            if (i >= static_cast<int>(0.26 * fs)) late.push_back(r.speaker);
        }
        // Rebuild equal-length windows.
        early.resize(std::min(early.size(), late.size()));
        late.resize(early.size());
        auto e = std::abs(tone(early, 1000.0)), l = std::abs(tone(late, 1000.0));
        std::printf("  a 0.3s burst at ~%.0f W: B+ falls to %.0f V (from 460); output %.2f dB quieter at the end than at the start\n", watts(e), minSupply, db(l / e));
        check(minSupply < 445.0, "B+ sags by more than 15V under sustained drive");
        check(db(l / e) < -0.15, "and the output compresses as it does");

        auto recoverSteps = static_cast<int>(0.2 * fs);
        for (int i = 0; i < recoverSteps; ++i) r.step(0.0);
        std::printf("  0.2s after the burst: B+ %.1f V\n", r.amp.supplyVolts());
        check(std::abs(r.amp.supplyVolts() - 460.0) < 2.0, "B+ recovers to within 2V of idle");
    }
    {
        Rig r;
        double worstShift = 0.0, hardPeak = 0.0;
        auto n = static_cast<int>(0.10 * fs);
        for (int i = 0; i < n; ++i)
        {
            r.step(80.0 * std::sin(2.0 * M_PI * 1000.0 * i / fs));
            if (i > n / 2) { worstShift = std::min(worstShift, r.amp.biasShiftVolts()); hardPeak = std::max(hardPeak, std::abs(r.speaker)); }
        }
        std::printf("  ... with a peak of %.1f V at the speaker (the stage's physical swing is about 460V / (a/2) = 89V)\n", hardPeak);
        check(hardPeak < 95.0, "the plate-swing bound holds under extreme drive (no flyback runaway)");
        for (int i = 0; i < static_cast<int>(0.15 * fs); ++i) r.step(0.0);
        auto after = r.amp.biasShiftVolts();
        std::printf("  hard overdrive (80V in): the driver grids' mean bias is pushed to %+.1f V from idle; 0.15s later %+.2f V\n", worstShift, after);
        check(worstShift < -1.0, "grid conduction charges the coupling caps and shifts the bias negative (blocking)");
        check(std::abs(after) < 0.5, "and it recovers when the drive stops");
    }
    std::printf("\n");

    // --- 8. Robustness. ---
    std::printf("=== Robustness ===\n");
    {
        Rig r;
        unsigned seed = 4242u;
        bool ok = true;
        for (int block = 0; block < 400; ++block)
        {
            r.amp.setPresence(static_cast<float>(block % 5) / 4.0f);
            r.amp.setImpedanceTap(block % 3 == 0 ? 4.0 : (block % 3 == 1 ? 8.0 : 16.0));
            r.stack.setControls(static_cast<float>(block % 4) / 3.0f, static_cast<float>(block % 3) / 2.0f, static_cast<float>(block % 7) / 6.0f);
            for (int i = 0; i < 256; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                r.step(40.0 * (static_cast<double>(seed >> 8) / 8388608.0 - 1.0));
                ok &= std::isfinite(r.speaker) && std::abs(r.speaker) < 600.0;
            }
        }
        check(ok, "noise at 40V with every control changing every 256 samples stays finite and bounded (< 600V at the speaker)");

        Rig a, b;
        std::vector<double> first, second;
        for (int i = 0; i < 20000; ++i) { a.step(5.0 * std::sin(2.0 * M_PI * 500.0 * i / fs)); first.push_back(a.speaker); }
        a.amp.reset(); a.stack.reset();
        for (int i = 0; i < 20000; ++i) { a.step(5.0 * std::sin(2.0 * M_PI * 500.0 * i / fs)); second.push_back(a.speaker); }
        double maxDiff = 0.0;
        for (size_t i = 0; i < first.size(); ++i) maxDiff = std::max(maxDiff, std::abs(first[i] - second[i]));
        std::printf("  replay after reset(): worst difference %.2e V\n", maxDiff);
        check(maxDiff < 1.0e-6, "reset() returns to a clean slate (a replay is identical)");
        (void) b;
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
