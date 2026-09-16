#pragma once

// A standard envelope-follower noise gate: fast open, user-controlled
// release, ramps to silence below Threshold. High-gain amps are hissy
// between notes at high Gain - for the metal build this is part of the
// genre's sound (it shapes the tail of a chug), not just cleanup, so it's
// its own reusable toolkit module rather than baked into the preamp.
// Framework-agnostic, matching the rest of the toolkit.
class NoiseGate
{
public:
    explicit NoiseGate(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setThresholdDb(float thresholdDb);
    void setReleaseMs(float releaseMs);

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;

    double sampleRate;
    float thresholdGain = 0.0f;
    float releaseSeconds = 0.15f;

    float envelope = 0.0f;
    float gateGain = 1.0f;

    double envCoeff = 0.0, gateUpCoeff = 0.0, gateDownCoeff = 0.0;

    void updateCoefficients();
};
