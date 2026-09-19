// Verify NodalNetwork against circuits whose answer is known independently:
//  - a simple RC low-pass and high-pass land on their textbook -3dB point
//    and slope;
//  - a resistive divider with a DC source gives the divider ratio, and a
//    capacitor-coupled node settles to zero;
//  - a realistic, INTERACTIVE network (a Fender-style tone stack with the
//    pot loading each other) matches an independent frequency-domain solve
//    (complex-number nodal analysis, written separately in this file) at
//    several frequencies and several pot positions - the check that would
//    catch a stamping/sign error in the companion model;
//  - changing a component value (a pot move) takes effect and changes the
//    response the right way;
//  - it stays finite and bounded on a long noise run.
//
// The frequency-domain reference shares NO code with the solver: it builds
// the complex admittance matrix directly and solves it by Gaussian
// elimination. Both are fed the same netlist, so this proves the numerics,
// not the topology - topology is checked against the schematic's own
// printed voltages in SuperSonic22PreampTest.

#include "../Source/dsp/NodalNetwork.h"
#include "TestUtils.h"

#include <complex>
#include <cstdio>
#include <vector>

namespace
{
    struct Part { int a, b; bool cap; double value; };

    // Independent reference: complex nodal analysis at one frequency.
    // Nodes: 0 ground, 1 source (1V), 2.. unknown.
    std::complex<double> referenceTransfer(const std::vector<Part>& parts, int numNodes, int outNode, double freq)
    {
        using C = std::complex<double>;
        auto n = numNodes - 2;
        std::vector<std::vector<C>> a(static_cast<size_t>(n), std::vector<C>(static_cast<size_t>(n), C(0.0)));
        std::vector<C> b(static_cast<size_t>(n), C(0.0));
        auto w = 2.0 * M_PI * freq;

        for (auto& p : parts)
        {
            C y = p.cap ? C(0.0, w * p.value) : C(1.0 / p.value);
            if (p.a >= 2) a[static_cast<size_t>(p.a - 2)][static_cast<size_t>(p.a - 2)] += y;
            if (p.b >= 2) a[static_cast<size_t>(p.b - 2)][static_cast<size_t>(p.b - 2)] += y;
            if (p.a >= 2 && p.b >= 2)
            {
                a[static_cast<size_t>(p.a - 2)][static_cast<size_t>(p.b - 2)] -= y;
                a[static_cast<size_t>(p.b - 2)][static_cast<size_t>(p.a - 2)] -= y;
            }
            if (p.a >= 2 && p.b == 1) b[static_cast<size_t>(p.a - 2)] += y;
            if (p.b >= 2 && p.a == 1) b[static_cast<size_t>(p.b - 2)] += y;
        }

        // Gaussian elimination with partial pivoting.
        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs(a[static_cast<size_t>(r)][static_cast<size_t>(col)]) > std::abs(a[static_cast<size_t>(pivot)][static_cast<size_t>(col)]))
                    pivot = r;
            std::swap(a[static_cast<size_t>(col)], a[static_cast<size_t>(pivot)]);
            std::swap(b[static_cast<size_t>(col)], b[static_cast<size_t>(pivot)]);
            for (int r = col + 1; r < n; ++r)
            {
                auto f = a[static_cast<size_t>(r)][static_cast<size_t>(col)] / a[static_cast<size_t>(col)][static_cast<size_t>(col)];
                for (int c = col; c < n; ++c)
                    a[static_cast<size_t>(r)][static_cast<size_t>(c)] -= f * a[static_cast<size_t>(col)][static_cast<size_t>(c)];
                b[static_cast<size_t>(r)] -= f * b[static_cast<size_t>(col)];
            }
        }
        std::vector<C> x(static_cast<size_t>(n));
        for (int r = n - 1; r >= 0; --r)
        {
            C sum = b[static_cast<size_t>(r)];
            for (int c = r + 1; c < n; ++c)
                sum -= a[static_cast<size_t>(r)][static_cast<size_t>(c)] * x[static_cast<size_t>(c)];
            x[static_cast<size_t>(r)] = sum / a[static_cast<size_t>(r)][static_cast<size_t>(r)];
        }
        return x[static_cast<size_t>(outNode - 2)];
    }

    // Steady-state gain of a NodalNetwork at one frequency, by driving it
    // with a sine and comparing the output node to the input measured the
    // SAME way (Goertzel on the settled second half). Never compare a raw
    // Goertzel reading to a hand-computed amplitude - scalloping loss makes
    // it read low even for a perfect wire (see TestUtils.h).
    double measuredGain(NodalNetwork& net, int outNode, double freq, double sampleRate)
    {
        auto total = static_cast<int>(sampleRate * 0.5);
        std::vector<float> in, out;
        net.reset();
        for (int i = 0; i < total; ++i)
        {
            auto x = std::sin(2.0 * M_PI * freq * i / sampleRate);
            net.process(x);
            if (i >= total / 2)
            {
                in.push_back(static_cast<float>(x));
                out.push_back(static_cast<float>(net.voltage(outNode)));
            }
        }
        return TestUtils::goertzelMagnitude(out, freq, sampleRate) / TestUtils::goertzelMagnitude(in, freq, sampleRate);
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    constexpr double fs = 192000.0; // the rate the amp modules run their networks at

    // --- 1. RC low-pass: -3dB at 1/(2 pi R C). ---
    std::printf("=== RC low-pass (10k, 10nF): corner 1.59 kHz ===\n");
    {
        NodalNetwork net;
        auto out = net.addNode();
        net.addResistor(NodalNetwork::source, out, 10.0e3);
        net.addCapacitor(out, NodalNetwork::ground, 10.0e-9);
        net.prepare(fs);

        auto fc = 1.0 / (2.0 * M_PI * 10.0e3 * 10.0e-9);
        auto atCorner = measuredGain(net, out, fc, fs);
        auto lowBand = measuredGain(net, out, 100.0, fs);
        auto tenX = measuredGain(net, out, fc * 10.0, fs);
        std::printf("  gain at corner %.4f (want 0.7071), at 100Hz %.4f (want ~1), at 10x corner %.4f (want ~0.0995)\n",
                     atCorner, lowBand, tenX);
        check(std::abs(atCorner - 0.7071) < 0.01, "-3dB at the corner");
        check(std::abs(lowBand - 1.0) < 0.01, "unity in the passband");
        check(std::abs(tenX - 0.0995) < 0.01, "-20dB a decade above");
        std::printf("\n");
    }

    // --- 2. RC high-pass (a coupling cap into a grid leak). The corner is
    // ~154 Hz rather than a real 15 Hz coupling cap so the settled half of
    // the measurement holds dozens of cycles - a handful of cycles leaks in
    // Goertzel and would test the measurement, not the solver. ---
    std::printf("=== RC high-pass (22nF into 47k): corner 154 Hz ===\n");
    {
        NodalNetwork net;
        auto out = net.addNode();
        net.addCapacitor(NodalNetwork::source, out, 22.0e-9);
        net.addResistor(out, NodalNetwork::ground, 47.0e3);
        net.prepare(fs);

        auto fc = 1.0 / (2.0 * M_PI * 47.0e3 * 22.0e-9);
        auto atCorner = measuredGain(net, out, fc, fs);
        auto high = measuredGain(net, out, 1000.0, fs);
        auto expectedHigh = 1.0 / std::sqrt(1.0 + (fc / 1000.0) * (fc / 1000.0)); // a first-order high-pass is not quite flat one octave-and-a-half up
        std::printf("  gain at corner %.4f (want 0.7071), at 1kHz %.4f (want %.4f)\n", atCorner, high, expectedHigh);
        check(std::abs(atCorner - 0.7071) < 0.01, "-3dB at the corner");
        check(std::abs(high - expectedHigh) < 0.005, "matches the first-order high-pass response in the passband");
        std::printf("\n");
    }

    // --- 3. DC: divider ratio, and a capacitor-coupled node blocks DC. ---
    std::printf("=== DC behaviour ===\n");
    {
        NodalNetwork net;
        auto mid = net.addNode();
        auto coupled = net.addNode();
        net.addResistor(NodalNetwork::source, mid, 30.0e3);
        net.addResistor(mid, NodalNetwork::ground, 10.0e3);
        net.addCapacitor(mid, coupled, 100.0e-9);            // 100nF into 1M: tau 0.1 s, so 2 s is 20 time constants
        net.addResistor(coupled, NodalNetwork::ground, 1.0e6);
        net.prepare(fs);

        for (int i = 0; i < static_cast<int>(fs * 2.0); ++i)
            net.process(4.0);
        std::printf("  divider node %.4f V (want 1.0000), coupled node %.6f V (want ~0)\n", net.voltage(mid), net.voltage(coupled));
        check(std::abs(net.voltage(mid) - 1.0) < 1.0e-3, "10k/(30k+10k) divider of 4V = 1V");
        check(std::abs(net.voltage(coupled)) < 0.01, "the coupling cap blocks DC");
        std::printf("\n");
    }

    // --- 4. Interactive network vs the independent complex-number reference. ---
    // A blackface-Fender-style tone stack: treble cap and pot, a slope
    // resistor feeding two caps, a bass pot in series with a fixed resistor
    // to ground, a volume pot on the wiper. Every pot loads the others.
    std::printf("=== Interactive tone stack vs independent frequency-domain solve ===\n");
    {
        struct Position { double treble, bass, volume; };
        for (auto pos : { Position { 0.30, 0.15, 0.30 }, Position { 0.05, 0.60, 0.80 }, Position { 0.95, 0.02, 0.10 } })
        {
            NodalNetwork net;
            std::vector<Part> parts;
            auto node = [&]() { return net.addNode(); };
            auto R = [&](int a, int b, double v) { net.addResistor(a, b, v); parts.push_back({ a, b, false, v }); };
            auto Cap = [&](int a, int b, double v) { net.addCapacitor(a, b, v); parts.push_back({ a, b, true, v }); };

            auto p = node(), s = node(), t = node(), x = node(), y = node(), w = node(), wv = node();
            constexpr int gnd = NodalNetwork::ground, src = NodalNetwork::source;
            R(src, p, 40.0e3);                          // the plate driving it
            Cap(p, t, 250.0e-12);                       // treble cap
            R(p, s, 100.0e3);                           // slope resistor
            Cap(s, x, 0.1e-6);
            Cap(s, y, 0.047e-6);
            R(t, w, 250.0e3 * (1.0 - pos.treble));      // treble pot, split at the wiper
            R(w, x, 250.0e3 * pos.treble);
            R(x, y, 250.0e3 * pos.bass);                // bass pot as a rheostat
            R(y, gnd, 6.8e3);
            R(w, wv, 1.0e6 * (1.0 - pos.volume));       // volume pot
            R(wv, gnd, 1.0e6 * pos.volume);
            R(wv, gnd, 470.0e3);                        // following grid leak
            Cap(wv, gnd, 100.0e-12);                    // Miller capacitance
            net.prepare(fs);

            double worstDb = 0.0;
            for (auto f : { 80.0, 250.0, 1000.0, 3000.0, 6000.0 })
            {
                auto measured = measuredGain(net, wv, f, fs);
                auto reference = std::abs(referenceTransfer(parts, 9, wv, f));
                worstDb = std::max(worstDb, std::abs(TestUtils::toDb(measured) - TestUtils::toDb(reference)));
            }
            std::printf("  treble %.2f bass %.2f volume %.2f: worst error vs reference over 80Hz-6kHz = %.3f dB\n",
                         pos.treble, pos.bass, pos.volume, worstDb);
            check(worstDb < 0.3, "matches the independent solve to within 0.3dB");
        }
        std::printf("\n");
    }

    // --- 5. Changing a component takes effect (a pot move). ---
    std::printf("=== Component changes (pot move) ===\n");
    {
        NodalNetwork net;
        auto out = net.addNode();
        auto top = net.addResistor(NodalNetwork::source, out, 10.0e3);
        net.addResistor(out, NodalNetwork::ground, 10.0e3);
        net.prepare(fs);

        auto before = measuredGain(net, out, 1000.0, fs);
        net.setResistance(top, 90.0e3);
        auto after = measuredGain(net, out, 1000.0, fs);
        std::printf("  10k/10k: %.4f (want 0.5) -> 90k/10k: %.4f (want 0.1)\n", before, after);
        check(std::abs(before - 0.5) < 0.005 && std::abs(after - 0.1) < 0.005, "divider ratio follows the resistor");

        net.setResistance(top, 0.0); // wiper at the end of its track: clamped to a short, not a divide-by-zero
        auto shorted = measuredGain(net, out, 1000.0, fs);
        check(std::isfinite(shorted) && shorted > 0.99, "a 0-ohm section is a clean short");
        std::printf("\n");
    }

    // --- 6. Stability on noise. ---
    std::printf("=== Stability ===\n");
    {
        NodalNetwork net;
        auto a = net.addNode(), b = net.addNode();
        net.addResistor(NodalNetwork::source, a, 5.0e3);
        net.addCapacitor(a, NodalNetwork::ground, 47.0e-12);   // stiff: a small cap behind a small resistor
        net.addResistor(a, b, 1.0e3);
        net.addCapacitor(b, NodalNetwork::ground, 47.0e-12);
        net.addResistor(b, NodalNetwork::ground, 220.0e3);
        net.prepare(fs);

        unsigned seed = 12345u;
        double peak = 0.0;
        bool finite = true;
        for (int i = 0; i < static_cast<int>(fs * 3.0); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            net.process((static_cast<double>(seed >> 8) / 8388608.0) - 1.0);
            auto v = net.voltage(b);
            finite &= std::isfinite(v);
            peak = std::max(peak, std::abs(v));
        }
        std::printf("  peak %.3f V over 3 s of full-scale noise\n", peak);
        check(finite && peak < 2.0, "finite and bounded (a passive network cannot gain)");
        std::printf("\n");
    }

    // --- 7. Injected currents (the DK method the power stage uses). ---
    std::printf("=== Injected current: solveFree() + transferOhms() + commit() ===\n");
    {
        // A: source -> R1 -> n, with R2 and C from n to ground, and a current
        // injected into n. B: the same with the injection replaced by a huge
        // resistor Rb from a source that is driven with I*Rb - the two must agree.
        auto build = [&](NodalNetwork& net, bool injected, int& nodeOut) {
            auto n = net.addNode();
            if (injected)
                net.addResistor(NodalNetwork::source, n, 10.0e3);          // R1 (driven by a quiet source in A)
            else
                net.addResistor(NodalNetwork::ground, n, 10.0e3);          // R1 to ground; the source is the injection
            net.addResistor(n, NodalNetwork::ground, 47.0e3);              // R2
            net.addCapacitor(n, NodalNetwork::ground, 22.0e-9);            // C
            if (! injected)
                net.addResistor(NodalNetwork::source, n, 1.0e12);          // Rb
            net.prepare(fs);
            nodeOut = n;
        };

        NodalNetwork a, b;
        int na = 0, nb = 0;
        build(a, true, na);
        build(b, false, nb);

        double worst = 0.0, peak = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            auto current = 1.0e-3 * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / fs);   // 1mA at 1kHz
            a.solveFree(0.0);
            a.commit(na, current);
            b.process(current * 1.0e12);
            worst = std::max(worst, std::abs(a.voltage(na) - b.voltage(nb)));
            peak = std::max(peak, std::abs(a.voltage(na)));
        }
        std::printf("  1mA at 1kHz into a node: peak %.3f V, worst difference from the Rb model %.2e V\n", peak, worst);
        check(worst < 1.0e-6 * peak && peak > 0.1, "an injected current gives the same node voltage as a real source through a huge resistor");

        // transferOhms(): the DC value is R1 || R2 (the capacitor is an open
        // circuit at DC, but at one sample's step it counts as 2C/T).
        NodalNetwork c;
        int nc = 0;
        build(c, true, nc);
        c.solveFree(0.0);
        auto ohms = c.transferOhms(nc, nc);
        auto expected = 1.0 / (1.0 / 10.0e3 + 1.0 / 47.0e3 + 2.0 * 22.0e-9 * fs / 1.0);
        std::printf("  transfer resistance at the node: %.1f ohms (R1 || R2 || the capacitor's 1/(2C/T) = %.1f)\n", ohms, expected);
        check(std::abs(ohms - expected) < 1.0e-6 * expected, "transferOhms() is the parallel of everything on the node, capacitor as 2C/T");

        // Nothing injected leaves process() unchanged.
        NodalNetwork d, e;
        int nd = 0, ne = 0;
        build(d, true, nd);
        build(e, true, ne);
        double diff = 0.0;
        for (int i = 0; i < 5000; ++i)
        {
            auto v = std::sin(2.0 * 3.14159265358979 * 500.0 * i / fs);
            d.process(v);
            e.solveFree(v);
            e.commit(ne, 0.0);
            diff = std::max(diff, std::abs(d.voltage(nd) - e.voltage(ne)));
        }
        check(diff < 1.0e-12, "solveFree() + commit(0) is exactly process()");
        std::printf("\n");
    }

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
