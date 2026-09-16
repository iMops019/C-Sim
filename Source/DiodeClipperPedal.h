#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/DiodeClipperStage.h"
#include "dsp/Oversampler4x.h"

#include <vector>

// A standalone diode clipper for the rack - the harder, symmetric-
// knee character (see DiodeClipperStage) used alongside tube stages in
// the metal build, here on its own so it can be mixed into any
// hand-built chain, not just blended inside a fixed preset amp.
class DiodeClipperPedal : public Pedal
{
public:
    DiodeClipperPedal();

    juce::String getName() const override { return "Diode Clipper"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    static constexpr int maxChannels = 2;
    DiodeClipperStage clipper[maxChannels];
    std::vector<Oversampler4x> oversamplers;

    PedalParameter drive { "Drive", 0.0f, 100.0f, 40.0f };
    PedalParameter level { "Level", 0.0f, 150.0f, 100.0f };
};
