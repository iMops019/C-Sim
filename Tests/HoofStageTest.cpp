// Verify the EarthQuaker Devices Hoof model against the circuit it is built from
// (Kit Rae's trace of a Hoof, cross-checked with PedalPCB's Ungula BOM).
//
// Layers, so a failure points at the right place:
//   1. DC BIAS. Nothing publishes it; the module solves it from the netlist and the
//      device models. It must land where the published analysis estimated it (collector
//      voltages and currents), the germanium junctions must sit at ~0.25V and the silicon
//      at ~0.65V, and Kirchhoff's current law must hold at the collectors when checked
//      by hand from the reported voltages.
//   2. THE WHOLE CIRCUIT, small signal. The test transcribes the netlist itself, replaces
//      each transistor with its hybrid-pi model at the module's own operating point, and
//      solves it with a complex-frequency solver that shares nothing with the module's
//      time-domain one. The module's measured response must match at every frequency and
//      knob setting - this checks the coupling between the stages, the pots, the DC
//      capture and the Newton machinery all at once.
//   3. THE TONE STACK against the response table the analysis published (36 values).
//   4. THE NONLINEAR PART. The LEDs are what limit the clipping stages (their voltage must
//      reach a red LED's ~1.8V and no more, and only when driven), the fuzz compresses
//      hard, the Newton solve converges every sample, and silence stays silent.
//
// Gains are measured as output/input through the same Goertzel window (see GraphicEQTest).

