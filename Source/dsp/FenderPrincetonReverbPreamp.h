#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// Fender Princeton Reverb blackface (AA1164) preamp: a 2-stage cascade
// voiced to break up noticeably EARLIER and "browner" than
// FenderTwinReverbPreamp - the toolkit's smallest, most overdrive-prone
// Fender voicing, the deliberate opposite design target of the Twin's
// headroom. Same family, genuinely different amp, not a re-skinned copy.
//
// Circuit facts behind this, sourced from published AA1164 schematic
// analysis (robrobinette.com's AA1164 deep dive and fenderguru.com's
// Princeton Reverb model page) - informed approximations, not a
// literal transcription, same honesty level as the other researched
// preamps in this toolkit:
//  - Only a single channel with two gain stages ahead of the
//    reverb/vibrato circuitry: V1 (12AX7 input) -> V2, notably a 12AT7
//    at the reverb-send tap, not another 12AX7 - a real, sourced tube
//    substitution with meaningfully lower mu (~60 vs 12AX7's ~100),
//    modeled here via KorenTriodeStage::Parameters::mu directly rather
//    than just a smaller inputToGridVolts. Fewer stages and a
//    lower-gain second tube than the Twin's 3-stage all-12AX7 cascade -
//    a real structural reason this amp has less gain on tap overall,
//    even though it saturates SOONER per available gain (see below).
//  - "Three fully bypassed preamp gain stages [give] abundant overdrive
//    capability" and the amp is documented to sound "browner" when
//    cranked - "more breakup in the lower frequencies and mid-focused
//    tone" - due in part to a comparatively inefficient phase-inverter
//    stage (out of this preamp module's scope; a separate power-amp
//    concern, see below) and in part to interstage coupling that
//    doesn't tighten the low end the way this toolkit's metal preamps
//    or even FenderTwinReverbPreamp do. Modeled here as: (1) a lower
//    interstage corner than the Twin's already-gentle one, letting bass
//    hit the cascade's nonlinearity harder rather than being filtered
//    ahead of it, and (2) a fixed, non-adjustable low-mid bump (~450Hz,
//    the sourced "mid-focused" trait) after the first stage - the same
//    "fixed voicing bump" technique PrecisionDriveStage uses for its
//    own documented mid-forward character, not a knob on the real amp
//    either.
//  - No Volume-pot bright-cap interaction is modeled here (unlike the
//    Twin's Vibrato-channel Volume): the Princeton Reverb's single
//    channel doesn't share that two-channel-amp circuit detail, so
//    giving it one would be inventing a feature the real amp doesn't
//    have, not researching one it does.
//  - Power section: only 2x6V6 (vs. the Twin's 4x6L6GC) at 12-15W into
//    a single 10" speaker, with a tube rectifier (5U4GB pre-CBS,
//    "noticeably" more sag than the Twin's solid-state diode rectifier)
//    and no choke - "softer" with more sag/compression. That's a
//    separate Lab-tab Power Amp block in this toolkit's split
//    architecture; pair this preamp with a Tube PowerAmpStage at
//    meaningfully HIGHER Sag than you'd use for the Twin, the opposite
//    pairing recommendation from FenderTwinReverbPreampPedal.
// The passive tone stack and any Presence control live at the Pedal
// layer (see FenderPrincetonReverbPreampPedal) as usual - but see that
// file for a real, sourced omission: the stock Princeton Reverb has
// NO Mid control (Bass/Treble only) and no Presence control at all,
// unlike every other researched preamp in this toolkit.
class FenderPrincetonReverbPreamp
{
public:
    explicit FenderPrincetonReverbPreamp(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setGain(float newGain);     // 0..1 - drive into the 2-stage cascade
    void setVolume(float newVolume); // 0..1 - plain post-cascade volume, no bright-cap coupling (see header)

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;

    static constexpr int numStages = 2;

    double sampleRate;
    float gain = 0.5f;
    float volume = 0.7f;

    CouplingHighpass preGainHighpass; // V1's own rolloff, base rate
    std::array<KorenTriodeStage, numStages> stages;
    CouplingHighpass interStageHighpass; // lower corner than the Twin's - lets bass hit the cascade harder

    // Fixed, non-adjustable low-mid bump between the stages - the
    // sourced "browner... mid-focused" character, same fixed-voicing
    // intent as PrecisionDriveStage's own mid bump, built here as a
    // difference-of-lowpasses bandpass shape (two simple one-pole
    // lowpasses, added back as lpLow - lpHigh) rather than a full
    // biquad, since only a fixed, non-adjustable bump is needed.
    double midLpLowAlpha = 0.0, midLpLowState = 0.0;   // lower corner of the bump
    double midLpHighAlpha = 0.0, midLpHighState = 0.0; // upper corner of the bump

    Oversampler4x oversampler;
};
