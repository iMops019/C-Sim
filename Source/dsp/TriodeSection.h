#pragma once

#include <algorithm>
#include <cmath>

#include "KorenTriodeStage.h"

// The pieces a schematic-traced preamp needs to run a 12AX7 section in real
// volts, kept apart from any one amp so the next traced amp can reuse them.
//
// The tube itself is the project's Koren curve (KorenTriodeStage). What this
// adds is what a circuit around it needs:
//  - a DC solver, so a stage's operating point (plate volts, grid bias) comes
//    out of the schematic's own resistors and supply rail instead of being
//    guessed;
//  - Section: the Koren curve scaled so its small-signal gain is the stage's
//    real voltage gain, with a soft grid-conduction knee;
//  - CathodeShelf: the bass shelf a too-small cathode bypass cap leaves.
//
// The tube numbers are the 12AX7 datasheet ones the project uses everywhere
// (mu 100, rp 62.5k) - a judgment call, not read off any schematic.
namespace TriodeSection
{
    constexpr double twoPi = 6.28318530717958647692;

    constexpr double mu = 100.0;
    constexpr double rp = 62.5e3;
    constexpr double gm = mu / rp;

    constexpr double parallel(double a, double b) noexcept { return a * b / (a + b); }

    // The Koren plate current, the same fit KorenTriodeStage uses (its
    // default Parameters), as a function of grid-to-cathode and
    // plate-to-cathode volts. KorenTriodeStage keeps its own copy private and
    // only evaluates it as a normalised waveshaper; the DC solver needs the
    // real current.
    inline double plateCurrent(double gridVolts, double plateVolts) noexcept
    {
        static const KorenTriodeStage::Parameters p;
        auto vp = std::max(plateVolts, 1.0);
        auto e1 = p.kp * (1.0 / p.mu + gridVolts / std::sqrt(p.kvb + vp * vp));
        auto softplus = e1 > 30.0 ? e1 : (e1 < -30.0 ? std::exp(e1) : std::log1p(std::exp(e1)));
        auto base = (vp / p.kp) * softplus;
        return base <= 0.0 ? 0.0 : std::pow(base, p.ex) / p.kg1;
    }

    // Plate current with its two partial derivatives, for a solver that
    // iterates on the tube (the phase inverter's Newton loop): amps, and
    // amps per volt of grid-to-cathode (gm) and of plate-to-cathode (gp = 1/rp).
    struct Eval { double i, gm, gp; };

    inline Eval plateCurrentEval(double gridVolts, double plateVolts) noexcept
    {
        static const KorenTriodeStage::Parameters p;
        auto vp = std::max(plateVolts, 1.0);
        auto root = std::sqrt(p.kvb + vp * vp);
        auto e1 = p.kp * (1.0 / p.mu + gridVolts / root);
        auto softplus = e1 > 30.0 ? e1 : (e1 < -30.0 ? std::exp(e1) : std::log1p(std::exp(e1)));
        auto sigmoid = e1 > 30.0 ? 1.0 : (e1 < -30.0 ? std::exp(e1) : 1.0 / (1.0 + std::exp(-e1)));
        auto base = (vp / p.kp) * softplus;
        if (base <= 0.0)
            return { 0.0, 0.0, 0.0 };

        auto i = std::pow(base, p.ex) / p.kg1;
        auto dIdBase = p.ex * i / base;

        auto dBase_dg = (vp / p.kp) * sigmoid * (p.kp / root);
        // vp is clamped at 1V: below that the plate voltage has no effect.
        auto dBase_dp = 0.0;
        if (plateVolts > 1.0)
        {
            auto dE1_dp = -p.kp * gridVolts * vp / (root * root * root);
            dBase_dp = softplus / p.kp + (vp / p.kp) * sigmoid * dE1_dp;
        }
        return { i, dIdBase * dBase_dg, dIdBase * dBase_dp };
    }

    struct DcPoint
    {
        double amps = 0.0;
        double gridBias = 0.0;        // grid-to-cathode volts
        double plateToCathode = 0.0;  // the Vp the tube curve sees
        double plateVolts = 0.0;      // plate to ground
        double cathodeVolts = 0.0;    // cathode to ground
    };

