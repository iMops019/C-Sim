#include "KorenTriodeStage.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Numerically stable softplus: ln(1 + exp(x))
    double softplus(double x) noexcept
    {
        if (x > 30.0)
            return x;
        if (x < -30.0)
            return std::exp(x); // effectively 0, but avoids denormal log1p input
        return std::log1p(std::exp(x));
    }
}

double KorenTriodeStage::plateCurrent(double vg, double vp, const Parameters& p) noexcept
{
    auto vpSafe = std::max(vp, 1.0);
    auto e1 = p.kp * (1.0 / p.mu + vg / std::sqrt(p.kvb + vpSafe * vpSafe));
    auto base = (vpSafe / p.kp) * softplus(e1);

    if (base <= 0.0)
        return 0.0;

    return std::pow(base, p.ex) / p.kg1;
}

KorenTriodeStage::KorenTriodeStage(Parameters parametersToUse)
{
    setParameters(parametersToUse);
}

void KorenTriodeStage::setParameters(const Parameters& newParams)
{
    params = newParams;
    quiescentCurrent = plateCurrent(params.gridBias, params.plateVoltage, params);

    // Calibrate so a unit-amplitude input maps back to a roughly unit-scale
    // AC-coupled output, using the positive-swing side as the reference.
    // The negative side will differ from this by design - that's the
    // asymmetric clipping the whole point of this model is to capture.
    auto probe = plateCurrent(params.gridBias + params.inputToGridVolts, params.plateVoltage, params)
                 - quiescentCurrent;
    outputNormalisation = std::abs(probe) > 1.0e-9 ? 1.0 / probe : 1.0;
}

float KorenTriodeStage::processSample(float input) const noexcept
{
    auto vg = params.gridBias + static_cast<double>(input) * params.inputToGridVolts;
    auto ip = plateCurrent(vg, params.plateVoltage, params);

    // AC-coupled (DC bias subtracted off, as a real coupling cap would) and
    // normalised. Inverted since a real plate stage inverts: grid voltage
    // up -> plate current up -> plate voltage down across the load resistor.
    return static_cast<float>(-(ip - quiescentCurrent) * outputNormalisation);
}

float KorenTriodeStage::processSampleWithPlateVoltage(float input, double dynamicPlateVoltage) const noexcept
{
    auto vg = params.gridBias + static_cast<double>(input) * params.inputToGridVolts;
    auto ip = plateCurrent(vg, dynamicPlateVoltage, params);
    auto quiescentAtDynamicVp = plateCurrent(params.gridBias, dynamicPlateVoltage, params);

    return static_cast<float>(-(ip - quiescentAtDynamicVp) * outputNormalisation);
}
