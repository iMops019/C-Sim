#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/MesaRectifierPreamp.h"

// A standalone PREAMP block modeled on the Mesa Boogie Dual/Triple
// Rectifier's "Modern" channel (see dsp/MesaRectifierPreamp for the
// researched circuit facts behind the gain cascade and its
// gain-dependent voicing filter) - deliberately preamp-only, with no
// power amp sag/breakup baked in. Pair it with a Power Amp block (Lab
// tab) afterward for the full amp, rather than a fixed all-in-one preset
// like Peavey5150Pedal - that's the whole point of splitting preamp and
// power amp into distinct rack blocks: the two stages' own breakup
// characters become independently controllable instead of fused
// together.
class MesaRectifierPreampPedal : public Pedal
{
public:
    MesaRectifierPreampPedal();

    juce::String getName() const override { return "Mesa Rectifier Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    void updateFilters();

    static constexpr int maxChannels = 2;

    double currentSampleRate = 44100.0;
    std::vector<MesaRectifierPreamp> preampStages;

    juce::IIRFilter bassFilter[maxChannels];
    juce::IIRFilter midFilter[maxChannels];
    juce::IIRFilter trebleFilter[maxChannels];
    juce::IIRFilter presenceFilter[maxChannels];

    PedalParameter gain     { "Gain",     0.0f, 100.0f, 65.0f };
    PedalParameter bass     { "Bass",     0.0f, 100.0f, 50.0f };
    PedalParameter mid      { "Mid",      0.0f, 100.0f, 40.0f };
    PedalParameter treble   { "Treble",   0.0f, 100.0f, 60.0f };
    PedalParameter presence { "Presence", 0.0f, 100.0f, 55.0f };
    PedalParameter level    { "Level",    0.0f, 150.0f, 90.0f };
};
