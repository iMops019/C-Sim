#pragma once

#include <cmath>

#include "AmpSpeakerLoad.h"

// A push-pull output transformer and the speaker it drives, as one linear
// element the output stage can solve against, sample by sample.
//
// THE TRANSFORMER, secondary-referred. Ideal turns ratio a = sqrt(Zaa / Ztap)
// (Zaa is the plate-to-plate primary impedance, Ztap the tap the speaker is
// wired to), so the tubes see the speaker's impedance times a^2 / 4 each. On
// top of the ideal transformer: the winding resistance and leakage inductance
// in series with the load (divided by a^2 to bring them to the secondary), and
// the primary's magnetizing inductance across it (divided by a^2 too) - the
// bass roll-off, and the reason a real transformer's low end saturates first.
//
// THE SPEAKER is the standard Thiele-Small electrical model already used by
// AmpSpeakerLoad.h: voice-coil Re + s*Le in series with the cone's mechanical
// resonance as a parallel RLC (Res, Cmes, Lces) - a resonant impedance PEAK
// near fs and an inductive rise above it. That curve is what lets the amp's
// negative feedback, taken from the speaker terminals, react to the speaker.
// Its 8-ohm-based numbers are scaled to whatever nominal impedance the cabinet
// is (a 4x12 of 16-ohm speakers wired 2 series x 2 parallel is 16 ohms): every
// impedance scales together, the resonance frequency and Qs don't move.
//
// DISCRETE TIME. Each reactive element uses the trapezoidal rule, so the
// digital impedance is the analog one at audio frequencies (run it at the
// oversampled rate). What makes it usable inside a nonlinear solve is that, at
// every step, the internal and terminal voltages are AFFINE in the load
// current i of that step:
//       v_internal = D * i + H        v_terminal = Dt * i + Ht
// with D and H depending only on the past. The output stage iterates on
// v_internal alone; commit() then advances the states.
//
// Framework-agnostic (no JUCE), so the physics is testable in isolation.
class OutputLoad
{
public:
    struct Transformer
    {
        // ~1.7k plate-to-plate for a 100W 4xEL34 Marshall (a forum-reported
        // figure for the 1959), so an 8-ohm tap is a = 14.6.
        double zaaOhms = 1700.0;
        // Judgment calls: total primary winding resistance, leakage inductance
        // (a treble roll-off near 18kHz into the nominal load) and magnetizing
        // inductance (a bass corner near 30Hz).
        double primaryOhms = 100.0;
        double leakageHenries = 0.015;
        double magnetizingHenries = 12.0;
        // Capacitance across the whole primary (plate to plate), which the
        // secondary sees multiplied by a^2. With the leakage inductance it is the
        // transformer's treble roll-off and ring; and in discrete time it is
        // what keeps the load finite at Nyquist, where the trapezoidal rule makes
        // any inductor an infinite impedance. 0 = none (the isolated tests).
        double windingFarads = 0.0;
        // The core saturates: the volt-seconds (primary-referred, across the whole
        // primary) it can carry before the magnetizing current climbs steeply. At
        // full power a 100W amp's primary swings ~600V, so 3.1 V*s begins to
        // saturate near 30Hz: bass drive, not treble, runs a real transformer out
        // of core. Without it the model's magnetizing inductance is a perfect
        // integrator and hard, unbalanced (blocking) drive winds up an unbounded
        // flux that later dumps into the speaker as huge offsets. 0 = linear.
        double saturationVoltSeconds = 0.0;
    };

    void prepare(double sampleRateHz)
    {
        period = 1.0 / sampleRateHz;
        rebuild();
        reset();
    }

    void setTransformer(const Transformer& t) { transformer = t; rebuild(); }

    // The connected cabinet: its 8-ohm-based speaker model, scaled to nominalOhms.
    void setSpeaker(const AmpSpeakerLoad::Speaker& s, double nominalOhms)
    {
        speaker = s;
        nominal = nominalOhms;
        rebuild();
    }

