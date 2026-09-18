#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/MesaTripleRectifierAmp.h"
#include "dsp/PowerAmpStage.h"

#include <memory>
#include <vector>

// The user's from-scratch, schematic-driven Mesa Boogie Triple Rectifier
// build (Amps tab) - seeded the same way FenderStyleAmp (WIP) was: start
// with just Gain and Volume, confirm the signal path works, then keep
// growing it one real, sourced stage at a time. See
// dsp/MesaTripleRectifierAmp.h for why this specific amp was picked
// (its tight, punchy low end for palm-muted Metalcore/djent rhythm work).
//
// Low: modeled on the two real, sourced switches that give the actual
// Dual/Triple Rectifier its name and its documented low-end character -
// traced directly from Mesa's own "3 Channel Dual/Triple Rectifier Solo
// Head" schematic (el34world.com, file RECBSUPP.S01):
//   - Rectifier Select: a real physical switch choosing between two 5U4
//     tube rectifiers or a pair of 1N4007 silicon diodes, both feeding
//     the exact same B+ filter network. A tube rectifier has real
//     internal resistance that droops the B+ rail under sustained
//     current draw (dynamic sag); silicon diodes have essentially none -
//     Mesa's own copy: "modern, tight, fast attack (diode) or a
//     smoother, vintage-style attack and sag (tube)".
//   - Bold/Spongy: a separate primary-side switch, seen on the schematic
//     routing the AC mains through an extra buck winding before the
//     power transformer in Spongy mode - a real built-in variac dropping
//     line voltage for additional, independent sag ("loosens up the feel
//     and raises upper harmonics" per Mesa's own documentation).
// Both are genuine, physically real sag mechanisms (not a feedback-
// resistor or tone-stack claim - nothing sourced ties either switch to
// the NFB loop), so this knob only drives the toolkit's existing, already
// -tested PowerAmpStage::setSag() - the same building block already used
// for FenderStyleAmpPedal's Tight switch, reusing its exact tight/loose
// sag numbers (0.05/0.6) as a documented, already-validated pair of
// endpoints rather than inventing new untested ones. A continuous knob
// here (vs. the real amp's two on/off switches) is the same "digital
// advantage" precedent as PrecisionDrivePedal's Attack knob - sweeping
// smoothly through the tight<->loose combinations the real switches only
// offer as four fixed points.
//
// Mid: traced from a real Peavey 5150-family schematic (the 5150/6505
// lineage shares the same core preamp/tone-stack platform across
// revisions - Peavey's own continuation of the circuit after the
// EVH/Peavey split; tubestore.com's "EVH 5150" schematic, Tone Stack/PI
// page). The amp's signature scooped-mid character comes from its own
// passive tone stack's Mid pot (VR4, 50k) sharing a node with the Bass
// pot (VR3) through a real 47k bridging resistor (R85) - a classic
// Marshall-family "mid scoop" network, distinct from this pedal's Mesa-
// sourced Bass/Treble (not built yet). Rather than modeling the WHOLE
// interactive 3-band stack (this pedal has no Treble/Bass knobs to
// interact with yet - that would need its own full nodal-analysis module,
// same class of decision as BassmanToneStack), this control is an honest,
// documented approximation of just that mid-scoop network's effect: a
// peaking EQ at its real, cross-sourced center frequency. The schematic's
// own bass-branch coupling cap at this node (C21) is .022uF - independently
// corroborated by Peavey mod-community documentation (audunmelbye.no/
// diyaudio forum threads on 5150/6505 mid mods) stating a .022uF cap at
// this exact node gives "a center of about 500Hz" - two independent
// sources agreeing on the same number. Knob turned down = deeper scoop
// (this amp's classic voicing), turned up = mids filled back in - same
// "playable approximation of a real, sourced fact" honesty as this
// toolkit's other single-knob researched controls (Hi-Treble, Deep).
//
// High: the real Revv Generator has no public factory schematic (a
// modern boutique amp, unlike this project's other vintage/DIY-archived
// sources), but its Purple channel's preamp has been accurately traced
// and released as a DIY pedal clone - PCB Guitar Mania's "Revolution
// III" ("Revvolution Pack", 7 June 2019), explicitly described by its
// own author as "an amazing conversion of the original Revv Generator
// Tube Amp, where you can see a pretty similar schematic," specifically
// "an accurate OpAmp emulation of the purple... channel from the
// Generator tube head." Downloaded and read that document's own
// schematic page directly (component values from a real, published
// trace, not guessed). The Treble control is a passive Baxandall-style
// shelf: a 50k pot (log taper) with a 4.7nF cap (C16) coupling its "top"
// end from the signal and a 47nF cap (C17) returning its "bottom" end to
// ground, wiper taken off through a 4.7k resistor (R20). Baxandall shelf
// controls don't have one single fixed corner (it shifts with the pot's
// own position), but the real component values bound where this one
// operates: ~677Hz (1/(2*pi*50k*4.7nF), full pot resistance dominating,
// the "shelf begins" end) up to ~7.2kHz (1/(2*pi*4.7k*4.7nF), R20
// dominating once the wiper is rolled toward the coupling-cap end, the
// "fully realized" end) - implemented as a high-shelf at their geometric
// mean (~2.2kHz), boost-capable past unity to match the Purple channel's
// well-documented "razor-sharp metal clarity" reputation, same "playable
// approximation of a real, sourced fact" honesty as Hi-Treble/Deep/Mid
// above (the exact dB range is a documented judgment call, same as
// those).
//
// Depth: the real Mesa Dual/Triple Rectifier has NO knob literally
// called "Depth" - checked directly against Mesa's own "Multi-Watt Dual
// & Triple" owner's manual (mesa-boogie.imgix.net), whose full control
// set is Gain/Treble/Mid/Bass/Presence/Master per channel plus the
// shared Rectifier Select/Bold-Spongy switches already used for Low
// above. Rather than inventing an unsourced control, reused a real,
// already-researched one from a DIFFERENT amp in this project's own
// history: the Diezel VH4's real, documented **Deep** control
// (`dsp::DiezelVH4Preamp`, recovered via `git show HEAD:<path>` the same
// way MesaRectifierPreamp/FortinMeshuggahPreamp were - see
// MesaTripleRectifierAmp.h). Diezel/Universal Audio's own plugin manual:
// "This loss is corrected by a[n]... control... designation Deep...
// centered at 80Hz... does not alter the dynamic behavior" - i.e. a
// static, boost-only low shelf, not an envelope- or gain-dependent
// effect. Implemented here exactly that simply (a Pedal-layer
// `juce::IIRFilter` low shelf, no new dsp module - same idiom as Mid/
// High above), continuing this pedal's "one real amp per knob"
// Frankenstein pattern (Low=Mesa's own switches, Mid=Peavey 6505,
// High=Revv Generator, Depth=Diezel VH4).
class MesaTripleRectifierPedal : public Pedal
{
public:
    MesaTripleRectifierPedal();

