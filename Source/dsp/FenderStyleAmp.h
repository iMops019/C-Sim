#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// A from-scratch, schematic-driven Fender-style amp build - growing one
// real, sourced stage at a time (same build-order pattern as the rest of
// this toolkit) rather than being a clone of one specific named amp, so
// the user can keep trying different tubes/stages against it.
//
// This step: a genuine 2-stage cascade, matching the real, sourced V1/V2
// preamp topology of the tweed Fender Bassman 5F6-A (ampbooks.com's
// circuit analysis of the actual schematic - the same amp this toolkit's
// BassmanToneStack already models the tone-stack half of, so this closes
// the loop with the preamp half):
//  - V1 (real hardware: a 12AY7) has a BYPASSED 820-ohm cathode resistor
//    (250uF bypass cap) - a full, undegenerated gain stage. Modeled here
//    as this cascade's first, higher-gain stage.
//  - V2 (real hardware: a 12AX7) has an UNBYPASSED 820-ohm cathode
//    resistor - ampbooks' own analysis: this "creates negative feedback
//    from cathode degeneration," reducing that stage's gain while
//    increasing its headroom, despite V2 being the higher-mu tube of the
//    two. Modeled as a genuinely lower drive/grid-swing into the second
//    stage than the first - the real, sourced, testable CONSEQUENCE of
//    cathode degeneration (lower gain, more headroom, cleaner at the
//    same input level) rather than a literal local-feedback-loop solve,
//    the same "model the documented effect, not the whole circuit"
//    honesty already used for this toolkit's other researched controls.
//  - The real interstage coupling cap between V1 and V2 is 0.02uF into a
//    1M-ohm grid leak resistor - a genuinely sourced, computable corner
//    of ~8Hz (1/(2*pi*1e6*0.02e-6)), i.e. essentially the full audio
//    band passes through untouched (a much gentler corner than even
//    FenderTwinReverbPreamp's own researched 30-35Hz interstage
//    corners) - modeled directly rather than approximated, since a real
//    number came straight out of the schematic here.
//
// This module deliberately keeps ONE shared Tube control across both
// stages (see setMu) rather than modeling V1 and V2 as two different
// fixed tube types the way the real amp actually is (12AY7 then 12AX7) -
// the whole point of this seed amp's Tube selector is to let the user
// freely experiment with what tube "populates" the cascade, which is a
// bigger, more useful experiment than locking two stages to two
// historically-correct but different fixed tubes. Worth remembering:
// the real V1 tube (12AY7) has a datasheet mu around 44 - between this
// selector's existing 12AT7 (60) and 12AU7 (20) options - a real,
// sourced number not currently offered as one of the four choices.
class FenderStyleAmp
{
public:
    explicit FenderStyleAmp(double sampleRate);

    void setGain(float amount) noexcept;   // 0..1 - drive into the tube stage
    void setVolume(float amount) noexcept; // 0..1 - output level

    // Real datasheet amplification factor (mu) for the tube type
    // occupying this cascade - e.g. 100 for a 12AX7, 60 for a 12AT7, ~20
    // for a 12AU7, ~70 for a 5751. Applied identically to both stages
    // (see header). Other Koren-model shape parameters (kg1/kp/kvb/ex)
    // stay at their 12AX7-fitted defaults regardless - same documented
    // simplification already used for the 12AT7 substitution in
    // FenderPrincetonReverbPreamp: mu is the real, sourced, most audible
    // spec difference between these related dual triodes, not a full
    // separate curve fit per tube type.
    void setMu(float mu) noexcept;

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processOversampledSample(float x) noexcept;

    static constexpr int numStages = 2;

    std::array<KorenTriodeStage, numStages> stages;
    CouplingHighpass interStageHighpass; // real, sourced V1->V2 coupling cap corner (~8Hz)
    Oversampler4x oversampler;

    float gain = 0.5f;
    float volume = 0.7f;
};
