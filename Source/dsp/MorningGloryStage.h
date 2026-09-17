#pragma once

#include "CouplingHighpass.h"
#include "Oversampler4x.h"

// Models the JHS Morning Glory V4, built on the Marshall Bluesbreaker
// circuit it's derived from - researched via published schematic/circuit
// analysis of the Bluesbreaker (a two-op-amp-stage design: a
// non-inverting "boost & filter" stage with a gain-dependent treble
// lift, feeding an INVERTING stage with ASYMMETRIC diode clipping inside
// its own feedback loop). Unlike a Tube Screamer's forward series-diode
// clipper (see PrecisionDriveStage), the diodes here sit in the feedback
// path of an inverting op-amp stage, and the well-documented asymmetry
// between the two clipping directions is what gives this circuit family
// its "even-order harmonics... more musical, less harsh" transparent
// character versus a symmetric clipper - a real, testable difference
// from every other clipper already in this toolkit.
//
// JHS's own documented additions on top of the bare Bluesbreaker
// circuit: a JFET output buffer, added specifically because independent
// circuit analysis describes the stock Bluesbreaker as barely reaching
// unity gain even with its own Volume maxed out (a real, sourced
// motivation for a genuine makeup-gain stage here, not a guess); a
// bright-cut high-frequency tame switch; and, specific to V4: a Gain
// toggle (switches between two overall gain ranges) and a Boost circuit
// JHS's own product page describes as adding "enhanced low end and
// grit."
//
// Like PrecisionDriveStage (a different circuit family solving the same
// kind of feedback-loop-clipper problem), the clipping itself is
// approximated with tanh rather than an implicit feedback-loop solve -
// the standard, widely-used approximation for this class of circuit,
// not a literal SPICE-level transcription; here it's evaluated with a
// DIFFERENT steepness on each half of the waveform to reproduce the
// real asymmetry, rather than the single shared steepness a symmetric
// clipper uses. Bright Cut and Boost are modeled as continuously
// adjustable (this toolkit's established "digital advantage" choice -
// see PrecisionDriveStage's own Attack control) even though both are
// real on/off switches on the actual V4 hardware.
class MorningGloryStage
{
public:
    explicit MorningGloryStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setGain(float amount);      // 0..1
    void setGainRangeHi(bool hi);    // V4's Gain toggle: false=Lo (tamer, closer to the vintage Bluesbreaker), true=Hi (hotter modern range)
    void setBoost(float amount);     // 0..1 - V4's boost circuit: extra low end + drive ("grit")
    void setBrightCut(float amount); // 0..1 - continuous version of the real high-cut toggle

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    double sampleRate;
    float gain = 0.3f;
    bool gainRangeHi = false;
    float boost = 0.0f;
    float brightCutAmount = 0.0f;

    CouplingHighpass preGainHighpass;   // ordinary input coupling cap, not a tightening trick
    CouplingHighpass trebleShelfFilter; // Stage 1's own gain-dependent additive treble lift

    // Boost's extra low end - additive-lowpass shelf, same technique as
    // DiezelVH4Preamp's Deep control.
    double boostLpAlpha = 0.0, boostLpState = 0.0;

    // Bright Cut - a fixed-corner one-pole lowpass, crossfaded in by
    // amount rather than sweeping its own cutoff, so the knob's audible
    // effect doesn't depend on retuning a filter inside the per-sample
    // loop.
    double brightLpAlpha = 0.0, brightLpState = 0.0;

    Oversampler4x oversampler;
};
