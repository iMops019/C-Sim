// Verify the Tube Screamer model against the circuit it is built from
// (ElectroSmash / Geofex / Cerutti's TS808 schematics).
//
// Three layers, so a failure points at the right place:
//   1. DERIVATION. The tone stage is a closed-form transfer function worked out
//      by hand from the netlist. Here the same netlist is solved by brute force
//      (complex modified nodal analysis with an ideal op-amp as a nullor) and the
//      two must agree - this catches an algebra slip, which no listening test
//      would find. The independent numbers R.G. Keen / ElectroSmash / a separate
//      derivation give for the stage are checked too.
//   2. DIGITAL vs ANALOG. The audio path at a tiny (diodes-off) level must have
//      the frequency response the analytic small-signal function predicts.
//   3. THE NONLINEAR PART. The feedback diode solve is checked against a
//      bisection solution, and the clipper against what a pair of silicon diodes
//      around an op-amp must do: clip symmetrically, at about a diode drop, with
//      a knee that moves with Drive, and compress rather than just get louder.
//
// Gains are measured as output/input at the same frequency through the same
// Goertzel window, so scalloping cancels (see GraphicEQTest). THD uses a Hann
// window (see CentaurDriveStageTest).

#include "../Source/dsp/TubeScreamerStage.h"
#include "TestUtils.h"

