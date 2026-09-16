#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/KorenTriodeStage.h"
#include "dsp/Oversampler4x.h"

#include <vector>

// A single raw Koren-model triode gain stage, for the rack: stack these
// with a Tone Stack, Diode Clipper, Power Amp etc. in any order to build
// your own amp from circuit-level pieces, rather than only the fixed
// preset amps. Runs oversampled since it's a standalone nonlinear stage
// that can now be placed (and driven hard) anywhere in the chain.
class TriodeStagePedal : public Pedal
{
public:
    TriodeStagePedal();

    juce::String getName() const override { return "Triode Stage"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    static constexpr int maxChannels = 2;
    KorenTriodeStage stage[maxChannels];
    std::vector<Oversampler4x> oversamplers;

    PedalParameter gain  { "Gain",  0.0f, 100.0f, 50.0f };
    PedalParameter bias  { "Bias",  -50.0f, 50.0f, 0.0f };
    PedalParameter level { "Level", 0.0f, 150.0f, 100.0f };
};