    // The transformer tap the speaker is wired to (4, 8 or 16 ohms).
    void setTap(double tapOhms) { tap = tapOhms; rebuild(); }

    // An R-C shunt across the speaker's terminals (a Zobel network). Above its
    // corner it makes the load a plain resistor, which is what stops a tube pair
    // (nearly a current source) from developing more and more voltage as the
    // voice coil's inductance climbs - the loop-gain rise that makes a feedback
    // amp ring at ultrasonic frequencies. 0 ohms / 0 farads = none.
    void setZobel(double ohms, double farads) { zobelOhms = ohms; zobelFarads = farads; rebuild(); }

    void reset() noexcept
    {
        iPrev = iSpkPrev = vLePrev = vLsPrev = vmPrev = iLcPrev = 0.0;
        vZobel = izPrev = 0.0;
        flux = xPrev = iCapPrev = 0.0;
    }

    double turnsRatio() const noexcept { return ratio; }

    // v_internal (the transformer's secondary, ahead of its own resistance and
    // leakage) and v_terminal (the speaker's terminals, where the feedback is
    // taken) as affine functions of the load current this step.
    double internalD() const noexcept { return dInt; }
    double internalH() const noexcept { return hInt; }
    double terminalD() const noexcept { return dTerm; }
    double terminalH() const noexcept { return hTerm; }

    // The current the magnetizing inductance draws from the internal node, given
    // this step's internal voltage x (trapezoidal flux, then the core's
    // characteristic: linear, with a steep rise past saturation); and its slope
    // d(i_mag)/dx at that x.
    double fluxAt(double x) const noexcept { return flux + 0.5 * period * (x + xPrev); }
    double magnetizingCurrent(double x) const noexcept
    {
        auto phi = fluxAt(x);
        return phi / lMagSecondary * (1.0 + saturationTerm(phi));
    }
    double magnetizingSlope(double x) const noexcept
    {
        auto phi = fluxAt(x);
        return 0.5 * period / lMagSecondary * (1.0 + 5.0 * saturationTerm(phi));
    }

    // The winding capacitance's current at internal voltage x, and the two shunts
    // together (what the output stage subtracts from the tubes' current to get
    // the load current), with their slope d(shunt)/dx.
    double capacitiveCurrent(double x) const noexcept { return gCap * (x - xPrev) - iCapPrev; }
    double shuntCurrent(double x) const noexcept { return magnetizingCurrent(x) + capacitiveCurrent(x); }
    double shuntSlope(double x) const noexcept { return magnetizingSlope(x) + gCap; }

    // The current i out of the secondary and the internal voltage x this step
    // settled on.
    void commit(double i, double x) noexcept
    {
        auto vTerm = dTerm * i + hTerm;
        auto iz = gZobel * vTerm - hZobel;
        auto iSpk = i - iz;

        auto vLe = gLe * (iSpk - iSpkPrev) - vLePrev;
        auto vLs = gLs * (i - iPrev) - vLsPrev;
        if (! speaker.resistive)
        {
            vLePrev = vLe;
            // Mechanical branch, from the affine form used in rebuildStep().
            auto vm = mechD * iSpk + mechH;
            iLcPrev += invLcTwo * (vm + vmPrev);
            vmPrev = vm;
        }
        vLsPrev = vLs;
        iPrev = i;
        iSpkPrev = iSpk;
        if (gZobel > 0.0)
        {
            vZobel += zobelHalfStep * (iz + izPrev);
            izPrev = iz;
        }
        iCapPrev = capacitiveCurrent(x);
        flux = fluxAt(x);
        xPrev = x;
        rebuildStep();
    }

