#include "FenderPrincetonReverbPreamp.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;

    // V1's own rolloff - slightly higher than the Twin's 60Hz since the
    // Princeton's smaller output transformer/10" speaker don't need as
    // much low end preserved ahead of the cascade, but still far gentler
    // than the metal preamps' tightening corners.
    constexpr double preGainCutoffHz = 70.0;

    // Deliberately LOWER than the Twin's already-gentle 30/35Hz corners
    // - the sourced "more breakup in the lower frequencies" trait means
    // bass should reach the second stage's nonlinearity mostly intact,
    // not be filtered ahead of it.
    constexpr double interStageCutoffHz = 18.0;

    // Fixed low-mid bump: a bandpass shape built as lpLow - lpHigh,
    // centred around the sourced "browner... mid-focused" ~400-500Hz
    // region.
    constexpr double midBumpLowHz = 300.0;
    constexpr double midBumpHighHz = 650.0;
    constexpr float midBumpGain = 0.5f;
}

FenderPrincetonReverbPreamp::FenderPrincetonReverbPreamp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    interStageHighpass.setCutoff(oversampledRate, interStageCutoffHz);

    auto dt = 1.0 / oversampledRate;
    auto rcLow = 1.0 / (twoPi * midBumpLowHz);
    midLpLowAlpha = dt / (rcLow + dt);
    auto rcHigh = 1.0 / (twoPi * midBumpHighHz);
    midLpHighAlpha = dt / (rcHigh + dt);

    // Two stages only - a real structural reason this amp carries less
    // overall gain than the Twin's 3-stage cascade (see header). V2 is
    // genuinely a 12AT7 at the reverb-send tap, not another 12AX7:
    // lower mu (~60 vs 100) is a real tube-type difference, not just a
    // tuned-down inputToGridVolts.
    KorenTriodeStage::Parameters p1, p2;
    p1.inputToGridVolts = 2.6;
    p2.inputToGridVolts = 2.2;
    p2.mu = 60.0; // 12AT7, not another 12AX7 - see header
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
}

void FenderPrincetonReverbPreamp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    interStageHighpass.setCutoff(oversampledRate, interStageCutoffHz);

    auto dt = 1.0 / oversampledRate;
    auto rcLow = 1.0 / (twoPi * midBumpLowHz);
    midLpLowAlpha = dt / (rcLow + dt);
    auto rcHigh = 1.0 / (twoPi * midBumpHighHz);
    midLpHighAlpha = dt / (rcHigh + dt);

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void FenderPrincetonReverbPreamp::setGain(float newGain)
{
    gain = std::clamp(newGain, 0.0f, 1.0f);
}

void FenderPrincetonReverbPreamp::setVolume(float newVolume)
{
    volume = std::clamp(newVolume, 0.0f, 1.0f);
}

void FenderPrincetonReverbPreamp::reset()
{
    preGainHighpass.reset();
    interStageHighpass.reset();
    midLpLowState = 0.0;
    midLpHighState = 0.0;
    oversampler.reset();
}

float FenderPrincetonReverbPreamp::processOversampledSample(float x) noexcept
{
    constexpr float interStageMakeupGain = 1.6f;

    // A smaller pre-gain range than the metal preamps but, per stage,
    // hotter than the Twin's - the sourced "abundant overdrive
    // capability" trait: this amp has less gain on tap overall (two
    // stages, one of them a lower-mu 12AT7) but reaches its own
    // saturation point sooner than the Twin does relative to its own
    // control range.
    auto driveGain = 1.0f + gain * 4.0f; // 1x to 5x into the first stage
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);

    // Fixed low-mid bump (bandpass via difference-of-lowpasses) - the
    // sourced "browner... mid-focused" character, not adjustable, same
    // as PrecisionDriveStage's own fixed mid bump.
    midLpLowState += midLpLowAlpha * (static_cast<double>(signal) - midLpLowState);
    midLpHighState += midLpHighAlpha * (static_cast<double>(signal) - midLpHighState);
    // LP(650Hz) passes most of the 300-650Hz band while LP(300Hz)
    // already attenuates it, so high-minus-low is the bump shape - the
    // reverse subtraction would cancel the mid-band instead of lifting it.
    signal += midBumpGain * static_cast<float>(midLpHighState - midLpLowState);

    signal = interStageHighpass.processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);

    auto volumeGain = 0.3f + volume * 1.4f;
    signal *= volumeGain;

    // Two cascaded stages compound gain less than the 3-stage preamps;
    // bring the level back down before this returns to whatever tone
    // stack/cab follows.
    return signal / (1.3f + gain * 0.7f);
}

void FenderPrincetonReverbPreamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
