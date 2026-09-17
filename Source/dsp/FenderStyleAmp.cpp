#include "FenderStyleAmp.h"

namespace
{
    float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
}

FenderStyleAmp::FenderStyleAmp(double sampleRate) : oversampler(sampleRate)
{
}

void FenderStyleAmp::setGain(float amount) noexcept
{
    gain = clamp01(amount);
}

void FenderStyleAmp::setVolume(float amount) noexcept
{
    volume = clamp01(amount);
}

void FenderStyleAmp::setMu(float mu) noexcept
{
    auto params = stage.getParameters();
    if (params.mu != static_cast<double>(mu))
    {
        params.mu = static_cast<double>(mu);
        stage.setParameters(params);
    }
}

void FenderStyleAmp::reset() noexcept
{
    oversampler.reset();
}

void FenderStyleAmp::processBlock(const float* input, float* output, int numSamples) noexcept
{
    // Deliberately modest, matching the project's own researched Fender
    // preamps (FenderTwinReverbPreamp etc: 1x-1.85x per stage) rather than
    // the Lab tab's raw TriodeStagePedal (1x-16x) - that range is meant
    // for a DIY building block the user chains with their own downstream
    // taming (a Diode Clipper, a deliberately pulled-down Level knob);
    // this seed amp has no such safety valve between here and the tone
    // stack/power amp, and KorenTriodeStage only self-normalises to unit
    // output AT unit input - driving it 16x past that doesn't clamp back
    // down, it just keeps growing, which is a real bug this project hit
    // (measured via a throwaway diagnostic: a realistic hard-picked power
    // chord came out the triode stage at ~14x its input peak).
    auto driveGain = 1.0f + gain * 3.0f; // 1x-4x into the stage

    oversampler.processBlock(input, output, numSamples,
                              [this, driveGain](float x) { return stage.processSample(x * driveGain); });

    for (int n = 0; n < numSamples; ++n)
        output[n] *= volume;
}
