#include "MetalPreampChain.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr double preGainCutoffHz = 100.0;   // within the spec's 80-120Hz range
    constexpr double interStageCutoffHz = 40.0; // below a dropped low C (~65Hz), lighter than the pre-gain filter
}

MetalPreampChain::MetalPreampChain(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    for (auto& hp : interStageHighpass)
        hp.setCutoff(oversampledRate, interStageCutoffHz);

    KorenTriodeStage::Parameters p1, p2, p3;
    p1.inputToGridVolts = 8.0;
    p2.inputToGridVolts = 5.0;
    p3.inputToGridVolts = 3.0;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);
}

void MetalPreampChain::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    for (auto& hp : interStageHighpass)
        hp.setCutoff(oversampledRate, interStageCutoffHz);

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void MetalPreampChain::setDrive(float newDrive)
{
    drive = std::clamp(newDrive, 0.0f, 1.0f);
}

void MetalPreampChain::reset()
{
    preGainHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    oversampler.reset();
}

float MetalPreampChain::processOversampledSample(float x) noexcept
{
    // Each Koren stage's own small-signal gain near its bias point is
    // well under unity (it's a compressive waveshaper, not a linear
    // amplifier) - real triode stages have real voltage gain, so a fixed
    // makeup multiplier between stages stands in for that, on top of the
    // user-controlled drive that pushes further into the compression.
    constexpr float interStageMakeupGain = 3.0f;

    auto driveGain = 1.0f + drive * 15.0f; // 1x to 16x into the first stage
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);
    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    signal = stages[2].processSample(signal);

    // Three cascaded stages compound gain fast; bring the level back down
    // before this returns to whatever tone stack/cab follows.
    return signal / (10.0f + drive * 10.0f);
}

void MetalPreampChain::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
