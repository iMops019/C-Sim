// Verify the Boss BD-2 model against its schematic (service-manual BD-2 MT board,
// as read for the research report the values come from).
//
// Layers, so a failure points at the right place:
//   1. NETLISTS. The test transcribes the passive networks (the fixed stack, the
//      shelf/Tone/Level network, the gyrator peak filter) from the schematic
//      itself and solves them at each frequency with its own complex circuit
//      solver. The module's time-domain networks must match that at every
//      frequency and knob setting - two independent implementations of the same
//      netlist - and the transcription must reproduce the figures the analyses
//      published (the stack's loss, the Tone control's curves, the bass peak).
//   2. THE AMPLIFIERS. The discrete op-amps are finite-gain, so their closed-loop
//      gain is checked against the ideal formulas (limit A -> infinity) and
//      against the published max-gain measurements.
//   3. THE WHOLE PATH at tiny level: measured against the cascade of all of the
//      above.
//   4. OVERLOAD. The output stops at the rails, roughly symmetrically, and the
//      diode stacks the schematic shows stay off in normal use (and are inert
//      where they can conduct).
//
// Gains are measured as output/input through the same Goertzel window (see
// GraphicEQTest); THD is Hann-windowed (see CentaurDriveStageTest).

#include "../Source/dsp/BluesDriverStage.h"
#include "../Source/dsp/PotTaper.h"
#include "ComplexCircuit.h"
#include "TestUtils.h"

#include <complex>
#include <cstdio>
#include <vector>

namespace
{
    using cd = std::complex<double>;
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 32768;
    constexpr int settleSamples = 12288;
    const BluesDriverStage::Components comps;

    int failures = 0;

    void check(bool ok, const char* what, double got, const char* unit = "")
    {
        std::printf("  [%s] %s (%.4g%s)\n", ok ? "PASS" : "FAIL", what, got, unit);
        if (! ok)
            ++failures;
    }

    using TestCircuit::Circuit;

    double db(cd v) { return TestUtils::toDb(std::abs(v)); }

    // ---- The schematic, transcribed for the test (values as read) ----
    struct Stack { Circuit circuit; int o = 0, gate = 0; };
    Stack stackFromSchematic()
    {
        Stack s;
        auto& c = s.circuit;
        auto x = c.node(), m = c.node(), o = c.node(), p = c.node(), y = c.node();
        c.r(Circuit::source, x, 330e3);   // R37
        c.c(x, o, 220e-12);               // C26
        c.r(Circuit::source, m, 100e3);   // R38
        c.c(m, o, 0.1e-6);                // C34
        c.c(m, p, 0.047e-6);              // C35
        c.r(o, p, 1e6);                   // R50
        c.r(p, Circuit::ground, 15e3);    // R51
        c.c(o, y, 2.2e-9);                // C27
        c.r(y, Circuit::ground, 1e6);     // R35 (stage 2 gate bias)
        s.o = o;
        s.gate = y;
        return s;
    }

    // Stage-2 output -> Tone/Level -> the peak filter's + input. tone: 1 = wiper at C100 end.
    struct Tone { Circuit circuit; int q = 0; };
    Tone toneFromSchematic(double tone, double levelWiper, bool withShelf)
    {
        Tone t;
        auto& c = t.circuit;
        auto b = c.node(), top = c.node(), wiper = c.node(), bottom = c.node(), lw = c.node(), q = c.node();
        if (withShelf)
        {
            c.r(Circuit::source, b, 5.6e3);          // R26
            c.c(Circuit::source, b, 5.6e-9);         // C17
            c.c(b, Circuit::ground, 5.6e-9);         // C19
        }
        else
            c.r(Circuit::source, b, 1.0);            // an ideal driver at node B
        c.c(b, top, 0.018e-6);                       // C100
        c.r(top, wiper, std::max((1.0 - tone) * 10e3, 1.0));   // VR2, 10k linear
        c.r(wiper, bottom, std::max(tone * 10e3, 1.0));
        c.c(bottom, Circuit::ground, 0.018e-6);      // C101
        c.r(wiper, lw, std::max((1.0 - levelWiper) * 100e3, 1.0)); // VR3
        c.r(lw, Circuit::ground, std::max(levelWiper * 100e3, 1.0));
        c.c(lw, q, 0.047e-6);                        // C10
        c.r(q, Circuit::ground, 470e3);              // R13
        t.q = q;
        return t;
    }

