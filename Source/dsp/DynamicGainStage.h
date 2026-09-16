#pragma once

#include "KorenTriodeStage.h"

// The emo build's "edge of breakup" gain stage: clean on light touch,
// light grit on harder strums - not one fixed clipping curve. An
// envelope follower tracks recent input level and scales how hard the
// signal is driven into the Koren nonlinearity, so quiet playing stays
// mostly clean while digging in pushes further into compression/breakup.
class DynamicGainStage
{
public:
    explicit DynamicGainStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setSensitivity(float amount); // 0..1: how much playing dynamics affect drive
    void setBaseDrive(float amount);   // 0..1: baseline drive even at quiet playing

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;

    double sampleRate;
    float sensitivity = 0.6f;
    float baseDrive = 0.15f;

    float envelope = 0.0f;
    double attackCoeff = 0.0, releaseCoeff = 0.0;

    KorenTriodeStage stage;
};
