#include "Oversampler4x.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    // Q factors for the cascaded second-order sections of an 8th-order
    // Butterworth lowpass (4 biquads) - standard pole-angle values:
    // Q_k = 1 / (2*cos((2k-1)*pi/(2*8))) for k=1..4.
    constexpr double butterworthQ[4] = { 0.50979, 0.60134, 0.89996, 2.56291 };

    // Cutoff fraction of the protected Nyquist. Lower = more guard band
    // (less usable bandwidth, but the harmonic content nearest Nyquist -
    // the hardest case to reject - gets pushed further into the filter's
    // stopband instead of sitting right at the transition edge.
    constexpr double cutoffFraction = 0.40;
}

void Oversampler4x::designRbjLowpass(Biquad& b, double cutoffHz, double sampleRate, double q)
{
    auto w0 = 2.0 * M_PI * cutoffHz / sampleRate;
    auto cosw0 = std::cos(w0);
    auto sinw0 = std::sin(w0);
    auto alpha = sinw0 / (2.0 * q);

    auto a0 = 1.0 + alpha;
    b.b0 = ((1.0 - cosw0) / 2.0) / a0;
    b.b1 = (1.0 - cosw0) / a0;
    b.b2 = b.b0;
    b.a1 = (-2.0 * cosw0) / a0;
    b.a2 = (1.0 - alpha) / a0;
}

void Oversampler4x::designButterworthLowpass(Biquad* stages, double cutoffHz, double sampleRate)
{
    for (int i = 0; i < filterOrder; ++i)
        designRbjLowpass(stages[i], cutoffHz, sampleRate, butterworthQ[i]);
}

Oversampler4x::Oversampler4x(double baseSampleRate)
    : baseRate(baseSampleRate)
{
    designButterworthLowpass(stage1x2.up, cutoffFraction * baseRate, 2.0 * baseRate);
    designButterworthLowpass(stage1x2.down, cutoffFraction * baseRate, 2.0 * baseRate);

    designButterworthLowpass(stage2x4.up, cutoffFraction * (2.0 * baseRate), 4.0 * baseRate);
    designButterworthLowpass(stage2x4.down, cutoffFraction * (2.0 * baseRate), 4.0 * baseRate);
}

void Oversampler4x::reset()
{
    stage1x2.reset();
    stage2x4.reset();
}

void Oversampler4x::upsample(const float* input, int numSamples, float* output)
{
    midBuffer.resize(static_cast<size_t>(numSamples) * 2);

    // Zero-stuff base rate -> 2x, gain-compensated, then anti-image filter.
    for (int n = 0; n < numSamples; ++n)
    {
        midBuffer[static_cast<size_t>(n) * 2] = input[n] * 2.0f;
        midBuffer[static_cast<size_t>(n) * 2 + 1] = 0.0f;
    }

    for (auto& stage : stage1x2.up)
        for (auto& s : midBuffer)
            s = stage.process(s);

    // Zero-stuff 2x -> 4x, gain-compensated, then anti-image filter.
    auto total = static_cast<size_t>(numSamples) * 4;
    for (size_t n = 0; n < midBuffer.size(); ++n)
    {
        output[n * 2] = midBuffer[n] * 2.0f;
        output[n * 2 + 1] = 0.0f;
    }

    for (auto& stage : stage2x4.up)
        for (size_t i = 0; i < total; ++i)
            output[i] = stage.process(output[i]);
}

void Oversampler4x::downsample(float* input, int numSamples, float* output)
{
    // Called with `input` aliasing this object's own upsampleBuffer (see
    // processBlock), so filter it in place rather than copying - filtering
    // is safe done this way since nothing later needs the pre-filtered data.
    auto total = static_cast<size_t>(numSamples) * 4;

    // Anti-alias filter at 4x rate (protects the 2x Nyquist), then decimate by 2.
    for (auto& stage : stage2x4.down)
        for (size_t i = 0; i < total; ++i)
            input[i] = stage.process(input[i]);

    midBuffer.resize(static_cast<size_t>(numSamples) * 2);
    for (size_t n = 0; n < midBuffer.size(); ++n)
        midBuffer[n] = input[n * 2];

    // Anti-alias filter at 2x rate (protects the base Nyquist), then decimate by 2.
    for (auto& stage : stage1x2.down)
        for (auto& s : midBuffer)
            s = stage.process(s);

    for (int n = 0; n < numSamples; ++n)
        output[n] = midBuffer[static_cast<size_t>(n) * 2];
}
