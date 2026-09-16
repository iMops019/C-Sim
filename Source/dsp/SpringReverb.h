#pragma once

#include <array>
#include <vector>

#include "DelayAllpass.h"

// A spring reverb tank, modeled as a dispersive delay line rather than a
// generic plate/hall algorithm - the metallic "boing" is genre-defining
// for the emo/math-rock build and comes specifically from dispersion: a
// real spring carries different frequencies at different effective
// speeds, smearing a transient into that characteristic ringing decay.
// Modeled here as a chain of delay-based allpass filters (each unity-gain
// but with a different delay length, so each imparts a different
// frequency-dependent group delay) feeding a damped feedback loop that
// controls decay time.
class SpringReverb
{
public:
    static constexpr int numAllpassStages = 4;

    explicit SpringReverb(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setDecay(float amount); // 0..1
    void setMix(float amount);   // 0..1 dry/wet

    void reset();

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;
    void buildDelayLines();

    double sampleRate;
    float decay = 0.5f;
    float mix = 0.3f;

    std::array<DelayAllpass, numAllpassStages> allpassChain;

    std::vector<float> feedbackDelay;
    size_t feedbackWriteIndex = 0;
    int feedbackDelayLength = 0;
    float dampingState = 0.0f;
};
