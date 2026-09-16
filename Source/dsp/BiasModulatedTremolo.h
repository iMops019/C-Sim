#pragma once

#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// Blackface Fender tremolo (the amp calls it "vibrato") modulates a
// preamp tube's grid bias with an LFO rather than simply multiplying the
// output by an amplitude envelope. Shifting the operating point changes
// both gain AND harmonic content as it moves, which is what gives it the
// shimmery quality distinct from a plain tremolo effect.
//
// Like every other nonlinear stage in this toolkit, the Koren evaluation
// runs inside a 4x oversampled block: without it, high harmonics fold
// back down near/below Nyquist and beat against the fundamental,
// producing spurious low-frequency envelope wobble that has nothing to
// do with the LFO - confirmed the hard way by BiasModulatedTremoloTest,
// which caught real envelope variation even at depth=0 before this was
// added.
class BiasModulatedTremolo
{
public:
    explicit BiasModulatedTremolo(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setRateHz(float hz);
    void setDepth(float amount); // 0..1

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    double sampleRate;
    float rateHz = 5.0f;
    float depth = 0.5f;
    double phase = 0.0;

    KorenTriodeStage stage;
    Oversampler4x oversampler;

    static constexpr double nominalBias = -1.5;
    static constexpr double biasSwing = 1.2; // volts the bias can shift at full depth
};
