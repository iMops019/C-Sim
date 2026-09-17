#include "FenderTwinReverbPreamp.h"

#include <algorithm>
#include <vector>

namespace
{
    // V1A input stage's own cathode-bypass rolloff - Fender's blackface
    // input stages are comparatively full-range next to the metal
    // preamps' tighter ~95Hz+ corners.
    constexpr double preGainCutoffHz = 60.0;

    // Gentle interstage corners (Fender's own coupling caps are
    // comparatively large into high grid-leak resistors) - deliberately
    // NOT tightened like the metal preamps' cascades, preserving the
    // full round bass a blackface Fender is known for.
    constexpr double interStage1CutoffHz = 30.0;
    constexpr double interStage2CutoffHz = 35.0;

    // 47pF bright cap across the Vibrato channel's Volume pot - modeled
    // as a fixed shelf corner whose applied STRENGTH (not corner
    // frequency) varies with Volume; see updateBrightAmount below.
    constexpr double brightCutoffHz = 2200.0;
}

FenderTwinReverbPreamp::FenderTwinReverbPreamp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    interStageHighpass[0].setCutoff(oversampledRate, interStage1CutoffHz);
    interStageHighpass[1].setCutoff(oversampledRate, interStage2CutoffHz);
    brightHighpass.setCutoff(oversampledRate, brightCutoffHz);

    // Three stages, all deliberately light drive - the sourced defining
    // trait of this amp is headroom, not saturation. Far lower than
    // MesaRectifierPreamp (7.0/5.0/3.5) or DiezelVH4Preamp (7.0-3.5).
    KorenTriodeStage::Parameters p1, p2, p3;
    p1.inputToGridVolts = 2.4;
    p2.inputToGridVolts = 2.0;
    p3.inputToGridVolts = 1.6;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);
}

void FenderTwinReverbPreamp::setSampleRate(double newSampleRate)
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

void FenderTwinReverbPreamp::setGain(float newGain)
{
    gain = std::clamp(newGain, 0.0f, 1.0f);
}

void FenderTwinReverbPreamp::setVolume(float newVolume)
{
    volume = std::clamp(newVolume, 0.0f, 1.0f);
}

void FenderTwinReverbPreamp::reset()
{
    preGainHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    brightHighpass.reset();
    oversampler.reset();
}

float FenderTwinReverbPreamp::processOversampledSample(float x) noexcept
{
    constexpr float interStageMakeupGain = 1.25f;

    // Far gentler than the metal preamps' 1+gain*14..15 - this is the
    // sourced headroom trait: the Twin stays clean far later into its
    // own control range than other Fenders/high-gain amps. Each
    // KorenTriodeStage self-normalises to unit output at unit input
    // regardless of inputToGridVolts (see KorenTriodeStage.cpp), so
    // what actually keeps this cascade clean is staying near that
    // calibration point rather than driving well past it three times in
    // a row - a much smaller pre-gain and interstage makeup than the
    // metal preamps use, not just a smaller inputToGridVolts number.
    auto driveGain = 1.0f + gain * 0.85f; // 1x to 1.85x into the first stage
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);
    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    // Mid-cascade Volume control - scales drive into the final,
    // reverb-recovery stage (V4B), matching the real channel's own
    // control placement (see header).
    auto volumeGain = 0.35f + volume * 1.65f; // never fully off, matches a real pot's audible taper floor
    signal *= volumeGain;

    // Bright cap: an additive high-shelf whose strength fades out as
    // Volume rises - strongest when the pot (and its shunt-to-ground
    // loading) is low, physically bypassed at high settings.
    auto brightStrength = 0.9f * (1.0f - volume);
    signal += brightStrength * brightHighpass.processSample(signal);

    signal = stages[2].processSample(signal);

    // Three cascaded stages compound gain; bring the level back down
    // before this returns to whatever tone stack/cab follows.
    return signal / (1.5f + gain * 0.9f);
}

void FenderTwinReverbPreamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
