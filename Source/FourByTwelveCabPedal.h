#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"

// A 4x12 cabinet + mic sim voiced like a Vintage 30-loaded cab - the
// classic pairing for a 5150/6505 half stack: tight low end, a body
// resonance around 120Hz, a V30-style upper-mid bite around 2.8kHz, and a
// steep top-end roll-off. Filter-based for now; a real impulse-response
// cab is a planned future upgrade.
class FourByTwelveCabPedal : public Pedal
{
public:
    FourByTwelveCabPedal();

    juce::String getName() const override { return "4x12 V30"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    void updateFilters();

    double currentSampleRate = 44100.0;

    static constexpr int maxChannels = 2;
    juce::IIRFilter lowCut[maxChannels];
    juce::IIRFilter bodyResonance[maxChannels];
    juce::IIRFilter presencePeak[maxChannels];
    juce::IIRFilter highCut1[maxChannels];
    juce::IIRFilter highCut2[maxChannels];

    PedalParameter level { "Level", 0.0f, 150.0f, 100.0f };
};
