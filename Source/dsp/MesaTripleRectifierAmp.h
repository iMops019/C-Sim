#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "DiodeClipperStage.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// A from-scratch, schematic-driven Mesa Boogie Triple Rectifier build -
// growing one real, sourced stage at a time, same seed-and-extend pattern
// already used for FenderStyleAmp (see dsp/FenderStyleAmp.h). Picked for
// this project's Metalcore-chug direction specifically for the Rectifier's
// famously tight, punchy low end under palm-muted rhythm playing.
//
// Real bug found and fixed (user report: "I don't hear anything... have
// to crank everything to get a tiny anything"): the original 1x-4x drive
// range plus a post-stage divide-down (copied from FenderStyleAmp's
// 2-stage cascade, where it compensates for TWO compounding stages) left
// this single, non-cascaded stage net QUIETER than its input across
// almost the whole Gain range - confirmed by a throwaway diagnostic
// measuring actual peak level through the real code at realistic guitar
// input amplitudes (~0.15), not assumed. First fix (widened drive range)
// made it audible on its own, but a second diagnostic then showed the
// REAL remaining gap: even fully cranked, this single stage's output had
// a crest factor (peak/RMS) around 2.1 no matter what, vs. ~1.08 for this
// project's own DistortionPedal - a single stage mathematically cannot
// reach the compressed, "wall of saturation" loudness of a real high-gain
// amp, because that character comes from CASCADING multiple gain stages,
// not from driving one stage harder.
//
// Fixed for real this time by cascading the actual 3-stage Mesa Dual/
// Triple Rectifier "Modern" channel topology - recovered from this
// project's own git history (a `MesaRectifierPreamp` module existed
// here before an earlier session's "clear out the Amps tab" cleanup;
// its research and circuit facts are still valid and are reused
// directly rather than re-derived). Real circuit facts, originally
// traced from warpedmusician.wordpress.com's "Mesa Rectifier Design
// Concepts" schematic-analysis series - informed approximations, not a
// literal component-by-component transcription, same honesty level as
// this toolkit's other researched cascades:
//  - Input stage: a ferrite bead (not a blocking resistor) into the
//    first grid, and a cathode bypass cap rolling off below ~95Hz -
//    very little low-end loss before the cascade even starts.
//  - The "Modern" voicing network (2.2M || 2.2M || 680k = 420k against
//    a 2.08nF cap) sits ahead of the second stage and highpasses around
//    182Hz; layered with it, the 250k gain pot's own bright cap sweeps
//    its own corner from ~656Hz (low Gain) up to ~1.4kHz (high Gain).
//    Modeled as one gain-dependent highpass whose cutoff rises with
//    Gain, blended back with the dry signal at a 60% floor (a real
//    shunt voltage divider attenuates below its corner, it doesn't
//    block it outright).
//  - The third stage runs with no cathode bypass cap at all (the other
//    two do) - modeled as a tighter interstage corner feeding it than
//    stage 2 gets, keeping the last stage's contribution treble-forward
//    rather than warm.
// Presence and the passive tone stack are NOT part of this module - they
// live at the Pedal layer (Low/Mid/High), composing this cascade with
// the app's existing tone-control machinery instead of duplicating it
// here.
//
// User report after the cascade above shipped: "I hear it but it all
// just sounds clean and dull... sounds like no high gain." Measured,
// not assumed: harmonic content plateaued around a 0.5-0.6 harmonics/
// fundamental ratio no matter how hard the interstage gains were pushed,
// because each KorenTriodeStage re-normalises to its OWN unity point
// regardless of how distorted the incoming signal already is - three of
// them cascaded doesn't compound distortion the way three real tube
// stages do. This actually matched the real, recovered design's own
// documented intent ("no diode clipping anywhere, pure tube cascade" -
// deliberately smoother than this toolkit's other high-gain preamp,
// FortinMeshuggahPreamp, which DOES use a diode-clipping master stage)
// - but real user feedback on the actual rebuilt implementation takes
// precedence over that stale, never-actually-heard research note.
// User chose: add real hard clipping, same technique FortinMeshuggahPreamp
// already uses. A `DiodeClipperStage` after the 3-stage cascade, driven
// harder as Gain rises, solves two problems at once: it's a genuine,
// audible "high gain" hard-clip character (measured: crest factor drops
// from ~2.1-2.9 to ~1.2, comparable to FortinMeshuggahPreamp's own
// ~1.30, vs. this project's DistortionPedal at ~1.08), AND - because a
// diode clipper's node voltage physically stays within a diode drop
// regardless of how hard it's driven (see DiodeClipperStage.h) - it's
// inherently self-limiting, so the interstage tube cascade can be
// allowed to compound freely again (no interstage tanh needed) without
// reintroducing the earlier blowup risk: measured worst-case peak (hot
// input, max Gain) is now ~1.7-1.8 pre-final-safety-tanh, not the 100+
// the unbounded tube-only cascade produced.
class MesaTripleRectifierAmp
{
public:
    explicit MesaTripleRectifierAmp(double sampleRate);

    void setGain(float amount) noexcept;   // 0..1 - drive into the cascade; also brightens
                                             // the voicing filter as it rises (real gain-
                                             // dependent bright-cap sweep, see header).
    void setVolume(float amount) noexcept; // 0..1 - output level

    // The voicing filter's current corner frequency (656Hz at Gain=0 up
    // to 1400Hz at Gain=1) - exposed for direct verification in tests,
    // since measuring it acoustically would otherwise be confounded by
    // Gain also scaling drive into three nonlinear stages.
    float getVoicingCutoffHz() const noexcept;

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processOversampledSample(float x) noexcept;
    void updateVoicingFilter();

    static constexpr int numStages = 3;

    CouplingHighpass preGainHighpass; // ~95Hz, base rate - input stage's own rolloff

    // Gain-dependent corner, oversampled rate - blended back with the dry
    // signal (see processOversampledSample), not a straight series
    // filter, since the real network is a shunt-to-ground divider.
    CouplingHighpass voicingHighpass;
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass; // oversampled rate
    DiodeClipperStage masterClipper; // real hard clipping after the cascade - see header

    Oversampler4x oversampler;

    double sampleRate;
    float gain = 0.5f;
    float volume = 0.7f;
    float voicingCutoffHz = 0.0f;
};
