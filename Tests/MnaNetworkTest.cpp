// Verify MnaNetwork - the time-domain modified-nodal solver with transconductances
// and ideal op-amps - against circuits whose answers are known in closed form:
// an RC low-pass (the capacitor's trapezoidal companion model), a transconductance
// amplifier's DC gain, and a non-inverting op-amp whose ground leg contains a
// capacitor (the same shape as a Tube Screamer's gain stage), whose response
// 1 + Rf / (R + 1/sC) is checked across the audio band.

#include "../Source/dsp/MnaNetwork.h"
#include "TestUtils.h"

#include <complex>
#include <cstdio>

namespace
{
    constexpr double sampleRate = 192000.0;
    int failures = 0;

    void check(bool ok, const char* what, double got, const char* unit = "")
    {
        std::printf("  [%s] %s (%.4g%s)\n", ok ? "PASS" : "FAIL", what, got, unit);
        if (! ok)
            ++failures;
    }

    // Gain at one frequency of a network's node, output/input measured the same way.
    double gainDbAt(MnaNetwork& net, int node, double freq)
    {
        constexpr int n = 65536, settle = 16384;
        net.reset();
        std::vector<float> in(n), out(n);
        for (int i = 0; i < n; ++i)
        {
            auto x = 0.001 * std::sin(2.0 * M_PI * freq * i / sampleRate);
            net.process(x);
            in[static_cast<size_t>(i)] = static_cast<float>(x);
            out[static_cast<size_t>(i)] = static_cast<float>(net.voltage(node));
        }
        std::vector<float> a(out.begin() + settle, out.end()), b(in.begin() + settle, in.end());
        return TestUtils::toDb(TestUtils::goertzelMagnitude(a, freq, sampleRate) / TestUtils::goertzelMagnitude(b, freq, sampleRate));
    }
}

