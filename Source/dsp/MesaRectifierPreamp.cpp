#include "MesaRectifierPreamp.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr double preGainCutoffHz = 95.0; // input stage's own cathode-bypass rolloff

    // Bright-cap sweep on the 250k gain pot, traced from the schematic
    // analysis: ~656Hz at low Gain up to ~1.4kHz at high Gain.
    constexpr double voicingCutoffLowGainHz = 656.0;
    constexpr double voicingCutoffHighGainHz = 1400.0;

    // Feeding stage 2: an ordinary interstage corner, same as
    // MetalPreampChain's general cascade. Feeding stage 3: tighter,
    // standing in for that stage's missing cathode bypass cap (see
    // header).
    constexpr double interStage1CutoffHz = 40.0;
    constexpr double interStage2CutoffHz = 120.0;
}

MesaRectifierPreamp::MesaRectifierPreamp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);

    // Progressively lighter drive per stage - three identical
    // nonlinearities in a row would reinforce the same harmonic content
    // rather than building on it, same rationale as MetalPreampChain.
    KorenTriodeStage::Parameters p1, p2, p3;
    p1.inputToGridVolts = 7.0;
    p2.inputToGridVolts = 5.0;
    p3.inputToGridVolts = 3.5;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);

    updateVoicingFilter();
}

void MesaRectifierPreamp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);

    oversampler = Oversampler4x(sampleRate);
    updateVoicingFilter();
    reset();
}

void MesaRectifierPreamp::setGain(float newGain)
{
    gain = std::clamp(newGain, 0.0f, 1.0f);
    updateVoicingFilter();
}

void MesaRectifierPreamp::updateVoicingFilter()
{
    voicingCutoffHz = static_cast<float>(voicingCutoffLowGainHz
                                          + (voicingCutoffHighGainHz - voicingCutoffLowGainHz) * gain);
    voicingHighpass.setCutoff(sampleRate * 4.0, voicingCutoffHz);
}

float MesaRectifierPreamp::getVoicingCutoffHz() const noexcept
{
    return voicingCutoffHz;
}

void MesaRectifierPreamp::reset()
{
    preGainHighpass.reset();
    voicingHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    oversampler.reset();
}

float MesaRectifierPreamp::processOversampledSample(float x) noexcept
{
    // Each Koren stage's own small-signal gain near its bias point is
    // well under unity - a fixed makeup multiplier between stages stands
    // in for a real triode stage's actual voltage gain, same as
    // MetalPreampChain.
    constexpr float interStageMakeupGain = 3.0f;

    // A shunt voltage divider attenuates below its corner rather than
    // blocking it outright - floored at 60% rather than toward 0, unlike
    // the true series coupling caps elsewhere in this cascade.
    constexpr float voicingFloor = 0.6f;

    auto driveGain = 1.0f + gain * 15.0f; // 1x to 16x into the first stage
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);
    signal = voicingFloor * signal + (1.0f - voicingFloor) * voicingHighpass.processSample(signal);
    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    signal = stages[2].processSample(signal);

    // Three cascaded stages compound gain fast; bring the level back
    // down before this returns to whatever tone stack/cab follows.
    return signal / (10.0f + gain * 10.0f);
}

void MesaRectifierPreamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
