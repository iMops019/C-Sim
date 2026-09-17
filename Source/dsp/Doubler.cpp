#include "Doubler.h"

#include <algorithm>
#include <cmath>

namespace
{
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
}

Doubler::Doubler(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    bufferLength = static_cast<int>((centerDelayMs + maxDepthMs + 5.0f) / 1000.0f * static_cast<float>(sampleRate)) + 4;
    buffer.assign(static_cast<size_t>(bufferLength), 0.0f);
}

void Doubler::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    bufferLength = static_cast<int>((centerDelayMs + maxDepthMs + 5.0f) / 1000.0f * static_cast<float>(sampleRate)) + 4;
    buffer.assign(static_cast<size_t>(bufferLength), 0.0f);
    reset();
}

void Doubler::setDetune(float amount)
{
    detune = std::clamp(amount, 0.0f, 1.0f);
}

void Doubler::setRateHz(float hz)
{
    rateHz = std::max(0.01f, hz);
}

void Doubler::setMix(float amount)
{
    mix = std::clamp(amount, 0.0f, 1.0f);
}

void Doubler::setLfoPhaseOffset(float turns) noexcept
{
    phaseOffsetTurns = turns;
}

void Doubler::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    lfoPhase = 0.0;
}

float Doubler::processSample(float input) noexcept
{
    buffer[static_cast<size_t>(writeIndex)] = input;

    auto lfo = std::sin(2.0 * M_PI * (lfoPhase + static_cast<double>(phaseOffsetTurns)));
    lfoPhase += static_cast<double>(rateHz) / sampleRate;
    if (lfoPhase >= 1.0)
        lfoPhase -= 1.0;

    auto delayMs = static_cast<double>(centerDelayMs) + lfo * static_cast<double>(maxDepthMs) * static_cast<double>(detune);
    auto delaySamples = delayMs / 1000.0 * sampleRate;

    auto readPos = static_cast<double>(writeIndex) - delaySamples;
    while (readPos < 0.0)
        readPos += bufferLength;

    auto i0 = static_cast<int>(readPos) % bufferLength;
    auto frac = static_cast<float>(readPos - std::floor(readPos));
    auto i1 = (i0 + 1) % bufferLength;
    auto wet = buffer[static_cast<size_t>(i0)] * (1.0f - frac) + buffer[static_cast<size_t>(i1)] * frac;

    writeIndex = (writeIndex + 1) % bufferLength;

    return input * (1.0f - mix) + wet * mix;
}

void Doubler::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
