#pragma once

#include <algorithm>
#include <cmath>

// Norman Koren's pentode model, with the EL34's published parameters as the
// default. From his SPICE library (normankoren.com, "Improved vacuum tube models
// for SPICE simulations", Part 2), the plate and screen currents are
//
//     E1  = (Vg2 / kP) * ln(1 + exp(kP * (1/mu + Vg1/Vg2)))
//     Ip  = (E1^ex + |E1|^ex) / kG1 * atan(Vp / kVB)         = 2 E1^ex / kG1 * atan(Vp/kVB)
//     Ig2 = (Vg2/mu + Vg1)^ex / kG2
//
// (the "+ |E1|^ex" is Koren's way of writing "zero below cutoff", and it is a
// factor of two - dropping it lands at half the real current), with, for the
// EL34: mu 11, ex 1.35, kG1 650, kG2 4200, kP 60, kVB 24. That reproduces the
// Philips/Mullard datasheet's headline point - 100mA at Va = Vg2 = 250V and
// Vg1 = -13.5V - to within a few percent; see Tests/KorenPentodeTest.cpp.
//
// Voltages are relative to the cathode. The screen is a parameter, not solved
// here: the caller drops it through the screen resistor from the supply.
struct KorenPentode
{
    struct Parameters
    {
        double mu = 11.0;
        double ex = 1.35;
        double kg1 = 650.0;
        double kg2 = 4200.0;
        double kp = 60.0;
        double kvb = 24.0;
    };

    // Plate current with its partials: amps, amps per volt of control grid
    // (gm) and of plate (gp = 1/rp).
    struct Eval { double i, gm, gp; };

    static Eval plate(double vg1, double vp, double vg2, const Parameters& k = {}) noexcept
    {
        vg2 = std::max(vg2, 1.0);
        auto arg = k.kp * (1.0 / k.mu + vg1 / vg2);
        auto softplus = arg > 30.0 ? arg : (arg < -30.0 ? std::exp(arg) : std::log1p(std::exp(arg)));
        auto sigmoid = arg > 30.0 ? 1.0 : (arg < -30.0 ? std::exp(arg) : 1.0 / (1.0 + std::exp(-arg)));
        auto e1 = (vg2 / k.kp) * softplus;
        if (e1 <= 0.0)
            return { 0.0, 0.0, 0.0 };

        auto vpc = std::max(vp, 0.0);
        auto atanTerm = std::atan(vpc / k.kvb);
        auto e1ToEx = std::pow(e1, k.ex);

        Eval r;
        r.i = 2.0 * e1ToEx / k.kg1 * atanTerm;
        r.gm = 2.0 * k.ex * (e1ToEx / e1) / k.kg1 * sigmoid * atanTerm;
        r.gp = vp > 0.0 ? 2.0 * e1ToEx / k.kg1 * (1.0 / k.kvb) / (1.0 + (vp / k.kvb) * (vp / k.kvb)) : 0.0;
        return r;
    }

    static double screen(double vg1, double vg2, const Parameters& k = {}) noexcept
    {
        auto x = vg2 / k.mu + vg1;
        return x > 0.0 ? std::pow(x, k.ex) / k.kg2 : 0.0;
    }
};
