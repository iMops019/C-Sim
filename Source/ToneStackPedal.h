#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/FenderToneStack.h"

#include <vector>

// A standalone passive tone stack for the rack - the Fender/Marshall-
// style circuit (see FenderToneStack), on its own so it can be dropped
// in after any raw gain stage you build rather than only inside a
// fixed preset amp.
class ToneStackPedal : public Pedal
{
public:
    ToneStackPedal();

    juce::String getName() const override { return "Tone Stack"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<FenderToneStack> stacks;

    PedalParameter bass   { "Bass",   0.0f, 100.0f, 50.0f };
    PedalParameter mid    { "Mid",    0.0f, 100.0f, 50.0f };
    PedalParameter treble { "Treble", 0.0f, 100.0f, 50.0f };
};
