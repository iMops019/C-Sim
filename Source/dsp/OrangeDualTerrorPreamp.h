#pragma once

#include <array>

#include "CouplingHighpass.h"
#include "KorenTriodeStage.h"
#include "Oversampler4x.h"

// The Orange Dual Terror's two preamp channels, traced directly from
// Orange's own "Dual Terror - PCB Schematic, Preamp & Power Amp, Rev.2
// 2008.09.30" (a community-archived copy at github.com/d3vCr0w/
// orange_terror_schematics). Every component value below was read off
// that drawing, not guessed - EXCEPT where explicitly marked as a
// judgment call. Both channels end in the same kind of stage: a 12AX7
// long-tailed-pair phase splitter with a single differential Tone
// control and a dual-gang Volume AFTER it (so unlike most amps, the
// splitter itself is overdriven before Volume ever sees the signal).
//
// TINY TERROR channel ("punchy") - V1/V2:
//  - V1-A: 12AX7, 1.5k cathode fully bypassed by 22uF (corner ~5Hz, i.e.
//    full gain at every audio frequency), 100k plate load.
//  - The coupling into the Gain pot is a tiny 1nF (C1) through 68k (R4)
//    into 470k (R6) || the 1M pot: a highpass at roughly 375Hz. THIS is
//    the Tiny Terror's tight, punchy, treble-forward overdrive - the
//    bass is stripped before the gain stages ever get to distort it.
//  - Gain is a dual-gang 1M log pot: RV1-A sits between V1-A and V1-B
//    with a 100pF bright cap (C3) across it (corner 1/(2*pi*1M*100pF) =
//    1.6kHz), RV1-B sits between V1-B and the splitter. One knob, both
//    attenuators - turning it up drives BOTH later stages harder.
//  - V1-B: same 1.5k/22uF cathode, plate load 100k (R7), 47nF (C4)
//    coupling through 100k (R8).
// FAT channel - V3/V4:
//  - The Gain pot (RV4-A, 1M log) sits at the INPUT of V3-A, not between
//    stages: gain is set by how hard the very first tube is driven.
//  - V3-A: same 1.5k/22uF cathode, 100k plate load, DIRECT-coupled to
//    V3-B, a cathode follower (100k load) - so nothing strips bass
//    between the first tube and the second pot.
//  - Coupling caps are 68nF (C21/C22, vs the Tiny Terror's 1nF/47nF): the
//    lows pass essentially untouched (corners under ~10Hz). The Tone
//    capacitor is 4.7nF vs 2.2nF - a much lower treble-cut corner.
//  - V4 splitter plate loads are 82k/100k (vs 100k/100k), output
//    coupling 68nF (vs 100nF).
// Tone (both channels): RV2/RV5 A500k in series with a 2.2nF/4.7nF cap
// BRIDGING the splitter's two anti-phase plates. That cap shorts the
// differential signal at high frequencies (source impedance = plate load
// || the 12AX7's ~62.5k rp, from the datasheet, per side), giving a
// first-order shelf: zero at 1/(2*pi*Rpot*C), pole at 1/(2*pi*(Rpot+Rsrc)*C).
// The pot's full sweep is modeled exactly this way (see updateTone).
//
// NOT from the Dual Terror schematic (and marked as such where used):
//  - The BRIGHT switch. That's an OR60 control (3-position, per Orange's
//    own OR60 materials: "shimmer" / neutral / "bite"); its circuit
//    isn't published, so it's a judgment-call additive highpass here.
//  - The splitter's clipping curve (tanh, the standard long-tailed-pair
//    transfer shape - a differential pair is inherently symmetric) and
//    the exact grid-bias numbers for the gain stages.
// Presence/Resonance (OR60 negative-feedback controls) live at the Pedal
// layer with the shared power amp, not here.
class OrangeDualTerrorPreamp
{
public:
    enum class Channel { TinyTerror = 0, Fat = 1 };

    explicit OrangeDualTerrorPreamp(double sampleRate);

    void setChannel(Channel newChannel) noexcept;
    void setGain(float amount) noexcept;   // 0..1 - the dual-gang Gain pot
    void setTone(float amount) noexcept;   // 0..1 - 0 = darkest (pot shorted), 1 = brightest (pot fully in series)
    void setVolume(float amount) noexcept; // 0..1 - post-splitter Volume
    void setBright(int position) noexcept; // 0 = shimmer, 1 = neutral, 2 = bite

    // The bright-cap strength the Gain pot currently gives (Tiny Terror
    // only) - exposed so a test can verify "fades as Gain rises" directly.
    float getPotBrightAmount() const noexcept;

    // The Tone control's current first-order shelf, exposed for exact
    // verification against the schematic-derived numbers.
    float getToneZeroHz() const noexcept { return toneZeroHz; }
    float getTonePoleHz() const noexcept { return tonePoleHz; }

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    float processOversampledSample(float x) noexcept;
    float processTinyTerror(float x) noexcept;
    float processFat(float x) noexcept;
    void updateTone() noexcept;

    Oversampler4x oversampler;
    double sampleRate;

    Channel channel = Channel::TinyTerror;
    float gain = 0.5f;
    float tone = 0.6f;
    float volume = 0.5f;
    int bright = 1;

    // Oversampled-rate filters.
    CouplingHighpass brightSwitchHighpass; // OR60 Bright switch (judgment call)
    CouplingHighpass c1Highpass;           // Tiny Terror: 1nF into 68k + 470k||1M
    CouplingHighpass potBrightHighpass;    // Tiny Terror: 100pF across RV1-A
    CouplingHighpass c4Highpass;           // 47nF interstage / 68nF - both DC blockers here
    CouplingHighpass fatDcBlock;           // Fat: 68nF coupling caps

    std::array<KorenTriodeStage, 2> tinyStages; // V1-A, V1-B
    KorenTriodeStage fatStage;                  // V3-A

    // Tone shelf state (base rate, after the oversampler).
    float toneZeroHz = 0.0f, tonePoleHz = 0.0f;
    double tb0 = 1.0, tb1 = 0.0, ta1 = 0.0;
    double toneX1 = 0.0, toneY1 = 0.0;
};
