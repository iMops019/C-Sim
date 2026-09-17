#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/FenderHybridReverbPreamp.h"
#include "dsp/FenderToneStack.h"

// A standalone PREAMP block combining specific pieces of
// FenderTwinReverbPreamp and FenderPrincetonReverbPreamp (see
// dsp/FenderHybridReverbPreamp for exactly which pieces and why, and
// the two brand-new controls - Breakup and Growl - that don't exist on
// either real amp). Genuinely more knobs than either parent (9 here vs.
// the Twin's 7 and the Princeton's 5), matching what those two extra
// continuously-adjustable controls actually add rather than knob count
// for its own sake.
//
// Tone stack reuses FenderToneStack at its own default (blackface)
// component values, same as both parent pedals - carries the full
// Bass/Mid/Treble/Presence set from the Twin side, since the hybrid's
// cascade backbone is the Twin's own 3-stage architecture.
class FenderHybridReverbPreampPedal : public Pedal
{
public:
    FenderHybridReverbPreampPedal();

    juce::String getName() const override { return "Fender Twin/Princeton Hybrid Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    double currentSampleRate = 44100.0;
    std::vector<FenderHybridReverbPreamp> preampStages;
    std::vector<FenderToneStack> toneStacks;

    PedalParameter gain     { "Gain",     0.0f, 100.0f, 45.0f };
    PedalParameter volume   { "Volume",   0.0f, 100.0f, 60.0f };
    PedalParameter breakup  { "Breakup",  0.0f, 100.0f, 40.0f }; // Twin-clean <-> Princeton-early-breakup, no real-amp equivalent
    PedalParameter growl    { "Growl",    0.0f, 100.0f, 30.0f }; // Princeton's fixed mid bump, made adjustable
    PedalParameter bass     { "Bass",     0.0f, 100.0f, 50.0f };
    PedalParameter mid      { "Mid",      0.0f, 100.0f, 45.0f };
    PedalParameter treble   { "Treble",   0.0f, 100.0f, 55.0f };
    PedalParameter presence { "Presence", 0.0f, 100.0f, 45.0f };
    PedalParameter level    { "Level",    0.0f, 150.0f, 90.0f };

    juce::IIRFilter presenceFilter[2];
};
