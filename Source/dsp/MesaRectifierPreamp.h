#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// Mesa Boogie Dual/Triple Rectifier's "Modern" channel: three cascaded
// 12AX7 gain stages voiced tight and treble-forward rather than warm,
// with no clipping diodes anywhere in the signal path. Unlike the
// Fortin-style preamp modeled alongside this one (see
// FortinMeshuggahPreamp), the Recto's "extra edge" comes entirely from
// voicing/filtering choices around an otherwise ordinary tube cascade,
// not from a hard clipper - a real, audible difference this toolkit
// should actually represent rather than having every high-gain preamp
// converge on the same sound.
//
// Circuit facts behind the voicing choices below, traced from published
// schematic analysis (see warpedmusician.wordpress.com's "Mesa
// Rectifier Design Concepts" series) - informed approximations, not a
// literal component-by-component transcription, same honesty level as
// MetalPreampChain/KorenTriodeStage elsewhere in this toolkit:
//  - Input stage: a ferrite bead (not a blocking resistor) into the
//    first grid, and a cathode bypass cap rolling off below ~95Hz -
//    very little low-end loss before the cascade even starts.
//  - The "Modern" voicing network (2.2M || 2.2M || 680k = 420k against
//    a 2.08nF cap) sits ahead of the second stage and highpasses around
//    182Hz; layered with it, the 250k gain pot's own bright cap sweeps
//    its own corner from ~656Hz (low Gain) up to ~1.4kHz (high Gain).
//    Modeled here as one gain-dependent highpass whose cutoff rises
//    with the Gain control - real gain pots don't literally raise a
//    separate fixed network's own corner, but folding both into a
//    single control-dependent filter captures the audible result
//    (higher Gain = more upper-mid/treble-forward, not just louder)
//    without two cascaded highpasses that would only ever move
//    together anyway.
//  - The third stage runs with no cathode bypass cap at all (the other
//    two do) - real amps lose low-frequency gain reinforcement on a
//    stage like that. A highpass can't remove gain after a nonlinear
//    stage has already run, so this is modeled as a higher inter-stage
//    corner feeding stage 3 than stage 2 gets, keeping the last stage's
//    contribution treble-forward rather than warm.
// Presence and the passive Bass/Mid/Treble tone stack are deliberately
// NOT part of this module - same scope choice as MetalPreampChain -
// they live at the Pedal layer (see MesaRectifierPreampPedal), which
// composes this gain cascade with the app's existing tone-control
// machinery instead of duplicating it here.
class MesaRectifierPreamp
{
public:
    explicit MesaRectifierPreamp(double sampleRate);

    void setSampleRate(double newSampleRate);

    // 0..1. Also brightens the voicing filter as it rises, matching the
    // real amp's gain-dependent bright-cap sweep.
    void setGain(float newGain);

    // The voicing filter's current corner frequency (656Hz at Gain=0 up
    // to 1400Hz at Gain=1, see header) - exposed for direct verification
    // in tests, since measuring it acoustically would otherwise be
    // confounded by Gain also scaling drive into three nonlinear stages.
    float getVoicingCutoffHz() const noexcept;

    void reset();

    // Not yet optimised for zero-allocation real-time use (allocates a
    // scratch buffer per call) - matches MetalPreampChain/Oversampler4x's
    // current verification-first scope.
    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;
    void updateVoicingFilter();

    static constexpr int numStages = 3;

    double sampleRate;
    float gain = 0.6f;
    float voicingCutoffHz = 0.0f;

    CouplingHighpass preGainHighpass; // ~95Hz, base rate - input stage's own rolloff

    // Gain-dependent corner, oversampled rate. The real network is a
    // shunt-to-ground voltage divider, not a series coupling cap - it
    // attenuates below its corner, it doesn't block it outright - so
    // this is blended back with the dry signal (see
    // processOversampledSample) rather than used as a straight series
    // filter the way preGainHighpass/interStageHighpass are.
    CouplingHighpass voicingHighpass;
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass; // oversampled rate

    Oversampler4x oversampler;
};
