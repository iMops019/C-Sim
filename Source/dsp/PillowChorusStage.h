#pragma once

#include <vector>

// Models the Boss CE-2 analog chorus - researched via ElectroSmash's
// circuit analysis and Anasounds' write-up of the BBD chorus technique.
// Real, sourced facts: an LFO (0.3-3.5Hz measured range) drives the
// MN3007 bucket-brigade chip's clock, sweeping its delay time (the BBD
// itself supports 5.12-51.2ms; the CE-2's own chorus voicing uses a
// window within that, commonly cited around 5-40ms) - i.e. exactly the
// same "moving delay line = Doppler-shifted copy" trick as this
// toolkit's own Doubler, described there too. The circuit's own
// documented character is "a very warm and vintage sound" (Anasounds).
//
// This pedal's whole point (per the user's own spec: "light on the
// chorus, like a soft mellow pillow chorus") is deliberately NOT the
// CE-2's full lush range - a single Chorus knob here only ever sweeps a
// narrow slice near the gentle end of the real circuit's own rate/depth/
// mix envelope (see PillowChorusStage.cpp for the exact numbers), a
// documented, deliberate departure from the real pedal's full range, not
// a smaller/incomplete port of it. A separate, dedicated module (rather
// than just retuning the existing Doubler) because the real, sourced
// parameter ranges here (LFO rate, base delay time) are genuinely
// different from Doubler's own "double-tracking" design point (a longer
// base delay with a wider swing, tuned for an audible second voice
// rather than a subtle shimmer).
class PillowChorusStage
{
public:
    explicit PillowChorusStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setChorus(float amount); // 0..1 - single knob, jointly scales rate/depth/mix within a deliberately narrow "light" range

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;

    double sampleRate;
    float chorus = 0.3f;

    double lfoPhase = 0.0;

    std::vector<float> buffer;
    int bufferLength = 0;
    int writeIndex = 0;
};
