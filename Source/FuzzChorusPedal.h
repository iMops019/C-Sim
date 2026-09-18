#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/FuzzFaceStage.h"
#include "dsp/PillowChorusStage.h"

// A Fuzz Face-style fuzz feeding a light, "soft mellow pillow" CE-2-
// style chorus - deliberately just two knobs, matching the user's own
// spec. Fuzz first, then Chorus, matching standard pedalboard ordering
// advice for a Fuzz Face specifically: it's a famously
// impedance-sensitive circuit that reacts badly to anything buffered or
// modulated in front of it, so any modulation belongs after it, not
// before. See dsp/FuzzFaceStage and dsp/PillowChorusStage for the
// researched circuit facts each side is built from.
class FuzzChorusPedal : public Pedal
{
public:
    FuzzChorusPedal();

    juce::String getName() const override { return "Fuzz Chorus"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<FuzzFaceStage> fuzzStages;
    std::vector<PillowChorusStage> chorusStages;

    PedalParameter fuzz    { "Fuzz",    0.0f, 100.0f, 50.0f };
    PedalParameter chorus  { "Chorus",  0.0f, 100.0f, 40.0f };
};
