#include "MorningGloryStage.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;

    constexpr double preGainCutoffHz = 20.0;      // plain input coupling, not a tightening trick
    constexpr double trebleShelfCutoffHz = 1200.0; // Stage 1's gain-dependent treble lift corner
    constexpr double boostLowpassHz = 150.0;       // Boost's extra low end target
    constexpr double brightCutLowpassHz = 2200.0;  // Bright Cut's fixed target corner

    // Asymmetric soft-clip softness per direction - a real, sourced
    // asymmetry (see header) modeled as different tanh steepness per
    // half; the SPECIFIC direction (positive softer, negative harder)
    // is a reasonable modeling choice standing in for the real unequal
    // diode count per direction, not verified against one measured unit.
    constexpr float positiveSoftness = 2.2f;
    constexpr float negativeSoftness = 3.4f;

    // The JFET output buffer's whole documented purpose: independent
    // circuit analysis describes the stock Bluesbreaker as barely
    // reaching unity gain even maxed out - a meaningful, deliberate
    // makeup gain here, not a rounding-error tweak.
    constexpr float outputMakeupGain = 1.8f;
}

MorningGloryStage::MorningGloryStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      preGainHighpass(sampleRateToUse, preGainCutoffHz),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    trebleShelfFilter.setCutoff(oversampledRate, trebleShelfCutoffHz);

    auto dt = 1.0 / oversampledRate;
    auto boostRc = 1.0 / (twoPi * boostLowpassHz);
    boostLpAlpha = dt / (boostRc + dt);
    auto brightRc = 1.0 / (twoPi * brightCutLowpassHz);
    brightLpAlpha = dt / (brightRc + dt);
}

void MorningGloryStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    preGainHighpass.setCutoff(sampleRate, preGainCutoffHz);

    auto oversampledRate = sampleRate * 4.0;
    trebleShelfFilter.setCutoff(oversampledRate, trebleShelfCutoffHz);

    auto dt = 1.0 / oversampledRate;
    auto boostRc = 1.0 / (twoPi * boostLowpassHz);
    boostLpAlpha = dt / (boostRc + dt);
    auto brightRc = 1.0 / (twoPi * brightCutLowpassHz);
    brightLpAlpha = dt / (brightRc + dt);

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void MorningGloryStage::setGain(float amount) { gain = std::clamp(amount, 0.0f, 1.0f); }
void MorningGloryStage::setGainRangeHi(bool hi) { gainRangeHi = hi; }
void MorningGloryStage::setBoost(float amount) { boost = std::clamp(amount, 0.0f, 1.0f); }
void MorningGloryStage::setBrightCut(float amount) { brightCutAmount = std::clamp(amount, 0.0f, 1.0f); }

void MorningGloryStage::reset() noexcept
{
    preGainHighpass.reset();
    trebleShelfFilter.reset();
    boostLpState = 0.0;
    brightLpState = 0.0;
    oversampler.reset();
}

float MorningGloryStage::processOversampledSample(float x) noexcept
{
    // Stage 1: non-inverting boost, with a gain-dependent treble lift -
    // the real circuit's "adjustable... filter with... frequency
    // roll-offs at different gain levels."
    auto driveGain = 1.0f + gain * (gainRangeHi ? 26.0f : 3.0f);
    float signal = x * driveGain;
    auto trebleLift = trebleShelfFilter.processSample(signal);
    signal += gain * 0.6f * trebleLift;

    // Boost: extra low end plus a bit more drive ("grit").
    boostLpState += boostLpAlpha * (static_cast<double>(signal) - boostLpState);
    signal += boost * static_cast<float>(boostLpState) * 4.0f;
    signal *= (1.0f + boost * 0.8f);

    // Stage 2: asymmetric soft clip, approximating the real inverting
    // stage's diode-in-feedback-loop clipper (see header) - different
    // steepness per half is what generates the real circuit's
    // documented even-order harmonic content.
    float clipped = (signal >= 0.0f) ? std::tanh(signal * positiveSoftness)
                                      : std::tanh(signal * negativeSoftness);

    // Bright Cut - crossfade toward a fixed-corner lowpassed copy rather
    // than sweeping the filter's own cutoff per knob move.
    brightLpState += brightLpAlpha * (static_cast<double>(clipped) - brightLpState);
    auto afterBright = clipped * (1.0f - brightCutAmount) + static_cast<float>(brightLpState) * brightCutAmount;

    return afterBright * outputMakeupGain;
}

void MorningGloryStage::processBlock(const float* input, float* output, int numSamples)
{
    std::vector<float> preFiltered(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
        preFiltered[static_cast<size_t>(n)] = preGainHighpass.processSample(input[n]);

    oversampler.processBlock(preFiltered.data(), output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
