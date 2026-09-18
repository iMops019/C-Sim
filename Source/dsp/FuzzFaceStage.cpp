#include "FuzzFaceStage.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // Envelope follower time constants - fast enough to track a pick
    // attack, slow enough that the bias shift doesn't chatter within a
    // single note's decay. Same class of time constant as
    // PowerAmpStage's own Sag envelope.
    constexpr double attackSeconds = 0.004;
    constexpr double releaseSeconds = 0.09;

    // The envelope range over which the touch-sensitive cleanup happens.
    // Below envLow the stage runs at its quietest (cleanest) drive
    // multiplier; above envHigh it runs at full drive. This band sits
    // inside typical guitar playing dynamics (picking softly vs. digging
    // in, or rolling the guitar volume back), matching the real,
    // documented "cleans up with guitar volume" behavior.
    constexpr double envLow = 0.05;
    constexpr double envHigh = 0.4;
    constexpr float cleanupFloor = 0.2f; // minimum drive multiplier even at the quietest input

    constexpr float minDrive = 0.3f;
    constexpr float maxDrive = 3.0f;

    // Shared steepness for both halves - a plain, per-half-identical
    // tanh has the same slope at the origin for positive and negative
    // signal, so it stays symmetric (no spurious even harmonic) for a
    // small, unclipped signal, exactly where the real touch-sensitive
    // cleanup should leave it nearly clean.
    constexpr float steepness = 4.5f;

    // The asymmetry itself - a lower ceiling on the positive half only -
    // is blended in by drive multiplier (the same envelope-driven value
    // that scales overall drive), so it's negligible at quiet input and
    // reaches its full, designed asymmetry only once the signal is
    // genuinely driving the stage. That keeps the asymmetric "splatty"
    // character (a documented judgment call standing in for the real
    // circuit's unequal transistor characteristics, not a measured
    // per-unit value) tied to the same real, sourced touch-sensitivity
    // this whole model is built around, rather than being an
    // amplitude-independent artifact that would leak into quiet passages
    // too.
    constexpr float positiveCeilingAtFullDrive = 0.6f;

    float smoothstep(float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

FuzzFaceStage::FuzzFaceStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      oversampler(sampleRateToUse)
{
    auto oversampledRate = sampleRateToUse * 4.0;
    envelopeCoeffAttack = 1.0 - std::exp(-1.0 / (attackSeconds * oversampledRate));
    envelopeCoeffRelease = 1.0 - std::exp(-1.0 / (releaseSeconds * oversampledRate));
}

void FuzzFaceStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    auto oversampledRate = sampleRate * 4.0;
    envelopeCoeffAttack = 1.0 - std::exp(-1.0 / (attackSeconds * oversampledRate));
    envelopeCoeffRelease = 1.0 - std::exp(-1.0 / (releaseSeconds * oversampledRate));

    oversampler = Oversampler4x(sampleRate);
    reset();
}

void FuzzFaceStage::setFuzz(float amount) { fuzz = std::clamp(amount, 0.0f, 1.0f); }

void FuzzFaceStage::reset() noexcept
{
    envelopeState = 0.0;
    oversampler.reset();
}

float FuzzFaceStage::processOversampledSample(float x) noexcept
{
    auto rectified = std::abs(static_cast<double>(x));
    auto coeff = (rectified > envelopeState) ? envelopeCoeffAttack : envelopeCoeffRelease;
    envelopeState += coeff * (rectified - envelopeState);

    auto cleanupFactor = smoothstep(static_cast<float>((envelopeState - envLow) / (envHigh - envLow)));
    auto driveMultiplier = cleanupFloor + (1.0f - cleanupFloor) * cleanupFactor;

    auto baseDrive = minDrive + fuzz * (maxDrive - minDrive);
    auto signal = x * baseDrive * driveMultiplier;

    auto shaped = std::tanh(signal * steepness);
    auto positiveCeiling = 1.0f - driveMultiplier * (1.0f - positiveCeilingAtFullDrive);
    return (shaped >= 0.0f) ? shaped * positiveCeiling : shaped;
}

void FuzzFaceStage::processBlock(const float* input, float* output, int numSamples)
{
    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
