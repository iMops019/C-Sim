#include "DynamicGainStage.h"
#include "EnvelopeUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
    KorenTriodeStage::Parameters makeCleanStageParameters()
    {
        KorenTriodeStage::Parameters p;
        p.inputToGridVolts = 3.0; // gentler swing than the metal build's stages - meant to stay mostly clean
        return p;
    }
}

DynamicGainStage::DynamicGainStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse), stage(makeCleanStageParameters())
{
    attackCoeff = EnvelopeUtils::timeToCoeff(0.005, sampleRate);
    releaseCoeff = EnvelopeUtils::timeToCoeff(0.200, sampleRate);
}

void DynamicGainStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    attackCoeff = EnvelopeUtils::timeToCoeff(0.005, sampleRate);
    releaseCoeff = EnvelopeUtils::timeToCoeff(0.200, sampleRate);
    reset();
}

void DynamicGainStage::setSensitivity(float amount)
{
    sensitivity = std::clamp(amount, 0.0f, 1.0f);
}

void DynamicGainStage::setBaseDrive(float amount)
{
    baseDrive = std::clamp(amount, 0.0f, 1.0f);
}

void DynamicGainStage::reset() noexcept
{
    envelope = 0.0f;
}

float DynamicGainStage::processSample(float input) noexcept
{
    auto rectified = std::abs(input);
    auto coeff = rectified > envelope ? attackCoeff : releaseCoeff;
    envelope += static_cast<float>(coeff) * (rectified - envelope);

    // Base drive keeps quiet playing clean-ish; sensitivity scales how much
    // digging in pushes further into the nonlinearity.
    auto driveGain = 1.0f + baseDrive * 2.0f + sensitivity * envelope * 8.0f;
    return stage.processSample(input * driveGain);
}

void DynamicGainStage::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
