#include "BiasModulatedTremolo.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

BiasModulatedTremolo::BiasModulatedTremolo(double sampleRateToUse)
    : sampleRate(sampleRateToUse), oversampler(sampleRateToUse)
{
}

void BiasModulatedTremolo::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampler = Oversampler4x(sampleRate);
    reset();
}

void BiasModulatedTremolo::setRateHz(float hz)
{
    rateHz = std::max(0.01f, hz);
}

void BiasModulatedTremolo::setDepth(float amount)
{
    depth = std::clamp(amount, 0.0f, 1.0f);
}

void BiasModulatedTremolo::reset() noexcept
{
    phase = 0.0;
    oversampler.reset();
}

float BiasModulatedTremolo::processOversampledSample(float x) noexcept
{
    auto lfo = std::sin(2.0 * M_PI * phase);
    phase += static_cast<double>(rateHz) / oversampler.getOversampledRate();
    if (phase >= 1.0)
        phase -= 1.0;

    // Bias stays comfortably away from the tube's hard cutoff region even
    // at full depth - real tremolo modulates gain smoothly, not into a
    // harsh on/off chop.
    auto dynamicBias = nominalBias + lfo * biasSwing * static_cast<double>(depth) * 0.5;
    return stage.processSampleWithBias(x, dynamicBias);
}

void BiasModulatedTremolo::processBlock(const float* input, float* output, int numSamples)
{
    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
