#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/DynamicGainStage.h"

#include <vector>

// The emo build's "edge of breakup" gain stage for the rack (see
// dsp/DynamicGainStage) - clean on a light touch, breaking up as you dig
// in, rather than one fixed clipping curve. On its own so it can be
// stacked with any tone stack/clipper/power amp you like.
class DynamicGainStagePedal : public Pedal
{
public:
    DynamicGainStagePedal();

    juce::String getName() const override { return "Dynamic Gain Stage"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<DynamicGainStage> stages;

    PedalParameter sensitivity { "Sensitivity", 0.0f, 100.0f, 60.0f };
    PedalParameter drive       { "Drive",       0.0f, 100.0f, 15.0f };
    PedalParameter level       { "Level",       0.0f, 150.0f, 100.0f };
};
