#include "GraphicEQ.h"

#include <algorithm>
#include <cmath>

namespace
{
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

    // ~0.74 octave spacing across the whole 100Hz-10kHz range (a fixed
    // ratio of ~1.668 per band), not ISO thirds - chosen to land exactly
    // on 100Hz and 10kHz at the ends with even spacing between.
    constexpr std::array<double, GraphicEQ::numBands> bandFrequencies {
        100.0, 170.0, 280.0, 460.0, 770.0, 1300.0, 2200.0, 3600.0, 6000.0, 10000.0
    };

    // Shared Q for every band: narrow enough that a boost is felt where
    // you put it, wide enough that adjacent bands (spaced ~0.74 octave
    // apart here) cross close to -3dB instead of leaving a gap or piling
    // up into a ripple when several neighbors are boosted together.
    constexpr double bandQ = 1.4;
}

void GraphicEQ::designPeaking(Biquad& b, double freqHz, double gainDb, double q, double sampleRate)
{
    auto a = std::pow(10.0, gainDb / 40.0);
    auto w0 = 2.0 * M_PI * freqHz / sampleRate;
    auto cosw0 = std::cos(w0);
    auto alpha = std::sin(w0) / (2.0 * q);

    auto b0 = 1.0 + alpha * a;
    auto b1 = -2.0 * cosw0;
    auto b2 = 1.0 - alpha * a;
    auto a0 = 1.0 + alpha / a;
    auto a1 = -2.0 * cosw0;
    auto a2 = 1.0 - alpha / a;

    b.b0 = b0 / a0;
    b.b1 = b1 / a0;
    b.b2 = b2 / a0;
    b.a1 = a1 / a0;
    b.a2 = a2 / a0;
}

float GraphicEQ::Biquad::process(float x) noexcept
{
    auto in = static_cast<double>(x);
    auto out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    return static_cast<float>(out);
}

void GraphicEQ::Biquad::reset() noexcept
{
    z1 = z2 = 0.0;
}

const std::array<double, GraphicEQ::numBands>& GraphicEQ::getBandFrequencies() noexcept
{
    return bandFrequencies;
}

GraphicEQ::GraphicEQ(double sampleRateToUse)
    : sampleRate(sampleRateToUse)
{
    for (int band = 0; band < numBands; ++band)
        updateBand(band);
}

void GraphicEQ::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    for (int band = 0; band < numBands; ++band)
        updateBand(band);
    reset();
}

void GraphicEQ::setBandGainDb(int band, float gainDb)
{
    if (band < 0 || band >= numBands)
        return;

    gainsDb[static_cast<size_t>(band)] = std::clamp(gainDb, -15.0f, 15.0f);
    updateBand(band);
}

void GraphicEQ::updateBand(int band)
{
    designPeaking(biquads[static_cast<size_t>(band)], bandFrequencies[static_cast<size_t>(band)],
                   gainsDb[static_cast<size_t>(band)], bandQ, sampleRate);
}

void GraphicEQ::reset() noexcept
{
    for (auto& b : biquads)
        b.reset();
}

float GraphicEQ::processSample(float input) noexcept
{
    auto signal = input;
    for (auto& b : biquads)
        signal = b.process(signal);
    return signal;
}

void GraphicEQ::processBlock(const float* input, float* output, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
        output[n] = processSample(input[n]);
}