#include "../Source/dsp/HoofStage.h"
#include "../Source/dsp/PotTaper.h"
#include "ComplexCircuit.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    using TestCircuit::Circuit;
    using cd = std::complex<double>;

    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 24576;
    constexpr int settleSamples = 10240;

    int failures = 0;

    void check(bool ok, const char* what, double got, const char* unit = "")
    {
        std::printf("  [%s] %s (%.4g%s)\n", ok ? "PASS" : "FAIL", what, got, unit);
        if (! ok)
            ++failures;
    }

    double db(cd v) { return TestUtils::toDb(std::abs(v)); }

    // ---- The schematic, transcribed for the test, with each transistor as its
    //      small-signal model at the operating point `op`. ----
    struct Small { Circuit circuit; int out = 0; };

    Small hoofSmallSignal(double fuzz, double tone, double shift, double level, const HoofStage::OperatingPoint& op)
    {
        Small s;
        auto& c = s.circuit;
        auto gnd = Circuit::ground;

        auto bjt = [&](int collector, int base, int emitter, double ic, double beta) {
            auto gm = ic / 0.02585;
            auto rPi = beta / gm;
            c.r(base, emitter, rPi);
            c.gm(collector, emitter, base, emitter, gm);
        };

        // Stage A (2N3904).
        auto x1 = c.node(), ba = c.node(), ca = c.node(), ea = c.node();
        c.r(Circuit::source, x1, 39e3);
        c.c(x1, ba, 100e-9);
        c.r(ba, gnd, 100e3);
        c.r(ca, ba, 470e3);
        c.c(ca, ba, 470e-12);
        c.r(ca, gnd, 15e3);                 // the 15k to +9V is a ground at AC
        c.r(ea, gnd, 100.0);
        bjt(ca, ba, ea, op.collectorAmps[0], 250.0);

        // Sustain: 100nF, 50k pot with 2.2k to ground under it, 100nF, 8.2k.
        auto p = c.node(), wiper = c.node(), bottom = c.node(), wc = c.node(), bb = c.node();
        c.c(ca, p, 100e-9);
        c.r(p, wiper, std::max((1.0 - fuzz) * 50e3, 1.0));
        c.r(wiper, bottom, std::max(fuzz * 50e3, 1.0));
        c.r(bottom, gnd, 2.2e3);
        c.c(wiper, wc, 100e-9);
        c.r(wc, bb, 8.2e3);

        // Stage B (germanium).
        auto cb = c.node(), eb = c.node(), lb = c.node(), xb = c.node(), bc = c.node();
        c.r(bb, gnd, 100e3);
        c.r(cb, bb, 470e3);
        c.c(cb, bb, 470e-12);
        c.r(cb, gnd, 15e3);
        c.r(eb, gnd, 100.0);
        bjt(cb, bb, eb, op.collectorAmps[1], 55.0);
        c.c(cb, lb, 100e-9);
        c.r(lb, gnd, 1.0e12);               // the LED pair is off: an open, tied down so the node is defined
        c.r(lb, bb, 1.0e12);
        c.c(cb, xb, 100e-9);
        c.r(xb, bc, 8.2e3);

        // Stage C (germanium).
        auto cc = c.node(), ec = c.node(), lc = c.node();
        c.r(bc, gnd, 100e3);
        c.r(cc, bc, 470e3);
        c.c(cc, bc, 470e-12);
        c.r(cc, gnd, 15e3);
        c.r(ec, gnd, 100.0);
        bjt(cc, bc, ec, op.collectorAmps[2], 55.0);
        c.c(cc, lc, 100e-9);
        c.r(lc, gnd, 1.0e12);
        c.r(lc, bc, 1.0e12);

        // Tone stack.
        auto tl = c.node(), th = c.node(), tw = c.node(), bd = c.node();
        c.r(cc, tl, 39e3);
        c.c(tl, gnd, 6.8e-9);
        c.c(cc, th, 6.8e-9);
        c.r(th, gnd, 2.2e3 + shift * 25e3);
        c.r(tl, tw, std::max(tone * 100e3, 1.0));
        c.r(tw, th, std::max((1.0 - tone) * 100e3, 1.0));
        c.c(tw, bd, 100e-9);

        // Stage D and Volume.
        auto cd2 = c.node(), ed = c.node(), vt = c.node(), vw = c.node();
        c.r(bd, gnd, 1.0 / (1.0 / 390e3 + 1.0 / 100e3));    // 390k to +9V || 100k to ground
        c.r(cd2, gnd, 10e3);
        c.r(ed, gnd, 2.2e3);
        bjt(cd2, bd, ed, op.collectorAmps[3], 250.0);
        c.c(cd2, vt, 100e-9);
        auto v = potFraction(level, 0.15);
        c.r(vt, vw, std::max((1.0 - v) * 1e6, 1.0));
        c.r(vw, gnd, std::max(v * 1e6, 1.0));
        c.r(vw, gnd, 1e6);                                    // the load
        s.out = vw;
        return s;
    }

    // ---- Time-domain helpers ----
    std::vector<float> sineAt(double freq, double amplitude)
    {
        std::vector<float> s(numSamples);
        for (int n = 0; n < numSamples; ++n)
            s[static_cast<size_t>(n)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * n / sampleRate));
        return s;
    }

    struct Settings { double fuzz, tone, shift, level; };

    std::vector<float> render(const Settings& s, const std::vector<float>& in, HoofStage::SolverStats* stats = nullptr)
    {
        HoofStage stage(sampleRate);
        stage.setFuzz(static_cast<float>(s.fuzz));
        stage.setTone(static_cast<float>(s.tone));
        stage.setShift(static_cast<float>(s.shift));
        stage.setLevel(static_cast<float>(s.level));
        // Start from this setting's own DC steady state: the tone stack hangs on stage C's
        // collector, so Tone and Shift move the bias slightly (a real pedal relaxes to the new
        // one over the coupling capacitors' time constants when a knob turns).
        stage.reset();
        stage.resetSolverStats();
        std::vector<float> out(in.size());
        stage.processBlock(in.data(), out.data(), static_cast<int>(in.size()));
        if (stats)
            *stats = stage.solverStats();
        return std::vector<float>(out.begin() + settleSamples, out.end());
    }

    HoofStage::OperatingPoint operatingPointFor(const Settings& s)
    {
        HoofStage stage(sampleRate);
        stage.setFuzz(static_cast<float>(s.fuzz));
        stage.setTone(static_cast<float>(s.tone));
        stage.setShift(static_cast<float>(s.shift));
        stage.setLevel(static_cast<float>(s.level));
        stage.reset();
        return stage.operatingPoint();
    }

    double measuredDb(const Settings& s, double freq, double amplitude)
    {
        auto in = sineAt(freq, amplitude);
        auto out = render(s, in);
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

    double peakOf(const std::vector<float>& v)
    {
        double p = 0.0;
        for (auto x : v) p = std::max(p, static_cast<double>(std::abs(x)));
        return p;
    }
}

