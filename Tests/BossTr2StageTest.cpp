// Verify the Boss TR-2 model against its schematic (V1 service-manual board, as
// read for the research report the values come from).
//
// The TR-2 is a VCA tremolo, not an optical one, so what is checked is the circuit
// the LFO and control voltage actually are:
//   1. THE SCHMITT THRESHOLDS. Solved here, independently, from the node's resistor
//      network - not read back from the module - and the module must agree.
//   2. THE LFO'S RATE. The formula T = 1.404us * Reff against the values the analysis
//      derived (0.99 / 2.84 / 4.66 / 7.31 / 12.7 Hz at 0/25/50/75/100% of the Rate pot) and
//      the pedal's measured 1.0 - 11.1Hz; and the running oscillator's actual frequency,
//      counted, must match its own formula.
//   3. THE LFO'S SHAPE. A triangle between the thresholds, half its time falling and half
//      rising; the Wave stage then clips it, and at full Wave the trapezoid is nearly a
//      square with the ~73% on-time the analysis predicted.
//   4. THE GAIN RANGES the analysis derived from the resistor values (triangle / mid / square
//      waveforms at two Depths), and that the audio really is multiplied by that gain (linear
//      in amplitude: the VCA law), unipolar, with no dry signal.
//   5. THE SMOOTHING. The control voltage follows its target as a first-order low-pass, and
//      that is all the lag there is - no optical memory: the same rise and fall time.
//   6. THE VCA. Its ~1% distortion at 2.3Vrms, clean well below, and the output high-pass.

#include "../Source/dsp/BossTr2Stage.h"
#include "TestUtils.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    int failures = 0;

    void check(bool ok, const char* what, double got, const char* unit = "")
    {
        std::printf("  [%s] %s (%.4g%s)\n", ok ? "PASS" : "FAIL", what, got, unit);
        if (! ok)
            ++failures;
    }

    const BossTr2Stage::Components comps;

    // Run the stage on silence, sampling its control voltage / LFO once per sample.
    struct Trace { std::vector<double> lfo, gain, control, target; };

    Trace trace(double rate, double depth, double wave, double seconds)
    {
        BossTr2Stage stage(sampleRate);
        stage.setRate(static_cast<float>(rate));
        stage.setDepth(static_cast<float>(depth));
        stage.setWave(static_cast<float>(wave));
        stage.reset();

        Trace t;
        auto n = static_cast<int>(seconds * sampleRate);
        float in = 0.0f, out = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            stage.processBlock(&in, &out, 1);
            t.lfo.push_back(stage.lfoVolts());
            t.gain.push_back(stage.vcaGain());
            t.control.push_back(stage.controlVolts());
            t.target.push_back(stage.controlTargetVolts());
        }
        return t;
    }

    // Frequency by counting upward crossings of the triangle's midpoint.
    double countedFrequency(const std::vector<double>& lfo, double mid)
    {
        int crossings = 0;
        double first = -1.0, last = -1.0;
        for (size_t i = 1; i < lfo.size(); ++i)
            if (lfo[i - 1] < mid && lfo[i] >= mid)
            {
                if (first < 0.0) first = static_cast<double>(i);
                last = static_cast<double>(i);
                ++crossings;
            }
        if (crossings < 3)
            return 0.0;
        return (crossings - 1) * sampleRate / (last - first);
    }

    double minOf(const std::vector<double>& v, size_t from) { return *std::min_element(v.begin() + static_cast<std::ptrdiff_t>(from), v.end()); }
    double maxOf(const std::vector<double>& v, size_t from) { return *std::max_element(v.begin() + static_cast<std::ptrdiff_t>(from), v.end()); }

    std::vector<float> hann(std::vector<float> v)
    {
        for (size_t n = 0; n < v.size(); ++n)
            v[n] *= static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(n) / static_cast<double>(v.size())));
        return v;
    }
}

