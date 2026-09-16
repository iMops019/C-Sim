#include "DiodeClipperStage.h"

#include <cmath>

double DiodeClipperStage::solve(double vin, double initialGuess, const Parameters& p) noexcept
{
    auto v = initialGuess;

    // The diode's exponential conductance is nearly flat near v=0 and then
    // explodes within a few tenths of a volt, so a naive Newton step from a
    // poor initial guess (e.g. v=0 on a fresh instance) can wildly
    // overshoot and never recover. Clamping the step size (a standard
    // damped-Newton technique) fixes that - verified by
    // DiodeClipperStageTest, which caught this as a real bug: fresh
    // instances were converging to a spurious near-linear "solution"
    // instead of the diode's actual clipping curve.
    constexpr double maxStepVolts = 0.05;

    for (int iter = 0; iter < 50; ++iter)
    {
        auto diodeCurrent = 2.0 * p.saturationCurrent * std::sinh(v / p.thermalVoltage);
        auto f = (vin - v) / p.seriesResistance - diodeCurrent;

        auto diodeConductance = 2.0 * p.saturationCurrent * std::cosh(v / p.thermalVoltage) / p.thermalVoltage;
        auto fPrime = -1.0 / p.seriesResistance - diodeConductance;

        if (std::abs(fPrime) < 1.0e-15)
            break;

        auto delta = f / fPrime;
        if (delta > maxStepVolts) delta = maxStepVolts;
        if (delta < -maxStepVolts) delta = -maxStepVolts;

        v -= delta;

        if (std::abs(delta) < 1.0e-9)
            break;
    }

    return v;
}

DiodeClipperStage::DiodeClipperStage(Parameters parametersToUse)
    : params(parametersToUse)
{
    // Calibrate so a unit-amplitude input maps to a roughly unit-scale
    // output - the raw node voltage stays within a diode drop or so
    // (tens to hundreds of millivolts) regardless of drive, since the
    // diodes' exponential conductance clamps it there.
    auto probeV = solve(params.inputScale, 0.3, params);
    outputNormalisation = std::abs(probeV) > 1.0e-9 ? 1.0 / probeV : 1.0;
}

float DiodeClipperStage::processSample(float input) noexcept
{
    auto vin = static_cast<double>(input) * params.inputScale;
    vOutPrev = solve(vin, vOutPrev, params);
    return static_cast<float>(vOutPrev * outputNormalisation);
}
