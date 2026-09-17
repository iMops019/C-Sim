#include "SolidStatePowerAmpStage.h"

#include <algorithm>
#include <cmath>

SolidStatePowerAmpStage::SolidStatePowerAmpStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse), oversampler(sampleRateToUse)
{
}

void SolidStatePowerAmpStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampler = Oversampler4x(sampleRate);
    reset();
}

void SolidStatePowerAmpStage::setDrive(float amount)
{
    drive = std::clamp(amount, 0.0f, 1.0f);
}

void SolidStatePowerAmpStage::reset()
{
    oversampler.reset();
}

float SolidStatePowerAmpStage::processOversampledSample(float x) const noexcept
{
    auto driveGain = 1.0f + drive * driveRange; // 1x to 6x
    auto driven = x * driveGain;

    auto ax = std::abs(driven);
    if (ax <= linearThreshold)
        return driven;

    auto sign = driven < 0.0f ? -1.0f : 1.0f;
    auto excess = std::min((ax - linearThreshold) / (1.0f - linearThreshold), 1.0f);
    auto knee = excess * excess * (3.0f - 2.0f * excess); // smoothstep, 0..1
    return sign * (linearThreshold + (1.0f - linearThreshold) * knee);
}

void SolidStatePowerAmpStage::processBlock(const float* input, float* output, int numSamples)
{
    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
