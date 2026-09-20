#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/DiodeClipperStage.h"
#include "dsp/Oversampler4x.h"

#include <vector>

// A basic, general-purpose distortion stompbox - the classic 3-knob
// layout (Gain, Tone, Level) rather than a fixed sound, deliberately
// simple so it's an easy starting point to extend later (a proper tone
// stack instead of one lowpass, asymmetric clipping, a mid control,
// etc). Built on this toolkit's existing DiodeClipperStage - the
// harder, more compressed series-diode clipping character that's the
// classic "distortion" clipper, as distinct from PrecisionDrivePedal's
// smoother Tube Screamer-style "overdrive" clipper - rather than
// inventing new clipping math for what's meant to be a simple building
// block.
class DistortionPedal : public Pedal
{
public:
    DistortionPedal();

    juce::String getName() const override { return "Distortion"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    static constexpr int maxChannels = 2;
    DiodeClipperStage clipper[maxChannels];
    std::vector<Oversampler4x> oversamplers;
    juce::IIRFilter toneFilters[maxChannels];

    double currentSampleRate = 44100.0;

    PedalParameter gain  { "Gain",  0.0f, 100.0f, 55.0f };
    PedalParameter tone  { "Tone",  0.0f, 100.0f, 50.0f };
    // Default Level is gain-staged: a strummed chord comes out ~3 dB louder than the guitar (RMS).
    // At 100 it was 21 dB hotter and hard-clipped digitally (peaks over full scale).
    PedalParameter level { "Level", 0.0f, 150.0f, 13.0f };
};
