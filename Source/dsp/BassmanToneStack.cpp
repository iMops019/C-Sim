#include "BassmanToneStack.h"

#include <algorithm>

namespace
{
    constexpr double minResistance = 1.0e-6; // guards against a literal divide-by-zero
                                              // when the bass rheostat is fully at zero
}

BassmanToneStack::BassmanToneStack(double sampleRateToUse, Components componentsToUse)
    : components(componentsToUse), sampleRate(sampleRateToUse)
{
    updateConductances();
}

void BassmanToneStack::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    updateConductances();
    reset();
}

void BassmanToneStack::setControls(float treble, float bass, float mid)
{
    trebleAmount = std::clamp(treble, 0.0f, 1.0f);
    bassAmount = std::clamp(bass, 0.0f, 1.0f);
    midAmount = std::clamp(mid, 0.0f, 1.0f);
    updateConductances();
}

void BassmanToneStack::reset()
{
    vTrebPrev = 0.0;
    vBassPrev = 0.0;
    vMidPrev = 0.0;
}

void BassmanToneStack::updateConductances()
{
    auto T = 1.0 / sampleRate;

    gTreb = components.trebleCapF * sampleRate;
    gTrebPot = 1.0 / components.treblePotOhms;
    gSlope = 1.0 / components.slopeResistorOhms;

    auto rBassUsed = std::max(components.bassPotOhms * static_cast<double>(bassAmount), minResistance);
    reqBassBranch = rBassUsed + T / components.bassCapF;
    gBassBranch = 1.0 / reqBassBranch;

    gMidCap = components.midCapF * sampleRate;
    gMidPot = 1.0 / components.midPotOhms;

    auto gBD = gBassBranch + gMidCap; // combined B<->D conductance (bass branch + mid cap, in parallel)

    // System for unknowns [A, B, D], see the header comment for topology.
    double m[3][3] = { { gTreb + gTrebPot, -gTrebPot, 0.0 },
                        { -gTrebPot, gSlope + gTrebPot + gBD, -gBD },
                        { 0.0, -gBD, gBD + gMidPot } };

    // General 3x3 inverse via cofactors.
    auto cofactor = [&m](int r0, int c0, int r1, int c1) { return m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]; };

    double cof[3][3];
    cof[0][0] = cofactor(1, 1, 2, 2);
    cof[0][1] = -cofactor(1, 0, 2, 2);
    cof[0][2] = cofactor(1, 0, 2, 1);
    cof[1][0] = -cofactor(0, 1, 2, 2);
    cof[1][1] = cofactor(0, 0, 2, 2);
    cof[1][2] = -cofactor(0, 0, 2, 1);
    cof[2][0] = cofactor(0, 1, 1, 2);
    cof[2][1] = -cofactor(0, 0, 1, 2);
    cof[2][2] = cofactor(0, 0, 1, 1);

    auto det = m[0][0] * cof[0][0] + m[0][1] * cof[1][0] + m[0][2] * cof[2][0];

    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            inv[r][c] = cof[c][r] / det; // adjugate is the cofactor matrix transposed
}

float BassmanToneStack::processSample(float input) noexcept
{
    auto vin = static_cast<double>(input);

    auto rhs0 = gTreb * vin - gTreb * vTrebPrev;
    auto rhs1 = gSlope * vin + gBassBranch * vBassPrev + gMidCap * vMidPrev;
    auto rhs2 = -gBassBranch * vBassPrev - gMidCap * vMidPrev;

    auto a = inv[0][0] * rhs0 + inv[0][1] * rhs1 + inv[0][2] * rhs2;
    auto b = inv[1][0] * rhs0 + inv[1][1] * rhs1 + inv[1][2] * rhs2;
    auto d = inv[2][0] * rhs0 + inv[2][1] * rhs1 + inv[2][2] * rhs2;

    vTrebPrev = vin - a;

    auto bassBranchCurrent = ((b - d) - vBassPrev) / reqBassBranch;
    vBassPrev += bassBranchCurrent * (1.0 / (components.bassCapF * sampleRate));

    vMidPrev = b - d;

    auto trebleOut = b + (a - b) * static_cast<double>(trebleAmount);
    auto midOut = d * static_cast<double>(midAmount);

    return static_cast<float>(trebleOut + midOut);
}