int main()
{
    std::printf("1. RC low-pass: the trapezoidal capacitor gives the analog response\n");
    {
        MnaNetwork net;
        auto out = net.addNode();
        net.addResistor(MnaNetwork::source, out, 10.0e3);
        net.addCapacitor(out, MnaNetwork::ground, 10.0e-9);
        net.prepare(sampleRate);

        double worst = 0.0;
        for (double f : { 100.0, 500.0, 1590.0, 3000.0, 8000.0 })
        {
            auto analytic = TestUtils::toDb(std::abs(1.0 / std::complex<double>(1.0, 2.0 * M_PI * f * 10.0e3 * 10.0e-9)));
            worst = std::max(worst, std::abs(gainDbAt(net, out, f) - analytic));
        }
        // The bilinear transform warps frequency by tan(x)/x (0.57% at 8 kHz on a 192 kHz
        // clock), which on a low-pass slope is worth up to ~0.05 dB - the bound below.
        check(worst < 0.06, "within 0.06 dB (the bilinear warp) of 1/(1+sRC), 100 Hz - 8 kHz", worst, " dB max error");
    }

    std::printf("2. Transconductance amplifier: v(out) = -gm * RL * v(in)\n");
    {
        MnaNetwork net;
        auto out = net.addNode();
        net.addResistor(out, MnaNetwork::ground, 5.0e3);
        // gm*v(source) leaves `out` and goes to ground.
        net.addTransconductance(out, MnaNetwork::ground, MnaNetwork::source, MnaNetwork::ground, 2.0e-3);
        net.prepare(sampleRate);
        net.process(0.1);
        check(std::abs(net.voltage(out) - (-1.0)) < 1.0e-6, "gm 2 mS into 5k: -10x", net.voltage(out), " V for 0.1 V in");
    }

    std::printf("3. Ideal op-amp, non-inverting, capacitor in the ground leg\n");
    {
        constexpr double r = 4.7e3, c = 0.047e-6, rf = 51.0e3;
        MnaNetwork net;
        auto inverting = net.addNode(), middle = net.addNode(), out = net.addNode();
        net.addResistor(out, inverting, rf);
        net.addResistor(inverting, middle, r);
        net.addCapacitor(middle, MnaNetwork::ground, c);
        net.addIdealOpAmp(MnaNetwork::source, inverting, out);
        net.prepare(sampleRate);

        double worst = 0.0;
        for (double f : { 50.0, 200.0, 720.0, 2000.0, 6000.0 })
        {
            auto s = std::complex<double>(0.0, 2.0 * M_PI * f);
            auto analytic = TestUtils::toDb(std::abs(1.0 + rf / (r + 1.0 / (s * c))));
            worst = std::max(worst, std::abs(gainDbAt(net, out, f) - analytic));
        }
        check(worst < 0.03, "within 0.03 dB of 1 + Rf/(R + 1/sC), 50 Hz - 6 kHz", worst, " dB max error");
        check(std::abs(gainDbAt(net, out, 20000.0) - TestUtils::toDb(1.0 + rf / r)) < 0.3, "plateau at 1 + Rf/R = 21.5 dB", gainDbAt(net, out, 20000.0), " dB");
    }

    std::printf("4. Op-amp inverting integrator, and a value change re-solves\n");
    {
        MnaNetwork net;
        auto minus = net.addNode(), out = net.addNode();
        auto rin = net.addResistor(MnaNetwork::source, minus, 10.0e3);
        net.addCapacitor(minus, out, 100.0e-9);
        net.addIdealOpAmp(MnaNetwork::ground, minus, out);
        net.prepare(sampleRate);

        // |H| = 1/(w R C): 1 kHz -> 0.159; doubling R halves it.
        auto expected = [](double f, double rr) { return TestUtils::toDb(1.0 / (2.0 * M_PI * f * rr * 100.0e-9)); };
        check(std::abs(gainDbAt(net, out, 1000.0) - expected(1000.0, 10.0e3)) < 0.05, "integrator gain at 1 kHz", gainDbAt(net, out, 1000.0), " dB");
        net.setResistance(rin, 20.0e3);
        net.refresh();
        check(std::abs(gainDbAt(net, out, 1000.0) - expected(1000.0, 20.0e3)) < 0.05, "after doubling R the gain drops 6 dB", gainDbAt(net, out, 1000.0), " dB");
    }

    std::printf("5. Supply rails, current injection and DC capture (the DK-method interface)\n");
    {
        // A 9V rail through 10k and 10k to ground: a 4.5V bias node, plus a capacitor into a second node
        // that a 1k resistor holds at 0V. Open-capacitor DC analysis finds 4.5V / 0V; after capturing,
        // the closed-capacitor circuit must simply stay there (no start-up thump).
        MnaNetwork net;
        auto a = net.addNode(), b = net.addNode();
        net.addBiasResistor(a, 10.0e3, 9.0);
        net.addResistor(a, MnaNetwork::ground, 10.0e3);
        net.addCapacitor(a, b, 1.0e-6);
        net.addResistor(b, MnaNetwork::ground, 1.0e3);
        net.prepare(sampleRate);

        net.setCapacitorsOpen(true);
        net.solveFree(0.0);
        check(std::abs(net.voltage(a) - 4.5) < 1.0e-6 && std::abs(net.voltage(b)) < 1.0e-6, "open-capacitor DC solution: 4.5 V and 0 V", net.voltage(a), " V");
        net.captureCapacitorVoltages();
        net.setCapacitorsOpen(false);

        double drift = 0.0;
        for (int i = 0; i < 2000; ++i)
        {
            net.process(0.0);
            drift = std::max(drift, std::max(std::abs(net.voltage(a) - 4.5), std::abs(net.voltage(b))));
        }
        check(drift < 1.0e-6, "started charged to that operating point, nothing moves", drift, " V");

        // A current injected into a node reads back through the transfer resistance.
        net.solveFree(0.0);
        auto before = net.voltage(a);
        auto transfer = net.inverseEntry(a, a);
        net.injectCurrent(a, 1.0e-4);
        check(std::abs((net.voltage(a) - before) - transfer * 1.0e-4) < 1.0e-9, "injecting 0.1 mA moves the node by transferResistance * 0.1 mA", net.voltage(a) - before, " V");
        // 10k || 10k = 5k, in parallel with the 1k branch behind the capacitor's companion resistance 1/(2C/T) = 2.6 ohms.
        auto expected = 1.0 / (1.0 / 5.0e3 + 1.0 / (1.0e3 + 1.0 / (2.0 * 1.0e-6 * sampleRate)));
        check(std::abs(transfer - expected) < 1.0, "and that transfer resistance is 5k || (1k + the capacitor's 2.6 ohm companion)", transfer, " ohms");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
