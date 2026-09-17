#include "DiezelVH4Preamp.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;

    // Diezel's own documented "tight bass" preferred setting: 2.7k into
    // 0.68uF, an RC corner of 1/(2*pi*2700*0.68e-6) ~= 86.7Hz.
    constexpr double preGainCutoffHz = 87.0;

    // Progressively tighter than Mesa's own cascade (40Hz/120Hz across 3
    // stages) - see the header on why: the manual's own description of
    // the Mega channel's compression "hitting mostly the lower
    // frequencies" is modeled as real attenuation from these corners,
    // not just a label.
    constexpr double interStage1CutoffHz = 50.0;
    constexpr double interStage2CutoffHz = 90.0;
    constexpr double interStage3CutoffHz = 150.0;

    constexpr double deepLowpassHz = 80.0; // matches the manual's own stated Deep center frequency
}

DiezelVH4Preamp::DiezelVH4Preamp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);
    interStageHighpass[2].setCutoff(oversampledRate, interStage3CutoffHz);

    auto dt = 1.0 / sampleRate;
    auto lpRc = 1.0 / (twoPi * deepLowpassHz);
    deepLpAlpha = dt / (lpRc + dt);

    // Four stages, progressively lighter drive - more cascaded stages
    // than Mesa (3) or Fortin (2), matching Diezel's documented extra
    // compression/tube count relative to those amps.
    KorenTriodeStage::Parameters p1, p2, p3, p4;
    p1.inputToGridVolts = 7.0;
    p2.inputToGridVolts = 5.5;
    p3.inputToGridVolts = 4.5;
    p4.inputToGridVolts = 3.5;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);
    stages[3].setParameters(p4);
}

void DiezelVH4Preamp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);
    interStageHighpass[2].setCutoff(oversampledRate, interStage3CutoffHz);

    auto dt = 1.0 / sampleRate;
    auto lpRc = 1.0 / (twoPi * deepLowpassHz);
    deepLpAlpha = dt / (lpRc + dt);

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void DiezelVH4Preamp::setGain(float newGain)
{
    gain = std::clamp(newGain, 0.0f, 1.0f);
}

void DiezelVH4Preamp::setDeep(float newDeep)
{
    deep = std::clamp(newDeep, 0.0f, 1.0f);
}

void DiezelVH4Preamp::reset()
{
    preGainHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    deepLpState = 0.0;
    oversampler.reset();
}

float DiezelVH4Preamp::processOversampledSample(float x) noexcept
{
    constexpr float interStageMakeupGain = 2.6f; // slightly lower than Mesa/Fortin's 3.0 - four stages compounds gain faster

    auto driveGain = 1.0f + gain * 14.0f; // 1x to 15x into the first stage
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);
    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    signal = stages[2].processSample(signal);
    signal = interStageHighpass[2].processSample(signal) * interStageMakeupGain;

    signal = stages[3].processSample(signal);

    // Deep: an additive-lowpass shelf (the mirror image of the
    // additive-highpass brightening trick used elsewhere in this
    // toolkit) - boosts back the low end the cascade's own tight
    // inter-stage corners just cut, without touching anything else.
    deepLpState += deepLpAlpha * (static_cast<double>(signal) - deepLpState);
    signal += static_cast<float>(deep) * static_cast<float>(deepLpState) * 1.2f;

    // Four cascaded stages compound gain fast; bring the level back down
    // before this returns to whatever tone stack/cab follows.
    return signal / (12.0f + gain * 10.0f);
}

void DiezelVH4Preamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