#include <cstdio>
#include <cstdlib>
#include <complex>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 32768;
    constexpr int settleSamples = 12288;

    int failures = 0;

    void check(bool ok, const char* what, double got, const char* unit = "")
    {
        std::printf("  [%s] %s (%.4g%s)\n", ok ? "PASS" : "FAIL", what, got, unit);
        if (! ok)
            ++failures;
    }

    using cd = std::complex<double>;
    const TubeScreamerStage::Components comps = TubeScreamerStage::ts808();

    std::vector<float> sineAt(double freq, double amplitude)
    {
        std::vector<float> s(numSamples);
        for (int n = 0; n < numSamples; ++n)
            s[static_cast<size_t>(n)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * freq * n / sampleRate));
        return s;
    }

    std::vector<float> render(double drive, double tone, double level, const std::vector<float>& in)
    {
        TubeScreamerStage stage(sampleRate);
        stage.setDrive(static_cast<float>(drive));
        stage.setTone(static_cast<float>(tone));
        stage.setLevel(static_cast<float>(level));
        std::vector<float> out(in.size());
        stage.processBlock(in.data(), out.data(), static_cast<int>(in.size()));
        return std::vector<float>(out.begin() + settleSamples, out.end());
    }

    // Output/input magnitude at one frequency, both measured the same way.
    double gainDb(double drive, double tone, double level, double freq, double amplitude)
    {
        auto in = sineAt(freq, amplitude);
        auto out = render(drive, tone, level, in);
        std::vector<float> inTail(in.begin() + settleSamples, in.end());
        return TestUtils::toDb(TestUtils::goertzelMagnitude(out, freq, sampleRate) / TestUtils::goertzelMagnitude(inTail, freq, sampleRate));
    }

    double analyticDb(double drive, double tone, double level, double freq, bool diodeLeakage = true)
    {
        return TestUtils::toDb(std::abs(TubeScreamerStage::smallSignalResponse(comps, drive, tone, level, freq, diodeLeakage)));
    }

    // ---- Brute-force reference for the tone stage ----
    // Unknowns: vP, vW, vZ, vN, vOut, and the op-amp's output current. The
    // op-amp is a nullor: vP - vN = 0 with the current supplied to vOut as
    // whatever it takes.
    cd toneStageByNodalAnalysis(double tone, double freq)
    {
        auto s = cd(0.0, 2.0 * M_PI * freq);
        auto rl = std::max(tone * comps.tonePot, 1.0);
        auto rr = std::max((1.0 - tone) * comps.tonePot, 1.0);

        enum { P, W, Z, N, O, I, count };
        cd a[count][count + 1] = {};
        auto stamp = [&](int x, int y, cd g) {
            a[x][x] += g;
            a[y][y] += g;
            a[x][y] -= g;
            a[y][x] -= g;
        };
        auto stampToGround = [&](int x, cd g) { a[x][x] += g; };

        // R7 from the source (1V, moved to the right-hand side) into P.
        stampToGround(P, 1.0 / comps.r7);
        a[P][count] += 1.0 / comps.r7;
        stampToGround(P, s * comps.c5);
        stampToGround(P, 1.0 / comps.r9);
        stamp(P, W, 1.0 / rl);
        stamp(W, N, 1.0 / rr);
        stamp(W, Z, 1.0 / comps.r8);
        stampToGround(Z, s * comps.c6);
        stamp(N, O, 1.0 / comps.r11);

        // The op-amp: current I enters node O; constraint row vP - vN = 0.
        a[O][I] += 1.0; // KCL at O, moving I to the left-hand side as +1*I
        a[I][P] += 1.0;
        a[I][N] -= 1.0;

        // Gaussian elimination.
        for (int c = 0; c < count; ++c)
        {
            int pivot = c;
            for (int r = c + 1; r < count; ++r)
                if (std::abs(a[r][c]) > std::abs(a[pivot][c]))
                    pivot = r;
            for (int k = 0; k <= count; ++k)
                std::swap(a[c][k], a[pivot][k]);
            for (int r = 0; r < count; ++r)
            {
                if (r == c)
                    continue;
                auto f = a[r][c] / a[c][c];
                for (int k = c; k <= count; ++k)
                    a[r][k] -= f * a[c][k];
            }
        }
        return a[O][count] / a[O][O];
    }

    cd toneStageAnalytic(double tone, double freq)
    {
        auto s = cd(0.0, 2.0 * M_PI * freq);
        auto t = TubeScreamerStage::toneStage(comps, tone);
        return (t.n0 + t.n1 * s) / (t.d0 + t.d1 * s + t.d2 * s * s);
    }

    double toneDb(double tone, double freq) { return TestUtils::toDb(std::abs(toneStageAnalytic(tone, freq))); }

    // Bisection solution of the diode network equation, as an independent check
    // of the Newton solver.
    double bisect(double gLinear, double b, double is, double nVt)
    {
        auto f = [&](double v) { return gLinear * v + is * (std::exp(v / nVt) - std::exp(-v / nVt)) - b; };
        double lo = -3.0, hi = 3.0;
        for (int i = 0; i < 200; ++i)
        {
            auto mid = 0.5 * (lo + hi);
            (f(mid) > 0.0 ? hi : lo) = mid;
        }
        return 0.5 * (lo + hi);
    }

    std::vector<float> hann(std::vector<float> v)
    {
        for (size_t n = 0; n < v.size(); ++n)
            v[n] *= static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(n) / static_cast<double>(v.size())));
        return v;
    }

    // Harmonic amplitude ratio to the fundamental.
    double harmonicRatio(const std::vector<float>& signal, double freq, int harmonic)
    {
        auto w = hann(signal);
        return TestUtils::goertzelMagnitude(w, freq * harmonic, sampleRate) / TestUtils::goertzelMagnitude(w, freq, sampleRate);
    }
}

