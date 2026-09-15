#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"

// A high-gain distortion amp modeled on the Peavey 5150/6505 lead channel:
// cascaded saturation stages (like cascading preamp tubes), an inter-stage
// high-pass to keep dropped-tuning low end tight rather than flubby, and
// the 5150's signature control set - Gain, Bass, Mid, Treble, Presence,
// Resonance, Level.
class Peavey5150Pedal : public Pedal
{
public:
    Peavey5150Pedal();

    juce::String getName() const override { return "5150 Lead"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    void updateFilters();
    float processSample(float x, int channel);

    double currentSampleRate = 44100.0;

    static constexpr int maxChannels = 2;
    juce::IIRFilter preGainHighPass[maxChannels];
    juce::IIRFilter interStageHighPass[maxChannels];
    juce::IIRFilter bassFilter[maxChannels];
    juce::IIRFilter midFilter[maxChannels];
    juce::IIRFilter trebleFilter[maxChannels];
    juce::IIRFilter resonanceFilter[maxChannels];
    juce::IIRFilter presenceFilter[maxChannels];
    juce::IIRFilter cabFilter[maxChannels];

    PedalParameter gain      { "Gain",      0.0f, 100.0f, 70.0f };
    PedalParameter bass      { "Bass",      0.0f, 100.0f, 50.0f };
    PedalParameter mid       { "Mid",       0.0f, 100.0f, 55.0f };
    PedalParameter treble    { "Treble",    0.0f, 100.0f, 55.0f };
    PedalParameter presence  { "Presence",  0.0f, 100.0f, 55.0f };
    PedalParameter resonance { "Resonance", 0.0f, 100.0f, 50.0f };
    PedalParameter level     { "Level",     0.0f, 150.0f, 90.0f };
};