    juce::String getName() const override { return "Mesa Triple Rectifier (WIP)"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    static constexpr int maxChannels = 2;
    std::vector<std::unique_ptr<MesaTripleRectifierAmp>> amps;
    std::vector<std::unique_ptr<PowerAmpStage>> powerAmps;
    juce::IIRFilter midFilters[maxChannels];
    juce::IIRFilter highFilters[maxChannels];
    juce::IIRFilter depthFilters[maxChannels];
    double currentSampleRate = 44100.0;

    PedalParameter gain  { "Gain",   0.0f, 100.0f, 50.0f };
    PedalParameter volume{ "Volume", 0.0f, 100.0f, 70.0f };
    // 0 = Diode Rectifier + Bold (tight, minimal sag), 100 = Tube
    // Rectifier + Spongy (loose, heavy sag) - see header comment above.
    PedalParameter low    { "Low",    0.0f, 100.0f, 30.0f };
    // 0 = deep scoop (this amp's classic voicing), 100 = mids filled in -
    // see header comment above for the real, sourced 500Hz center.
    PedalParameter mid    { "Mid",    0.0f, 100.0f, 30.0f };
    // 0 = dark, 100 = the Purple channel's searing top-end clarity - see
    // header comment above for the real, sourced ~2.2kHz shelf corner.
    PedalParameter high   { "High",   0.0f, 100.0f, 65.0f };
    // 0 = off, 100 = full boost - see header comment above for the real,
    // sourced 80Hz center (Diezel VH4's own documented Deep control).
    PedalParameter depth  { "Depth",  0.0f, 100.0f, 0.0f };

    // Real, sourced sag endpoints reused from FenderStyleAmpPedal's Twin
    // Reverb Tight switch (see header comment) - Diode+Bold's dynamics
    // are the same "essentially no dynamic voltage drop" regime as a
    // solid-state rectifier, and Tube+Spongy's are the same "heavy sag"
    // regime as a loose tweed-style supply.
    static constexpr float tightSagAmount = 0.05f, looseSagAmount = 0.6f;

    // Real, cross-sourced mid-scoop center frequency (schematic's C21 =
    // .022uF at the Bass/Mid bridging node, independently corroborated by
    // Peavey mod-community documentation of the same value at the same
    // node - see header comment). -12dB at Mid=0 (a real deep scoop, this
    // amp's signature) to +6dB at Mid=100 (mids filled back in) - a
    // documented judgment call for the RANGE (not recoverable from the
    // schematic alone without modeling the whole interactive stack), same
    // honesty as this toolkit's other single-knob approximations.
    static constexpr double midFrequencyHz = 500.0;
    static constexpr float midCutDb = -12.0f, midBoostDb = 6.0f;

    // Real, sourced Baxandall shelf corner - geometric mean of the two
    // real component-derived bounds (~677Hz/~7.2kHz) computed from the
    // Revolution III trace's C16 (4.7nF)/R20 (4.7k)/TREBLE pot (50k) - see
    // header comment. -9dB at High=0 to +12dB at High=100, boost-capable
    // to match the Purple channel's own documented clarity/brightness.
    static constexpr double highFrequencyHz = 2200.0;
    static constexpr float highCutDb = -9.0f, highBoostDb = 12.0f;

    // Real, sourced center frequency (Diezel/Universal Audio's own manual
    // states Deep is "centered at 80Hz" - see header comment). Boost-only
    // (0 to +9dB), matching the real control's own documented nature as a
    // corrective bass restoration, not a cut.
    static constexpr double depthFrequencyHz = 80.0;
    static constexpr float depthBoostDb = 9.0f;
};
