#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/SpringReverb.h"

#include <vector>

// Spring-tank reverb for the rack (see dsp/SpringReverb) - the dispersive
// "boing" built for the emo/math-rock preset, on its own so it can go
// anywhere in a hand-built chain rather than only inside that fixed amp.
class SpringReverbPedal : public Pedal
{
public:
    SpringReverbPedal();

    juce::String getName() const override { return "Spring Reverb"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<SpringReverb> reverbs;

    PedalParameter decay { "Decay", 0.0f, 100.0f, 50.0f };
    PedalParameter mix   { "Mix",   0.0f, 100.0f, 30.0f };
};
