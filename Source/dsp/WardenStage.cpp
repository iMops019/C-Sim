#include "WardenStage.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Attack/Release ranges - independently adjustable, unlike
    // DynaCompStage's fixed single time constant. A slow Attack is what
    // actually lets a tapped note's transient speak before the
    // compressor clamps down - see header.
    constexpr double attackMinSeconds = 0.0005;
    constexpr double attackMaxSeconds = 0.030;
    constexpr double releaseMinSeconds = 0.030;
    constexpr double releaseMaxSeconds = 0.400;

    // Fixed threshold - Sustain drives the signal hotter or cooler
    // relative to this rather than moving the threshold itself, matching
    // EarthQuaker's own "how hot the signal is" description.
    constexpr float thresholdDb = -18.0f;

    // Ratio's real range: "reduced counterclockwise" up to "full
    // compression" at maximum.
    constexpr float ratioMin = 1.5f;
    constexpr float ratioMax = 20.0f;

    // Soft-knee width in dB - the optical element's smoother, less
    // abrupt gain-reduction curve versus DynaCompStage's harder knee,
    // representing EarthQuaker's own "more character... than a VCA or
    // FET" description of optical compression.
    constexpr float kneeWidthDb = 12.0f;

    constexpr double toneCutoffHz = 2000.0;
}

WardenStage::WardenStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    attackCoeff = EnvelopeUtils::timeToCoeff(attackMinSeconds + (attackMaxSeconds - attackMinSeconds) * attack, sampleRate);
    releaseCoeff = EnvelopeUtils::timeToCoeff(releaseMinSeconds + (releaseMaxSeconds - releaseMinSeconds) * release, sampleRate);
    toneHighpass.setCutoff(sampleRate, toneCutoffHz);
}

void WardenStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    attackCoeff = EnvelopeUtils::timeToCoeff(attackMinSeconds + (attackMaxSeconds - attackMinSeconds) * attack, sampleRate);
    releaseCoeff = EnvelopeUtils::timeToCoeff(releaseMinSeconds + (releaseMaxSeconds - releaseMinSeconds) * release, sampleRate);
    toneHighpass.setCutoff(sampleRate, toneCutoffHz);
    reset();
}

void WardenStage::setSustain(float amount) { sustain = std::clamp(amount, 0.0f, 1.0f); }
void WardenStage::setRatio(float amount) { ratio = std::clamp(amount, 0.0f, 1.0f); }

void WardenStage::setAttack(float amount)
{
    attack = std::clamp(amount, 0.0f, 1.0f);
    attackCoeff = EnvelopeUtils::timeToCoeff(attackMinSeconds + (attackMaxSeconds - attackMinSeconds) * attack, sampleRate);
}

void WardenStage::setRelease(float amount)
{
    release = std::clamp(amount, 0.0f, 1.0f);
    releaseCoeff = EnvelopeUtils::timeToCoeff(releaseMinSeconds + (releaseMaxSeconds - releaseMinSeconds) * release, sampleRate);
}

void WardenStage::setTone(float amount) { tone = std::clamp(amount, 0.0f, 1.0f); }
void WardenStage::setLevel(float amount) { level = std::clamp(amount, 0.0f, 1.0f); }

void WardenStage::reset() noexcept
{
    envelope = 0.0f;
    lastOutput = 0.0f;
    toneHighpass.reset();
}

float WardenStage::processSample(float input) noexcept
{
    // Sustain: "how hot the signal is" - a pre-compression drive, not a
    // threshold move (see header).
    auto driveGain = 0.5f + sustain * 2.5f;
    auto driven = input * driveGain;

    // Feedback-style: envelope follows this stage's own previous
    // output.
    auto rectified = std::abs(lastOutput);
    auto coeff = (rectified > envelope) ? attackCoeff : releaseCoeff;
    envelope += static_cast<float>(coeff) * (rectified - envelope);

    auto envDb = 20.0f * std::log10(std::max(envelope, 1.0e-6f));
    auto currentRatio = ratioMin + (ratioMax - ratioMin) * ratio;

    // Soft-knee gain computation - a smooth quadratic transition around
    // the threshold rather than a hard corner, standing in for the
    // optical element's smoother response.
    float gainReductionDb = 0.0f;
    auto delta = envDb - thresholdDb;
    if (delta > kneeWidthDb * 0.5f)
    {
        gainReductionDb = delta * (1.0f - 1.0f / currentRatio);
    }
    else if (delta > -kneeWidthDb * 0.5f)
    {
        auto x = delta + kneeWidthDb * 0.5f;
        gainReductionDb = (1.0f - 1.0f / currentRatio) * (x * x) / (2.0f * kneeWidthDb);
    }

    auto makeupDb = sustain * 4.0f + ratio * 1.0f;
    auto gain = std::pow(10.0f, (makeupDb - gainReductionDb) / 20.0f);

    auto out = driven * gain;
    lastOutput = out;
    return out;
}

void WardenStage::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        auto compressed = processSample(input[n]);

        // Tone: a bidirectional tilt around the highpassed content -
        // cuts treble below centre, boosts it above (see header).
        auto highpassed = toneHighpass.processSample(compressed);
        auto tilt = (tone - 0.5f) * 2.0f; // -1..1
        auto toned = compressed + tilt * 0.8f * highpassed;

        output[n] = toned * (0.4f + level * 1.3f);
    }
}