    // Refresh the affine terms; called by commit() and by the setters.
    void rebuildStep() noexcept
    {
        hZobel = gZobel * (vZobel + zobelHalfStep * izPrev);
        double mechDirect = 0.0, mechHistory = 0.0;
        if (! speaker.resistive)
        {
            // Parallel RLC driven by current i: trapezoid on
            //   C dv/dt = i - v/R - iL,   L diL/dt = v
            auto g = 2.0 * cmes / period;
            auto denom = g + 1.0 / res + period / (2.0 * lces);
            mechD = 1.0 / denom;
            auto k0 = iSpkPrev + (g - 1.0 / res - period / (2.0 * lces)) * vmPrev - 2.0 * iLcPrev;
            mechH = k0 * mechD;
            mechDirect = mechD;
            mechHistory = mechH;
        }

        // The speaker alone: v = dSpk * (its current) + hSpk.
        auto dSpk = reScaled + gLe + mechDirect;
        auto hSpk = -gLe * iSpkPrev - vLePrev + mechHistory;

        // The Zobel shunt takes gZobel * v - hZobel of the secondary's current i,
        // so the speaker gets i - that:  v = dSpk (i - gZobel v + hZobel) + hSpk.
        auto denom = 1.0 + dSpk * gZobel;
        dTerm = dSpk / denom;
        hTerm = (dSpk * hZobel + hSpk) / denom;
        dInt = dTerm + rSecondary + gLs;
        hInt = hTerm - gLs * iPrev - vLsPrev;
    }

private:
    // (flux / saturation flux)^4: 0 for a linear core.
    double saturationTerm(double phi) const noexcept
    {
        if (fluxSaturationSecondary <= 0.0)
            return 0.0;
        auto r = phi / fluxSaturationSecondary;
        auto r2 = r * r;
        return r2 * r2;
    }

    void rebuild() noexcept
    {
        ratio = std::sqrt(transformer.zaaOhms / tap);
        auto k = nominal / 8.0;

        reScaled = speaker.re * k;
        auto leScaled = speaker.resistive ? 0.0 : speaker.le * k;
        gLe = 2.0 * leScaled / period;

        // Res = Re Qms/Qes, Cmes = Qes/(2 pi fs Re), Lces = Re/(2 pi fs Qes), on the scaled Re.
        auto w = 2.0 * AmpSpeakerLoad::pi * speaker.fs;
        res = reScaled * speaker.qms / speaker.qes;
        cmes = speaker.qes / (w * reScaled);
        lces = reScaled / (w * speaker.qes);
        invLcTwo = period / (2.0 * lces);

        auto a2 = ratio * ratio;
        rSecondary = transformer.primaryOhms / a2;
        gLs = 2.0 * (transformer.leakageHenries / a2) / period;
        lMagSecondary = transformer.magnetizingHenries / a2;
        fluxSaturationSecondary = transformer.saturationVoltSeconds / ratio;
        gCap = 2.0 * (transformer.windingFarads * a2) / period;

        zobelHalfStep = zobelFarads > 0.0 ? period / (2.0 * zobelFarads) : 0.0;
        gZobel = (zobelOhms > 0.0 || zobelFarads > 0.0) ? 1.0 / (zobelOhms + zobelHalfStep) : 0.0;

        rebuildStep();
    }

    AmpSpeakerLoad::Speaker speaker;
    Transformer transformer;
    double nominal = 16.0, tap = 16.0;
    double period = 1.0 / 192000.0;

    double ratio = 10.0;
    double reScaled = 13.0, gLe = 0.0, res = 1.0, cmes = 1.0, lces = 1.0, invLcTwo = 0.0;
    double rSecondary = 0.0, gLs = 0.0, gCap = 0.0;
    double lMagSecondary = 1.0, fluxSaturationSecondary = 0.0;

    double zobelOhms = 0.0, zobelFarads = 0.0, gZobel = 0.0, zobelHalfStep = 0.0, hZobel = 0.0, vZobel = 0.0, izPrev = 0.0;
    double iPrev = 0.0, iSpkPrev = 0.0, vLePrev = 0.0, vLsPrev = 0.0, vmPrev = 0.0, iLcPrev = 0.0;
    double flux = 0.0, xPrev = 0.0, iCapPrev = 0.0;
    double mechD = 0.0, mechH = 0.0;
    double dInt = 0.0, hInt = 0.0, dTerm = 0.0, hTerm = 0.0;
};
