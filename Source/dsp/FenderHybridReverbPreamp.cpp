#include "FenderHybridReverbPreamp.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;

    // Twin-side and Princeton-side extremes, sourced from each amp's own
    // module (see FenderTwinReverbPreamp.cpp / FenderPrincetonReverbPreamp.cpp).
    constexpr double preGainCutoffTwinHz = 60.0;
    constexpr double preGainCutoffPrincetonHz = 70.0;

    constexpr double interStage1TwinHz = 30.0;
    constexpr double interStage2TwinHz = 35.0;
    constexpr double interStagePrincetonHz = 18.0; // Princeton's single, lower corner - both hybrid gaps move toward this

    constexpr double driveSlopeTwin = 0.85;      // Twin: 1x to 1.85x
    constexpr double driveSlopePrinceton = 4.0;  // Princeton: 1x to 5x

    constexpr float interStageMakeupGainTwin = 1.25f;
    constexpr float interStageMakeupGainPrinceton = 1.6f;

    constexpr double muTwin = 100.0;      // 12AX7
    constexpr double muPrinceton = 60.0;  // 12AT7 - Princeton's own researched substitution

    constexpr double brightCutoffHz = 2200.0; // Twin's bright cap, unchanged

    constexpr double midBumpLowHz = 300.0;
    constexpr double midBumpHighHz = 650.0;
    constexpr float midBumpGain = 0.5f; // Princeton's own strength at Growl=1
}

FenderHybridReverbPreamp::FenderHybridReverbPreamp(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffTwinHz),
      oversampler(sampleRateToUse)
{
    brightHighpass.setCutoff(sampleRateToUse * 4.0, brightCutoffHz);

    KorenTriodeStage::Parameters p1, p2, p3;
    p1.inputToGridVolts = 2.4;
    p2.inputToGridVolts = 2.0;
    p3.inputToGridVolts = 1.6;
    stages[0].setParameters(p1);
    stages[1].setParameters(p2);
    stages[2].setParameters(p3);

    updateBreakupDependentState();
}

void FenderHybridReverbPreamp::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    brightHighpass.setCutoff(sampleRate * 4.0, brightCutoffHz);
    oversampler = Oversampler4x(sampleRate);
    updateBreakupDependentState();
    reset();
}

void FenderHybridReverbPreamp::setGain(float newGain)
{
    gain = std::clamp(newGain, 0.0f, 1.0f);
}

void FenderHybridReverbPreamp::setVolume(float newVolume)
{
    volume = std::clamp(newVolume, 0.0f, 1.0f);
}

void FenderHybridReverbPreamp::setBreakup(float newBreakup)
{
    breakup = std::clamp(newBreakup, 0.0f, 1.0f);
    updateBreakupDependentState();
}

void FenderHybridReverbPreamp::setGrowl(float newGrowl)
{
    growl = std::clamp(newGrowl, 0.0f, 1.0f);
}

void FenderHybridReverbPreamp::updateBreakupDependentState()
{
    auto b = static_cast<double>(breakup);

    auto preGainCutoffHz = preGainCutoffTwinHz + (preGainCutoffPrincetonHz - preGainCutoffTwinHz) * b;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    auto corner1 = interStage1TwinHz + (interStagePrincetonHz - interStage1TwinHz) * b;
    auto corner2 = interStage2TwinHz + (interStagePrincetonHz - interStage2TwinHz) * b;
    interStageHighpass[0].setCutoff(oversampledRate, corner1);
    interStageHighpass[1].setCutoff(oversampledRate, corner2);

    // Stage 2 (the "V2" position in both parents) - Breakup pulls its mu
    // from a 12AX7 toward a 12AT7, Princeton's own researched
    // substitution, rather than just retuning inputToGridVolts.
    auto stage2Params = stages[1].getParameters();
    stage2Params.mu = muTwin + (muPrinceton - muTwin) * b;
    stages[1].setParameters(stage2Params);

    auto dt = 1.0 / oversampledRate;
    auto rcLow = 1.0 / (twoPi * midBumpLowHz);
    midLpLowAlpha = dt / (rcLow + dt);
    auto rcHigh = 1.0 / (twoPi * midBumpHighHz);
    midLpHighAlpha = dt / (rcHigh + dt);
}

void FenderHybridReverbPreamp::reset()
{
    preGainHighpass.reset();
    for (auto& hp : interStageHighpass)
        hp.reset();
    brightHighpass.reset();
    midLpLowState = 0.0;
    midLpHighState = 0.0;
    oversampler.reset();
}

float FenderHybridReverbPreamp::processOversampledSample(float x) noexcept
{
    auto b = static_cast<double>(breakup);
    auto driveSlope = static_cast<float>(driveSlopeTwin + (driveSlopePrinceton - driveSlopeTwin) * b);
    auto interStageMakeupGain = interStageMakeupGainTwin + (interStageMakeupGainPrinceton - interStageMakeupGainTwin) * static_cast<float>(b);

    auto driveGain = 1.0f + gain * driveSlope;
    float signal = x * driveGain;

    signal = stages[0].processSample(signal);

    // Growl: Princeton's fixed bandpass bump, made independently
    // adjustable in strength (see header) - spliced in at the same
    // point Princeton itself applies it, right after the first stage.
    midLpLowState += midLpLowAlpha * (static_cast<double>(signal) - midLpLowState);
    midLpHighState += midLpHighAlpha * (static_cast<double>(signal) - midLpHighState);
    signal += growl * midBumpGain * static_cast<float>(midLpHighState - midLpLowState);

    signal = interStageHighpass[0].processSample(signal) * interStageMakeupGain;

    signal = stages[1].processSample(signal);
    signal = interStageHighpass[1].processSample(signal) * interStageMakeupGain;

    // Twin's own mid-cascade Volume + bright-cap mechanism, unchanged -
    // a real Twin-only feature kept as part of the hybrid's backbone.
    auto volumeGain = 0.35f + volume * 1.65f;
    signal *= volumeGain;
    auto brightStrength = 0.9f * (1.0f - volume);
    signal += brightStrength * brightHighpass.processSample(signal);

    signal = stages[2].processSample(signal);

    // Breakup also nudges the output normalisation - Princeton's hotter
    // per-stage drive needs slightly more headroom brought back down
    // than Twin's gentler cascade does at the same Gain.
    auto divisorBase = 1.5f + (1.9f - 1.5f) * static_cast<float>(b);
    auto divisorSlope = 0.9f + (1.3f - 0.9f) * static_cast<float>(b);
    return signal / (divisorBase + gain * divisorSlope);
}

void FenderHybridReverbPreamp::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
