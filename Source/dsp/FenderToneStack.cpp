#include "FenderToneStack.h"

#include <algorithm>

namespace
{
    constexpr double minResistance = 1.0e-6; // guards against a literal divide-by-zero
                                              // when a rheostat knob is fully at zero
}

FenderToneStack::FenderToneStack(double sampleRateToUse, Components componentsToUse)
    : components(componentsToUse), sampleRate(sampleRateToUse)
{
    updateConductances();
}

void FenderToneStack::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    updateConductances();
    reset();
}

void FenderToneStack::setControls(float treble, float bass, float mid)
{
    trebleAmount = std::clamp(treble, 0.0f, 1.0f);
    bassAmount = std::clamp(bass, 0.0f, 1.0f);
    midAmount = std::clamp(mid, 0.0f, 1.0f);
    updateConductances();
}

void FenderToneStack::reset()
{
    vC1Prev = 0.0;
    vC2Prev = 0.0;
}

void FenderToneStack::updateConductances()
{
    auto T = 1.0 / sampleRate;

    g1 = components.trebleCapF * sampleRate; // C1/T = C1 * sampleRate
    reqBassBranch = components.slopeResistorOhms + T / components.bassCapF;

    r1 = components.treblePotOhms;
    rBass = std::max(components.bassPotOhms * static_cast<double>(bassAmount), minResistance);
    rMid = std::max(components.midPotOhms * static_cast<double>(midAmount), minResistance);

    auto m00 = g1 + 1.0 / r1;
    auto m01 = -1.0 / r1;
    auto m10 = -1.0 / r1;
    auto m11 = 1.0 / r1 + 1.0 / reqBassBranch + 1.0 / rBass + 1.0 / rMid;

    auto det = m00 * m11 - m01 * m10;
    invM00 = m11 / det;
    invM01 = -m01 / det;
    invM10 = -m10 / det;
    invM11 = m00 / det;
}

float FenderToneStack::processSample(float input) noexcept
{
    auto vin = static_cast<double>(input);

    auto rhs1 = g1 * vin - g1 * vC1Prev;
    auto rhs2 = vin / reqBassBranch - vC2Prev / reqBassBranch;

    auto a = invM00 * rhs1 + invM01 * rhs2;
    auto vm = invM10 * rhs1 + invM11 * rhs2;

    vC1Prev = vin - a;

    auto branchCurrent = ((vin - vm) - vC2Prev) / reqBassBranch;
    vC2Prev += branchCurrent * (1.0 / (components.bassCapF * sampleRate));

    auto vout = vm + (a - vm) * static_cast<double>(trebleAmount);
    return static_cast<float>(vout);
}