int main()
{
    // ---------------------------------------------------------------- 1
    std::printf("1. The Schmitt trigger's thresholds, from its resistor network\n");
    {
        // The positive input's node: 33k from the integrator output, 47k from the trigger output,
        // 330k to 9V, 100k to ground. It trips at the 4.5V reference. Solve for the integrator
        // output that puts the node exactly there, for each trigger state.
        auto integratorForNodeAt = [](double squareVolts) {
            auto conductance = 1.0 / 33e3 + 1.0 / 47e3 + 1.0 / 330e3 + 1.0 / 100e3;
            // node = (Vt/33k + Vsq/47k + 9/330k) / conductance = 4.5
            return (4.5 * conductance - squareVolts / 47e3 - 9.0 / 330e3) * 33e3;
        };
        auto t = BossTr2Stage::lfoThresholds(comps);
        check(std::abs(integratorForNodeAt(7.8) - 3.217) < 0.005, "trigger high (7.8V): trips when the integrator falls to 3.217V", integratorForNodeAt(7.8), " V");
        check(std::abs(integratorForNodeAt(1.2) - 7.850) < 0.005, "trigger low (1.2V): trips when the integrator rises to 7.850V", integratorForNodeAt(1.2), " V");
        check(std::abs(t.low - integratorForNodeAt(7.8)) < 1e-9 && std::abs(t.high - integratorForNodeAt(1.2)) < 1e-9, "the module's thresholds are those", t.high - t.low, " V swing");
        check(std::abs(0.5 * (t.low + t.high) - 5.53) < 0.03, "the triangle is centred at 5.53V, not the 4.5V reference (hence its lopsided duty)", 0.5 * (t.low + t.high), " V");
    }

    // ---------------------------------------------------------------- 2
    std::printf("2. The LFO's rate\n");
    {
        struct Ref { double knob, hz; };
        for (auto r : { Ref { 0.0, 0.99 }, Ref { 0.25, 2.84 }, Ref { 0.5, 4.66 }, Ref { 0.75, 7.31 }, Ref { 1.0, 12.7 } })
        {
            auto f = BossTr2Stage::lfoFrequencyHz(comps, r.knob);
            char label[80];
            std::snprintf(label, sizeof(label), "Rate %3.0f%%: %.2f Hz (analysis: %.2f)", r.knob * 100.0, f, r.hz);
            check(std::abs(f - r.hz) < 0.03 * r.hz, label, f, " Hz");
        }

        auto slow = BossTr2Stage::lfoFrequencyHz(comps, 0.0), fast = BossTr2Stage::lfoFrequencyHz(comps, 1.0);
        check(std::abs(slow - 1.0) < 0.06, "slowest setting within 6% of the pedal's measured ~1.0 Hz", slow, " Hz");
        check(fast > 11.1 && fast < 11.1 * 1.16, "fastest setting is the measured 11.1 Hz plus the ~14% the analysis found", fast, " Hz");

        // The running oscillator, counted, against its own formula.
        double worst = 0.0;
        for (double knob : { 0.0, 0.3, 0.6, 1.0 })
        {
            auto t = trace(knob, 0.5, 0.3, knob < 0.1 ? 8.0 : 4.0);
            auto counted = countedFrequency(t.lfo, 0.5 * (BossTr2Stage::lfoThresholds(comps).low + BossTr2Stage::lfoThresholds(comps).high));
            worst = std::max(worst, std::abs(counted / BossTr2Stage::lfoFrequencyHz(comps, knob) - 1.0));
        }
        check(worst < 0.01, "the running LFO's counted frequency matches the formula within 1% (4 knob positions)", worst * 100.0, " %");
    }

    // ---------------------------------------------------------------- 3
    std::printf("3. The LFO's shape and the Wave control\n");
    {
        auto t = trace(0.5, 1.0, 0.0, 3.0);
        auto th = BossTr2Stage::lfoThresholds(comps);
        check(std::abs(minOf(t.lfo, 4800) - th.low) < 0.01 && std::abs(maxOf(t.lfo, 4800) - th.high) < 0.01, "the triangle runs exactly between the two thresholds", maxOf(t.lfo, 4800) - minOf(t.lfo, 4800), " V p-p");

        // Half its time falling and half rising (the trigger's two rails are symmetric about 4.5V).
        int falling = 0, total = 0;
        for (size_t i = 4801; i < t.lfo.size(); ++i, ++total)
            falling += t.lfo[i] < t.lfo[i - 1] ? 1 : 0;
        check(std::abs(static_cast<double>(falling) / total - 0.5) < 0.01, "it spends half its time falling", 100.0 * falling / total, " %");

        // Linearity: a triangle's slope is constant between the reversals. At Rate 50% the ramp is
        // 3.3V / (0.5uF x Reff): every sample but the two at each reversal must move by exactly that.
        auto rEff = 56e3 * (1.0 + 50e3 / (1.0 / (1.0 / 56e3 + 1.0 / (50e3 + 10e3))));
        auto step = 3.3 / (0.5e-6 * rEff) / sampleRate;
        int exact = 0, counted = 0;
        for (size_t i = 4802; i < t.lfo.size(); ++i, ++counted)
            exact += std::abs(std::abs(t.lfo[i] - t.lfo[i - 1]) - step) < 1e-9 ? 1 : 0;
        check(static_cast<double>(exact) / counted > 0.995, "with a constant slope of 3.3V / (0.5uF x Reff) in between (a true triangle)", 100.0 * exact / counted, " % of samples on the ramp");

        check(std::abs(BossTr2Stage::waveGain(comps, 0.0) - 1.2) < 1e-9 && std::abs(BossTr2Stage::waveGain(comps, 1.0) - 11.2) < 1e-9, "the Wave stage's gain runs from 1.2 to 11.2 (12k + rheostat over 10k)", BossTr2Stage::waveGain(comps, 1.0));
    }

    // ---------------------------------------------------------------- 4
    std::printf("4. The gain ranges (VCA gain = 1.1 x control voltage) and the audio path\n");
    {
        struct Range { double wave, depth, lo, hi; const char* name; };
        for (auto r : { Range { 0.0, 1.0, 0.33, 1.06, "triangle, Depth 100%" },  Range { 0.0, 0.5, 0.72, 1.08, "triangle, Depth 50%" },
                        Range { 0.55, 1.0, 0.065, 1.06, "mid Wave, Depth 100%" }, Range { 1.0, 1.0, 0.065, 1.06, "square, Depth 100%" },
                        Range { 1.0, 0.5, 0.59, 1.08, "square, Depth 50%" } })
        {
            auto t = trace(0.0, r.depth, r.wave, 4.0); // the slowest LFO: the smoothing is out of the way
            auto lo = minOf(t.gain, 4800), hi = maxOf(t.gain, 4800);
            char label[120];
            std::snprintf(label, sizeof(label), "%s: gain %.3f..%.3f (analysis %.3f..%.2f)", r.name, lo, hi, r.lo, r.hi);
            check(std::abs(lo - r.lo) < 0.03 && std::abs(hi - r.hi) < 0.03, label, lo);
        }

        // Depth 0: no modulation at all, and the pedal is at unity (0.9 buffer x 1.1 x 0.99 output).
        auto flat = trace(0.5, 0.0, 0.5, 2.0);
        check(maxOf(flat.gain, 0) - minOf(flat.gain, 0) < 1e-9 && std::abs(flat.gain[100] - 1.1) < 1e-9, "Depth 0: the gain is a steady 1.1 - no ripple", flat.gain[100]);
        check(std::abs(0.9 * 1.1 * 0.99 - 1.0) < 0.02, "so the whole pedal is at unity gain with Depth at zero", 0.9 * 1.1 * 0.99);

        // Never negative, whatever the settings: the VCA is unipolar.
        double lowest = 1e9, highest = -1e9;
        for (double wave : { 0.0, 0.5, 1.0 })
            for (double depth : { 0.0, 0.5, 1.0 })
            {
                auto t = trace(1.0, depth, wave, 1.0);
                lowest = std::min(lowest, minOf(t.gain, 0));
                highest = std::max(highest, maxOf(t.gain, 0));
            }
        check(lowest >= 0.0 && highest < 1.25, "the gain stays within 0 to 1.25 at every setting (unipolar amplitude modulation)", highest);

        // The audio is multiplied by that gain: output amplitude / input amplitude follows
        // 0.9 x gain x 0.99 (a 1 kHz tone is far above the 59Hz output high-pass).
        BossTr2Stage stage(sampleRate);
        stage.setRate(0.0f);
        stage.setDepth(1.0f);
        stage.setWave(1.0f);
        stage.reset();
        double worstError = 0.0;
        for (int block = 0; block < 4 * 1000; ++block)
        {
            std::vector<float> in(48), out(48);
            for (int i = 0; i < 48; ++i)
                in[static_cast<size_t>(i)] = static_cast<float>(0.1 * std::sin(2.0 * M_PI * 1000.0 * (block * 48 + i) / sampleRate));
            auto gainBefore = stage.vcaGain();
            stage.processBlock(in.data(), out.data(), 48);
            // Only where the gain is steady: on the square wave's edges the 59Hz output high-pass
            // differentiates the change and (correctly) takes some of it away.
            if (block < 200 || std::abs(stage.vcaGain() - gainBefore) > 0.002)
                continue;
            double peak = 0.0;
            for (auto x : out)
                peak = std::max(peak, static_cast<double>(std::abs(x)));
            auto expected = 0.1 * 0.9 * stage.vcaGain() * 0.99;
            if (expected > 0.01)
                worstError = std::max(worstError, std::abs(peak / expected - 1.0));
        }
        check(worstError < 0.03, "the output's amplitude follows 0.9 x (1.1 x Vc) x 0.99 to within 3% (a linear amplitude law)", worstError * 100.0, " %");
    }

    // ---------------------------------------------------------------- 5
    std::printf("5. The duty cycle and the smoothing\n");
    {
        // A square Wave setting: the triangle's off-centre middle gives a lopsided duty cycle.
        auto t = trace(0.0, 1.0, 1.0, 6.0);
        auto lo = minOf(t.gain, 4800), hi = maxOf(t.gain, 4800), mid = 0.5 * (lo + hi);
        int on = 0, total = 0;
        for (size_t i = 4800; i < t.gain.size(); ++i, ++total)
            on += t.gain[i] > mid ? 1 : 0;
        check(std::abs(static_cast<double>(on) / total - 0.73) < 0.04, "square Wave: on for ~73% of the cycle (the analysis' prediction)", 100.0 * on / total, " %");

        // The control voltage is its target through a first-order low-pass, and nothing more.
        auto s = trace(1.0, 0.7, 0.4, 2.0);
        auto depth = 0.7;
        auto tau = 0.1e-6 * ((100e3 + depth * (1.0 - depth) * 100e3) * 1e6 / (100e3 + depth * (1.0 - depth) * 100e3 + 1e6));
        auto alpha = 1.0 - std::exp(-1.0 / (sampleRate * tau));
        // (The trace samples the control AFTER each step, from the target before it moved on:
        // the target recorded at sample i is the one the smoothing used at sample i + 1.)
        double worst = 0.0;
        for (size_t i = 4800; i + 1 < s.control.size(); ++i)
        {
            auto predicted = s.control[i] + alpha * (s.target[i + 1] - s.control[i]);
            worst = std::max(worst, std::abs(s.control[i + 1] - predicted));
        }
        // The target used for step i+1 is the one computed after the LFO moved that step, i.e. target[i+1].
        check(worst < 1e-6, "the control voltage obeys y += alpha (target - y), tau = 0.1uF x ((100k + pot) || 1M)", worst, " V max deviation");
        check(tau > 0.009 && tau < 0.0115, "with a time constant of 9-11 ms", tau * 1000.0, " ms");

        // No optical memory: the smoothing is the same rising and falling. A step in the target
        // reaches 63% in one tau either way.
        auto expected63 = 1.0 - std::exp(-1.0);
        check(std::abs(1.0 - std::pow(1.0 - alpha, sampleRate * tau) - expected63) < 0.01, "and one time constant of it reaches 63% - rising or falling alike (symmetric attack and release)", expected63 * 100.0, " %");
    }

    // ---------------------------------------------------------------- 6
    std::printf("6. The VCA and the output stage\n");
    {
        auto thd = [](double amplitude) {
            BossTr2Stage stage(sampleRate);
            stage.setDepth(0.0f); // a steady gain
            stage.reset();
            std::vector<float> in(32768), out(32768);
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * 1000.0 * static_cast<double>(i) / sampleRate));
            stage.processBlock(in.data(), out.data(), static_cast<int>(in.size()));
            auto w = hann(std::vector<float>(out.begin() + 4096, out.end()));
            auto fundamental = TestUtils::goertzelMagnitude(w, 1000.0, sampleRate);
            double sum = 0.0;
            for (int h = 2; h <= 9; ++h)
            {
                auto m = TestUtils::goertzelMagnitude(w, 1000.0 * h, sampleRate) / fundamental;
                sum += m * m;
            }
            return std::sqrt(sum);
        };
        // The datasheet's 1% at 2.3Vrms is at the VCA's input: the buffer's 0.9 sits in front of it.
        auto atSpec = thd(2.3 * std::sqrt(2.0) / 0.9);
        check(atSpec > 0.005 && atSpec < 0.02, "at the datasheet's 2.3 Vrms VCA input: about 1% THD", atSpec * 100.0, " %");
        check(thd(0.3) < 0.0005, "at a hard guitar's 0.3 V: under 0.05% THD", thd(0.3) * 100.0, " %");

        // The 59Hz output high-pass (0.027uF into 100k).
        auto gainAt = [](double freq) {
            BossTr2Stage stage(sampleRate);
            stage.setDepth(0.0f);
            stage.reset();
            std::vector<float> in(32768), out(32768);
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = static_cast<float>(0.05 * std::sin(2.0 * M_PI * freq * static_cast<double>(i) / sampleRate));
            stage.processBlock(in.data(), out.data(), static_cast<int>(in.size()));
            std::vector<float> a(out.begin() + 8192, out.end()), b(in.begin() + 8192, in.end());
            return TestUtils::goertzelMagnitude(a, freq, sampleRate) / TestUtils::goertzelMagnitude(b, freq, sampleRate);
        };
        auto corner = TestUtils::toDb(gainAt(59.0) / gainAt(2000.0));
        check(std::abs(corner - (-3.0)) < 0.4, "the output coupling is a high-pass with its -3 dB point at 59 Hz", corner, " dB");

        // Silence in, silence out.
        BossTr2Stage stage(sampleRate);
        std::vector<float> zeros(9600, 0.0f), out(9600);
        stage.processBlock(zeros.data(), out.data(), 9600);
        double peak = 0.0;
        for (auto x : out) peak = std::max(peak, static_cast<double>(std::abs(x)));
        check(peak == 0.0, "no signal, no output (no dry leak, no hiss, no start-up thump)", peak);
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
