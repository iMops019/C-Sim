#pragma once

#include <complex>

// How an amp's input impedance changes the sound of the guitar plugged into it,
// as a correction filter for a signal that has ALREADY been through a pickup, a
// cable and the input of a recording interface.
//
// A pickup is a voltage source (the string's induced EMF) in series with its
// inductance L and DC resistance R, with its winding capacitance Cp across the
// coil. Add the cable's capacitance Cl and whatever loads the output - the
// guitar's own volume and tone pots in parallel with the amp's input resistance
// Rl - and the transfer function into that load is a resonant low-pass:
//
//     H(s) = 1 / ( s^2 L Ct + s (R Ct + L/Rl) + (1 + R/Rl) ),   Ct = Cp + Cl
//
// Its resonance sits around 2-4 kHz for a guitar, and how tall it is depends
// almost entirely on Rl: a low input impedance damps it (a darker, duller
// guitar), a high one leaves it ringing (brighter). That is what Marshall's 1959
// manual means by the Low input being "darker due to its significantly lower
// input impedance". The recording the app receives was made into a high, known
// load (an interface's ~1M input), so the change to the amp's actual input
// impedance is the RATIO of the two responses:
//
//     H_corr(s) = H(Ramp, s) / H(Rref, s) = P_ref(s) / P_amp(s)
//
// where P_x is the polynomial above evaluated for that load (a response is the
// reciprocal of its polynomial): the numerator is the reference load's, the
// denominator the amp's. Both are second order with the same s^2 term, so the
// high-frequency gain is exactly 1 and it is one biquad.
//
// DIRECTION MATTERS. With the amp's load lower than the reference (Ramp < Rref -
// the Marshall Low input, 136k against ~1M) this only DAMPS the resonance: a
// gentle, always-well-conditioned filter. The other direction (a high-impedance
// input) would BOOST the resonance region to recover what the recording never
// had; a few dB of it is fine, a lot amplifies noise and is only as good as the
// assumed pickup, so the module refuses to boost: a load at or above the
// reference is treated as no change.
//
// SOURCES for the parameter values: Lemme, "Secrets of Electric Guitar
// Pickups"; Zollner, "Physik der E-Gitarre" 5.5; Ban (Penn State), "Analysis of
// Electric Guitar Pickups"; measured units on cons.org/music/pickup-data.txt.
// The defaults below are generic single-coil and PAF-style humbucker values
// (inductance and resistance are measured figures, the capacitances are
// estimates back-calculated from the resonances - the one number nobody can
// measure across an inductor).
//
// Framework-agnostic (no JUCE).
class PickupLoading
{
public:
    struct Pickup
    {
        double henries = 2.2;
        double ohms = 6.0e3;
        double farads = 100.0e-12;
    };

    static constexpr Pickup singleCoil() noexcept { return { 2.2, 6.0e3, 100.0e-12 }; }
    static constexpr Pickup humbucker() noexcept { return { 4.5, 8.0e3, 130.0e-12 }; }

    struct Setup
    {
        Pickup pickup = singleCoil();
        double cableFarads = 500.0e-12;     // ~5m of guitar cable
        double potsOhms = 125.0e3;          // the guitar's volume and tone pots in parallel (250k || 250k)
        double referenceInputOhms = 1.0e6;  // what the recording was made into
        double ampInputOhms = 1.0e6;        // what the amp presents
    };

    // Defaults for each kind of guitar: single coils use 250k pots, humbuckers 500k.
    static Setup setupFor(const Pickup& pickup, double ampInputOhms) noexcept
    {
        Setup s;
        s.pickup = pickup;
        s.potsOhms = pickup.henries > 3.0 ? 250.0e3 : 125.0e3;
        s.ampInputOhms = ampInputOhms;
        return s;
    }

    // The analog correction's complex gain at one frequency (the filter's own
    // target, so a test can compare the digital filter with it).
    static std::complex<double> response(const Setup& setup, double frequencyHz) noexcept;

    void prepare(double sampleRateHz) noexcept;
    void configure(const Setup& setup) noexcept;
    void setBypassed() noexcept { active = false; }

    void reset() noexcept { z1 = z2 = 0.0; }

    double process(double x) noexcept
    {
        if (! active)
            return x;
        auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    double sampleRate = 192000.0;
    bool active = false;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};
