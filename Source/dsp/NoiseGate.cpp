#include "NoiseGate.h"
#include "EnvelopeUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
    float dbToGain(float db) noexcept
    {
        return std::pow(10.0f, db / 20.0f);
    }
}

NoiseGate::NoiseGate(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    thresholdGain = dbToGain(-50.0f);
    updateCoefficients();
}

void NoiseGate::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    updateCoefficients();
    reset();
}

void NoiseGate::setThresholdDb(float thresholdDb)
{
    thresholdGain = dbToGain(thresholdDb);
}

void NoiseGate::setReleaseMs(float releaseMs)
{
    releaseSeconds = std::max(releaseMs, 1.0f) / 1000.0f;
    updateCoefficients();
}

void NoiseGate::updateCoefficients()
{
    envCoeff = EnvelopeUtils::timeToCoeff(0.005, sampleRate);       // ~5ms envelope smoothing
    gateUpCoeff = EnvelopeUtils::timeToCoeff(0.002, sampleRate);    // fast open, ~2ms
    gateDownCoeff = EnvelopeUtils::timeToCoeff(releaseSeconds, sampleRate);
}

void NoiseGate::reset() noexcept
{
    envelope = 0.0f;
    gateGain = 1.0f;
}

float NoiseGate::processSample(float input) noexcept
{
    auto rectified = std::abs(input);
    envelope += static_cast<float>(envCoeff) * (rectified - envelope);

    auto targetGain = envelope >= thresholdGain ? 1.0f : 0.0f;
    auto coeff = targetGain > gateGain ? gateUpCoeff : gateDownCoeff;
    gateGain += static_cast<float>(coeff) * (targetGain - gateGain);

    return input * gateGain;
}

void NoiseGate::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