    // Peak filter: IC1B non-inverting, R9 || C8 feedback, C9 + gyrator (C16, R10, R21, Q7, R20) in the ground leg.
    // beta = 0 means an ideal follower.
    struct Peak { Circuit circuit; int out = 0; };
    Peak peakFromSchematic(double beta, double emitterAmps)
    {
        Peak p;
        auto& c = p.circuit;
        auto minus = c.node(), out = c.node(), j = c.node(), base = c.node(), emitter = c.node();
        c.r(out, minus, 6.8e3);          // R9
        c.c(out, minus, 2.2e-9);         // C8
        c.opamp(Circuit::source, minus, out);
        c.c(minus, j, 0.056e-6);         // C9
        c.c(j, base, 0.056e-6);          // C16
        c.r(base, Circuit::ground, 470e3);   // R10
        c.r(j, emitter, 1.2e3);          // R21
        c.r(emitter, Circuit::ground, 10e3); // R20
        if (beta > 0.0)
        {
            auto rPi = beta / (emitterAmps / 0.02585);
            c.r(base, emitter, rPi);
            c.gm(Circuit::ground, emitter, base, emitter, beta / rPi);
        }
        else
        {
            // Ideal follower: the emitter IS the base (a tiny resistor between them).
            c.r(base, emitter, 1.0e-3);
            c.gm(Circuit::ground, emitter, base, emitter, 1.0e9);
        }
        p.out = out;
        return p;
    }

    // Closed-loop gain of one discrete stage: A(s)/(1 + A(s)*Zg/(Zg+Zf)), A(s) = A0/(1+s/wp).
    cd stageResponse(double a0, double gbw, double rFixed, double rheostat, double legR, double legC, double feedC, double freq)
    {
        auto s = cd(0.0, 2.0 * M_PI * freq);
        auto a = a0 / (1.0 + s * a0 / (2.0 * M_PI * gbw));
        auto rf = rFixed + rheostat;
        auto zf = rf / (1.0 + s * feedC * rf);
        auto zg = legR + 1.0 / (s * legC);
        return a / (1.0 + a * zg / (zg + zf));
    }

    double rheostat(double gain) { return BluesDriverStage::gainRheostatOhms(comps, gain); }
    cd stage1(double gain, double f, double a0 = 225.0)
    {
        return stageResponse(a0, 4.0e6, 22e3, rheostat(gain), 1.5e3, 0.15e-6, 47e-12, f);
    }
    cd stage2(double gain, double f, double a0 = 225.0)
    {
        return stageResponse(a0, 4.0e6, 33e3, rheostat(gain), 2.2e3, 1.0e-6, 100e-12, f);
    }

    cd highPass(double tau, double f) { auto s = cd(0.0, 2.0 * M_PI * f); return s * tau / (1.0 + s * tau); }

    // The whole small-signal chain, built from the test's own netlists.
    cd chainResponse(double gain, double tone, double level, double f)
    {
        auto stack = stackFromSchematic();
        auto pot = potFraction(level, 0.15);
        auto toneNet = toneFromSchematic(tone, pot, true);
        auto peak = peakFromSchematic(200.0, 0.33e-3);
        return 0.9 * highPass(0.047e-6 * 1e6, f) * highPass(0.1e-6 * 220e3, f)
             * stage1(gain, f) * stack.circuit.solve(f, stack.gate) * stage2(gain, f)
             * toneNet.circuit.solve(f, toneNet.q) * peak.circuit.solve(f, peak.out) * BluesDriverStage::outputGain(comps);
    }

