#include "PillowChorusStage.h"

#include <algorithm>
#include <cmath>

namespace
{
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

    // A short base delay near the gentle end of the CE-2's own real BBD
    // delay window (5-40ms) - a longer base delay (like Doubler's 22ms,
    // tuned for an audible "second voice") would read as a doubling
    // effect rather than a light shimmer.
    constexpr float baseDelayMs = 9.0f;

    // The whole "soft mellow pillow" spec lives here: even at Chorus=1,
    // the LFO stays in the slow half of the CE-2's real measured
    // 0.3-3.5Hz range, the delay swing stays a couple of milliseconds
    // (a fraction of the real circuit's own usable window), and the wet
    // mix never passes about a third - a deliberate narrowing of the
    // real circuit's own range to the user's explicit "light, not lush"
    // request, not a smaller/incomplete port of it.
    constexpr float maxRateHz = 0.9f;
    constexpr float minRateHz = 0.35f;
    constexpr float maxDepthMs = 2.2f;
    constexpr float maxMix = 0.33f;
}

PillowChorusStage::PillowChorusStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    bufferLength = static_cast<int>((baseDelayMs + maxDepthMs + 5.0f) / 1000.0f * static_cast<float>(sampleRate)) + 4;
    buffer.assign(static_cast<size_t>(bufferLength), 0.0f);
}

void PillowChorusStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    bufferLength = static_cast<int>((baseDelayMs + maxDepthMs + 5.0f) / 1000.0f * static_cast<float>(sampleRate)) + 4;
    buffer.assign(static_cast<size_t>(bufferLength), 0.0f);
    reset();
}

void PillowChorusStage::setChorus(float amount)
{
    chorus = std::clamp(amount, 0.0f, 1.0f);
}

void PillowChorusStage::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    lfoPhase = 0.0;
}

float PillowChorusStage::processSample(float input) noexcept
{
    buffer[static_cast<size_t>(writeIndex)] = input;

    auto rateHz = minRateHz + chorus * (maxRateHz - minRateHz);
    auto lfo = std::sin(2.0 * M_PI * lfoPhase);
    lfoPhase += static_cast<double>(rateHz) / sampleRate;
    if (lfoPhase >= 1.0)
        lfoPhase -= 1.0;

    auto depthMs = chorus * maxDepthMs;
    auto delayMs = static_cast<double>(baseDelayMs) + lfo * static_cast<double>(depthMs);
    auto delaySamples = delayMs / 1000.0 * sampleRate;

    auto readPos = static_cast<double>(writeIndex) - delaySamples;
    while (readPos < 0.0)
        readPos += bufferLength;

    auto i0 = static_cast<int>(readPos) % bufferLength;
    auto frac = static_cast<float>(readPos - std::floor(readPos));
    auto i1 = (i0 + 1) % bufferLength;
    auto wet = buffer[static_cast<size_t>(i0)] * (1.0f - frac) + buffer[static_cast<size_t>(i1)] * frac;

    writeIndex = (writeIndex + 1) % bufferLength;

    auto mix = chorus * maxMix;
    return input * (1.0f - mix) + wet * mix;
}

void PillowChorusStage::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
