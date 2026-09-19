#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/CentaurDriveStage.h"

#include <vector>

// A Klon Centaur-style transparent overdrive with a series Light
// Distortion stage and a post-clip Depth control added. Knobs: Light
// Distortion, Depth, Drive, Tone, Volume. Drive is the Klon's dual-ganged
// Gain (mid-hump into a germanium clipper, with the clean path receding but
// never vanishing); Tone is its real -8dB..+18dB high shelf. See
// dsp/CentaurDriveStage for the circuit research and what's calibrated
// rather than transcribed.
class CentaurDrivePedal : public Pedal
{
public:
    CentaurDrivePedal();

    juce::String getName() const override { return "Centaur Drive"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<CentaurDriveStage> stages;

    PedalParameter lightDistortion { "Light Distortion", 0.0f, 100.0f, 20.0f };
    PedalParameter depth           { "Depth",            0.0f, 100.0f, 30.0f };
    PedalParameter drive           { "Drive",            0.0f, 100.0f, 40.0f };
    PedalParameter tone            { "Tone",             0.0f, 100.0f, 55.0f };
    PedalParameter volume          { "Volume",           0.0f, 150.0f, 100.0f };
};
