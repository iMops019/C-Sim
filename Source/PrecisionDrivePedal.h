#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/PrecisionDriveStage.h"
#include "dsp/NoiseGate.h"

#include <vector>

// A model of the Horizon Devices Precision Drive: a Tube Screamer-family
// soft-clipping overdrive with its own take on two of that circuit's
// classic controls (an adjustable low cut - Attack - in place of a fixed
// one, and a treble-shelf Bright control instead of a passive tone cut),
// plus the pedal's signature built-in analog noise gate. Knob names/count
// (Attack, Drive, Bright, Gate, Volume) match the real pedal. See
// dsp/PrecisionDriveStage for the clipping/filter model and its
// documented approximations.
class PrecisionDrivePedal : public Pedal
{
public:
    PrecisionDrivePedal();

    juce::String getName() const override { return "Precision Drive"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<PrecisionDriveStage> stages;
    std::vector<NoiseGate> gates;

    PedalParameter attack { "Attack", 0.0f, 100.0f, 30.0f };
    PedalParameter drive  { "Drive",  0.0f, 100.0f, 25.0f };
    PedalParameter bright { "Bright", 0.0f, 100.0f, 40.0f };
    PedalParameter gate   { "Gate",   0.0f, 100.0f, 40.0f };
    // Default Volume is gain-staged: a strummed chord comes out ~3 dB louder than the guitar (RMS).
    // At 100 it was 12 dB hotter and clipped digitally on a hard strum.
    PedalParameter volume { "Volume", 0.0f, 150.0f, 37.0f };
};
