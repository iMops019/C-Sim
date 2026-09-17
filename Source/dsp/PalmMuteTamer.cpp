#include "PalmMuteTamer.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;
}

PalmMuteTamer::PalmMuteTamer(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    updateFilterCoefficients();
    attackCoeff = EnvelopeUtils::timeToCoeff(0.003, sampleRate);   // ~3ms - catch the pick transient
    releaseCoeff = EnvelopeUtils::timeToCoeff(0.100, sampleRate);  // ~100ms - snap back once the chug decays
}

void PalmMuteTamer::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    updateFilterCoefficients();
    attackCoeff = EnvelopeUtils::timeToCoeff(0.003, sampleRate);
    releaseCoeff = EnvelopeUtils::timeToCoeff(0.100, sampleRate);
    reset();
}

void PalmMuteTamer::updateFilterCoefficients()
{
    auto dt = 1.0 / sampleRate;

    auto hpRc = 1.0 / (twoPi * bandHighpassHz);
    bandHpAlpha = hpRc / (hpRc + dt);

    auto lpRc = 1.0 / (twoPi * bandLowpassHz);
    bandLpAlpha = dt / (lpRc + dt);
}

void PalmMuteTamer::setAmount(float newAmount)
{
    amount = std::clamp(newAmount, 0.0f, 1.0f);
}

void PalmMuteTamer::setSensitivity(float newSensitivity)
{
    sensitivity = std::clamp(newSensitivity, 0.0f, 1.0f);
}

void PalmMuteTamer::reset()
{
    bandHpPrevIn[0] = bandHpPrevIn[1] = bandHpPrevOut[0] = bandHpPrevOut[1] = 0.0;
    bandLpState[0] = bandLpState[1] = 0.0;
    envelope = 0.0;
}

float PalmMuteTamer::processSample(float inputSample) noexcept
{
    auto x = static_cast<double>(inputSample);

    // Extract the target band: two cascaded one-pole highpasses (same
    // simple primitive as CouplingHighpass elsewhere in this toolkit)
    // then two cascaded one-pole lowpasses - see the header comment on
    // why one of each wasn't selective enough.
    auto hp = x;
    for (int stage : { 0, 1 })
    {
        auto y = bandHpAlpha * (bandHpPrevOut[stage] + hp - bandHpPrevIn[stage]);
        bandHpPrevIn[stage] = hp;
        bandHpPrevOut[stage] = y;
        hp = y;
    }

    auto band = hp;
    for (int stage : { 0, 1 })
    {
        bandLpState[stage] += bandLpAlpha * (band - bandLpState[stage]);
        band = bandLpState[stage];
    }

    // Everything outside the band, computed as a subtraction so
    // recombination below is exact and phase-coherent by construction -
    // this is what stays completely untouched no matter how hard the
    // band gets reduced.
    auto residual = x - band;

    auto rectified = std::abs(band);
    auto envCoeff = rectified > envelope ? attackCoeff : releaseCoeff;
    envelope += envCoeff * (rectified - envelope);

    // Threshold drops (triggers more easily) as Sensitivity rises - a
    // light, low-sensitivity setting only reacts to a genuinely hot
    // chug; a high setting catches lighter palm-muting too.
    // Recalibrated against the two-pole band filter's real achievable
    // range, not guessed: even a full-tilt sustained chug only reaches
    // ~0.2-0.25 envelope here (the two-pole cascade's own passband gain
    // at the target frequencies is well under unity by design - see the
    // header), so thresholds anywhere near "1.0" would never trigger at
    // all in practice, however reasonable they looked on paper.
    constexpr double thresholdAtMinSensitivity = 0.15;
    constexpr double thresholdAtMaxSensitivity = 0.03;
    constexpr double kneeWidth = 0.15; // soft-knee range the reduction ramps in over, not a hard on/off

    auto threshold = thresholdAtMinSensitivity
                    + (thresholdAtMaxSensitivity - thresholdAtMinSensitivity) * static_cast<double>(sensitivity);
    auto excess = std::max(0.0, envelope - threshold);
    auto reductionFraction = std::min(1.0, excess / kneeWidth);

    auto reductionDb = maxReductionDb * static_cast<double>(amount) * reductionFraction;
    auto bandGain = std::pow(10.0, reductionDb / 20.0);

    return static_cast<float>(residual + band * bandGain);
}

void PalmMuteTamer::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