    // ---- Time-domain helpers ----
    double networkGainDb(BluesDriverStage::Network& n, double freq)
    {
        constexpr double rate = sampleRate * 4.0;
        constexpr int len = 65536, settle = 24576;
        n.net.reset();
        std::vector<float> in(len), out(len);
        for (int i = 0; i < len; ++i)
        {
            auto x = 0.001 * std::sin(2.0 * M_PI * freq * i / rate);
            n.net.process(x);
            in[static_cast<size_t>(i)] = static_cast<float>(x);
            out[static_cast<size_t>(i)] = static_cast<float>(n.net.voltage(n.out));
        }
        std::vector<float> a(out.begin() + settle, out.end()), b(in.begin() + settle, in.end());
        return TestUtils::toDb(TestUtils::goertzelMagnitude(a, freq, rate) / TestUtils::goertzelMagnitude(b, freq, rate));
    }

    std::vector<float> sineAt(double freq, double amplitude)
    {
        std::vector<float> s(numSamples);
        for (int n = 0; n < numSamples; ++n)
            s[static_cast<size_t>(n)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * n / sampleRate));
        return s;
    }

    std::vector<float> render(double gain, double tone, double level, const std::vector<float>& in)
    {
        BluesDriverStage stage(sampleRate);
        stage.setGain(static_cast<float>(gain));
        stage.setTone(static_cast<float>(tone));
        stage.setLevel(static_cast<float>(level));
        std::vector<float> out(in.size());
        stage.processBlock(in.data(), out.data(), static_cast<int>(in.size()));
        return std::vector<float>(out.begin() + settleSamples, out.end());
    }

    double measuredDb(double gain, double tone, double level, double freq, double amplitude)
    {
        auto in = sineAt(freq, amplitude);
        auto out = render(gain, tone, level, in);
        std::vector<float> tail(in.begin() + settleSamples, in.end());
        return TestUtils::toDb(TestUtils::goertzelMagnitude(out, freq, sampleRate) / TestUtils::goertzelMagnitude(tail, freq, sampleRate));
    }

    std::vector<float> hann(std::vector<float> v)
    {
        for (size_t n = 0; n < v.size(); ++n)
            v[n] *= static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(n) / static_cast<double>(v.size())));
        return v;
    }

    double thd(const std::vector<float>& out, double freq)
    {
        auto w = hann(out);
        auto fund = TestUtils::goertzelMagnitude(w, freq, sampleRate);
        double sum = 0.0;
        for (int h = 2; h <= 9; ++h)
        {
            auto m = TestUtils::goertzelMagnitude(w, freq * h, sampleRate) / fund;
            sum += m * m;
        }
        return std::sqrt(sum);
    }

    double rmsOf(const std::vector<float>& v)
    {
        double s = 0.0;
        for (auto x : v) s += static_cast<double>(x) * x;
        return std::sqrt(s / static_cast<double>(v.size()));
    }
}