int main()
{
    // ---------------------------------------------------------------- 1
    std::printf("1. Tone stage: hand derivation vs brute-force nodal analysis\n");
    {
        double worst = 0.0;
        for (double tone : { 0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0 })
            for (double f : { 20.0, 100.0, 300.0, 720.0, 1000.0, 2000.0, 3000.0, 6000.0, 10000.0, 20000.0 })
            {
                auto err = std::abs(TestUtils::toDb(std::abs(toneStageAnalytic(tone, f))) - TestUtils::toDb(std::abs(toneStageByNodalAnalysis(tone, f))));
                worst = std::max(worst, err);
            }
        check(worst < 0.01, "closed form matches the netlist solved by MNA, all tone/frequency combos", worst, " dB max error");
    }

    // The values Keen/ElectroSmash/an independent derivation give for the stage
    // (tone stage alone, dB) - a check of the netlist reading itself.
    std::printf("2. Tone stage against the independently derived reference values\n");
    {
        struct Ref { double tone, freq, db; };
        for (auto r : { Ref { 0.0, 300, -3.0 },  Ref { 0.0, 720, -7.4 },  Ref { 0.0, 1000, -9.6 },  Ref { 0.0, 3000, -17.1 },  Ref { 0.0, 10000, -24.1 },
                        Ref { 0.5, 300, -1.4 },  Ref { 0.5, 720, -3.1 },  Ref { 0.5, 1000, -4.5 },  Ref { 0.5, 3000, -11.9 },  Ref { 0.5, 10000, -22.1 },
                        Ref { 1.0, 300, -0.5 },  Ref { 1.0, 720, 0.3 },   Ref { 1.0, 1000, 0.5 },   Ref { 1.0, 3000, -1.0 },   Ref { 1.0, 10000, -8.4 } })
        {
            auto got = toneDb(r.tone, r.freq);
            char label[96];
            std::snprintf(label, sizeof(label), "tone %.1f @ %5.0f Hz vs %+.1f dB", r.tone, r.freq, r.db);
            check(std::abs(got - r.db) < 0.35, label, got, " dB");
        }
        check(std::abs(toneDb(0.5, 20.0) - (-0.83)) < 0.1, "flat -0.83 dB at 20 Hz (the 1k / 10k divider)", toneDb(0.5, 20.0), " dB");
    }

    // ---------------------------------------------------------------- 3
    std::printf("3. Clipping stage gain law (whole chain, diodes off, Tone centre, Level max)\n");
    {
        // Gmin = 1 + 51k/4.7k = 11.85 (21.5dB); Gmax = 1 + 551k/4.7k = 118 (41.4dB).
        // Whole-chain 1kHz figures from the independent derivation: 15.2 / 35.0 dB.
        check(std::abs(analyticDb(0.0, 0.5, 1.0, 1000.0, false) - 15.2) < 0.4, "Drive min, 1 kHz ~ 15.2 dB", analyticDb(0.0, 0.5, 1.0, 1000.0, false), " dB");
        check(std::abs(analyticDb(1.0, 0.5, 1.0, 1000.0, false) - 35.0) < 0.4, "Drive max, 1 kHz ~ 35.0 dB", analyticDb(1.0, 0.5, 1.0, 1000.0, false), " dB");

        // The 720Hz high-pass in the inverting leg: bass gets ~unity, the mids the full gain.
        auto lowEnd = analyticDb(1.0, 1.0, 1.0, 100.0, false);
        auto midHump = analyticDb(1.0, 1.0, 1.0, 1000.0, false);
        check(midHump - lowEnd > 12.0, "mid hump: 1 kHz at least 12 dB above 100 Hz (Drive max)", midHump - lowEnd, " dB");

        // Where the peak sits moves with Tone (independent derivation: 525Hz / 780Hz / 1.56kHz at Drive max).
        auto peakAt = [](double tone) {
            double bestF = 0.0, best = -1e9;
            for (double f = 200.0; f < 5000.0; f *= 1.01)
            {
                auto d = analyticDb(1.0, tone, 1.0, f, false);
                if (d > best) { best = d; bestF = f; }
            }
            return bestF;
        };
        check(std::abs(peakAt(0.0) - 525.0) < 40.0, "Drive max, Tone 0: peak near 525 Hz", peakAt(0.0), " Hz");
        check(std::abs(peakAt(0.5) - 780.0) < 50.0, "Drive max, Tone 0.5: peak near 780 Hz", peakAt(0.5), " Hz");
        check(std::abs(peakAt(1.0) - 1560.0) < 100.0, "Drive max, Tone 1: peak near 1.56 kHz", peakAt(1.0), " Hz");

        // The clipping op-amp alone (whole chain / tone stage / output divider),
        // against the independent derivation's table: min drive 18.5/19.7/21.2/21.3/21.0 dB and
        // max drive 38.4/39.5/40.1/35.3/30.2 dB at 720Hz/1k/3k/10k/20k. The pole from the
        // 51pF is 61kHz at min drive and 5.6kHz at max, which is why the max column falls.
        struct ClipRef { double drive, freq, db; };
        for (auto r : { ClipRef { 0.0, 720, 18.5 }, ClipRef { 0.0, 1000, 19.7 }, ClipRef { 0.0, 3000, 21.2 }, ClipRef { 0.0, 10000, 21.3 }, ClipRef { 0.0, 20000, 21.0 },
                        ClipRef { 1.0, 720, 38.4 }, ClipRef { 1.0, 1000, 39.5 }, ClipRef { 1.0, 3000, 40.1 }, ClipRef { 1.0, 10000, 35.3 }, ClipRef { 1.0, 20000, 30.2 } })
        {
            auto outputDivider = 20.0 * std::log10((1.0 / (1.0 / comps.rc + 1.0 / comps.loadOhms)) / (comps.rb + 1.0 / (1.0 / comps.rc + 1.0 / comps.loadOhms)));
            auto got = analyticDb(r.drive, 0.5, 1.0, r.freq, false) - toneDb(0.5, r.freq) - outputDivider;
            char label[96];
            std::snprintf(label, sizeof(label), "clipping op-amp, Drive %.0f @ %5.0f Hz vs %.1f dB", r.drive, r.freq, r.db);
            check(std::abs(got - r.db) < 0.3, label, got, " dB");
        }
    }

    // ---------------------------------------------------------------- 4
    std::printf("4. The digital audio path reproduces the analytic small-signal response\n");
    {
        struct Setting { double drive, tone, level; };
        double worst = 0.0;
        for (auto s : { Setting { 0.0, 0.5, 1.0 }, Setting { 0.5, 0.2, 0.8 }, Setting { 1.0, 0.8, 1.0 }, Setting { 0.3, 1.0, 0.5 } })
            for (double f : { 60.0, 200.0, 500.0, 1000.0, 2000.0, 4000.0 })
            {
                // 40uV in is at most ~5mV across the diodes at the highest gain: only their leakage matters.
                auto err = std::abs(gainDb(s.drive, s.tone, s.level, f, 0.00004) - analyticDb(s.drive, s.tone, s.level, f));
                worst = std::max(worst, err);
            }
        check(worst < 0.25, "measured gain within 0.25 dB of the analytic response (4 settings x 6 frequencies)", worst, " dB max error");
    }

    // ---------------------------------------------------------------- 5
    std::printf("5. The feedback diode solve\n");
    {
        double worst = 0.0;
        const double nVt = 1.752 * 0.02585;
        for (double gLin : { 1.0 / 551.0e3 + 1.0e-5, 1.0 / 51.0e3 + 2.0e-5 })
            for (double b : { -5.0e-3, -1.0e-4, -2.0e-6, 0.0, 1.0e-7, 3.0e-6, 6.4e-5, 1.0e-3, 2.0e-2 })
            {
                auto got = TubeScreamerStage::solveDiodePair(gLin, b, 2.52e-9, nVt);
                worst = std::max(worst, std::abs(got - bisect(gLin, b, 2.52e-9, nVt)));
            }
        check(worst < 1.0e-9, "Newton solve agrees with bisection over 8 decades of drive current", worst, " V max error");

        // 1N914-class parameters should drop about 0.5-0.6V at a fraction of a milliamp.
        auto v = TubeScreamerStage::solveDiodePair(1.0e-5, 1.0e-3, 2.52e-9, nVt);
        check(v > 0.5 && v < 0.7, "the silicon diodes hold ~0.6 V at 1 mA", v, " V");
    }

    // ---------------------------------------------------------------- 6
    std::printf("6. The clipper: symmetric, about a diode drop, knee moves with Drive\n");
    {
        auto runClipper = [](double driveFraction, double amplitude, double freq, std::vector<float>* voutOut, double* peakDiodeVolts) {
            TubeScreamerStage::ClippingStage clip;
            clip.prepare(sampleRate * 4.0, comps);
            clip.setFeedbackResistance(TubeScreamerStage::driveFeedbackOhms(comps, driveFraction));
            auto rate = sampleRate * 4.0;
            auto n = numSamples * 4;
            std::vector<float> vout;
            double peak = 0.0;
            for (int i = 0; i < n; ++i)
            {
                auto out = clip.process(amplitude * std::sin(2.0 * M_PI * freq * i / rate));
                if (i > n / 4)
                {
                    peak = std::max(peak, std::abs(clip.lastFeedbackVolts()));
                    if (i % 4 == 0)
                        vout.push_back(static_cast<float>(out));
                }
            }
            if (voutOut) *voutOut = std::move(vout);
            if (peakDiodeVolts) *peakDiodeVolts = peak;
        };

        std::vector<float> vout;
        double peak = 0.0;
        runClipper(0.7, 0.3, 1000.0, &vout, &peak);
        // Length here is 3/4 of numSamples; the ratio is unaffected.
        auto h2 = harmonicRatio(vout, 1000.0, 2);
        auto h3 = harmonicRatio(vout, 1000.0, 3);
        check(h3 > 0.02, "hard-driven, it produces odd harmonics (3rd)", TestUtils::toDb(h3), " dB");
        check(h2 < h3 * 0.01, "matched diodes: 2nd harmonic at least 40 dB below the 3rd", TestUtils::toDb(h2 / h3), " dB");

        // The diodes are what limits the feedback voltage: at the leg's peak
        // current (amplitude over the 4.7k + 0.047uF impedance at 1kHz) the network
        // sits where the diode equation says, and that is a silicon drop, not a
        // fixed number - it rises with the current (about 45mV per e-fold).
        auto omega = 2.0 * M_PI * 1000.0;
        auto legImpedance = std::hypot(comps.r4, 1.0 / (omega * comps.c3));
        auto predicted = bisect(1.0 / TubeScreamerStage::driveFeedbackOhms(comps, 0.7), 0.3 / legImpedance, 2.52e-9, 1.752 * 0.02585);
        check(std::abs(peak - predicted) < 0.02, "voltage across the diodes matches the diode equation at the leg current", peak, " V");
        check(peak > 0.40 && peak < 0.70, "...and is a silicon-diode-sized drop", peak, " V");

        // The knee: at the same small input the diodes carry almost none of the
        // current at Drive min but nearly all of it at Drive max.
        auto diodeShare = [](double vf, double legPeakAmps) { return 2.0 * 2.52e-9 * std::sinh(vf / (1.752 * 0.02585)) / legPeakAmps; };
        auto legPeak = 0.02 / legImpedance;
        double lowPeak = 0.0, highPeak = 0.0;
        runClipper(0.0, 0.02, 1000.0, nullptr, &lowPeak);
        runClipper(1.0, 0.02, 1000.0, nullptr, &highPeak);
        check(diodeShare(lowPeak, legPeak) < 0.10, "Drive min, 20 mV in: the diodes carry under 10% of the current", diodeShare(lowPeak, legPeak) * 100.0, " %");
        check(diodeShare(highPeak, legPeak) > 0.60, "Drive max, same 20 mV in: the diodes carry most (>60%) of it - the rest still leaks through the 551k", diodeShare(highPeak, legPeak) * 100.0, " %");
    }

    // ---------------------------------------------------------------- 7
    std::printf("7. Whole pedal: clean at low level, compresses when driven\n");
    {
        auto thd = [](double drive, double amplitude) {
            auto out = render(drive, 0.5, 1.0, sineAt(1000.0, amplitude));
            double harm = 0.0;
            auto w = hann(out);
            auto fund = TestUtils::goertzelMagnitude(w, 1000.0, sampleRate);
            for (int h = 2; h <= 9; ++h)
            {
                auto m = TestUtils::goertzelMagnitude(w, 1000.0 * h, sampleRate) / fund;
                harm += m * m;
            }
            return std::sqrt(harm);
        };
        auto quiet = thd(0.0, 0.010);
        auto loud = thd(0.0, 0.30);
        check(quiet < 0.01, "Drive min, 10 mV guitar: under 1% THD (the stage's clean region)", quiet * 100.0, " %");
        // (Measured after the tone stage, which lowpasses the harmonics away, so
        // the figure is modest next to the clipper's own - the claim is the jump.)
        check(loud > 0.03 && loud > quiet * 50.0, "Drive min, 300 mV guitar: THD over 3% and 50x the clean case (clipping)", loud * 100.0, " %");
        check(thd(1.0, 0.010) > thd(0.0, 0.010) * 5.0, "the same 10 mV clips far harder at Drive max", thd(1.0, 0.010) * 100.0, " %");

        // Once clipping, 20dB more input should raise the output far less than 20dB.
        auto rms = [](const std::vector<float>& v) {
            double s = 0.0;
            for (auto x : v) s += static_cast<double>(x) * x;
            return std::sqrt(s / static_cast<double>(v.size()));
        };
        auto lowIn = rms(render(0.5, 0.5, 1.0, sineAt(1000.0, 0.05)));
        auto highIn = rms(render(0.5, 0.5, 1.0, sineAt(1000.0, 0.50)));
        auto rise = TestUtils::toDb(highIn / lowIn);
        check(rise < 8.0, "Drive 0.5: +20 dB in gives under +8 dB out once clipping", rise, " dB");

        // The diodes, not the op-amp's supply, set the ceiling: a hard 0.5V input at
        // Drive max can only reach the input plus a diode drop at the clipper's output
        // (about 1.2V, then the Tone stage takes some of it away). With the diodes
        // taken out the rail limit is all that is left, at 3V+.
        auto hardDriven = render(1.0, 0.5, 1.0, sineAt(1000.0, 0.5));
        double hardPeak = 0.0;
        for (auto x : hardDriven) hardPeak = std::max(hardPeak, static_cast<double>(std::abs(x)));
        check(hardPeak < 1.3 && hardPeak > 0.3, "0.5 V in at Drive max: output ceiling set by the diodes (< 1.3 V)", hardPeak, " V");

        // Output at typical settings lands at a usable level for an amp input (a
        // real TS at Level max puts out a volt or so).
        auto typical = render(0.4, 0.5, 0.7, sineAt(440.0, 0.15));
        double peak = 0.0;
        for (auto x : typical) peak = std::max(peak, static_cast<double>(std::abs(x)));
        check(peak > 0.25 && peak < 1.5, "default settings, 150 mV guitar: output peak 0.25-1.5 V", peak, " V");
    }

    // ---------------------------------------------------------------- 8
    std::printf("8. Robustness\n");
    {
        bool finite = true;
        double peak = 0.0;
        unsigned seed = 12345u;
        std::vector<float> noise(numSamples);
        for (auto& s : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            s = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * 5.0f; // +-5V bursts, well past a guitar
        }
        for (double drive : { 0.0, 1.0 })
            for (double tone : { 0.0, 1.0 })
            {
                auto out = render(drive, tone, 1.0, noise);
                for (auto x : out)
                {
                    finite = finite && std::isfinite(x);
                    peak = std::max(peak, static_cast<double>(std::abs(x)));
                }
            }
        check(finite, "extreme input at extreme settings stays finite", peak, " V peak");
        check(peak < 5.0, "output stays inside the op-amp rails' reach", peak, " V peak");
    }

    // ---------------------------------------------------------------- 9
    std::printf("9. TS9: same circuit as the TS808, different output resistors\n");
    {
        auto ts9 = TubeScreamerStage::ts9();
        auto response = [&](const TubeScreamerStage::Components& c, double f) {
            return TestUtils::toDb(std::abs(TubeScreamerStage::smallSignalResponse(c, 0.6, 0.5, 1.0, f)));
        };

        // Geofex: the output divider (series R over shunt R) is 0.9901 on the 808
        // (100 / 10k) and 0.9953 on the TS9 (470 / 100k) unloaded; into an amp's 1M
        // input it is 0.9900 and 0.9949 - a level step of about +0.04 dB.
        auto step = response(ts9, 1000.0) - response(comps, 1000.0);
        check(step > 0.03 && step < 0.06, "TS9 sits ~0.04 dB above the TS808 at 1 kHz (the output divider)", step, " dB");

        // Nothing else in the audio path differs: the response is the same shape
        // everywhere above the sub-audio output corner.
        double spread = 0.0;
        for (double f : { 100.0, 300.0, 720.0, 2000.0, 5000.0, 12000.0 })
            spread = std::max(spread, std::abs((response(ts9, f) - response(comps, f)) - step));
        check(spread < 0.01, "and the difference is flat from 100 Hz to 12 kHz", spread, " dB");

        // The digital model shows the same step.
        auto measure = [&](const TubeScreamerStage::Components& c) {
            TubeScreamerStage stage(sampleRate, c);
            stage.setDrive(0.6f);
            stage.setTone(0.5f);
            stage.setLevel(1.0f);
            auto in = sineAt(1000.0, 0.00004);
            std::vector<float> out(in.size());
            stage.processBlock(in.data(), out.data(), static_cast<int>(in.size()));
            return TestUtils::goertzelMagnitude(std::vector<float>(out.begin() + settleSamples, out.end()), 1000.0, sampleRate);
        };
        auto measuredStep = TestUtils::toDb(measure(ts9) / measure(comps));
        check(std::abs(measuredStep - step) < 0.02, "the audio path reproduces that step", measuredStep, " dB");

        // The clipping is identical: hard-driven, the two outputs differ only by that level step.
        auto hard = [&](const TubeScreamerStage::Components& c) {
            TubeScreamerStage stage(sampleRate, c);
            stage.setDrive(0.9f);
            stage.setTone(0.5f);
            stage.setLevel(1.0f);
            auto in = sineAt(500.0, 0.4);
            std::vector<float> out(in.size());
            stage.processBlock(in.data(), out.data(), static_cast<int>(in.size()));
            double sum = 0.0;
            for (size_t n = settleSamples; n < out.size(); ++n) sum += static_cast<double>(out[n]) * out[n];
            return std::sqrt(sum);
        };
        auto hardStep = TestUtils::toDb(hard(ts9) / hard(comps));
        check(std::abs(hardStep - step) < 0.02, "hard-driven, the clipped output differs by the same step only", hardStep, " dB");
    }

    // ---------------------------------------------------------------- 10
    std::printf("10. Turning a knob does not click\n");
    {
        // A knob is read once per audio block. Snapping to the new value at the block boundary
        // steps the gain there - a click, once per block while a knob is moving. Each of the three
        // knobs is jumped mid-signal at a block boundary; the worst second difference of the output
        // right at the change must not exceed the signal's own worst elsewhere.
        struct Move { const char* name; char knob; float from, to; };
        for (auto m : { Move { "Level 0.5 -> 0.6", 'L', 0.5f, 0.6f }, Move { "Level 0.2 -> 0.9", 'L', 0.2f, 0.9f }, Move { "Tone 0.3 -> 0.8", 'T', 0.3f, 0.8f },
                        Move { "Drive 0.3 -> 0.8", 'D', 0.3f, 0.8f }, Move { "Drive 0.9 -> 0.91", 'D', 0.9f, 0.91f } })
        {
            const int block = 512, changeAt = block * 40, total = 48000;
            TubeScreamerStage stage(sampleRate);
            stage.setDrive(m.knob == 'D' ? m.from : 0.5f);
            stage.setTone(m.knob == 'T' ? m.from : 0.5f);
            stage.setLevel(m.knob == 'L' ? m.from : 0.7f);
            stage.reset();

            std::vector<float> in(total), out(total);
            for (int i = 0; i < total; ++i)
                in[static_cast<size_t>(i)] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * 196.0 * i / sampleRate));
            for (int start = 0; start < total; start += block)
            {
                if (start == changeAt)
                {
                    if (m.knob == 'D') stage.setDrive(m.to);
                    if (m.knob == 'T') stage.setTone(m.to);
                    if (m.knob == 'L') stage.setLevel(m.to);
                }
                stage.processBlock(in.data() + start, out.data() + start, std::min(block, total - start));
            }

            double natural = 0.0, atChange = 0.0;
            for (int i = 12000; i < total - 2; ++i)
            {
                auto d2 = std::abs(static_cast<double>(out[static_cast<size_t>(i) + 1]) - 2.0 * out[static_cast<size_t>(i)] + out[static_cast<size_t>(i) - 1]);
                (std::abs(i - changeAt) <= 40 ? atChange : natural) = std::max(std::abs(i - changeAt) <= 40 ? atChange : natural, d2);
            }
            char label[100];
            std::snprintf(label, sizeof(label), "%s: worst discontinuity at the change is x%.2f the signal's own", m.name, atChange / natural);
            check(atChange < natural * 1.5, label, atChange / natural, "x");
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
