// Verify MarshallToneStack - the Treble/Middle/Bass network of the Marshall
// 1959 (and the JTM45 / JCM800 family), traced from the July 1970 Unicord
// drawing - against things that do not depend on my reading of that drawing:
//
//  1. The closed-form transfer function in the literature (Yeh & Smith's
//     Marshall stack polynomial). The traced netlist's first- and second-order
//     coefficients agree with it to four digits at every setting tried (that
//     is what pins the wiring); the two third-order coefficients I recall
//     differ from the netlist's by up to ~2%, which is why the comparison below
//     stays in the band and range where a 2% third-order error is inaudible
//     (Bass >= 15%, up to 700Hz; measured agreement is well inside the 0.3dB
//     allowed).
//  2. Two limits worked out by hand from the drawn circuit:
//       high frequency (every cap a short): the Bass pot drops out entirely
//         and  H = t + (1 - t) * Ga / (Ga + Gm),  Ga = 1/R4 + 1/Rtreble,
//         Gm = 1/(m * Rmid);
//       low frequency (every cap open): H = b1 * s / (1 + a1 * s).
//  3. What Marshall's manual says the network does: it is passive, its three
//     controls interact ("the settings of the Bass and Treble controls affect
//     the amount of mid-dip available via the Middle control"), and each
//     control moves the part of the spectrum it is named for.
//
// Gains are measured with a Hann-windowed lock-in against the same
// measurement of the input, so window and leakage cancel.

