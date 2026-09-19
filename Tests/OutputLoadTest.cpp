// Verify OutputLoad - the output transformer and speaker as one per-sample
// linear element - against things worked out independently:
//
//  - the speaker's impedance, measured by driving a current through it and
//    dividing the voltage by it (a lock-in at each frequency), matches
//    AmpSpeakerLoad::speakerImpedance() - the project's own analytic
//    Thiele-Small curve, evaluated in the frequency domain - in magnitude and
//    phase, at both an 8 and a 16 ohm nominal cabinet: the resonance peak near
//    fs, the minimum above it, and the inductive rise;
//  - the transformer's series winding resistance and leakage inductance appear
//    in the INTERNAL voltage but not at the speaker terminals, divided by a^2;
//  - the magnetizing inductance draws V / (2 pi f L) - the bass roll-off;
//  - the turns ratio is sqrt(Zaa / tap), so a lower tap is a bigger ratio and
//    the tubes see the speaker's impedance times a^2/4 each;
//  - the load is passive at every frequency (a positive real part).

#include "../Source/dsp/OutputLoad.h"

#include <complex>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    using Cx = std::complex<double>;
    constexpr double fs = 192000.0; // the rate the output stage runs at

    Cx tone(const std::vector<double>& x, double f)
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

    // Drive a 1A sine of current through the load; return terminal and internal
    // voltage phasors per amp.
    struct Measured { Cx terminal, internal; };
    Measured impedance(OutputLoad& load, double f)
    {
        load.reset();
        load.rebuildStep();
        auto settle = static_cast<int>(0.4 * fs);
        auto n = static_cast<int>(std::max(30.0 * fs / f, 0.1 * fs));
        std::vector<double> in(static_cast<size_t>(n)), vt(static_cast<size_t>(n)), vi(static_cast<size_t>(n));
        for (int k = -settle; k < n; ++k)
        {
            auto i = std::sin(2.0 * M_PI * f * (k + settle) / fs);
            auto term = load.terminalD() * i + load.terminalH();
            auto internal = load.internalD() * i + load.internalH();
            load.commit(i, 0.0);
            if (k >= 0)
            {
                in[static_cast<size_t>(k)] = i;
                vt[static_cast<size_t>(k)] = term;
                vi[static_cast<size_t>(k)] = internal;
            }
        }
        auto ref = tone(in, f);
        return { tone(vt, f) / ref, tone(vi, f) / ref };
    }

    AmpSpeakerLoad::Speaker scaled(AmpSpeakerLoad::Speaker s, double nominalOhms)
    {
        s.re *= nominalOhms / 8.0;
        s.le *= nominalOhms / 8.0;
        return s;
    }
}

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    // A load with no transformer parasitics, to isolate the speaker.
    OutputLoad::Transformer bare;
    bare.primaryOhms = 0.0;
    bare.leakageHenries = 1.0e-12;
    bare.magnetizingHenries = 1.0e9;

    std::printf("=== The speaker's impedance vs the analytic Thiele-Small curve ===\n");
    for (double nominal : { 8.0, 16.0 })
    {
        OutputLoad load;
        AmpSpeakerLoad::Speaker sp;
        load.prepare(fs);
        load.setTransformer(bare);
        load.setSpeaker(sp, nominal);

        double worstMag = 0.0, worstPhase = 0.0, peak = 0.0, minimum = 1.0e9;
        double peakAt = 0.0;
        for (double f : { 20.0, 50.0, 75.0, 85.0, 100.0, 150.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0 })
        {
            auto z = impedance(load, f).terminal;
            auto ref = AmpSpeakerLoad::speakerImpedance(scaled(sp, nominal), f);
            worstMag = std::max(worstMag, std::abs(std::abs(z) / std::abs(ref) - 1.0));
            worstPhase = std::max(worstPhase, std::abs(std::arg(z) - std::arg(ref)) * 180.0 / M_PI);
            // The resonance is the peak below 250Hz; the minimum is what the
            // impedance falls to above it, before the voice coil's inductance
            // lifts it again.
            if (f <= 250.0 && std::abs(z) > peak) { peak = std::abs(z); peakAt = f; }
            if (f >= 250.0 && f <= 1000.0) minimum = std::min(minimum, std::abs(z));
        }
        std::printf("  %.0f-ohm cab: worst magnitude error %.3f%%, worst phase error %.2f deg; peak %.1f ohms near %.0f Hz, minimum in 250Hz-1kHz %.1f ohms\n",
                     nominal, worstMag * 100.0, worstPhase, peak, peakAt, minimum);
        check(worstMag < 0.01 && worstPhase < 1.0, "matches the analytic impedance to 1% and 1 degree from 20Hz to 10kHz");
        check(peak > 3.0 * minimum && peakAt >= 75.0 && peakAt <= 100.0, "shows the resonance peak (near fs, > 3x the mid-band minimum)");
    }
    std::printf("\n");

    std::printf("=== Real part positive (passive) ===\n");
    {
        OutputLoad load;
        load.prepare(fs);
        load.setSpeaker(AmpSpeakerLoad::Speaker {}, 16.0);
        double minReal = 1.0e9;
        for (double f : { 20.0, 60.0, 90.0, 200.0, 800.0, 3000.0, 9000.0, 15000.0 })
            minReal = std::min(minReal, impedance(load, f).terminal.real());
        std::printf("  smallest real part over 8 frequencies: %.2f ohms\n", minReal);
        check(minReal > 0.0, "the load never returns power");
    }
    std::printf("\n");

    std::printf("=== A resistive reference load ===\n");
    {
        OutputLoad load;
        AmpSpeakerLoad::Speaker sp;
        sp.resistive = true;
        sp.re = 8.0;
        load.prepare(fs);
        load.setTransformer(bare);
        load.setSpeaker(sp, 8.0);
        auto lo = impedance(load, 50.0).terminal, hi = impedance(load, 8000.0).terminal;
        std::printf("  |Z| at 50Hz %.3f, at 8kHz %.3f, phase %.2f / %.2f deg\n", std::abs(lo), std::abs(hi), std::arg(lo) * 57.29578, std::arg(hi) * 57.29578);
        check(std::abs(std::abs(lo) - 8.0) < 0.01 && std::abs(std::abs(hi) - 8.0) < 0.01 && std::abs(std::arg(hi)) < 0.01, "a flat 8 ohms at every frequency, no phase");
    }
    std::printf("\n");

    std::printf("=== Transformer series resistance and leakage, and the turns ratio ===\n");
    {
        OutputLoad::Transformer t;
        t.primaryOhms = 300.0;
        t.leakageHenries = 0.03;
        t.magnetizingHenries = 1.0e9;
        OutputLoad load;
        load.prepare(fs);
        load.setTransformer(t);
        load.setSpeaker(AmpSpeakerLoad::Speaker {}, 16.0);
        load.setTap(16.0);

        auto a = load.turnsRatio();
        std::printf("  turns ratio at the 16 ohm tap %.3f (sqrt(1700/16) = %.3f)\n", a, std::sqrt(1700.0 / 16.0));
        check(std::abs(a - std::sqrt(1700.0 / 16.0)) < 1.0e-9, "the ratio is sqrt(Zaa / tap)");

        double worst = 0.0;
        for (double f : { 100.0, 1000.0, 8000.0 })
        {
            auto m = impedance(load, f);
            auto expected = m.terminal + Cx(t.primaryOhms / (a * a), 2.0 * M_PI * f * t.leakageHenries / (a * a));
            worst = std::max(worst, std::abs(m.internal - expected) / std::abs(expected));
        }
        std::printf("  internal - terminal = R/a^2 + j w L/a^2: worst relative error %.4f%%\n", worst * 100.0);
        check(worst < 0.005, "the winding resistance and leakage sit between the internal node and the terminals, / a^2");

        load.setTap(4.0);
        std::printf("  at the 4 ohm tap the ratio is %.3f (twice the 16 ohm tap's)\n", load.turnsRatio());
        check(std::abs(load.turnsRatio() / a - 2.0) < 1.0e-9, "a quarter of the impedance is twice the turns ratio");
    }
    std::printf("\n");

    std::printf("=== The magnetizing inductance ===\n");
    {
        OutputLoad::Transformer t;
        t.magnetizingHenries = 8.0;
        OutputLoad load;
        load.prepare(fs);
        load.setTransformer(t);
        load.setSpeaker(AmpSpeakerLoad::Speaker {}, 16.0);
        load.setTap(16.0);
        auto a = load.turnsRatio();
        auto lSecondary = t.magnetizingHenries / (a * a);

        double worst = 0.0;
        for (double f : { 30.0, 60.0, 200.0, 1000.0 })
        {
            // A sine of amplitude 10V across the internal node.
            load.reset();
            auto settle = static_cast<int>(0.3 * fs);
            auto n = static_cast<int>(std::max(30.0 * fs / f, 0.1 * fs));
            std::vector<double> x(static_cast<size_t>(n)), im(static_cast<size_t>(n));
            for (int k = -settle; k < n; ++k)
            {
                auto v = 10.0 * std::sin(2.0 * M_PI * f * (k + settle) / fs);
                auto i = load.magnetizingCurrent(v);
                load.commit(0.0, v);
                if (k >= 0) { x[static_cast<size_t>(k)] = v; im[static_cast<size_t>(k)] = i; }
            }
            auto ratio = std::abs(tone(im, f) / tone(x, f));           // amps per volt
            auto expected = 1.0 / (2.0 * M_PI * f * lSecondary);
            worst = std::max(worst, std::abs(ratio / expected - 1.0));
        }
        std::printf("  magnetizing current = V / (w L_secondary): worst error %.3f%% over 30Hz-1kHz\n", worst * 100.0);
        check(worst < 0.01, "the magnetizing branch draws V/(wL), a -6dB/octave bass shunt");
    }
    std::printf("\n");

    std::printf("=== Core saturation ===\n");
    {
        OutputLoad::Transformer t = bare;
        t.magnetizingHenries = 8.0;
        t.saturationVoltSeconds = 1.0;   // primary-referred
        OutputLoad load;
        load.prepare(fs);
        load.setTransformer(t);
        load.setSpeaker(AmpSpeakerLoad::Speaker {}, 16.0);
        load.setTap(16.0);
        auto a = load.turnsRatio();
        auto lSecondary = t.magnetizingHenries / (a * a);
        auto fluxSat = t.saturationVoltSeconds / a;

        // A 60Hz sine whose peak flux is `fraction` of the saturation flux: returns
        // the peak magnetizing current relative to the linear V/(wL) peak.
        auto peakRatio = [&](double fraction) {
            auto f = 60.0;
            auto amplitude = fraction * fluxSat * 2.0 * M_PI * f;
            load.reset();
            auto settle = static_cast<int>(0.05 * fs), n = static_cast<int>(3.0 * fs / f);
            double peak = 0.0;
            for (int k = -settle; k < n; ++k)
            {
                // A cosine: the flux (its integral) is then a pure sine with no DC offset.
                auto v = amplitude * std::cos(2.0 * M_PI * f * (k + settle) / fs);
                auto i = load.magnetizingCurrent(v);
                load.commit(0.0, v);
                if (k >= 0) peak = std::max(peak, std::abs(i));
            }
            return peak / (fraction * fluxSat / lSecondary);
        };
        auto low = peakRatio(0.3), high = peakRatio(1.5);
        std::printf("  peak magnetizing current vs the linear inductor's: %.3fx at 0.3 of the saturation flux, %.1fx at 1.5\n", low, high);
        check(low < 1.02, "well below saturation the core is a plain inductor (within 2%)");
        check(std::abs(high - (1.0 + std::pow(1.5, 4.0))) < 0.15, "past saturation the current follows the characteristic 1 + (flux/saturation)^4 (6.06x at 1.5)");
    }
    std::printf("\n");

    std::printf("=== The Zobel shunt and the winding capacitance ===\n");
    {
        // A Zobel (R in series with C) across the terminals: the load becomes
        // Z_speaker || (R + 1/jwC).
        OutputLoad load;
        AmpSpeakerLoad::Speaker sp;
        load.prepare(fs);
        load.setTransformer(bare);
        load.setSpeaker(sp, 16.0);
        load.setZobel(16.0, 3.0e-6);
        double worst = 0.0;
        for (double f : { 100.0, 1000.0, 3000.0, 8000.0, 16000.0 })
        {
            auto z = impedance(load, f).terminal;
            auto zs = AmpSpeakerLoad::speakerImpedance(scaled(sp, 16.0), f);
            auto zz = Cx(16.0, -1.0 / (2.0 * M_PI * f * 3.0e-6));
            auto expected = zs * zz / (zs + zz);
            worst = std::max(worst, std::abs(z - expected) / std::abs(expected));
        }
        std::printf("  Zobel (16 ohm + 3uF) across a 16 ohm cabinet: worst error vs Z_speaker || Z_zobel %.3f%%\n", worst * 100.0);
        check(worst < 0.01, "the terminal impedance is the speaker in parallel with the Zobel branch");

        auto lowF = std::abs(impedance(load, 1000.0).terminal), highF = std::abs(impedance(load, 16000.0).terminal);
        OutputLoad plain;
        plain.prepare(fs);
        plain.setTransformer(bare);
        plain.setSpeaker(sp, 16.0);
        auto plainHigh = std::abs(impedance(plain, 16000.0).terminal);
        std::printf("  at 16kHz the load is %.0f ohms with the Zobel and %.0f ohms without (the voice coil's inductance alone)\n", highF, plainHigh);
        check(highF < 0.4 * plainHigh, "the Zobel holds the load down where the voice coil's inductance would run it up");
        (void) lowF;

        // The winding capacitance draws w * C * a^2 * V at the internal node.
        OutputLoad::Transformer t = bare;
        t.windingFarads = 500.0e-12;
        OutputLoad wound;
        wound.prepare(fs);
        wound.setTransformer(t);
        wound.setSpeaker(sp, 16.0);
        wound.setTap(16.0);
        auto a = wound.turnsRatio();
        double worstC = 0.0;
        for (double f : { 1000.0, 5000.0, 20000.0 })
        {
            wound.reset();
            auto settle = static_cast<int>(0.05 * fs);
            auto n = static_cast<int>(std::max(30.0 * fs / f, 0.1 * fs));
            std::vector<double> x(static_cast<size_t>(n)), ic(static_cast<size_t>(n));
            for (int k = -settle; k < n; ++k)
            {
                auto v = 10.0 * std::sin(2.0 * M_PI * f * (k + settle) / fs);
                auto i = wound.capacitiveCurrent(v);
                wound.commit(0.0, v);
                if (k >= 0) { x[static_cast<size_t>(k)] = v; ic[static_cast<size_t>(k)] = i; }
            }
            auto ratio = std::abs(tone(ic, f) / tone(x, f));
            auto expected = 2.0 * fs * std::tan(M_PI * f / fs) * t.windingFarads * a * a;   // the trapezoidal rule's own (warped) w
            worstC = std::max(worstC, std::abs(ratio / expected - 1.0));
        }
        std::printf("  winding capacitance current = w' C a^2 V (w' the trapezoid's warped frequency): worst error %.3f%% over 1-20kHz\n", worstC * 100.0);
        check(worstC < 0.005, "the winding capacitance, seen from the secondary, is a^2 times the primary's");
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
