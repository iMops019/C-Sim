#include "MesaDiezelHybridPreamp.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;

    constexpr double preGainCutoffHz = 87.0; // Diezel's documented "tight bass" corner

    // Mesa's own gain-dependent voicing sweep, unchanged from
    // MesaRectifierPreamp.
    constexpr double voicingCutoffLowGainHz = 656.0;
    constexpr double voicingCutoffHighGainHz = 1400.0;

    // Roughly midway between Mesa's own corners (40/120Hz across 3
    // stages) and Diezel's (50/90/150Hz across 4) - stacking Diezel's
    // full tightness in series with Mesa's voicing shelf compounded into
    // losing the drop-C fundamental almost entirely (caught by this
    // module's own test, not assumed fine on paper), so the hybrid
    // splits the difference rather than taking both parents' tightness
    // at full strength on top of each other.
    constexpr double interStage1CutoffHz = 40.0;
    constexpr double interStage2CutoffHz = 70.0;
    constexpr double interStage3CutoffHz = 110.0;

    constexpr double deepLowpassHz = 80.0;
}

MesaDiezelHybridPreamp::MesaDiezelHybridPreamp(double sampleRateToUse)
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

    KorenTriodeStage::Parameters p1, p2, p3, p4;
    p1.inputToGridVolts = 7.0;
    p2.inputToGridVolts = 5.5;
    p3.inputToGridVolts = 4.5;
    p4.inputToGridVolts = 3.5;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);
    stages[3].setParameters(p4);

    voicingCutoffHz = static_cast<float>(voicingCutoffLowGainHz);
    voicingHighpass.setCutoff(oversampledRate, voicingCutoffHz);
}

void MesaDiezelHybridPreamp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);
    interStageHighpass[2].setCutoff(oversampledRate, interStage3CutoffHz);
    voicingHighpass.setCutoff(oversampledRate, voicingCutoffHz);

    auto dt = 1.0 / sampleRate;
    auto lpRc = 1.0 / (twoPi * deepLowpassHz);
    deepLpAlpha = dt / (lpRc + dt);

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void MesaDiezelHybridPreamp::setGain(float newGain)
{
    gain = std::clamp(newGain, 0.0f, 1.0f);
    voicingCutoffHz = static_cast<float>(voicingCutoffLowGainHz
                                          + (voicingCutoffHighGainHz - voicingCutoffLowGainHz) * gain);
    voicingHighpass.setCutoff(sampleRate * 4.0, voicingCutoffHz);
}

void MesaDiezelHybridPreamp::setDeep(float newDeep)
{
    deep = std::clamp(newDeep, 0.0f, 1.0f);
}

void MesaDiezelHybridPreamp::reset()
{
    preGainHighpass.reset();
    voicingHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    deepLpState = 0.0;
    oversampler.reset();
}

float MesaDiezelHybridPreamp::processOversampledSample(float x) noexcept
{
    constexpr float interStageMakeupGain = 2.6f;
    // Higher (lighter touch) than MesaRectifierPreamp's own 0.6 - this
    // shelf is now stacked in series with Diezel-style inter-stage
    // corners too, so a lighter contribution from each piece avoids the
    // two tightening mechanisms compounding into losing the fundamental.
    constexpr float voicingFloor = 0.75f;

    auto driveGain = 1.0f + gain * 14.0f;
    float signal = x * driveGain;

    // Stage 1, then Mesa's own voicing shelf spliced in at the exact spot
    // it occupies in MesaRectifierPreamp, before Diezel's tighter
    // inter-stage corner takes over for the rest of the cascade.
    signal = stages[0].processSample(signal);
    signal = voicingFloor * signal + (1.0f - voicingFloor) * voicingHighpass.processSample(signal);
    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    signal = stages[2].processSample(signal);
    signal = interStageHighpass[2].processSample(signal) * interStageMakeupGain;

    signal = stages[3].processSample(signal);

    // Diezel's own Deep control.
    deepLpState += deepLpAlpha * (static_cast<double>(signal) - deepLpState);
    signal += static_cast<float>(deep) * static_cast<float>(deepLpState) * 1.2f;

    return signal / (12.0f + gain * 10.0f);
}

void MesaDiezelHybridPreamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
