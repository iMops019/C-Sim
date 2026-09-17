#include "FortinMeshuggahPreamp.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr double preGainCutoffHz = 120.0;    // a Marshall-family input runs tighter than a Fender/Mesa one
    constexpr double interStageCutoffHz = 60.0;  // between V1a and V1b, oversampled rate
    constexpr double brightCutoffHz = 2200.0;    // Gain 2's bright cap - a small, fixed-corner coupling cap
    constexpr float brightFloor = 0.75f;         // how little the bright shelf touches lows/mids - see header
}

FortinMeshuggahPreamp::FortinMeshuggahPreamp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    interStageHighpass.setCutoff(oversampledRate, interStageCutoffHz);
    brightHighpass.setCutoff(oversampledRate, brightCutoffHz);

    // V1a runs a goosed (increased) plate load for extra gain over a
    // stock JCM800 first stage - modeled as a wider grid-voltage swing
    // (more sensitive to input) than V1b's more ordinary second stage.
    KorenTriodeStage::Parameters p1, p2;
    p1.inputToGridVolts = 6.0;
    p2.inputToGridVolts = 5.0;
    stage1.setParameters(p1);
    stage2.setParameters(p2);
}

void FortinMeshuggahPreamp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    interStageHighpass.setCutoff(oversampledRate, interStageCutoffHz);
    brightHighpass.setCutoff(oversampledRate, brightCutoffHz);

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void FortinMeshuggahPreamp::setGain1(float newGain)
{
    gain1 = std::clamp(newGain, 0.0f, 1.0f);
}

void FortinMeshuggahPreamp::setGain2(float newGain)
{
    gain2 = std::clamp(newGain, 0.0f, 1.0f);
}

void FortinMeshuggahPreamp::setMaster(float newMaster)
{
    master = std::clamp(newMaster, 0.0f, 1.0f);
}

void FortinMeshuggahPreamp::reset()
{
    preGainHighpass.reset();
    interStageHighpass.reset();
    brightHighpass.reset();
    masterClipper.reset();
    oversampler.reset();
}

float FortinMeshuggahPreamp::processOversampledSample(float x) noexcept
{
    constexpr float interStageMakeupGain = 3.0f;

    // V1a: goosed plate load, so a wider drive range than a typical
    // single stage gets elsewhere in this toolkit.
    auto drive1 = 1.0f + gain1 * 18.0f;
    float signal = stage1.processSample(x * drive1);
    signal = interStageHighpass.processSample(signal) * interStageMakeupGain;

    // V1b, with its bright cap: a fixed-corner shelf, not a series
    // block - a real bright cap bypasses part of the gain network's
    // resistance at high frequencies, it doesn't remove lows outright.
    auto drive2 = 1.0f + gain2 * 15.0f;
    signal = stage2.processSample(signal * drive2);
    signal = brightFloor * signal + (1.0f - brightFloor) * brightHighpass.processSample(signal);

    // The defining stage: a hard, symmetric diode clip, pre-tone-stack,
    // driven harder as Master rises - the genuine differentiator from
    // MesaRectifierPreamp's pure-tube-cascade approach.
    auto masterDrive = 1.0f + master * 4.0f;
    signal = masterClipper.processSample(signal * masterDrive);

    // Two tube stages plus a diode clamp compound level fast; bring it
    // back down before this returns to whatever tone stack/cab follows.
    return signal / (6.0f + master * 6.0f);
}

void FortinMeshuggahPreamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
