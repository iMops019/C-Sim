#include "SpringReverb.h"

#include <algorithm>

namespace
{
    // Delay lengths in milliseconds for the dispersive allpass chain -
    // chosen to be mutually non-integer-related (avoid reinforcing a
    // single resonance) across a few milliseconds each, approximating a
    // real spring's dispersive delay.
    constexpr double allpassMs[SpringReverb::numAllpassStages] = { 3.1, 4.4, 6.7, 9.3 };
    constexpr float allpassGain = 0.6f;
    constexpr double feedbackLoopMs = 45.0;
    constexpr float dampingCoeff = 0.35f; // fixed - springs lose high end as they decay
}

SpringReverb::SpringReverb(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    buildDelayLines();
}

void SpringReverb::buildDelayLines()
{
    for (size_t i = 0; i < allpassChain.size(); ++i)
        allpassChain[i].setup(static_cast<int>(allpassMs[i] / 1000.0 * sampleRate), allpassGain);

    feedbackDelayLength = std::max(1, static_cast<int>(feedbackLoopMs / 1000.0 * sampleRate));
    feedbackDelay.assign(static_cast<size_t>(feedbackDelayLength), 0.0f);
    feedbackWriteIndex = 0;
}

void SpringReverb::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    buildDelayLines();
    reset();
}

void SpringReverb::setDecay(float amount)
{
    // Clamped below 1.0 with margin - this is a feedback loop, and this
    // stays the stable side of it.
    decay = std::clamp(amount, 0.0f, 1.0f) * 0.85f;
}

void SpringReverb::setMix(float amount)
{
    mix = std::clamp(amount, 0.0f, 1.0f);
}

void SpringReverb::reset()
{
    for (auto& ap : allpassChain)
        ap.reset();
    std::fill(feedbackDelay.begin(), feedbackDelay.end(), 0.0f);
    feedbackWriteIndex = 0;
    dampingState = 0.0f;
}

float SpringReverb::processSample(float input) noexcept
{
    auto feedbackOut = feedbackDelay[feedbackWriteIndex];

    float signal = input + feedbackOut * decay;
    for (auto& ap : allpassChain)
        signal = ap.processSample(signal);

    // Damping: a real spring loses high end as the wave travels and decays.
    dampingState += dampingCoeff * (signal - dampingState);

    feedbackDelay[feedbackWriteIndex] = dampingState;
    feedbackWriteIndex = (feedbackWriteIndex + 1) % static_cast<size_t>(feedbackDelayLength);

    return input * (1.0f - mix) + signal * mix;
}

void SpringReverb::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