    // A cathode-biased stage: grid at 0V (through its leak), plate load to the
    // supply, cathode resistor to ground.
    inline DcPoint solveCathodeBiased(double supplyVolts, double plateLoadOhms, double cathodeOhms) noexcept
    {
        double lo = 0.0, hi = supplyVolts / (plateLoadOhms + cathodeOhms);
        for (int i = 0; i < 80; ++i)
        {
            auto mid = 0.5 * (lo + hi);
            auto residual = plateCurrent(-cathodeOhms * mid, supplyVolts - (plateLoadOhms + cathodeOhms) * mid) - mid;
            (residual > 0.0 ? lo : hi) = mid;
        }

        DcPoint d;
        d.amps = 0.5 * (lo + hi);
        d.cathodeVolts = cathodeOhms * d.amps;
        d.gridBias = -d.cathodeVolts;
        d.plateVolts = supplyVolts - plateLoadOhms * d.amps;
        d.plateToCathode = d.plateVolts - d.cathodeVolts;
        return d;
    }

    // A cathode follower: plate straight to the supply, grid held at gridVolts
    // (here by the previous stage's plate), cathode resistor to ground.
    inline DcPoint solveCathodeFollower(double supplyVolts, double gridVolts, double cathodeOhms) noexcept
    {
        double lo = 0.0, hi = supplyVolts / cathodeOhms;
        for (int i = 0; i < 80; ++i)
        {
            auto mid = 0.5 * (lo + hi);
            auto residual = plateCurrent(gridVolts - cathodeOhms * mid, supplyVolts - cathodeOhms * mid) - mid;
            (residual > 0.0 ? lo : hi) = mid;
        }

        DcPoint d;
        d.amps = 0.5 * (lo + hi);
        d.cathodeVolts = cathodeOhms * d.amps;
        d.gridBias = gridVolts - d.cathodeVolts;
        d.plateVolts = supplyVolts;
        d.plateToCathode = supplyVolts - d.cathodeVolts;
        return d;
    }

    // One triode section in real volts. The Koren stage is normalised (unit
    // input swings the grid up to 0V, unit output is the plate-current change
    // that produces); this scales its small-signal slope to the stage's real
    // voltage gain and adds a soft grid-conduction knee: positive grid swings
    // beyond the bias plus a margin are squashed by grid current through the
    // source impedance.
    struct Section
    {
        KorenTriodeStage koren;
        double swingVolts = 1.0;   // grid volts at the Koren stage's unit input
        double scaleVolts = 1.0;   // plate volts per unit of Koren output
        double clampVolts = 1.0;   // grid-conduction knee

        void configure(const DcPoint& dc, double smallSignalGain, double gridConductionMargin = 0.3)
        {
            KorenTriodeStage::Parameters p;
            p.mu = mu;
            p.gridBias = dc.gridBias;
            p.plateVoltage = dc.plateToCathode;
            p.inputToGridVolts = std::abs(dc.gridBias);
            koren.setParameters(p);

            swingVolts = std::abs(dc.gridBias);
            clampVolts = std::abs(dc.gridBias) + gridConductionMargin;

            // The curve's slope is negative (a plate stage inverts): use its size.
            constexpr float eps = 0.01f;
            auto slope = (static_cast<double>(koren.processSample(eps)) - static_cast<double>(koren.processSample(-eps)))
                         / (2.0 * static_cast<double>(eps));
            scaleVolts = smallSignalGain * swingVolts / std::abs(slope);
        }

        // The tube's open-circuit plate voltage (AC only) for a grid voltage.
        double process(double gridVolts) const noexcept
        {
            auto v = gridVolts > 0.0 ? clampVolts * std::tanh(gridVolts / clampVolts) : gridVolts;
            return static_cast<double>(koren.processSample(static_cast<float>(v / swingVolts))) * scaleVolts;
        }
    };

    // A cathode resistor Rk bypassed by C. With the bypass too small to hold
    // the whole audio band, the stage gain is
    //   A(s) = A0 * (1 + s*Rk*C) / (N + s*Rk*C),   N = 1 + gm*Rk
    // which equals A0 * (1 - (1 - 1/N) * LP(s)) with LP a one-pole low-pass
    // at N/(2*pi*Rk*C): full gain above that corner, 1/N of it far below.
    struct CathodeShelf
    {
        double alpha = 0.0, depth = 0.0, state = 0.0;

        void configure(double sampleRate, double cathodeOhms, double bypassFarads) noexcept
        {
            auto n = 1.0 + gm * cathodeOhms;
            auto lowpassHz = n / (twoPi * cathodeOhms * bypassFarads);
            alpha = 1.0 - std::exp(-twoPi * lowpassHz / sampleRate);
            depth = 1.0 - 1.0 / n;
        }

        void reset() noexcept { state = 0.0; }

        double process(double x) noexcept
        {
            state += alpha * (x - state);
            return x - depth * state;
        }
    };
}
