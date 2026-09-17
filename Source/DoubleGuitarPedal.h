#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/Doubler.h"

#include <vector>

// Studio double-tracking in a box: blends in a detuned, modulated copy of
// the signal to sound like a second guitarist playing the same part, the
// classic trick for thickening a part without recording it twice. See
// dsp/Doubler. On a stereo chain the two channels' LFOs are offset a
// quarter-turn from each other so the doubled voice spreads left/right
// instead of both sides wobbling in lockstep.
class DoubleGuitarPedal : public Pedal
{
public:
    DoubleGuitarPedal();

    juce::String getName() const override { return "Double Guitar"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<Doubler> doublers;

    PedalParameter detune { "Detune", 0.0f, 100.0f, 40.0f };
    PedalParameter rate   { "Rate",   0.05f, 3.0f, 0.6f };
    PedalParameter mix    { "Mix",    0.0f, 100.0f, 50.0f };
};
