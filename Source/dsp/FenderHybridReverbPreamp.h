#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// A hybrid preamp combining specific pieces of FenderTwinReverbPreamp
// and FenderPrincetonReverbPreamp - matching this toolkit's existing
// hybrid precedent (HybridAmp, MesaDiezelHybridPreamp) of splicing
// shared stages together in series rather than crossfading two fully
// separate final mixes. The backbone is the Twin's 3-stage cascade and
// its mid-cascade Volume/bright-cap mechanism (a real Twin-only
// circuit feature, kept as-is); a new **Breakup** control - which
// doesn't exist as a knob on either real amp - continuously blends the
// cascade's own character between the two parents' researched, sourced
// extremes:
//
//   Breakup=0: Twin Reverb's own light drive, gentle interstage
//              corners, and a 12AX7 second stage - clean and headroomy.
//   Breakup=1: Princeton Reverb's hotter drive, lower interstage
//              corners (letting bass hit the nonlinearity harder), and
//              the second stage's mu pulled toward a 12AT7's (~60,
//              from Princeton's own researched substitution) - earlier,
//              "browner" breakup.
//
// This is the same "digital advantage" reasoning PrecisionDriveStage's
// continuous Attack control already uses in this toolkit (a control
// that's a fixed hardware choice on the real amp, made continuously
// adjustable here since nothing stops a plugin from doing that) -
// applied to blending between two whole amps' worth of researched
// character instead of one control. A second new control, **Growl**,
// makes the Princeton's own fixed, non-adjustable low-mid bump
// independently adjustable in strength (0 = off entirely, 1 = the same
// strength Princeton itself uses) - on real hardware that bump is a
// fixed passive network with no knob at all.
//
// Real lesson from this toolkit's other hybrid (MesaDiezelHybridPreamp
// - see its own header): two independently-tuned tightening/voicing
// mechanisms are not automatically safe to stack at both parents' full
// strength - this module's own test checks the fundamental survives at
// max Gain/Breakup/Growl together, not just each control in isolation.
//
// Presence and the passive Bass/Mid/Treble tone stack live at the Pedal
// layer (see FenderHybridReverbPreampPedal), reusing FenderToneStack's
// own default (blackface) component values - the same tone stack both
// parent amps use, so no custom override is needed here either.
class FenderHybridReverbPreamp
{
public:
    explicit FenderHybridReverbPreamp(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setGain(float newGain);       // 0..1 - drive into the 3-stage cascade
    void setVolume(float newVolume);   // 0..1 - Twin's own mid-cascade Volume + bright-cap mechanism
    void setBreakup(float newBreakup); // 0..1 - Twin-clean <-> Princeton-early-breakup cascade character
    void setGrowl(float newGrowl);     // 0..1 - Princeton's fixed low-mid bump, made independently adjustable

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

private:
    float processOversampledSample(float x) noexcept;
    void updateBreakupDependentState();

    static constexpr int numStages = 3;

    double sampleRate;
    float gain = 0.5f;
    float volume = 0.6f;
    float breakup = 0.3f;
    float growl = 0.3f;

    CouplingHighpass preGainHighpass;
    std::array<KorenTriodeStage, numStages> stages;
    std::array<CouplingHighpass, numStages - 1> interStageHighpass; // corners interpolated by Breakup

    CouplingHighpass brightHighpass; // Twin's bright cap, unchanged

    // Growl: Princeton's fixed bandpass bump (difference-of-lowpasses),
    // same construction as FenderPrincetonReverbPreamp but with an
    // adjustable strength rather than a fixed one.
    double midLpLowAlpha = 0.0, midLpLowState = 0.0;
    double midLpHighAlpha = 0.0, midLpHighState = 0.0;

    Oversampler4x oversampler;
};
