#include "PowerAmpStage.h"
#include "EnvelopeUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
    KorenTriodeStage::Parameters makePowerTubeParameters()
    {
        // A power tube's own bias point and grid swing differ from a
        // preamp 12AX7 - wider swing before its own compression sets in,
        // since most of the perceived "power amp compression" here should
        // come from sag (via dynamic Vp) rather than from this curve alone.
        KorenTriodeStage::Parameters p;
        p.plateVoltage = 300.0;
        p.gridBias = -2.0;
        p.inputToGridVolts = 4.0;
        return p;
    }
}

PowerAmpStage::PowerAmpStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse), powerTriode(makePowerTubeParameters()), oversampler(sampleRateToUse)
{
    auto oversampledRate = oversampler.getOversampledRate();
    sagAttackCoeff = EnvelopeUtils::timeToCoeff(0.015, oversampledRate);  // ~15ms - rail droops fast under demand
    sagReleaseCoeff = EnvelopeUtils::timeToCoeff(0.150, oversampledRate); // ~150ms - PSU cap recovery
}

void PowerAmpStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampler = Oversampler4x(sampleRate);
    auto oversampledRate = oversampler.getOversampledRate();
    sagAttackCoeff = EnvelopeUtils::timeToCoeff(0.015, oversampledRate);
    sagReleaseCoeff = EnvelopeUtils::timeToCoeff(0.150, oversampledRate);
    reset();
}

void PowerAmpStage::setSag(float amount)
{
    sagAmount = std::clamp(amount, 0.0f, 1.0f);
}

void PowerAmpStage::setFeedback(float amount)
{
    feedbackAmount = std::clamp(amount, 0.0f, 1.0f);
}

void PowerAmpStage::reset()
{
    sagEnvelope = 0.0;
    previousOutput = 0.0f;
    oversampler.reset();
}

float PowerAmpStage::processOversampledSample(float x) noexcept
{
    // Negative feedback: the Koren stage inverts (grid up -> plate voltage
    // down), so for THIS stage, stabilising/gain-reducing feedback means
    // adding a fraction of the previous output back to the input, not
    // subtracting - subtracting would be negative feedback for a
    // non-inverting stage, but positive (gain-increasing) feedback here.
    // One-sample delayed to avoid an algebraic loop; inaudible at audio
    // sample rates (and now an even shorter delay in oversampled-sample
    // terms, so tighter still).
    float stageInput = x + previousOutput * feedbackAmount;

    // Sag: a smoothed envelope of the stage's own demand droops the
    // effective B+ rail, recovering over the release time.
    auto rectified = std::abs(static_cast<double>(stageInput));
    auto coeff = rectified > sagEnvelope ? sagAttackCoeff : sagReleaseCoeff;
    sagEnvelope += coeff * (rectified - sagEnvelope);

    auto droop = static_cast<double>(sagAmount) * sagEnvelope * sagDepthScale;
    auto dynamicPlateVoltage = nominalPlateVoltage * std::max(1.0 - droop, minPlateVoltageFraction);

    // Push-pull: the odd part of the nonlinearity cancels even harmonics,
    // matching how two tubes handling opposite waveform halves combine.
    auto positive = powerTriode.processSampleWithPlateVoltage(stageInput, dynamicPlateVoltage);
    auto negative = powerTriode.processSampleWithPlateVoltage(-stageInput, dynamicPlateVoltage);
    auto output = 0.5f * (positive - negative);

    previousOutput = output;
    return output;
}

void PowerAmpStage::processBlock(const float* input, float* output, int numSamples)
{
    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
