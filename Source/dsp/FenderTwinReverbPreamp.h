#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// Fender Twin Reverb blackface (AB763) Vibrato channel preamp: three
// cascaded 12AX7/7025 gain stages voiced deliberately clean and
// headroomy, the opposite design target of the high-gain preamps
// already in this toolkit (MesaRectifierPreamp, FortinMeshuggahPreamp,
// DiezelVH4Preamp) - this is the toolkit's first genuinely "stays clean"
// amp, not just a lower-gain version of the same voicing.
//
// Circuit facts behind this, sourced from published AB763 schematic
// analysis (kr-sound.com's AB763 deep dive, fenderguru.com's Twin
// Reverb model page, and Fender's own AB763 schematic) - informed
// approximations, not a literal transcription, same honesty level as
// the other researched preamps in this toolkit:
//  - The Vibrato channel is a genuine 3-stage cascade: V1A (input,
//    7025/12AX7, 1.5k/25uF cathode, 100k plate load) -> V1B/V2B (a
//    recovery stage after the tone stack, shared 820-ohm cathode
//    between channels) -> V4B (a third stage after the reverb
//    mix/vibrato circuit that "more than compensates for signal losses
//    in the reverb circuit... higher overall gain and earlier
//    saturation" than the 2-stage Normal channel). This module models
//    that 3-stage topology directly - it's a real, sourced structural
//    fact, not an arbitrary stage count.
//  - Each stage runs at deliberately light drive (low
//    inputToGridVolts) and the pre-stage multiplier tops out far lower
//    than this toolkit's high-gain preamps - the sourced, defining
//    character trait: "[the Twin] stays clean up to almost 6 [on the
//    volume dial], whereas other Fenders break up around 4... designed
//    not to break up like the other Fender amps." Four 6L6GC output
//    tubes and a solid-state (diode) rectifier - no sag, a stiff
//    supply - reinforce the same headroom story at the power-amp stage
//    (a separate Lab block in this toolkit; pair this preamp with a low
//    Sag Tube PowerAmpStage or a SolidStatePowerAmpStage rather than a
//    heavily-sagging one for an authentic result).
//  - Interstage coupling corners are gentle (Fender's own coupling caps
//    are comparatively large, ~0.02-0.1uF into high grid-leak
//    resistors) - deliberately NOT tightened the way the metal preamps'
//    cascades are, since a Fender blackface amp is prized for full,
//    round bass response, not a palm-mute-friendly tight low end.
//  - The real Vibrato channel Volume control sits mid-cascade (after
//    the tone stack's recovery stage, before the reverb-recovery V4B
//    stage) rather than at the very end of the chain like a typical
//    master volume - modeled here as setVolume(), which both scales
//    drive into the final stage and interacts with a 47pF bright cap
//    wired across that same pot. A physical bright cap increasingly
//    bypasses the pot's own treble-rolloff as the pot resistance to
//    ground gets larger, i.e. at LOWER Volume settings - modeled as an
//    additive high-shelf whose strength fades out as Volume rises,
//    the inverse relationship of the gain-dependent brightening used
//    in MesaRectifierPreamp/FortinMeshuggahPreamp.
// Presence and the passive Bass/Mid/Treble/Mid tone stack live at the
// Pedal layer (see FenderTwinReverbPreampPedal) using FenderToneStack's
// own default component values - which already represent typical
// blackface-era values (250pF treble cap, 0.1uF bass cap, 10k mid pot),
// the same tone stack this amp actually uses, so no custom Components
// override is needed here (unlike Diezel/Fortin's Marshall-family
// values).
class FenderTwinReverbPreamp
{
public:
    explicit FenderTwinReverbPreamp(double sampleRate);

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

    CouplingHighpass preGainHighpass; // input stage's own rolloff, base rate
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass; // gentle corners, oversampled rate

    // Bright cap: an additive-highpass shelf feeding the final stage,
    // strongest at low Volume and fading toward zero as Volume rises -
    // see header for the physical reasoning.
    CouplingHighpass brightHighpass;

    Oversampler4x oversampler;
};
