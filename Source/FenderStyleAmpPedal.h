#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/BassmanToneStack.h"
#include "dsp/DiodeClipperStage.h"
#include "dsp/FenderStyleAmp.h"
#include "dsp/Oversampler4x.h"
#include "dsp/PowerAmpStage.h"

#include <memory>
#include <vector>

// The seed of the user's own from-scratch, schematic-driven Fender-style
// amp build (Amps tab): Gain and Volume confirm the signal path works end
// to end, plus a real Bass/Mid/Treble tone stack (from the tweed Fender
// Bassman 5F6-A, see dsp/BassmanToneStack.h) and a Hi-Treble switch
// borrowed from a different sourced schematic (see below). Future
// sessions grow this further (coupling caps, cathode bias, tube-type
// swaps) one stage at a time - see dsp/FenderStyleAmp.h. Signal order
// matches the real amp: preamp gain stage -> tone stack -> Hi-Treble ->
// Volume -> power amp (Tight switch).
//
// Tight: real Fender Twin Reverb (AB763) amps have a genuinely tighter,
// cleaner low end than smaller sagging tweed-era amps like the Bassman
// above, for two real, sourced reasons (Rob Robinette's AB763 circuit
// analysis): a solid-state rectifier instead of a tube one, giving "very
// little dynamic voltage sag", and a "heavy" 100-ohm negative-feedback
// resistor (vs. 47 ohms in single-speaker amps), which measurably tightens
// the bottom end at the cost of some gain. Both of those are power-amp
// characteristics already modeled by this toolkit's existing, tested
// `PowerAmpStage` (sag + feedback) - reused here as-is rather than
// rebuilding anything, tuned to Twin Reverb's real regime when the switch
// is On (near-zero sag, heavy feedback) vs. a looser, more tweed-like
// regime when Off. `PowerAmpStage`'s own Drive is left at 0 - the signal
// is already properly gain-staged by everything upstream, so no extra
// artificial multiplier is needed before the power tube nonlinearity.
//
// Drive + Tone: a lightweight built-in boost stage in front of the amp
// (like stacking a small drive pedal ahead of a clean Fender), not a
// full separate pedal - a single Off/On switch (no adjustable Gain of
// its own, deliberately minimal) reusing the toolkit's existing, tested
// `DiodeClipperStage` (the same clipper `DistortionPedal` builds on),
// plus one always-on Tone knob shaping whatever comes out of it. Modest
// fixed drive amount (2x, gentle next to DistortionPedal's own 1x-14x
// range) since this is meant to be a subtle push into the amp's own
// front end, not a distortion pedal in its own right.
//
// Tube: swaps the preamp stage's real datasheet mu (amplification
// factor) between four common dual-triode types - 12AX7 (100, default),
// 12AT7 (60), 12AU7 (~20), 5751 (~70) - the exact same documented
// simplification already used for the 12AT7 substitution in
// FenderPrincetonReverbPreamp (mu only; the Koren model's other shape
// parameters stay fixed). Lower mu means less gain/earlier compression
// headroom, not a different curve shape - a real, audible, sourced
// difference between these tubes, just not the ONLY difference a full
// SPICE re-fit per tube would capture.
//
// Hi-Treble: real Roland Jazz Chorus JC-120 amps (the analog op-amp-era
// board, traced from the Dec 1984 service schematic - NOT the modern
// JC-40, which turned out to be a hybrid analog/DSP design with the tone
// shaping done in unpublished firmware, nothing to trace there) have a
// physical pushbutton next to the passive Treble/Bass/Middle/Volume
// stack that patches in an *extra* bright-boost tap on top of whatever
// Treble is already doing - a 470pF cap (C42) into a 100k resistor (R42)
// feeding a switch, rather than a continuously-adjustable knob. The
// 100k/470pF pair sets a real, sourced corner of ~3.4kHz; modeled here as
// a boost-only high-shelf switched fully in/out at that corner, same
// "playable approximation of a real, sourced fact" honesty as this
// toolkit's other researched controls (KorenTriodeStage's curve,
// PrecisionDriveStage's clipper) - the exact boost amount isn't
// recoverable from the schematic alone (it depends on the rest of the
// loop this scan doesn't fully resolve), so it's a documented judgment
// call, not a measured value.
class FenderStyleAmpPedal : public Pedal
{
public:
    FenderStyleAmpPedal();

    juce::String getName() const override { return "Fender Style Amp (WIP)"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    static constexpr int maxChannels = 2;
    std::vector<DiodeClipperStage> driveClippers;
    std::vector<Oversampler4x> driveOversamplers;
    juce::IIRFilter driveToneFilters[maxChannels];
    std::vector<std::unique_ptr<FenderStyleAmp>> amps;
    std::vector<std::unique_ptr<BassmanToneStack>> toneStacks;
    std::vector<juce::IIRFilter> hiTrebleFilters;
    std::vector<std::unique_ptr<PowerAmpStage>> powerAmps;
    std::vector<float> scratch;
    double currentSampleRate = 44100.0;

    PedalParameter drive    { "Drive",     0.0f, 1.0f,   0.0f, { "Off", "On" } };
    PedalParameter driveTone{ "Tone",      0.0f, 100.0f, 50.0f };
    PedalParameter gain     { "Gain",      0.0f, 100.0f, 50.0f };
    PedalParameter treble   { "Treble",    0.0f, 100.0f, 50.0f };
    PedalParameter bass     { "Bass",      0.0f, 100.0f, 50.0f };
    PedalParameter mid      { "Mid",       0.0f, 100.0f, 50.0f };
    PedalParameter volume   { "Volume",    0.0f, 100.0f, 70.0f };
    PedalParameter hiTreble { "Hi-Treble", 0.0f, 1.0f,   0.0f, { "Off", "On" } };
    PedalParameter tight    { "Tight",     0.0f, 1.0f,   0.0f, { "Off", "On" } };
    PedalParameter tube     { "Tube",      0.0f, 3.0f,   0.0f, { "12AX7", "12AT7", "12AU7", "5751" } };

    // Twin Reverb's real, sourced regime (near-zero sag, heavy feedback)
    // vs. a looser, more tweed-like regime - see the header comment above.
    static constexpr float looseSagAmount = 0.6f, looseFeedbackAmount = 0.15f;
    static constexpr float tightSagAmount = 0.05f, tightFeedbackAmount = 0.75f;

    // The tone stack's own real insertion loss - measured (not guessed)
    // via a throwaway diagnostic at default knob settings: -2.6dB at
    // 100Hz, -5.9dB at 800Hz (the mid dip), -3.9dB at 2kHz. Same class of
    // issue already found and fixed for FenderToneStack - see
    // [[project_csim_rack_pivot]]. A fixed makeup gain applied right
    // after it, not a knob, sized to the worst case (the mid dip) rather
    // than fully undoing every band, since overcorrecting the bass/treble
    // bands would just make Gain hotter for those settings specifically.
    static constexpr float toneStackRecoveryGain = 2.0f; // +6dB

    // Real datasheet mu (amplification factor) per Tube selection, same
    // index order as the "Tube" PedalParameter's valueLabels above - see
    // dsp/FenderStyleAmp.h::setMu for what this does and doesn't change.
    static constexpr float tubeMuValues[4] = { 100.0f, 60.0f, 20.0f, 70.0f };

    // Fixed, modest drive amount for the built-in Drive boost - see the
    // header comment above.
    static constexpr float builtInDriveGain = 2.0f;
};
