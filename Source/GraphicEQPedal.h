#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/GraphicEQ.h"

#include <vector>

// A 10-band graphic EQ for the rack, spanning the useful guitar/amp
// tone-shaping range (100Hz-10kHz) - wide enough to carve a scooped
// metal mid, a bright lead boost, or just experiment genre to genre,
// rather than only the fixed 3-knob tone stack. See dsp/GraphicEQ for
// the band layout and filter design.
class GraphicEQPedal : public Pedal
{
public:
    GraphicEQPedal();

    juce::String getName() const override { return "Graphic EQ"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<GraphicEQ> eqs;

    // Names match dsp/GraphicEQ's fixed band centers, in order.
    PedalParameter band100   { "100Hz", -15.0f, 15.0f, 0.0f };
    PedalParameter band170   { "170Hz", -15.0f, 15.0f, 0.0f };
    PedalParameter band280   { "280Hz", -15.0f, 15.0f, 0.0f };
    PedalParameter band460   { "460Hz", -15.0f, 15.0f, 0.0f };
    PedalParameter band770   { "770Hz", -15.0f, 15.0f, 0.0f };
    PedalParameter band1300  { "1.3k",  -15.0f, 15.0f, 0.0f };
    PedalParameter band2200  { "2.2k",  -15.0f, 15.0f, 0.0f };
    PedalParameter band3600  { "3.6k",  -15.0f, 15.0f, 0.0f };
    PedalParameter band6000  { "6k",    -15.0f, 15.0f, 0.0f };
    PedalParameter band10000 { "10k",   -15.0f, 15.0f, 0.0f };
};
