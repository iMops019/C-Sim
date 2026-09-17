#pragma once

#include "EnvelopeUtils.h"

// A single-band dynamic EQ locked to the 120-180Hz "palm mute" pocket -
// the well-known problem this targets: a hard chug builds up a huge,
// narrow spike right there that a static EQ cut can't address without
// also thinning out open chords/notes that live in the same range.
// Instead, this only pulls that band down when a hot, percussive burst
// actually appears in it, and releases immediately once the burst
// decays - thick during open notes, tight during chugs, same signal
// path either way.
//
// Not a broadband dynamics stage like DynamicGainStage (which reacts to
// overall level and scales drive into a nonlinearity) - this is
// frequency-selective and linear (no waveshaping at all), a genuinely
// different tool: a "dynamic EQ" rather than a dynamics-driven gain
// stage.
//
// Implementation: split the input into the target band (two cascaded
// one-pole filters - a highpass then a lowpass, the same simple, easily
// verified primitive CouplingHighpass already uses elsewhere in this
// toolkit, rather than a resonant biquad bandpass) and everything else
// (the residual, computed as input-minus-band so recombination is exact
// and phase-coherent by construction). An envelope follower on the
// band's own level drives a downward-expander-style gain reduction
// applied ONLY to that band before recombining - the residual never
// changes, so content outside 120-180Hz is completely unaffected
// regardless of how hard the target band gets hit.
class PalmMuteTamer
{
public:
    explicit PalmMuteTamer(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setAmount(float amount);      // 0..1: how much reduction is applied when triggered (0 = off, 1 = up to -6dB)
    void setSensitivity(float amount); // 0..1: how easily a burst in the band triggers (higher = triggers on lighter picking)

    void reset();

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;
    void updateFilterCoefficients();

    double sampleRate;
    float amount = 0.6f;
    float sensitivity = 0.5f;

    // Band-extraction filters: two cascaded one-pole highpasses and two
    // cascaded one-pole lowpasses (12dB/octave each side) rather than
    // one of each - a single one-pole slope (6dB/octave) turned out not
    // to be nearly selective enough in practice: a hot 400Hz note still
    // leaked ~40% of its amplitude into the "band" bucket and got
    // needlessly reduced (caught by PalmMuteTamerTest, not just assumed
    // fine on paper). Two poles each side tightens that to a small
    // fraction while still passing enough of the actual 120-180Hz pocket
    // through for real reduction there.
    double bandHpAlpha = 0.0;
    double bandHpPrevIn[2] = { 0.0, 0.0 }, bandHpPrevOut[2] = { 0.0, 0.0 };
    double bandLpAlpha = 0.0;
    double bandLpState[2] = { 0.0, 0.0 };

    double envelope = 0.0;
    double attackCoeff = 0.0, releaseCoeff = 0.0;

    static constexpr double bandHighpassHz = 110.0;
    static constexpr double bandLowpassHz = 190.0;
    static constexpr double maxReductionDb = -6.0;
};
