#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Pedal.h"

// A 4x12 cabinet + mic sim using real convolution (juce::dsp::Convolution)
// against a synthesized impulse response approximating a Vintage
// 30-loaded cab - the classic pairing for a 5150/6505 half stack. The IR
// is procedurally generated (see CabImpulseResponse), not a captured
// real-world impulse, but the processing itself is genuine convolution -
// a real captured IR .wav can be loaded in its place later with no other
// code changes.
class FourByTwelveCabPedal : public Pedal
{
public:
    FourByTwelveCabPedal();

    juce::String getName() const override { return "4x12 V30"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

    bool supportsImpulseResponseFile() const override { return true; }
    bool loadImpulseResponseFile(const juce::File& file) override;

private:
    juce::dsp::Convolution convolution;
    double currentSampleRate = 44100.0;

    PedalParameter level { "Level", 0.0f, 150.0f, 100.0f };
};