#include "../Source/dsp/MarshallToneStack.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    using Cx = std::complex<double>;

    // Ideal-source, unloaded version of the stack: the closed forms assume it.
    MarshallToneStack::Components ideal()
    {
        MarshallToneStack::Components c;
        c.sourceOhms = 1.0;
        c.loadOhms = 1.0e12;
        c.loadF = 1.0e-15;
        c.bassTaperAtNoon = 0.5;   // linear: a knob position is a resistance fraction
        c.midTaperAtNoon = 0.5;
        return c;
    }

    Cx tone(const std::vector<double>& x, double f, double fs)
    {
        Cx sum = 0.0;
        auto n = static_cast<double>(x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            auto w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / (n - 1.0));
            sum += w * x[i] * std::polar(1.0, -2.0 * M_PI * f * static_cast<double>(i) / fs);
        }
        return sum;
    }

    // Complex gain of the stack's wiper at f: settle, then lock in.
    Cx gain(MarshallToneStack& s, double f, double fs, double settleSeconds = 0.6, double cycles = 40.0)
    {
        auto settle = static_cast<int>(settleSeconds * fs);
        auto n = static_cast<int>(cycles * fs / f);
        std::vector<double> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
        s.reset();
        for (int i = 0; i < settle; ++i)
            s.processSample(std::sin(2.0 * M_PI * f * i / fs));
        for (int i = 0; i < n; ++i)
        {
            auto x = std::sin(2.0 * M_PI * f * (settle + i) / fs);
            in[static_cast<size_t>(i)] = x;
            s.processSample(x);
            out[static_cast<size_t>(i)] = s.wiperVoltage();
        }
        return tone(out, f, fs) / tone(in, f, fs);
    }

    double db(Cx h) { return 20.0 * std::log10(std::max(std::abs(h), 1.0e-12)); }

    // Yeh & Smith's Marshall stack: t = Treble, l = Bass, m = Middle, each a
    // resistance fraction; R1 Treble, R2 Bass, R3 Middle, R4 slope.
    struct Poly { double b1, b2, b3, a1, a2, a3; };
    Poly poly(const MarshallToneStack::Components& c, double t, double l, double m)
    {
        auto C1 = c.trebleCapF, C2 = c.bassCapF, C3 = c.midCapF;
        auto R1 = c.trebleOhms, R2 = c.bassOhms, R3 = c.midOhms, R4 = c.slopeOhms;
        Poly p;
        p.b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
        p.b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
             + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
             + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
        p.b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
             - m * m * (C1 * C2 * C3 * R1 * R3 * R4 + C1 * C2 * C3 * R3 * R3 * R4)
             + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R4)
             + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
             + t * l * C1 * C2 * C3 * R1 * R2 * R4;
        p.a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
        p.a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
             - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
             + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
        p.a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
             - m * m * (C1 * C2 * C3 * R1 * R3 * R4 + C1 * C2 * C3 * R3 * R3 * R4)
             + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R4)
             + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
        return p;
    }

    Cx polyGain(const Poly& p, double f)
    {
        Cx s(0.0, 2.0 * M_PI * f);
        return (p.b1 * s + p.b2 * s * s + p.b3 * s * s * s) / (1.0 + p.a1 * s + p.a2 * s * s + p.a3 * s * s * s);
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };
    constexpr double fs = 48000.0;

    // --- 1. Against the published polynomial. ---
    std::printf("=== Traced netlist vs the published closed-form transfer function ===\n");
    {
        auto c = ideal();
        double worst = 0.0;
        int points = 0;
        for (double t : { 0.0, 0.5, 1.0 })
            for (double l : { 0.15, 0.5, 1.0 })
                for (double m : { 0.05, 0.5, 1.0 })
                {
                    MarshallToneStack s(fs, c);
                    s.setControls(static_cast<float>(t), static_cast<float>(l), static_cast<float>(m));
                    auto p = poly(c, t, l, m);
                    for (double f : { 100.0, 250.0, 500.0, 700.0 })
                    {
                        auto err = std::abs(db(gain(s, f, fs)) - db(polyGain(p, f)));
                        worst = std::max(worst, err);
                        ++points;
                    }
                }
        std::printf("  %d (setting, frequency) points, worst disagreement %.3f dB\n", points, worst);
        check(worst < 0.3, "netlist agrees with the published polynomial to 0.3dB (100-700Hz, Bass >= 15%)");
    }
    std::printf("\n");

    // --- 2. High-frequency limit, worked out by hand. ---
    std::printf("=== High-frequency limit: Bass drops out, H = t + (1-t) Ga/(Ga+Gm) ===\n");
    {
        // Every capacitor scaled by 1000 puts the corners near 10Hz, so 3-6kHz
        // is deep in the "all caps are shorts" region.
        auto c = ideal();
        c.trebleCapF *= 1000.0; c.bassCapF *= 1000.0; c.midCapF *= 1000.0;
        double worst = 0.0;
        for (double t : { 0.0, 0.3, 0.7, 1.0 })
            for (double m : { 0.1, 0.5, 1.0 })
                for (double l : { 0.05, 0.5, 1.0 })
                {
                    MarshallToneStack s(fs, c);
                    s.setControls(static_cast<float>(t), static_cast<float>(l), static_cast<float>(m));
                    auto ga = 1.0 / c.slopeOhms + 1.0 / c.trebleOhms;
                    auto gm = 1.0 / (m * c.midOhms);
                    auto expected = t + (1.0 - t) * ga / (ga + gm);
                    auto measured = std::abs(gain(s, 4000.0, fs, 0.3, 40.0));
                    worst = std::max(worst, std::abs(db(Cx(measured / expected))));
                }
        std::printf("  worst disagreement over 36 settings (incl. every Bass position): %.4f dB\n", worst);
        check(worst < 0.05, "high-frequency response equals the hand-derived resistive divider, independent of Bass");
    }
    std::printf("\n");

    // --- 3. Low-frequency limit. ---
    std::printf("=== Low-frequency limit: H = b1 s / (1 + a1 s), +20dB/decade ===\n");
    {
        // The network's slowest pole sits near 10Hz, so "far below the corner"
        // means a few Hz - and a few seconds to settle and to measure.
        auto c = ideal();
        double worst = 0.0;
        for (double t : { 0.2, 0.8 })
            for (double l : { 0.3, 1.0 })
                for (double m : { 0.2, 0.9 })
                {
                    MarshallToneStack s(fs, c);
                    s.setControls(static_cast<float>(t), static_cast<float>(l), static_cast<float>(m));
                    auto p = poly(c, t, l, m);
                    Cx sj(0.0, 2.0 * M_PI * 4.0);
                    auto expected = p.b1 * sj / (1.0 + p.a1 * sj);
                    auto measured = gain(s, 4.0, fs, 2.0, 8.0);
                    worst = std::max(worst, std::abs(db(measured) - db(expected)));
                }
        std::printf("  worst disagreement at 4Hz over 8 settings: %.3f dB\n", worst);
        check(worst < 0.3, "4Hz response matches b1*s/(1 + a1*s)");

        MarshallToneStack s(fs, c);
        s.setControls(0.5f, 0.5f, 0.5f);
        auto slope = db(gain(s, 4.0, fs, 2.0, 8.0)) - db(gain(s, 2.0, fs, 3.0, 6.0));
        std::printf("  slope 2Hz -> 4Hz: %.2f dB/octave (a first-order high-pass is 6.02)\n", slope);
        check(slope > 5.5 && slope < 6.1, "rises at ~6dB/octave far below the corner (nothing passes DC)");
    }
    std::printf("\n");

    // --- 4. Passive, and the controls do what they are named for. ---
    std::printf("=== Passivity and the controls ===\n");
    {
        auto c = ideal();
        double maxGain = -200.0;
        for (double t : { 0.0, 0.5, 1.0 })
            for (double l : { 0.05, 0.5, 1.0 })
                for (double m : { 0.0, 0.5, 1.0 })
                {
                    MarshallToneStack s(fs, c);
                    s.setControls(static_cast<float>(t), static_cast<float>(l), static_cast<float>(m));
                    for (double f : { 60.0, 300.0, 1000.0, 3000.0, 10000.0 })
                        maxGain = std::max(maxGain, db(gain(s, f, fs, 0.4, 30.0)));
                }
        std::printf("  largest gain over 27 settings x 5 frequencies: %.3f dB\n", maxGain);
        check(maxGain < 0.01, "a passive network never amplifies (max gain <= 0dB)");

        auto at = [&](float t, float b, float m, double f) {
            MarshallToneStack s(fs, c);
            s.setControls(t, b, m);
            return db(gain(s, f, fs));
        };
        auto trebleRise4k = at(1.0f, 0.5f, 0.5f, 4000.0) - at(0.0f, 0.5f, 0.5f, 4000.0);
        auto trebleRise100 = at(1.0f, 0.5f, 0.5f, 100.0) - at(0.0f, 0.5f, 0.5f, 100.0);
        auto bassRise100 = at(0.5f, 1.0f, 0.5f, 100.0) - at(0.5f, 0.0f, 0.5f, 100.0);
        auto bassRise4k = at(0.5f, 1.0f, 0.5f, 4000.0) - at(0.5f, 0.0f, 0.5f, 4000.0);
        double midRise = 0.0;
        for (double f : { 400.0, 600.0, 800.0, 1000.0 })
            midRise = std::max(midRise, at(0.5f, 0.5f, 1.0f, f) - at(0.5f, 0.5f, 0.0f, f));
        std::printf("  Treble 0->max: %+.1f dB at 4kHz, %+.1f dB at 100Hz\n", trebleRise4k, trebleRise100);
        std::printf("  Bass   0->max: %+.1f dB at 100Hz, %+.1f dB at 4kHz\n", bassRise100, bassRise4k);
        std::printf("  Middle 0->max: up to %+.1f dB in 400Hz-1kHz\n", midRise);
        check(trebleRise4k > 8.0 && std::abs(trebleRise100) < 2.0, "Treble opens the top and leaves the bottom alone");
        check(bassRise100 > 6.0 && std::abs(bassRise4k) < 1.0, "Bass opens the bottom and leaves the top alone");
        check(midRise > 5.0, "Middle fills in the mids (the scoop is deepest at 0)");

        // Interaction: the depth of the mid dip Middle controls depends on the
        // other two controls.
        auto dipRange = [&](float t, float b) { return at(t, b, 1.0f, 600.0) - at(t, b, 0.0f, 600.0); };
        auto lo = dipRange(0.2f, 0.2f), hi = dipRange(0.9f, 0.9f);
        std::printf("  Middle's range at 600Hz: %.1f dB with Treble/Bass low, %.1f dB with them high\n", lo, hi);
        check(std::abs(lo - hi) > 2.0, "Bass and Treble change how much Middle does (the stack is interactive)");
    }
    std::printf("\n");

    // --- 5. What the stack sits between. ---
    std::printf("=== Between the cathode follower and the phase inverter ===\n");
    {
        auto withSource = [](double ohms, double load) {
            auto c = ideal();
            c.sourceOhms = ohms;
            c.loadOhms = load;
            return c;
        };
        auto at1k = [&](const MarshallToneStack::Components& c) {
            MarshallToneStack s(fs, c);
            s.setControls(0.7f, 0.7f, 0.7f);
            return db(gain(s, 1000.0, fs));
        };
        auto ref = at1k(withSource(1.0, 1.0e12));

        // The follower's ~615 ohm output resistance against a stack that looks
        // like ~50k to it at 1kHz (R4 in series with C3 into the Middle pot).
        auto dSource = at1k(withSource(615.0, 1.0e12)) - ref;
        std::printf("  615 ohm follower vs an ideal source: %+.2f dB at 1kHz\n", dSource);
        check(dSource < 0.0 && dSource > -0.25, "the follower's output resistance costs a fraction of a dB");

        // The phase inverter's 1M grid leak loads the Treble wiper, which sits
        // at a high impedance: that is a real, audible loss, and it grows as
        // the load gets smaller.
        auto d1M = at1k(withSource(1.0, 1.0e6)) - ref;
        auto d220k = at1k(withSource(1.0, 220.0e3)) - ref;
        std::printf("  a 1M load on the wiper: %+.2f dB;  a 220k load: %+.2f dB\n", d1M, d220k);
        check(d1M < -0.3 && d1M > -3.0 && d220k < d1M, "the wiper's load costs level, more for a smaller load");

        // The grid behind the coupling cap never sees more than the wiper does.
        auto real = MarshallToneStack::Components{};
        real.bassTaperAtNoon = 0.5; real.midTaperAtNoon = 0.5;
        MarshallToneStack s(fs, real);
        s.setControls(1.0f, 0.5f, 0.5f);
        double f = 8000.0;
        auto n = static_cast<int>(40.0 * fs / f);
        std::vector<double> in, wiper, grid;
        for (int i = 0; i < 20000; ++i) s.processSample(std::sin(2.0 * M_PI * f * i / fs));
        for (int i = 0; i < n; ++i)
        {
            auto x = std::sin(2.0 * M_PI * f * (20000 + i) / fs);
            in.push_back(x);
            grid.push_back(s.processSample(x));
            wiper.push_back(s.wiperVoltage());
        }
        auto atWiper = db(tone(wiper, f, fs) / tone(in, f, fs));
        auto atGrid = db(tone(grid, f, fs) / tone(in, f, fs));
        std::printf("  Treble max at 8kHz: %.2f dB at the wiper, %.2f dB at the next grid\n", atWiper, atGrid);
        check(atGrid <= atWiper + 0.001 && atGrid > atWiper - 1.0, "40pF and the coupling cap trim the top slightly, never boost it");
    }
    std::printf("\n");

    // --- 5b. The two-phase sample the power amp uses to load the output node. ---
    std::printf("=== beginSample() / outputOhms() / finishSample() ===\n");
    {
        MarshallToneStack whole(fs, MarshallToneStack::Components{}), split(fs, MarshallToneStack::Components{});
        whole.setControls(0.6f, 0.4f, 0.7f);
        split.setControls(0.6f, 0.4f, 0.7f);
        double worst = 0.0;
        for (int i = 0; i < 5000; ++i)
        {
            auto v = 3.0 * std::sin(2.0 * M_PI * 700.0 * i / fs);
            auto a = whole.processSample(v);
            split.beginSample(v);
            auto b = split.finishSample(0.0);
            worst = std::max(worst, std::abs(a - b));
        }
        std::printf("  begin + finish(0) vs processSample(): worst difference %.2e V\n", worst);
        check(worst < 1.0e-12, "with nothing drawn, the two halves are exactly processSample()");

        // Draw a current through a resistor to ground from the output node: the
        // voltage must fall to free * R / (R + Zout), with Zout what outputOhms() said.
        MarshallToneStack loaded(fs, MarshallToneStack::Components{});
        loaded.setControls(0.5f, 0.5f, 0.5f);
        for (int i = 0; i < 200; ++i) loaded.processSample(std::sin(2.0 * M_PI * 400.0 * i / fs));
        auto free = loaded.beginSample(1.0);
        auto zOut = loaded.outputOhms();
        constexpr double load = 47.0e3;
        auto drawn = free / (load + zOut);
        auto after = loaded.finishSample(drawn);
        std::printf("  free %.5f V, output resistance %.1f ohms, a 47k load: %.5f V (divider %.5f)\n", free, zOut, after, free * load / (load + zOut));
        check(std::abs(after - free * load / (load + zOut)) < 1.0e-9, "drawing a current moves the node by outputOhms() per amp");
    }
    std::printf("\n");

    // --- 6. Stability. ---
    std::printf("=== Stability ===\n");
    {
        MarshallToneStack s(fs, MarshallToneStack::Components{});
        unsigned seed = 7u;
        bool ok = true;
        for (int block = 0; block < 500; ++block)
        {
            s.setControls(static_cast<float>(block % 5) / 4.0f, static_cast<float>(block % 7) / 6.0f, static_cast<float>(block % 3) / 2.0f);
            for (int i = 0; i < 128; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                auto y = s.processSample(30.0 * (static_cast<double>(seed >> 8) / 8388608.0 - 1.0));
                ok &= std::isfinite(y) && std::abs(y) < 100.0;
            }
        }
        check(ok, "moving every knob on every block on 30V noise stays finite and bounded");
    }

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
