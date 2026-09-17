#include "FenderDeluxeReverbPreamp.h"

#include <algorithm>
#include <vector>

namespace
{
    // Between the Twin's 60Hz and Princeton's 70Hz - a mid-size AB763
    // combo's own input stage rolloff.
    constexpr double preGainCutoffHz = 65.0;

    // Between the Twin's gentle 30/35Hz corners and the Princeton's
    // tighter 18Hz - this amp's own "loose but not boomy" middle ground.
    constexpr double interStage1CutoffHz = 25.0;
    constexpr double interStage2CutoffHz = 28.0;

    // Same 47pF bright cap corner as the Twin - a real, shared
    // AB763-family detail, not independently tuned.
    constexpr double brightCutoffHz = 2200.0;
}

FenderDeluxeReverbPreamp::FenderDeluxeReverbPreamp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);
    brightHighpass.setCutoff(oversampledRate, brightCutoffHz);

    // Three stages, same topology as the Twin's cascade but driven a
    // little hotter per stage - this amp's own "breaks up nicely when
    // pushed" middle ground between the Princeton and the Twin.
    KorenTriodeStage::Parameters p1, p2, p3;
    p1.inputToGridVolts = 2.5;
    p2.inputToGridVolts = 2.1;
    p3.inputToGridVolts = 1.8;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);
}

void FenderDeluxeReverbPreamp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);
    brightHighpass.setCutoff(oversampledRate, brightCutoffHz);

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void FenderDeluxeReverbPreamp::setGain(float newGain)
{
    gain = std::clamp(newGain, 0.0f, 1.0f);
}

void FenderDeluxeReverbPreamp::setVolume(float newVolume)
{
    volume = std::clamp(newVolume, 0.0f, 1.0f);
}

void FenderDeluxeReverbPreamp::reset()
{
    preGainHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    brightHighpass.reset();
    oversampler.reset();
}

float FenderDeluxeReverbPreamp::processOversampledSample(float x) noexcept
{
    constexpr float interStageMakeupGain = 1.4f; // between the Twin's 1.25 and the Princeton's 1.6

    // Between the Twin's 1+gain*0.85 (max 1.85x) and the Princeton's
    // 1+gain*4.0 (max 5x) - genuinely intermediate headroom.
    auto driveGain = 1.0f + gain * 2.0f; // 1x to 3x into the first stage
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);
    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    // Mid-cascade Volume control - same mechanism as the Twin.
    auto volumeGain = 0.35f + volume * 1.65f;
    signal *= volumeGain;

    // Bright cap - same shared AB763-family detail as the Twin.
    auto brightStrength = 0.9f * (1.0f - volume);
    signal += brightStrength * brightHighpass.processSample(signal);

    signal = stages[2].processSample(signal);

    // Between the Twin's 1.5+gain*0.9 and the Princeton's 1.3+gain*0.7.
    return signal / (1.4f + gain * 0.8f);
}

void FenderDeluxeReverbPreamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
