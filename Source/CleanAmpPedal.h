#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"

// A clean amp voiced for dropped tunings: a rumble filter tuned below a
// dropped low string, a Mid band centred on the ~200Hz zone that gets
// boxy/muddy when tuning down, and a simple filter-based cabinet roll-off.
class CleanAmpPedal : public Pedal
{
public:
    CleanAmpPedal();

    juce::String getName() const override { return "Clean Amp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    void updateFilters();

    double currentSampleRate = 44100.0;

    static constexpr int maxChannels = 2;
    juce::IIRFilter rumbleFilter[maxChannels];
    juce::IIRFilter bassFilter[maxChannels];
    juce::IIRFilter midFilter[maxChannels];
    juce::IIRFilter trebleFilter[maxChannels];
    juce::IIRFilter presenceFilter[maxChannels];
    juce::IIRFilter cabFilter[maxChannels];

    PedalParameter bass     { "Bass",     0.0f, 100.0f, 50.0f };
    PedalParameter mid      { "Mid",      0.0f, 100.0f, 50.0f };
    PedalParameter treble   { "Treble",   0.0f, 100.0f, 50.0f };
    PedalParameter presence { "Presence", 0.0f, 100.0f, 50.0f };
    PedalParameter level    { "Level",    0.0f, 150.0f, 100.0f };
};
