#include "BossTr2Stage.h"

#include "PotTaper.h"

#include <algorithm>
#include <cmath>

// -------------------------------------------------------------------------
// The circuit's numbers
// -------------------------------------------------------------------------

BossTr2Stage::Thresholds BossTr2Stage::lfoThresholds(const Components& c)
{
    // The Schmitt trigger flips when its positive input crosses the 4.5V reference. That node
    // is the resistive average of the integrator output (via 33k), the trigger's own output
    // (via 47k) and the bias divider (330k to the supply, 100k to ground):
    //     ref * G = Vt/R29 + Vsq/R37 + supply/R36,   G = 1/R29 + 1/R37 + 1/R36 + 1/R45
    // so the integrator output that trips it is Vt = R29 * (ref*G - Vsq/R37 - supply/R36).
    auto g = 1.0 / c.schmittIntegratorR + 1.0 / c.schmittFeedbackR + 1.0 / c.schmittUpperR + 1.0 / c.schmittLowerR;
    auto trip = [&](double squareVolts) {
        return c.schmittIntegratorR * (c.referenceVolts * g - squareVolts / c.schmittFeedbackR - c.supplyVolts / c.schmittUpperR);
    };
    // While the trigger is high the integrator falls until it reaches the LOW threshold, and vice versa.
    return { trip(c.railHighVolts), trip(c.railLowVolts) };
}

namespace
{
    // The integrator ramp for a Rate pot position: 3.3V / (C * Reff), Reff = 56k (1 + Ra/Rp),
    // Ra the pot resistance from the square-wave end to the wiper (largest = slowest), Rp = the
    // integrator 56k in parallel with the rest of the pot plus its 10k cold-end resistor.
    double effectiveResistance(const BossTr2Stage::Components& c, double rate)
    {
        auto ra = (1.0 - std::clamp(rate, 0.0, 1.0)) * c.ratePot;
        auto rb = c.ratePot - ra;
        auto rp = 1.0 / (1.0 / c.integratorR + 1.0 / (rb + c.rateColdR));
        return c.integratorR * (1.0 + ra / rp);
    }
}

double BossTr2Stage::lfoFrequencyHz(const Components& c, double rate)
{
    auto rEff = effectiveResistance(c, rate);
    auto t = lfoThresholds(c);
    auto swing = t.high - t.low;
    auto down = (c.railHighVolts - c.referenceVolts) / (c.integratorC * rEff);
    auto up = (c.referenceVolts - c.railLowVolts) / (c.integratorC * rEff);
    return 1.0 / (swing / down + swing / up);
}

double BossTr2Stage::smoothingSeconds(const Components& c, double depth)
{
    // The control node: the Depth pot's source resistance plus 100k, in parallel with the 1M
    // bias resistor (both are AC ground on the far side), into 0.1uF.
    auto rs = depth * (1.0 - depth) * c.depthPot;
    auto series = c.controlSeriesR + rs;
    return c.controlC * (series * c.controlBiasR / (series + c.controlBiasR));
}

double BossTr2Stage::waveGain(const Components& c, double wave)
{
    return (c.waveFixedR + c.wavePot * potFraction(wave, c.wavePotMid)) / c.waveInputR;
}

double BossTr2Stage::softLimit(double x, double limit) noexcept
{
    // x / (1 + (x/L)^4)^(1/4): linear near zero, ~1% distortion at x = 0.53 L (the VCA's spec).
    auto s = x / limit;
    auto s2 = s * s;
    return x / std::sqrt(std::sqrt(1.0 + s2 * s2));
}

// -------------------------------------------------------------------------
// The stage
// -------------------------------------------------------------------------

BossTr2Stage::BossTr2Stage(double sampleRateToUse, Components components)
    : c(components), sampleRate(sampleRateToUse)
{
    configure();
}

void BossTr2Stage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    configure();
}

void BossTr2Stage::configure()
{
    thresholds = lfoThresholds(c);

    auto tau = c.outputCouplingC * c.outputBiasR;
    auto k = 2.0 * tau * sampleRate;
    hpA = k / (k + 1.0);
    hpP = (k - 1.0) / (k + 1.0);

    updateRamp();
    reset();
}

void BossTr2Stage::updateRamp() noexcept
{
    auto rEff = effectiveResistance(c, rate);
    rampDown = (c.railHighVolts - c.referenceVolts) / (c.integratorC * rEff);
    rampUp = (c.referenceVolts - c.railLowVolts) / (c.integratorC * rEff);
}

void BossTr2Stage::setRate(float amount)
{
    rate = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    updateRamp();
}

void BossTr2Stage::setDepth(float amount)
{
    depth = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    controlAlpha = 1.0 - std::exp(-1.0 / (sampleRate * smoothingSeconds(c, depth)));
}

void BossTr2Stage::setWave(float amount)
{
    wave = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    waveStageGain = waveGain(c, wave);
}

void BossTr2Stage::reset() noexcept
{
    setDepth(static_cast<float>(depth));
    setWave(static_cast<float>(wave));

    // Start mid-triangle, falling, with the control voltage already at its value there.
    integrator = 0.5 * (thresholds.low + thresholds.high);
    triggerHigh = true;
    smoothedControl = controlTarget();
    hpX1 = hpY1 = 0.0;
}

double BossTr2Stage::controlTarget() const noexcept
{
    // IC3A: an inverting amplifier about the reference, clipping at the rails.
    auto v3a = std::clamp(c.referenceVolts - waveStageGain * (integrator - c.referenceVolts), c.railLowVolts, c.railHighVolts);

    // IC3B: inverting again, about its 4.9V bias.
    auto ratio = c.ic3bFeedbackR / c.ic3bInputR;
    auto v3b = c.ic3bBiasVolts * (1.0 + ratio) - ratio * v3a;

    // The Depth pot mixes between the fixed end voltage and IC3B; 100k then 1M to the fixed
    // voltage form a divider onto the control node.
    auto rs = depth * (1.0 - depth) * c.depthPot;
    auto wiper = c.depthEndVolts + depth * (v3b - c.depthEndVolts);
    auto node = c.depthEndVolts + (wiper - c.depthEndVolts) * c.controlBiasR / (c.controlSeriesR + rs + c.controlBiasR);
    return node - c.commonVolts;
}

void BossTr2Stage::stepLfo() noexcept
{
    auto dt = 1.0 / sampleRate;
    if (triggerHigh)
    {
        integrator -= rampDown * dt;
        if (integrator <= thresholds.low)
        {
            integrator = thresholds.low + (thresholds.low - integrator); // reflect the overshoot
            triggerHigh = false;
        }
    }
    else
    {
        integrator += rampUp * dt;
        if (integrator >= thresholds.high)
        {
            integrator = thresholds.high - (integrator - thresholds.high);
            triggerHigh = true;
        }
    }
}

void BossTr2Stage::processBlock(const float* input, float* output, int numSamples)
{
    for (int n = 0; n < numSamples; ++n)
    {
        stepLfo();
        smoothedControl += controlAlpha * (controlTarget() - smoothedControl);

        // The VCA: gain linear in the control voltage, never negative.
        auto x = softLimit(static_cast<double>(input[n]) * c.inputGain, c.vcaInputLimitVolts);
        auto y = x * c.vcaGainPerVolt * std::max(smoothedControl, 0.0);

        // The wet path's output coupling (a high-pass) and the output buffer.
        auto hp = hpP * hpY1 + hpA * (y - hpX1);
        hpX1 = y;
        hpY1 = hp;
        output[n] = static_cast<float>(hp * c.outputGain);
    }
}
