#include "PitchShifter.h"

#include <algorithm>
#include <cmath>

namespace
{
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

    // Long enough to cover a few cycles even of a low guitar note (a
    // dropped-tuning low string is ~70-80Hz, ~13-14ms period) while
    // staying short enough to feel playable in a live chain.
    constexpr double grainMs = 45.0;
}

PitchShifter::PitchShifter(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    rebuildForSampleRate();
}

void PitchShifter::rebuildForSampleRate()
{
    grainSamples = grainMs / 1000.0 * sampleRate;
    bufferLength = static_cast<int>(grainSamples * 2.0) + 8;
    buffer.assign(static_cast<size_t>(bufferLength), 0.0f);
}

void PitchShifter::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    rebuildForSampleRate();
    reset();
}

void PitchShifter::setSemitones(float newSemitones)
{
    semitones = std::clamp(newSemitones, -12.0f, 12.0f);
    ratio = std::pow(2.0f, semitones / 12.0f);
}

void PitchShifter::setMix(float amount)
{
    mix = std::clamp(amount, 0.0f, 1.0f);
}

void PitchShifter::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    tapDelay1 = 0.0;
}

float PitchShifter::grainWindow(float phase01) noexcept
{
    // Raised cosine (Hann): 0 at the edges, 1 at the center - two of
    // these offset by half a grain sum to exactly 1 everywhere, so the
    // crossfade never dips or peaks in level as the taps hand off.
    return 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * phase01);
}

float PitchShifter::readInterpolated(double delaySamples) const noexcept
{
    auto readPos = static_cast<double>(writeIndex) - delaySamples;
    while (readPos < 0.0)
        readPos += bufferLength;

    auto i0 = static_cast<int>(readPos) % bufferLength;
    auto frac = static_cast<float>(readPos - std::floor(readPos));
    auto i1 = (i0 + 1) % bufferLength;
    return buffer[static_cast<size_t>(i0)] * (1.0f - frac) + buffer[static_cast<size_t>(i1)] * frac;
}

float PitchShifter::processSample(float input) noexcept
{
    buffer[static_cast<size_t>(writeIndex)] = input;

    // Delay drifts relative to real time by (1-ratio) each sample: ratio<1
    // (pitch down) falls further behind (grows), ratio>1 (pitch up) catches
    // up (shrinks) - wrapping within one grain rather than growing forever.
    tapDelay1 += (1.0 - static_cast<double>(ratio));
    if (tapDelay1 >= grainSamples) tapDelay1 -= grainSamples;
    if (tapDelay1 < 0.0) tapDelay1 += grainSamples;

    auto tapDelay2 = tapDelay1 + grainSamples * 0.5;
    if (tapDelay2 >= grainSamples) tapDelay2 -= grainSamples;

    auto s1 = readInterpolated(tapDelay1);
    auto s2 = readInterpolated(tapDelay2);

    auto w1 = grainWindow(static_cast<float>(tapDelay1 / grainSamples));
    auto w2 = grainWindow(static_cast<float>(tapDelay2 / grainSamples));

    auto wetSum = w1 + w2;
    auto wet = wetSum > 1.0e-6f ? (s1 * w1 + s2 * w2) / wetSum : 0.0f;

    writeIndex = (writeIndex + 1) % bufferLength;

    return input * (1.0f - mix) + wet * mix;
}

void PitchShifter::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
