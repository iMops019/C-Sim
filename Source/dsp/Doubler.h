#pragma once

#include <vector>

// A studio "double-track" simulator: mixes in a second voice read from a
// short delay line whose read position is slowly modulated by an LFO, so
// the copy drifts continuously sharp/flat around the dry pitch - exactly
// like a second guitarist can never quite match the first take, without
// needing any pitch-detection or grain crossfades. A moving delay line
// IS a Doppler-shifted copy; that's the classic chorus/doubler trick.
//
// Framework-agnostic, matching the rest of the toolkit.
class Doubler
{
public:
    explicit Doubler(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setDetune(float amount); // 0..1: LFO modulation depth
    void setRateHz(float hz);     // LFO speed
    void setMix(float amount);    // 0..1 dry/wet

    // Offsets this instance's LFO phase by `turns` (0..1 of a cycle) so a
    // stereo pair using identical Detune/Rate can still drift apart from
    // each other instead of wobbling in lockstep - a real double-tracked
    // take panned wide does the same thing.
    void setLfoPhaseOffset(float turns) noexcept;

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;

    double sampleRate;
    float detune = 0.4f;
    float rateHz = 0.6f;
    float mix = 0.5f;
    float phaseOffsetTurns = 0.0f;

    double lfoPhase = 0.0;

    std::vector<float> buffer;
    int bufferLength = 0;
    int writeIndex = 0;

    static constexpr float centerDelayMs = 22.0f;
    static constexpr float maxDepthMs = 6.0f; // delay swing at full Detune
};
