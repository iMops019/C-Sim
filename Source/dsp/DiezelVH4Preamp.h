#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// Diezel VH4's Mega/Lead channel: a 4-stage cascaded 12AX7 preamp voiced
// noticeably tighter than either MesaRectifierPreamp or
// FortinMeshuggahPreamp, with a dedicated Deep control to restore the
// bass that tightness costs - matching the real amp's own documented
// design, not a generic "high-gain amp #3."
//
// Circuit facts behind this, sourced from Diezel/Universal Audio's own
// plugin manual (Brainworx, developed in partnership with Diezel GmbH,
// approved by Peter Diezel) and a published pedal clone of the Mega
// channel's actual passive network (component values cross-checked
// against independent schematic-tracing discussion) - informed
// approximations, not a literal transcription, same honesty level as
// the other researched preamps in this toolkit:
//  - The manual's own words on channel 3 (Mega): "distortion noticeably
//    tighter than [the crunch channel]... added compression... the
//    signal gets somewhat limited in its dynamic range. This limitation
//    hits mostly the lower frequencies." That's modeled here as
//    genuinely tighter inter-stage highpass corners than Mesa's own
//    cascade (progressively 50Hz/90Hz/150Hz across 4 stages, vs Mesa's
//    40Hz/120Hz across 3) - real attenuation from the cascade itself,
//    not a cosmetic label.
//  - "This loss is corrected by a[n] ... control... [with the]
//    designation Deep... centered at 80Hz... does not alter the dynamic
//    behavior" - modeled as a static (non-envelope-driven, deliberately)
//    boost-only low shelf at 80Hz after the cascade. Real amp wires this
//    into the power amp's own negative feedback loop; this toolkit
//    keeps preamp and power amp as separate blocks (see
//    DiezelVH4PreampPedal), so it's applied here instead - the audible
//    result (tight cascade, restorable bass) is what's being matched,
//    not the exact circuit location.
//  - Pre-gain tightness: Diezel's own hardware "tight bass" 3-position
//    switch documents its preferred setting as a 2.7k resistor into a
//    0.68uF cap - an RC corner of ~87Hz - used here for the pre-gain
//    highpass, tighter than Mesa's ~95Hz input stage.
// Presence and the passive Bass/Mid/Treble/Mid tone stack live at the
// Pedal layer (see DiezelVH4PreampPedal), same scope choice as the other
// researched preamps - this module is just the gain cascade + Deep.
class DiezelVH4Preamp
{
public:
    explicit DiezelVH4Preamp(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setGain(float newGain); // 0..1
    void setDeep(float newDeep); // 0..1 - boost-only 80Hz low shelf, restoring what the tight cascade costs

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    static constexpr int numStages = 4;

    double sampleRate;
    float gain = 0.6f;
    float deep = 0.3f;

    CouplingHighpass preGainHighpass; // ~87Hz, base rate - Diezel's own documented "tight bass" setting
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass; // progressively tighter, oversampled rate

    // Deep's own lowpass, used as the complement in an additive-lowpass
    // shelf (the mirror image of the additive-highpass trick
    // MesaRectifierPreamp/FortinMeshuggahPreamp use to brighten - this
    // one darkens/restores bass by adding a scaled lowpassed copy back
    // on top of the dry signal).
    double deepLpAlpha = 0.0;
    double deepLpState = 0.0;

    Oversampler4x oversampler;
};
