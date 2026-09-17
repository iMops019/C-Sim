#include "PrecisionDriveStage.h"

#include <algorithm>
#include <cmath>

namespace
{
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

    constexpr double attackMinHz = 60.0;  // Attack=0: thick/bassy, closest to a stock TS's own low end
    constexpr double attackMaxHz = 350.0; // Attack=1: tight/picky, aggressively cut for palm mutes

    // Tube Screamer family's well-documented pre-clip mid bump - fixed,
    // not user-adjustable, the same way it isn't a knob on the real
    // circuit either.
    constexpr double midBumpHz = 720.0;
    constexpr double midBumpQ = 0.7;
    constexpr double midBumpGainDb = 6.0;

    constexpr double brightShelfHz = 3000.0;
    constexpr double brightMaxGainDb = 9.0;
}

float PrecisionDriveStage::Biquad::process(float x) noexcept
{
    auto in = static_cast<double>(x);
    auto out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    return static_cast<float>(out);
}

void PrecisionDriveStage::Biquad::reset() noexcept
{
    z1 = z2 = 0.0;
}

void PrecisionDriveStage::designMidBump(Biquad& b, double sr)
{
    auto a = std::pow(10.0, midBumpGainDb / 40.0);
    auto w0 = 2.0 * M_PI * midBumpHz / sr;
    auto cosw0 = std::cos(w0);
    auto alpha = std::sin(w0) / (2.0 * midBumpQ);

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

void PrecisionDriveStage::designBrightShelf(Biquad& b, double sr, float amount)
{
    // RBJ Audio EQ Cookbook high-shelf, shelf slope S=1.
    auto gainDb = std::clamp(amount, 0.0f, 1.0f) * brightMaxGainDb;
    auto a = std::pow(10.0, gainDb / 40.0);
    auto w0 = 2.0 * M_PI * brightShelfHz / sr;
    auto cosw0 = std::cos(w0);
    auto sinw0 = std::sin(w0);
    constexpr double shelfSlope = 1.0;
    auto alpha = (sinw0 / 2.0) * std::sqrt((a + 1.0 / a) * (1.0 / shelfSlope - 1.0) + 2.0);
    auto twoSqrtAAlpha = 2.0 * std::sqrt(a) * alpha;

    auto b0 = a * ((a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha);
    auto b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw0);
    auto b2 = a * ((a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha);
    auto a0 = (a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha;
    auto a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw0);
    auto a2 = (a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha;

    b.b0 = b0 / a0;
    b.b1 = b1 / a0;
    b.b2 = b2 / a0;
    b.a1 = a1 / a0;
    b.a2 = a2 / a0;
}

PrecisionDriveStage::PrecisionDriveStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse), oversampler(sampleRateToUse)
{
    attackFilter.setCutoff(sampleRate, attackMinHz);
    designMidBump(midBump, sampleRate);
    designBrightShelf(brightShelf, sampleRate, 0.0f);
}

void PrecisionDriveStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampler = Oversampler4x(sampleRate);
    designMidBump(midBump, sampleRate);
    reset();
}

void PrecisionDriveStage::setAttack(float amount)
{
    auto a = std::clamp(amount, 0.0f, 1.0f);
    auto cutoffHz = attackMinHz + a * (attackMaxHz - attackMinHz);
    attackFilter.setCutoff(sampleRate, cutoffHz);
}

void PrecisionDriveStage::setDrive(float amount)
{
    auto a = std::clamp(amount, 0.0f, 1.0f);
    // Steep early taper - real-world reports of this circuit describe
    // usable saturation from as little as 1-2 out of 10 on the Drive
    // knob, i.e. it's sensitive low in its range rather than linear.
    driveGain = 1.0f + std::pow(a, 1.5f) * 14.0f;
}

void PrecisionDriveStage::setBright(float amount)
{
    designBrightShelf(brightShelf, sampleRate, std::clamp(amount, 0.0f, 1.0f));
}

void PrecisionDriveStage::reset() noexcept
{
    attackFilter.reset();
    midBump.reset();
    brightShelf.reset();
    oversampler.reset();
}

void PrecisionDriveStage::processBlock(const float* input, float* output, int numSamples)
{
    for (int n = 0; n < numSamples; ++n)
    {
        auto shaped = attackFilter.processSample(input[n]);
        output[n] = midBump.process(shaped);
    }

    oversampler.processBlock(output, output, numSamples,
                              [this](float x) { return std::tanh(driveGain * x); });

    for (int n = 0; n < numSamples; ++n)
        output[n] = brightShelf.process(output[n]);
}
