#pragma once

#include "Oversampler4x.h"

// Models the Dallas Arbiter Fuzz Face - researched via published circuit
// analyses (Coda Effects' "Sunface/Fuzzface circuit analysis", R.G.
// Keen's "The Technology of the Fuzz Face"). Real, sourced facts this
// model is built from:
//
//  - Two transistors in a feedback-biased cascade: "the emitter of the
//    second transistor is linked directly to the base of the first one
//    through a [~100k] resistor... a part of the current goes back to
//    the first transistor, creating an amplification loop" (Coda). The
//    Fuzz pot sits at Q2's emitter and controls how much of that
//    feedback current reaches Q1's base, setting overall drive/
//    saturation - modeled here as a drive gain into an asymmetric
//    clipper, the same idiom already used for MorningGloryStage's own
//    feedback-loop clipper, not a literal two-transistor SPICE solve.
//  - Keen's own simulation work finds "a definite sweet spot for
//    musical sounding clipping at transistor gains of about 80-110" -
//    i.e. this is a circuit that saturates hard well before unity-gain
//    headroom is used up, not a mild boost.
//  - The circuit's single most famous, most-cited real trait: because
//    the feedback loop is DC-coupled, Q1's actual bias point shifts
//    with the input signal's own level - genuinely touch/volume
//    sensitive, cleaning up markedly when the input drops (rolling
//    back the guitar's volume knob, or just playing softer) rather
//    than just scaling a fixed clipping curve. That's a real, testable,
//    previously-unmodeled-in-this-toolkit characteristic, and is the
//    whole reason players reach for a Fuzz Face over a fixed-curve
//    fuzz/distortion. Modeled with an envelope follower (same
//    time-constant idiom as PowerAmpStage's Sag) that scales the
//    effective drive down as input level falls, on top of the Fuzz
//    knob's own base drive - not a literal transistor bias-point
//    solve, but a direct, testable stand-in for the documented effect.
//
// Clipping itself is tanh-based, the same asymmetric-clipper family
// already established for MorningGloryStage, but with a twist tied
// directly to the touch-sensitivity story above: the positive half's
// clip ceiling is only pulled down (creating the real asymmetry, and
// the even-harmonic content it produces) as the envelope-driven drive
// multiplier rises, so a quiet, unclipped signal stays genuinely
// symmetric/clean while a fully-driven one gets Fuzz Face's documented
// "splatty"/gated asymmetric character - not an amplitude-independent
// artifact that would leak into quiet passages too. See
// FuzzFaceStage.cpp for the exact blend.
class FuzzFaceStage
{
public:
    explicit FuzzFaceStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setFuzz(float amount); // 0..1

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    double sampleRate;
    float fuzz = 0.5f;

    // Envelope follower tracking input level, driving the real circuit's
    // documented touch-sensitive bias shift.
    double envelopeCoeffAttack = 0.0;
    double envelopeCoeffRelease = 0.0;
    double envelopeState = 0.0;

    Oversampler4x oversampler;
};
