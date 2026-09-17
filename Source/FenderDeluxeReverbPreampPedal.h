#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/FenderDeluxeReverbPreamp.h"
#include "dsp/FenderToneStack.h"

// A standalone PREAMP block modeled on the Fender Deluxe Reverb
// blackface (AB763) - see dsp/FenderDeluxeReverbPreamp for the
// researched circuit facts: the same 3-stage cascade topology as the
// Twin, tuned to break up genuinely between the Princeton (early) and
// the Twin (stays clean) - this toolkit's "sweet spot" blackface amp.
// Preamp-only, same as the other researched preamps here - pair it
// with a Tube Power Amp block (Lab tab) at a moderate Sag, between the
// Twin's stiff low-Sag pairing and the Princeton's heavily-sagging one,
// matching the real amp's own "undersized power supply" character
// (documented to cause "loose low end"/"farting out" under heavy drive
// that the Twin's much larger transformers don't exhibit).
//
// Deliberately only Bass and Treble knobs, same real-hardware-fidelity
// choice as FenderPrincetonReverbPreampPedal: the real AB763 Deluxe
// Reverb's TMB tone stack has a Mid position, but it's a FIXED
// 6,800-ohm resistor ("equivalent to a mid pot set at 68%"), not an
// adjustable pot - there's no Mid knob on the real amp's panel. Also no
// Presence control at all, matching the source's own "No presence
// control mentioned" (unlike the Twin, which does have one).
class FenderDeluxeReverbPreampPedal : public Pedal
{
public:
    FenderDeluxeReverbPreampPedal();

    juce::String getName() const override { return "Fender Deluxe Reverb Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    double currentSampleRate = 44100.0;
    std::vector<FenderDeluxeReverbPreamp> preampStages;
    std::vector<FenderToneStack> toneStacks;

    PedalParameter gain   { "Gain",   0.0f, 100.0f, 42.0f };
    PedalParameter volume { "Volume", 0.0f, 100.0f, 62.0f };
    PedalParameter bass   { "Bass",   0.0f, 100.0f, 50.0f };
    PedalParameter treble { "Treble", 0.0f, 100.0f, 55.0f };
    PedalParameter level  { "Level",  0.0f, 150.0f, 90.0f };
};