int main()
{
    HoofStage reference(sampleRate);
    auto op = reference.operatingPoint();

    // ---------------------------------------------------------------- 1
    std::printf("1. DC bias, solved from the netlist\n");
    {
        const char* names[4] = { "A (2N3904)", "B (germanium)", "C (germanium)", "D (2N3904 recovery)" };
        for (size_t i = 0; i < 4; ++i)
            std::printf("     %-20s Vb %.3f  Vc %.3f  Ve %.3f  Ic %.3f mA\n", names[i], op.baseVolts[i], op.collectorVolts[i], op.emitterVolts[i], op.collectorAmps[i] * 1e3);

        // The published analysis' estimates (its assumed beta and Vbe span a range, so a little slack).
        check(op.collectorVolts[0] > 4.2 && op.collectorVolts[0] < 5.1, "input stage collector 4.4-5.0 V", op.collectorVolts[0], " V");
        check(op.collectorAmps[0] > 0.22e-3 && op.collectorAmps[0] < 0.34e-3, "input stage collector current 0.25-0.3 mA", op.collectorAmps[0] * 1e3, " mA");
        check(op.collectorVolts[1] > 3.2 && op.collectorVolts[1] < 5.4 && op.collectorVolts[2] > 3.2 && op.collectorVolts[2] < 5.4, "germanium collectors 3.2-5.4 V", op.collectorVolts[1], " V");
        check(op.collectorAmps[1] > 0.23e-3 && op.collectorAmps[1] < 0.38e-3 && op.collectorAmps[2] > 0.23e-3 && op.collectorAmps[2] < 0.38e-3, "germanium collector currents 0.23-0.38 mA", op.collectorAmps[1] * 1e3, " mA");
        check(op.collectorVolts[3] > 4.2 && op.collectorVolts[3] < 5.1, "recovery stage collector 4.2-5.1 V", op.collectorVolts[3], " V");
        check(op.emitterVolts[3] > 0.9 && op.emitterVolts[3] < 1.1, "recovery stage emitter 0.9-1.06 V", op.emitterVolts[3], " V");
        check(op.collectorAmps[3] > 0.4e-3 && op.collectorAmps[3] < 0.52e-3, "recovery stage collector current 0.4-0.5 mA", op.collectorAmps[3] * 1e3, " mA");

        // Germanium turns on at a quarter of a volt, silicon at two thirds.
        auto vbe = [&](size_t i) { return op.baseVolts[i] - op.emitterVolts[i]; };
        check(vbe(0) > 0.60 && vbe(0) < 0.70 && vbe(3) > 0.60 && vbe(3) < 0.70, "silicon base-emitter voltages 0.60-0.70 V", vbe(0), " V");
        check(vbe(1) > 0.20 && vbe(1) < 0.32 && vbe(2) > 0.20 && vbe(2) < 0.32, "germanium base-emitter voltages 0.20-0.32 V", vbe(1), " V");

        // Kirchhoff's current law at the collectors, checked by hand from the reported voltages:
        // what the 15k (or 10k) supplies, less what the 470k feedback resistor takes, is the collector current.
        auto kclA = (9.0 - op.collectorVolts[0]) / 15e3 - (op.collectorVolts[0] - op.baseVolts[0]) / 470e3;
        auto kclB = (9.0 - op.collectorVolts[1]) / 15e3 - (op.collectorVolts[1] - op.baseVolts[1]) / 470e3;
        auto kclD = (9.0 - op.collectorVolts[3]) / 10e3;
        auto worstKcl = std::max({ std::abs(kclA - op.collectorAmps[0]), std::abs(kclB - op.collectorAmps[1]), std::abs(kclD - op.collectorAmps[3]) });
        check(worstKcl < 2.0e-6, "KCL at the collectors of A, B and D holds to 2 uA", worstKcl * 1e6, " uA");

        // ...and at the recovery stage's emitter: Ve/2.2k = Ic + Ib.
        auto emitterCurrent = op.emitterVolts[3] / 2.2e3;
        check(std::abs(emitterCurrent - op.collectorAmps[3] * (1.0 + 1.0 / 250.0)) < 2.0e-6, "recovery emitter current = Ic (1 + 1/beta)", emitterCurrent * 1e3, " mA");
    }

    // ---------------------------------------------------------------- 2
    std::printf("2. The whole circuit at small signal: module vs an independently solved netlist\n");
    {
        double worst = 0.0;
        double worstAt = 0.0;
        for (auto s : { Settings { 1.0, 0.5, 0.4, 1.0 }, Settings { 0.6, 0.2, 0.0, 0.7 }, Settings { 0.9, 0.9, 1.0, 0.8 } })
        {
            auto ref = hoofSmallSignal(s.fuzz, s.tone, s.shift, s.level, operatingPointFor(s));
            for (double f : { 100.0, 250.0, 600.0, 1000.0, 2000.0, 4000.0 })
            {
                auto measured = measuredDb(s, f, 2.0e-5), expected = db(ref.circuit.solve(f, ref.out));
                auto err = std::abs(measured - expected);
                if (err > 0.15)
                    std::printf("         fuzz %.1f tone %.1f shift %.1f level %.1f @ %4.0f Hz: module %.3f dB, netlist %.3f dB\n", s.fuzz, s.tone, s.shift, s.level, f, measured, expected);
                if (err > worst) { worst = err; worstAt = f; }
            }
        }
        check(worst < 0.15, "measured gain within 0.15 dB of the linearized netlist (3 settings x 6 frequencies)", worst, " dB max error");
        std::printf("         (worst at %.0f Hz)\n", worstAt);

        Settings mid { 1.0, 0.5, 0.4, 1.0 };
        auto gainMax = measuredDb(mid, 1000.0, 2.0e-5);
        check(gainMax > 40.0 && gainMax < 65.0, "the small-signal gain at 1 kHz with Sustain up is 40-65 dB (a fuzz: a lot)", gainMax, " dB");

        auto sustainMin = measuredDb({ 0.0, 0.5, 0.4, 1.0 }, 1000.0, 2.0e-5);
        check(gainMax - sustainMin > 20.0 && gainMax - sustainMin < 40.0, "Sustain range: 20-40 dB (the pot's -27.5 dB plus the loading it removes)", gainMax - sustainMin, " dB");
    }

    // ---------------------------------------------------------------- 3
    std::printf("3. The tone stack against the published response table\n");
    {
        // The analysis assumed a 14.5k source (15k || 470k) and a 79.6k load (390k || 100k).
        auto tonePoint = [](double shuntOhms, double tone, double f) {
            Circuit c;
            auto gnd = Circuit::ground;
            auto a = c.node(), l = c.node(), h = c.node(), w = c.node();
            c.r(Circuit::source, a, 14.5e3);
            c.r(a, l, 39e3);
            c.c(l, gnd, 6.8e-9);
            c.c(a, h, 6.8e-9);
            c.r(h, gnd, shuntOhms);
            c.r(l, w, std::max(tone * 100e3, 1.0));
            c.r(w, h, std::max((1.0 - tone) * 100e3, 1.0));
            c.r(w, gnd, 79.6e3);
            return db(c.solve(f, w));
        };

        struct Row { double shunt, tone; double db[4]; };
        const double freqs[4] = { 100.0, 1000.0, 5000.0, 10000.0 };
        double worst = 0.0;
        for (auto row : { Row { 2.2e3, 0.0, { -7.0, -11.4, -29.2, -37.9 } },   Row { 2.2e3, 0.5, { -12.9, -19.8, -29.2, -27.3 } },  Row { 2.2e3, 1.0, { -36.7, -25.2, -19.0, -18.6 } },
                          Row { 14.7e3, 0.0, { -6.8, -11.1, -23.6, -29.6 } },  Row { 14.7e3, 0.5, { -12.0, -17.0, -16.7, -16.6 } }, Row { 14.7e3, 1.0, { -22.0, -12.0, -8.8, -8.6 } },
                          Row { 27.2e3, 0.0, { -6.6, -10.3, -21.9, -27.9 } },  Row { 27.2e3, 0.5, { -11.4, -14.0, -14.8, -14.8 } }, Row { 27.2e3, 1.0, { -18.1, -9.3, -7.1, -7.0 } } })
            for (int i = 0; i < 4; ++i)
                worst = std::max(worst, std::abs(tonePoint(row.shunt, row.tone, freqs[i]) - row.db[i]));
        check(worst < 0.6, "all 36 published table values (3 Shift x 3 Tone x 4 frequencies) within 0.6 dB", worst, " dB max error");

        // The mid scoop: at Tone centre and Shift's middle, a notch near 1.5 kHz about 5.6 dB below the bass shelf.
        double notchF = 0.0, notchDb = 1e9;
        for (double f = 300.0; f < 6000.0; f *= 1.01)
        {
            auto d = tonePoint(14.7e3, 0.5, f);
            if (d < notchDb) { notchDb = d; notchF = f; }
        }
        check(notchF > 1100.0 && notchF < 2000.0, "the mid scoop sits near 1.5 kHz at Shift's middle", notchF, " Hz");
        auto shelf = tonePoint(14.7e3, 0.5, 100.0);
        check(shelf - notchDb > 4.0 && shelf - notchDb < 8.0, "...about 5.6 dB below the bass shelf", shelf - notchDb, " dB");
    }

    // ---------------------------------------------------------------- 4
    std::printf("4. The clipping: LEDs, compression, convergence\n");
    {
        Settings full { 1.0, 0.5, 0.4, 1.0 };

        // A tiny signal leaves the LEDs off; a hard one drives them to a red LED's forward voltage and no further.
        HoofStage::SolverStats quiet, loud;
        render(full, sineAt(1000.0, 2.0e-5), &quiet);
        render(full, sineAt(1000.0, 0.3), &loud);
        check(quiet.peakLedVolts < 0.1, "tiny signal: the LEDs see under 0.1 V (off)", quiet.peakLedVolts, " V");
        check(loud.peakLedVolts > 1.5 && loud.peakLedVolts < 2.05, "hard-driven: the LEDs hold the swing at a red LED's ~1.8 V", loud.peakLedVolts, " V");

        auto thdQuiet = thd(render(full, sineAt(1000.0, 2.0e-5)), 1000.0);
        auto thdLoud = thd(render(full, sineAt(1000.0, 0.1)), 1000.0);
        check(thdQuiet < 0.02, "20 uV in: under 2% THD (linear)", thdQuiet * 100.0, " %");
        check(thdLoud > 0.30, "0.1 V in: over 30% THD (a fuzz)", thdLoud * 100.0, " %");

        auto small = peakOf(render(full, sineAt(1000.0, 0.01)));
        auto big = peakOf(render(full, sineAt(1000.0, 0.5)));
        auto rise = TestUtils::toDb(big / small);
        check(rise < 8.0, "+34 dB more input gives under +8 dB more output", rise, " dB");

        check(loud.worstResidual < 1.0e-6, "the Newton solve satisfies its equations to 1 uV every sample", loud.worstResidual * 1e6, " uV worst");
        check(loud.mostIterations < 20, "and never needs more than 19 iterations", loud.mostIterations);

        // Bounded by the supply.
        auto hardest = render(full, sineAt(200.0, 5.0));
        check(peakOf(hardest) < 9.0 && peakOf(hardest) > 1.0, "5 V in: output peak between 1 V and the 9 V supply", peakOf(hardest), " V");

        auto silent = render({ 1.0, 0.5, 0.4, 0.0 }, sineAt(1000.0, 0.3));
        check(peakOf(silent) < 1.0e-4, "Volume at zero is silent", peakOf(silent), " V");
    }

    // ---------------------------------------------------------------- 5
    std::printf("5. Start-up, reset and robustness\n");
    {
        // With the capacitors started at the DC operating point, silence stays silent - no thump.
        HoofStage stage(sampleRate);
        std::vector<float> zeros(20000, 0.0f), out(20000);
        stage.processBlock(zeros.data(), out.data(), 20000);
        check(peakOf(out) < 1.0e-6, "silence at the input: no start-up thump", peakOf(out), " V");

        // reset() brings the same operating point back after the circuit has been driven hard.
        std::vector<float> loud(sineAt(500.0, 3.0));
        std::vector<float> sink(loud.size());
        stage.processBlock(loud.data(), sink.data(), static_cast<int>(loud.size()));
        stage.reset();
        auto after = stage.operatingPoint();
        double drift = 0.0;
        for (size_t i = 0; i < 4; ++i)
            drift = std::max(drift, std::abs(after.collectorVolts[i] - op.collectorVolts[i]));
        check(drift < 1.0e-9, "reset restores the same DC operating point", drift, " V");

        bool finite = true;
        double peak = 0.0;
        unsigned seed = 777u;
        std::vector<float> noise(numSamples);
        for (auto& s : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            s = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * 5.0f;
        }
        for (double fuzz : { 0.0, 1.0 })
            for (double tone : { 0.0, 1.0 })
            {
                auto out2 = render({ fuzz, tone, 0.5, 1.0 }, noise);
                for (auto x : out2) { finite = finite && std::isfinite(x); peak = std::max(peak, static_cast<double>(std::abs(x))); }
            }
        check(finite && peak < 9.0, "+-5 V noise at extreme settings: finite and inside the supply", peak, " V peak");
    }

    // ---------------------------------------------------------------- 6
    std::printf("6. Turning a knob does not click\n");
    {
        // A knob is read once per audio block; snapping to its new value at the block boundary steps
        // the circuit there - a click once per block while a knob is moving. Each knob is jumped
        // mid-signal at a block boundary; the worst second difference of the output at the change must
        // not exceed the signal's own worst elsewhere.
        struct Move { const char* name; char knob; float from, to; };
        for (auto m : { Move { "Fuzz 0.60 -> 0.70", 'F', 0.60f, 0.70f }, Move { "Tone 0.30 -> 0.80", 'T', 0.30f, 0.80f }, Move { "Shift 0.20 -> 0.60", 'S', 0.20f, 0.60f }, Move { "Level 0.50 -> 0.60", 'L', 0.50f, 0.60f } })
        {
            const int block = 512, changeAt = block * 40, total = 48000;
            HoofStage stage(sampleRate);
            stage.setFuzz(m.knob == 'F' ? m.from : 0.7f);
            stage.setTone(m.knob == 'T' ? m.from : 0.5f);
            stage.setShift(m.knob == 'S' ? m.from : 0.4f);
            stage.setLevel(m.knob == 'L' ? m.from : 0.6f);
            stage.reset();

            std::vector<float> in(total), out(total);
            for (int i = 0; i < total; ++i)
                in[static_cast<size_t>(i)] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * 196.0 * i / sampleRate));
            for (int start = 0; start < total; start += block)
            {
                if (start == changeAt)
                {
                    if (m.knob == 'F') stage.setFuzz(m.to);
                    if (m.knob == 'S') stage.setShift(m.to);
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
