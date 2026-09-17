#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// Fender Deluxe Reverb blackface (AB763) Vibrato channel preamp - the
// "sweet spot" of this toolkit's three researched blackface Fenders,
// sitting deliberately between FenderPrincetonReverbPreamp's early,
// browner breakup and FenderTwinReverbPreamp's stay-clean headroom, not
// a copy of either. Real, sourced facts behind this model (robrobinette.com's
// AB763 Deluxe Reverb deep dive, cross-checked against published AB763
// schematics) - informed approximations, same honesty level as this
// toolkit's other researched preamps:
//  - A genuine 3-stage cascade, the SAME stage count and topology as the
//    Twin's Vibrato channel: V2A (input) -> V2B (a recovery stage after
//    the tone stack) -> V4B (a third stage after the reverb mix). Same
//    cathode resistor values as the Twin too (1.5k on the input stage,
//    an 820-ohm shared cathode on the recovery stage) - genuinely the
//    same circuit family, not a coincidence.
//  - Headroom sits between its two siblings by real physical cause, not
//    just a tuned-in-between knob: "higher 6V6 plate voltages" than the
//    Princeton give it more headroom than that amp, while its
//    comparatively "undersized power supply" (vs. the Twin's much
//    larger transformers) is documented to cause "loose low end" and
//    "farting out" under heavy drive that the Twin doesn't exhibit -
//    the amp's own well-known reputation of "breaks up nicely when you
//    push it," neither Princeton-early nor Twin-clean. That power-supply
//    looseness is a power-amp-stage concern in this toolkit's split
//    architecture (see FenderDeluxeReverbPreampPedal) rather than
//    something this preamp module itself should reach into.
//  - Same 47pF bright cap across the Vibrato channel's Volume pot as the
//    Twin (a real, sourced, shared AB763-family detail) - modeled
//    identically here via setVolume().
// The passive TMB tone stack and the real absence of a Presence control
// (the source's own words: "No presence control mentioned" - unlike the
// Twin, which has one) live at the Pedal layer - see
// FenderDeluxeReverbPreampPedal for a real, sourced detail worth
// remembering: the "M" in this amp's TMB tone stack is a FIXED 6,800-ohm
// resistor ("equivalent to a mid pot set at 68%"), not an adjustable
// pot - there's no Mid knob on the real amp's panel either, matching
// this toolkit's already-established precedent of exposing exactly the
// controls the real hardware has (see FenderPrincetonReverbPreampPedal's
// own fixed-mid decision).
class FenderDeluxeReverbPreamp
{
public:
    explicit FenderDeluxeReverbPreamp(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setGain(float newGain);     // 0..1 - drive into the 3-stage cascade
    void setVolume(float newVolume); // 0..1 - mid-cascade Vibrato-channel Volume; also drives the bright-cap shelf

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    static constexpr int numStages = 3;

    double sampleRate;
    float gain = 0.5f;
    float volume = 0.6f;

    CouplingHighpass preGainHighpass;
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass;

    // Bright cap - same mechanism as FenderTwinReverbPreamp: an
    // additive-highpass shelf feeding the final stage, strongest at low
    // Volume and fading toward zero as Volume rises.
    CouplingHighpass brightHighpass;

    Oversampler4x oversampler;
};
