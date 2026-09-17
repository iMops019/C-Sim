#pragma once

#include <vector>

// A real-time pitch shifter for the Transpose knob - hear the rig as if
// it were tuned down (or up) without retuning the guitar. Time-domain
// granular approach: two read taps into a delay buffer, each advancing
// through the buffer at `ratio` samples per input sample instead of 1
// (ratio<1 plays back slower/lower, ratio>1 faster/higher). Each tap's
// delay wraps within a fixed grain window, and the two taps are offset
// by half a grain and crossfaded with a raised-cosine window - so when
// one tap's delay wraps (an unavoidable discontinuity for a delay-only
// shift), the other tap is at full window weight and masks the click.
// This is the standard dual-tap delay ("PSOLA-lite") technique behind
// classic hardware granular pitch shifters.
//
// A short grain keeps latency low and playable in a live chain; the
// trade-off (same one those classic units have) is audible warble on
// very low notes, where the grain covers only a few cycles. A phase
// vocoder would trade latency for cleaner tracking - out of scope here.
//
// Framework-agnostic, matching the rest of the toolkit.
class PitchShifter
{
public:
    explicit PitchShifter(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setSemitones(float semitones); // -12..+12
    void setMix(float amount);          // 0..1 dry/wet

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;
    static float grainWindow(float phase01) noexcept;
    float readInterpolated(double delaySamples) const noexcept;

    double sampleRate;
    float semitones = 0.0f;
    float ratio = 1.0f;
    float mix = 1.0f;

    std::vector<float> buffer;
    int bufferLength = 0;
    int writeIndex = 0;

    double grainSamples = 0.0;
    double tapDelay1 = 0.0; // 0..grainSamples

    void rebuildForSampleRate();
};
