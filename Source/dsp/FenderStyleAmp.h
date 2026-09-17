#pragma once

#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// Seed of a from-scratch, schematic-driven Fender-style amp build: one
// 12AX7 gain stage (KorenTriodeStage's own default fit) with just Gain
// and Volume - enough to confirm the signal path works end to end before
// growing this into an actual schematic (coupling caps, cathode bias,
// tone stack, tube-type swaps) one stage at a time, same build-order
// pattern as the rest of this toolkit.
class FenderStyleAmp
{
public:
    explicit FenderStyleAmp(double sampleRate);

    void setGain(float amount) noexcept;   // 0..1 - drive into the tube stage
    void setVolume(float amount) noexcept; // 0..1 - output level

    // Real datasheet amplification factor (mu) for the tube occupying
    // this stage - e.g. 100 for a 12AX7, 60 for a 12AT7, ~20 for a
    // 12AU7, ~70 for a 5751. Other Koren-model shape parameters
    // (kg1/kp/kvb/ex) stay at their 12AX7-fitted defaults regardless -
    // same documented simplification already used for the 12AT7
    // substitution in FenderPrincetonReverbPreamp: mu is the real,
    // sourced, most audible spec difference between these related dual
    // triodes, not a full separate curve fit per tube type.
    void setMu(float mu) noexcept;

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    KorenTriodeStage stage;
    Oversampler4x oversampler;

    float gain = 0.5f;
    float volume = 0.7f;
};
