#include "DynaCompStage.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Fixed, non-adjustable envelope time constant - there's no
    // Attack/Release knob on the real pedal, just one Sensitivity
    // control. Tuned for the fast, percussive "grab" this pedal is
    // widely known for on tight, palm-muted rhythm playing.
    constexpr double attackTimeSeconds = 0.004;
    constexpr double releaseTimeSeconds = 0.12;

    // Sensitivity moves both an effective threshold and ratio together,
    // since that's genuinely how the real single-knob circuit behaves -
    // not a simplification of separate threshold/ratio controls.
    constexpr float thresholdAtMinSensitivityDb = -6.0f;
    constexpr float thresholdAtMaxSensitivityDb = -36.0f;
    constexpr float ratioAtMinSensitivity = 3.0f;
    constexpr float ratioAtMaxSensitivity = 10.0f;

    // Output's real, sourced quirk (see header): the output filter's own
    // cutoff sweeps with the Output control, not just level.
    constexpr double outputFilterMinHz = 52.0;
    constexpr double outputFilterMaxHz = 310.0;
}

DynaCompStage::DynaCompStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    attackCoeff = EnvelopeUtils::timeToCoeff(attackTimeSeconds, sampleRate);
    releaseCoeff = EnvelopeUtils::timeToCoeff(releaseTimeSeconds, sampleRate);
    outputFilter.setCutoff(sampleRate, outputFilterMaxHz);
}

void DynaCompStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    attackCoeff = EnvelopeUtils::timeToCoeff(attackTimeSeconds, sampleRate);
    releaseCoeff = EnvelopeUtils::timeToCoeff(releaseTimeSeconds, sampleRate);
    setOutput(outputAmount);
    reset();
}

void DynaCompStage::setSensitivity(float amount)
{
    sensitivity = std::clamp(amount, 0.0f, 1.0f);
}

void DynaCompStage::setOutput(float amount)
{
    outputAmount = std::clamp(amount, 0.0f, 1.0f);

    // Higher Output retains more low end (lower cutoff) - a modelling
    // choice for the direction of this real, sourced sweep (the source
    // documents the range, 52-310Hz, but not which way the knob moves
    // it), same honesty level as this toolkit's other approximations.
    auto cutoffHz = outputFilterMaxHz + (outputFilterMinHz - outputFilterMaxHz) * outputAmount;
    outputFilter.setCutoff(sampleRate, cutoffHz);

    // A plain volume trim plus a little automatic makeup - heavier
    // compression (which isn't this control's own job, but the two
    // controls interact on the real pedal too) needs more makeup to
    // read as "louder and more sustained," the pedal's whole appeal.
    levelGain = 0.6f + outputAmount * 1.2f;
}

void DynaCompStage::reset() noexcept
{
    envelope = 0.0f;
    lastOutput = 0.0f;
    outputFilter.reset();
}

float DynaCompStage::processSample(float input) noexcept
{
    // Feedback-style: the envelope follows this stage's own PREVIOUS
    // output, not the raw input arriving right now - see header for why
    // that's a real, deliberate topological choice, not an accident.
    auto rectified = std::abs(lastOutput);
    auto coeff = (rectified > envelope) ? attackCoeff : releaseCoeff;
    envelope += static_cast<float>(coeff) * (rectified - envelope);

    auto envDb = 20.0f * std::log10(std::max(envelope, 1.0e-6f));
    auto thresholdDb = thresholdAtMinSensitivityDb + (thresholdAtMaxSensitivityDb - thresholdAtMinSensitivityDb) * sensitivity;
    auto ratio = ratioAtMinSensitivity + (ratioAtMaxSensitivity - ratioAtMinSensitivity) * sensitivity;

    float gainReductionDb = 0.0f;
    if (envDb > thresholdDb)
        gainReductionDb = (envDb - thresholdDb) * (1.0f - 1.0f / ratio);

    // Automatic makeup proportional to how much sensitivity is dialed
    // in - part of why this pedal reads as "more sustain," not just
    // "quieter peaks."
    auto makeupDb = sensitivity * 6.0f;

    auto gain = std::pow(10.0f, (makeupDb - gainReductionDb) / 20.0f);
    auto out = input * gain;
    lastOutput = out;
    return out;
}

void DynaCompStage::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        auto compressed = processSample(input[n]);
        auto filtered = outputFilter.processSample(compressed);
        output[n] = filtered * levelGain;
    }
}
