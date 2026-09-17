#pragma once

#include "CouplingHighpass.h"
#include "EnvelopeUtils.h"

// Models the EarthQuaker Devices Warden - an optical (LED+photocell)
// feedback-style compressor, researched via EarthQuaker's own product
// page. Real, sourced control facts behind this model - six independent
// controls, a genuinely richer set than the two-knob DynaCompStage this
// toolkit also has, and the actual reason this pedal (rather than a
// Dyna Comp) is the go-to for mathrock/tapping-heavy playing where quiet
// notes and fast legato passages need to survive alongside compression:
//  - Sustain: EarthQuaker's own words - "controls how hot the signal
//    is... the hotter the signal, the more active the compression" -
//    this is the FEEDBACK-style drive control (also confirmed
//    feedback-style on their own page, the same family DynaCompStage
//    is in), modeled here as a pre-compression drive multiplier rather
//    than a direct threshold knob, matching that description.
//  - Ratio: a genuinely separate compression-ratio control - "full
//    compression at maximum, reduced counterclockwise" - unlike the
//    Dyna Comp, where Sensitivity moves threshold AND ratio together,
//    here Ratio is independent of Sustain.
//  - Attack: "all the way counterclockwise is fast, nearly immediate...
//    turning clockwise slows the reaction time" - independently
//    adjustable, unlike the Dyna Comp's fixed envelope time. A SLOW
//    Attack setting is what actually lets a pick attack or a tapped
//    note's initial transient speak before the compressor clamps down -
//    the real, practical reason this control matters for mathrock-style
//    articulation.
//  - Release: same idea, independently adjustable recovery time.
//  - Tone: "counterclockwise is treble cut, clockwise is treble boost...
//    nearly flat around 11 o'clock" - a tilt-style shelf around a
//    roughly-centred knob position, not a boost-only or cut-only
//    control.
//  - Level: output trim.
// The optical element itself is modelled with a softer compression
// knee than DynaCompStage's more clamped curve - EarthQuaker's own
// description of optical compression having "more character" than a
// VCA/FET design, and a smoother, less abrupt gain-reduction curve is
// the standard way that reputation gets represented in a digital model.
class WardenStage
{
public:
    explicit WardenStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setSustain(float amount); // 0..1 - pre-compression drive ("how hot the signal is")
    void setRatio(float amount);   // 0..1 - compression ratio, independent of Sustain
    void setAttack(float amount);  // 0..1 - 0=fast/immediate, 1=slow (lets transients through)
    void setRelease(float amount); // 0..1 - 0=fast, 1=slow recovery
    void setTone(float amount);    // 0..1 - 0=treble cut, 0.5=flat, 1=treble boost
    void setLevel(float amount);   // 0..1 - output trim

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processSample(float input) noexcept;
    void updateToneFilter();

    double sampleRate;
    float sustain = 0.5f;
    float ratio = 0.5f;
    float attack = 0.2f;
    float release = 0.3f;
    float tone = 0.5f;
    float level = 0.7f;

    float envelope = 0.0f;   // tracks this stage's own previous output - feedback-style, see header
    float lastOutput = 0.0f;
    double attackCoeff = 0.0, releaseCoeff = 0.0;

    // Tone: a simple tilt built from one highpass, blended between a
    // cut and boost side depending on which side of centre Tone sits -
    // same lightweight technique this toolkit already uses for other
    // tilt-style controls.
    CouplingHighpass toneHighpass;
};
