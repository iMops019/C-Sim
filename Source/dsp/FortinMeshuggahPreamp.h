#pragma once

#include "CouplingHighpass.h"
#include "DiodeClipperStage.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// The Fortin Meshuggah's preamp: a Marshall JCM800 cascade with the
// "Jose mod" - a boosted first-stage plate load for extra gain, two
// independent Gain controls (matching the real amp's own two-gain-stage
// control set, unlike the single Gain knob every other preamp in this
// toolkit uses), a bright cap on the second stage, and - the genuinely
// defining feature - a pre-tone-stack Master Volume stage with hard
// clipping diodes in the signal path. That diode master is what makes
// this preamp a real, audible contrast to MesaRectifierPreamp: the
// Mesa's "extra edge" comes entirely from tube-cascade voicing/filtering
// choices with no clipper anywhere, while this amp adds an actual hard,
// symmetric clipping stage on top of its own tube cascade - a harder,
// more clamped, more "modern djent" character than pure tube saturation
// can produce on its own.
//
// Circuit facts behind this, from published teardown/forum analysis
// (sevenstring.org's "Fortin Meshuggah amps" thread and related
// discussion) - informed approximations, not a literal schematic
// transcription, same honesty level as MesaRectifierPreamp/
// MetalPreampChain elsewhere in this toolkit:
//  - Based on a JCM800 with the well-known "Jose mod": a goosed
//    (increased) plate load resistor on the first gain stage (V1a) for
//    more gain than a stock JCM800 gets there.
//  - Two independent gain controls (Gain 1, Gain 2), one per cascaded
//    stage - modeled here as two separate setGain1/setGain2 calls,
//    rather than collapsing to one Drive knob the way this toolkit's
//    other cascades do, since having two really is part of the real
//    amp's character (you can push one stage harder than the other).
//  - A bright cap on the second stage (Gain 2) - a small fixed coupling
//    cap bypassing part of that stage's gain network, boosting treble
//    presence. Modeled as a fixed-corner high-shelf blend (not a Gain-
//    swept one like Mesa's voicing filter - a small bright cap's own
//    corner doesn't move with the pot the way Mesa's larger voicing
//    network's does).
//  - The defining feature: a pre-tone-stack Master Volume stage with
//    clipping diodes (20V zeners, wired symmetrically so both polarities
//    clip the same) - reuses this toolkit's existing DiodeClipperStage
//    (same hard, symmetric antiparallel-diode topology) rather than a
//    new clipper model, driven harder as the Master control rises.
// The passive Bass/Mid/Treble tone stack that follows the diode master
// in the real amp ("the rest is stock JCM800") is NOT part of this
// module, same scope choice as MesaRectifierPreamp/MetalPreampChain - it
// lives at the Pedal layer (see FortinMeshuggahPreampPedal), reusing the
// existing FenderToneStack machinery with Marshall-typical component
// values instead of duplicating tone-stack math here.
class FortinMeshuggahPreamp
{
public:
    explicit FortinMeshuggahPreamp(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setGain1(float newGain); // 0..1 - V1a, the goosed first stage
    void setGain2(float newGain); // 0..1 - V1b, with its own bright cap
    void setMaster(float newMaster); // 0..1 - how hard the cascade hits the diode-clipping master stage

    void reset();

    // Not yet optimised for zero-allocation real-time use (allocates a
    // scratch buffer per call) - matches this toolkit's other cascades'
    // current verification-first scope.
    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    double sampleRate;
    float gain1 = 0.6f;
    float gain2 = 0.6f;
    float master = 0.5f;

    CouplingHighpass preGainHighpass;    // amp input coupling - Marshalls run a tighter corner than a Fender/Mesa input
    CouplingHighpass interStageHighpass; // between V1a and V1b, oversampled rate
    CouplingHighpass brightHighpass;     // Gain 2's bright cap - fixed corner, blended as a shelf (see .cpp)

    KorenTriodeStage stage1; // V1a - goosed plate load
    KorenTriodeStage stage2; // V1b - bright cap stage

    DiodeClipperStage masterClipper; // the pre-tone-stack diode master

    Oversampler4x oversampler;
};