int main()
{
    // ---------------------------------------------------------------- 1
    std::printf("1. The fixed passive network between the stages\n");
    {
        auto stack = stackFromSchematic();
        struct Ref { double f, db; };
        // Node O (before stage 2's coupling capacitor), from the analysis' nodal solve.
        for (auto r : { Ref { 20, -2.4 }, Ref { 50, -6.2 }, Ref { 100, -10.4 }, Ref { 200, -14.3 }, Ref { 500, -16.8 },
                        Ref { 1000, -17.1 }, Ref { 3000, -16.3 }, Ref { 10000, -15.9 } })
        {
            auto got = db(stack.circuit.solve(r.f, stack.o));
            char label[80];
            std::snprintf(label, sizeof(label), "stack loss at %5.0f Hz vs %.1f dB", r.f, r.db);
            check(std::abs(got - r.db) < 0.3, label, got, " dB");
        }

        // The 1SS133 stacks (two diodes in series each way, ~1.1V) hang on node O. What reaches
        // them is stage 1's output through the stack. (a) In normal use - a 0.3V guitar at Gain
        // noon - it stays below their threshold at every frequency. (b) At max Gain a hot low
        // note can reach them, but then stage 2 (whose rail-limited output saturates for an input
        // of only rail/gain, ~0.03V) is already far beyond needing them: a clamp at 1.1V on an
        // input that saturates it at 0.03V changes nothing.
        auto worstAtNode = [&](double guitarVolts, double gain) {
            double worstVolts = 0.0;
            for (double f = 20.0; f < 20000.0; f *= 1.05)
                worstVolts = std::max(worstVolts, std::min(3.8, guitarVolts * 0.9 * std::abs(stage1(gain, f))) * std::abs(stack.circuit.solve(f, stack.o)));
            return worstVolts;
        };
        check(worstAtNode(0.3, 0.5) < 1.1, "0.3 V guitar at Gain noon: < 1.1 V on the diode stacks at every frequency (they stay off)", worstAtNode(0.3, 0.5), " V");
        auto stage2Saturates = 3.8 / std::abs(stage2(1.0, 1000.0));
        check(1.1 / stage2Saturates > 20.0, "where they can conduct (1 V, max Gain) stage 2 is already > 20x (26 dB) past its own saturation", 1.1 / stage2Saturates, "x");

        auto module = BluesDriverStage::makeStackNetwork(comps);
        module.net.prepare(sampleRate * 4.0);
        double worst = 0.0;
        for (double f : { 20.0, 72.0, 200.0, 1000.0, 5000.0, 12000.0 })
            worst = std::max(worst, std::abs(networkGainDb(module, f) - db(stack.circuit.solve(f, stack.gate))));
        check(worst < 0.1, "module's network matches the independently solved netlist (stage 2 gate)", worst, " dB max error");
    }

    // ---------------------------------------------------------------- 2
    std::printf("2. Tone and Level\n");
    {
        // The published curves: shelf + Tone + Level into the peak filter's + input, Level max. (The
        // shelf's 5.6k is a source impedance for the Tone network, so it must be included: leaving
        // it out reads ~1.8 dB high at 1 kHz.)
        struct Ref { double tone, f, db; };
        for (auto r : { Ref { 0.5, 1000, -8.5 }, Ref { 0.5, 5000, -11.7 }, Ref { 0.5, 20000, -12.3 }, Ref { 0.0, 1000, -9.6 }, Ref { 0.0, 10000, -27.0 },
                        Ref { 0.0, 20000, -33.1 }, Ref { 1.0, 200, -6.7 }, Ref { 1.0, 1000, -6.2 }, Ref { 1.0, 10000, -6.0 }, Ref { 1.0, 20000, -6.0 } })
        {
            auto t = toneFromSchematic(r.tone, 1.0, true);
            auto got = db(t.circuit.solve(r.f, t.q));
            char label[96];
            std::snprintf(label, sizeof(label), "Tone %.1f @ %5.0f Hz vs %.1f dB", r.tone, r.f, r.db);
            check(std::abs(got - r.db) < 0.3, label, got, " dB");
        }
        auto ccw = toneFromSchematic(0.0, 1.0, true), cw = toneFromSchematic(1.0, 1.0, true);
        auto darker = db(cw.circuit.solve(10000.0, cw.q)) - db(ccw.circuit.solve(10000.0, ccw.q));
        check(darker > 15.0, "Tone works as a treble cut: fully clockwise is > 15 dB brighter than fully counter-clockwise at 10 kHz", darker, " dB");

        // The fixed shelf on its own, unloaded: (1 + s R C17)/(1 + s R (C17 + C19)), -0.5/-2.5/-5.3 dB at 1/3/10kHz.
        Circuit shelf;
        auto b = shelf.node();
        shelf.r(Circuit::source, b, 5.6e3);
        shelf.c(Circuit::source, b, 5.6e-9);
        shelf.c(b, Circuit::ground, 5.6e-9);
        check(std::abs(db(shelf.solve(1000.0, b)) - (-0.5)) < 0.15 && std::abs(db(shelf.solve(3000.0, b)) - (-2.5)) < 0.2 && std::abs(db(shelf.solve(10000.0, b)) - (-5.3)) < 0.2,
              "fixed shelf: -0.5 / -2.5 / -5.3 dB at 1 / 3 / 10 kHz", db(shelf.solve(3000.0, b)), " dB at 3k");

        double worst = 0.0;
        for (double tone : { 0.0, 0.3, 0.5, 1.0 })
            for (double level : { 0.3, 0.7, 1.0 })
            {
                auto module = BluesDriverStage::makeToneNetwork(comps);
                module.net.setResistance(module.toneUpper, BluesDriverStage::toneUpperOhms(comps, tone));
                module.net.setResistance(module.toneLower, BluesDriverStage::toneLowerOhms(comps, tone));
                module.net.setResistance(module.levelUpper, BluesDriverStage::levelUpperOhms(comps, level));
                module.net.setResistance(module.levelLower, BluesDriverStage::levelLowerOhms(comps, level));
                module.net.prepare(sampleRate * 4.0);
                auto ref = toneFromSchematic(tone, potFraction(level, 0.15), true);
                for (double f : { 100.0, 1000.0, 5000.0, 12000.0 })
                    worst = std::max(worst, std::abs(networkGainDb(module, f) - db(ref.circuit.solve(f, ref.q))));
            }
        check(worst < 0.15, "module's network matches the independently solved netlist (12 knob settings x 4 frequencies; the bilinear warp is worth ~0.1 dB at 12 kHz)", worst, " dB max error");
    }

    // ---------------------------------------------------------------- 3
    std::printf("3. The bass peak filter (gyrator)\n");
    {
        auto real = peakFromSchematic(200.0, 0.33e-3);
        auto ideal = peakFromSchematic(0.0, 0.0);

        double bestF = 0.0, best = -1e9, idealBest = -1e9;
        for (double f = 40.0; f < 600.0; f *= 1.01)
        {
            auto d = db(real.circuit.solve(f, real.out));
            if (d > best) { best = d; bestF = f; }
            idealBest = std::max(idealBest, db(ideal.circuit.solve(f, ideal.out)));
        }
        // Exact closed form with an ideal follower: Zgyr = R21 (1 + s C16 R10)/(1 + s C16 R21) in series with C9,
        // resonating with the simulated 31.6 H at 1/(2 pi sqrt(L C9)) = 119.7 Hz.
        double closedBest = -1e9;
        for (double f = 40.0; f < 600.0; f *= 1.01)
        {
            auto s = cd(0.0, 2.0 * M_PI * f);
            auto zgyr = 1.2e3 * (1.0 + s * 0.056e-6 * 470e3) / (1.0 + s * 0.056e-6 * 1.2e3);
            auto zg = zgyr + 1.0 / (s * 0.056e-6);
            auto zf = 6.8e3 / (1.0 + s * 6.8e3 * 2.2e-9);
            closedBest = std::max(closedBest, db(1.0 + zf / zg));
        }
        check(std::abs(idealBest - closedBest) < 0.1, "with an ideal follower the netlist reproduces the gyrator's closed form (+11.7 dB)", idealBest, " dB");
        check(best > 5.0 && best < 9.0, "with the follower's real impedance the peak is +6..8 dB (published 6 @ 120Hz, 8 @ 150Hz)", best, " dB");
        check(bestF > 100.0 && bestF < 170.0, "...centred near 120-150 Hz", bestF, " Hz");
        check(std::abs(db(real.circuit.solve(1000.0, real.out)) - 0.5) < 1.0 && std::abs(db(real.circuit.solve(5000.0, real.out)) - 0.5) < 1.5,
              "above ~500 Hz it is flat at about +0.5 dB", db(real.circuit.solve(1000.0, real.out)), " dB");

        auto module = BluesDriverStage::makePeakNetwork(comps);
        module.net.prepare(sampleRate * 4.0);
        double worst = 0.0;
        for (double f : { 30.0, 80.0, 130.0, 300.0, 1000.0, 6000.0 })
            worst = std::max(worst, std::abs(networkGainDb(module, f) - db(real.circuit.solve(f, real.out))));
        check(worst < 0.1, "module's network matches the independently solved netlist", worst, " dB max error");
    }

    // ---------------------------------------------------------------- 4
    std::printf("4. The two discrete amplifier stages: gain law and finite open-loop gain\n");
    {
        // Ideal (A -> infinity) closed-loop plateaus: 1 + (22k + Rg)/1.5k and 1 + (33k + Rg)/2.2k.
        auto ideal1Min = db(stage1(0.0, 20000.0, 1e9));
        check(std::abs(ideal1Min - 23.9) < 0.6, "stage 1 at Gain min: 15.7x = 23.9 dB (ideal)", ideal1Min, " dB");
        auto ideal1MaxPlateau = 20.0 * std::log10(1.0 + (22e3 + 250e3) / 1.5e3);
        check(std::abs(ideal1MaxPlateau - 45.2) < 0.1, "stage 1 at Gain max: 182x = 45.2 dB (ideal)", ideal1MaxPlateau, " dB");
        auto ideal2MaxPlateau = 20.0 * std::log10(1.0 + (33e3 + 250e3) / 2.2e3);
        check(std::abs(ideal2MaxPlateau - 42.2) < 0.4, "stage 2 at Gain max: ~130x = 42.3 dB (ideal)", ideal2MaxPlateau, " dB");

        // Below the 707Hz corner the gain falls at 6 dB/octave (the leg's capacitor - not a unity plateau:
        // at max gain |Zg| only equals the 272k feedback below ~4 Hz) and is on its plateau above ~3 kHz.
        auto octave = db(stage1(1.0, 200.0)) - db(stage1(1.0, 100.0));
        check(octave > 5.0 && octave < 6.5, "stage 1 gives up 6 dB/octave below its 707 Hz corner (100 -> 200 Hz)", octave, " dB");
        auto plateau = db(stage1(1.0, 3000.0)) - db(stage1(1.0, 6000.0));
        check(std::abs(plateau) < 1.5, "and is flat (within 1.5 dB) from 3 to 6 kHz", plateau, " dB");

        // The published measurement: "a little over 40 dB" at max Gain, peaking at 2-3 kHz.
        double peak = -1e9, peakF = 0.0;
        for (double f = 500.0; f < 8000.0; f *= 1.02)
            if (db(stage1(1.0, f)) > peak) { peak = db(stage1(1.0, f)); peakF = f; }
        check(peak > 39.0 && peak < 41.5, "stage 1 at max Gain peaks a little over 40 dB (ideal would be 45)", peak, " dB");
        check(peakF > 1800.0 && peakF < 4000.0, "...between 2 and 3 kHz", peakF, " Hz");
    }

    // ---------------------------------------------------------------- 5
    std::printf("5. The whole path at tiny level: measured vs the cascade of the netlists\n");
    {
        struct Setting { double gain, tone, level; };
        double worst = 0.0;
        for (auto s : { Setting { 0.0, 0.5, 1.0 }, Setting { 0.5, 0.3, 0.8 }, Setting { 1.0, 0.8, 1.0 }, Setting { 0.3, 1.0, 0.6 } })
            for (double f : { 60.0, 130.0, 500.0, 1000.0, 2500.0, 4000.0 })
            {
                auto amp = 1.0e-6;
                auto err = std::abs(measuredDb(s.gain, s.tone, s.level, f, amp) - db(chainResponse(s.gain, s.tone, s.level, f)));
                worst = std::max(worst, err);
            }
        check(worst < 0.25, "measured gain within 0.25 dB of the analytic chain (4 settings x 6 frequencies)", worst, " dB max error");

        // End to end at 1 kHz, Tone noon, Level max: published max ~50-52 dB; the ideal circuit would be ~62.
        auto atMin = measuredDb(0.0, 0.5, 1.0, 1000.0, 1.0e-6);
        auto atMax = measuredDb(1.0, 0.5, 1.0, 1000.0, 1.0e-6);
        check(atMin > 19.0 && atMin < 25.0, "Gain min, 1 kHz: about +20..24 dB", atMin, " dB");
        check(atMax > 49.0 && atMax < 54.0, "Gain max, 1 kHz: 50-52 dB as published (calibrated - see the header)", atMax, " dB");
        check(atMax - atMin > 24.0, "the Gain knob spans over 24 dB", atMax - atMin, " dB");
    }

    // ---------------------------------------------------------------- 6
    std::printf("6. Overload: the output stops at the rails\n");
    {
        BluesDriverStage::DiscreteStage amp;
        amp.prepare(sampleRate * 4.0, comps, 1.5e3, 0.15e-6, 47e-12);
        amp.setFeedbackResistance(22e3 + 250e3);
        double maxOut = -1e9, minOut = 1e9;
        for (int i = 0; i < 400000; ++i)
        {
            auto out = amp.process(0.5 * std::sin(2.0 * M_PI * 1500.0 * i / (sampleRate * 4.0)));
            if (i > 100000) { maxOut = std::max(maxOut, out); minOut = std::min(minOut, out); }
        }
        check(maxOut > 3.2 && maxOut < 3.9, "hard-driven, the top stops at PNP saturation (+3.8 V rail)", maxOut, " V");
        check(minOut < -3.4 && minOut > -4.1, "and the bottom at cutoff (-4.0 V)", minOut, " V");
        auto offset = (maxOut + minOut) / (0.5 * (maxOut - minOut));
        check(std::abs(offset) < 0.15, "nominally symmetric about Vref (within 15%)", offset * 100.0, " % offset");

        // The soft limit is monotone, linear near zero, and never passes its rail.
        bool monotone = true;
        double prev = -1e9;
        for (double x = -20.0; x <= 20.0; x += 0.01)
        {
            auto y = BluesDriverStage::DiscreteStage::limit(x, 3.8, 4.0, 4.0, 3.0);
            monotone = monotone && y >= prev;
            prev = y;
        }
        check(monotone, "the rail limit is monotonic", 0.0);
        auto small = BluesDriverStage::DiscreteStage::limit(0.01, 3.8, 4.0, 4.0, 3.0);
        check(std::abs(small - 0.01) < 1e-6, "and linear near zero", small);
        auto hi = BluesDriverStage::DiscreteStage::limit(1000.0, 3.8, 4.0, 4.0, 3.0), lo = BluesDriverStage::DiscreteStage::limit(-1000.0, 3.8, 4.0, 4.0, 3.0);
        check(hi < 3.8 && lo > -4.0, "and never passes the rails", hi, " V");
    }

    // ---------------------------------------------------------------- 7
    std::printf("7. Whole pedal: clean when quiet, compressing when driven\n");
    {
        auto quiet = thd(render(0.4, 0.5, 1.0, sineAt(1000.0, 0.002)), 1000.0);
        auto loud = thd(render(0.4, 0.5, 1.0, sineAt(1000.0, 0.3)), 1000.0);
        check(quiet < 0.01, "Gain 0.4, 2 mV guitar: under 1% THD", quiet * 100.0, " %");
        check(loud > 0.05 && loud > quiet * 20.0, "Gain 0.4, 300 mV guitar: over 5% THD and 20x the clean case", loud * 100.0, " %");

        auto rise = TestUtils::toDb(rmsOf(render(0.6, 0.5, 1.0, sineAt(1000.0, 0.3))) / rmsOf(render(0.6, 0.5, 1.0, sineAt(1000.0, 0.03))));
        check(rise < 10.0, "Gain 0.6: +20 dB in gives under +10 dB out once overloaded", rise, " dB");

        auto lvlMax = rmsOf(render(0.6, 0.5, 1.0, sineAt(1000.0, 0.1)));
        auto lvlMid = rmsOf(render(0.6, 0.5, 0.5, sineAt(1000.0, 0.1)));
        auto lvlMin = rmsOf(render(0.6, 0.5, 0.0, sineAt(1000.0, 0.1)));
        check(lvlMax > lvlMid * 2.0 && lvlMin < lvlMax * 0.001, "Level: max is well above mid (audio taper); zero is silent", TestUtils::toDb(lvlMin / lvlMax), " dB at zero");

        double peak = 0.0;
        for (auto x : render(1.0, 1.0, 1.0, sineAt(1000.0, 0.5))) peak = std::max(peak, static_cast<double>(std::abs(x)));
        check(peak > 0.3 && peak < 3.0, "hard-driven at full Tone/Level: output peak stays within a couple of volts", peak, " V");
    }

    // ---------------------------------------------------------------- 8
    std::printf("8. Robustness\n");
    {
        bool finite = true;
        double peak = 0.0;
        unsigned seed = 4242u;
        std::vector<float> noise(numSamples);
        for (auto& s : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            s = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * 5.0f;
        }
        for (double gain : { 0.0, 1.0 })
            for (double tone : { 0.0, 1.0 })
            {
                auto out = render(gain, tone, 1.0, noise);
                for (auto x : out) { finite = finite && std::isfinite(x); peak = std::max(peak, static_cast<double>(std::abs(x))); }
            }
        check(finite, "extreme input at extreme settings stays finite", peak, " V peak");
        check(peak < 10.0, "and bounded", peak, " V peak");
    }

    // ---------------------------------------------------------------- 9
    std::printf("9. Turning a knob does not click\n");
    {
        // A knob is read once per audio block; snapping to its new value at the block boundary steps
        // the circuit there - a click once per block while a knob is moving. Each knob is jumped
        // mid-signal at a block boundary; the worst second difference of the output at the change must
        // not exceed the signal's own worst elsewhere.
        struct Move { const char* name; char knob; float from, to; };
        for (auto m : { Move { "Gain 0.40 -> 0.55", 'G', 0.40f, 0.55f }, Move { "Tone 0.30 -> 0.80", 'T', 0.30f, 0.80f }, Move { "Level 0.50 -> 0.60", 'L', 0.50f, 0.60f } })
        {
            const int block = 512, changeAt = block * 40, total = 48000;
            BluesDriverStage stage(sampleRate);
            stage.setGain(m.knob == 'G' ? m.from : 0.4f);
            stage.setTone(m.knob == 'T' ? m.from : 0.5f);
            stage.setLevel(m.knob == 'L' ? m.from : 0.6f);
            stage.reset();

            std::vector<float> in(total), out(total);
            for (int i = 0; i < total; ++i)
                in[static_cast<size_t>(i)] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * 196.0 * i / sampleRate));
            for (int start = 0; start < total; start += block)
            {
                if (start == changeAt)
                {
                    if (m.knob == 'G') stage.setGain(m.to);
                    if (m.knob == 'T') stage.setTone(m.to);
                    if (m.knob == 'L') stage.setLevel(m.to);
                }
                stage.processBlock(in.data() + start, out.data() + start, std::min(block, total - start));
            }

            double natural = 0.0, atChange = 0.0;
            for (int i = 12000; i < total - 2; ++i)
            {
                auto d2 = std::abs(static_cast<double>(out[static_cast<size_t>(i) + 1]) - 2.0 * out[static_cast<size_t>(i)] + out[static_cast<size_t>(i) - 1]);
                if (std::abs(i - changeAt) <= 40) atChange = std::max(atChange, d2); else natural = std::max(natural, d2);
            }
            char label[100];
            std::snprintf(label, sizeof(label), "%s: worst discontinuity at the change is x%.2f the signal's own", m.name, atChange / natural);
            check(atChange < natural * 1.5, label, atChange / natural, "x");
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
