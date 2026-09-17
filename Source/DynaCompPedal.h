#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/DynaCompStage.h"

// Models the MXR Dyna Comp (M102) - a feedback-style OTA compressor
// prized for its fast, percussive "grab," a favorite for tightening
// palm-muted rhythm playing in heavier genres. Just two knobs, matching
// the real pedal exactly - Sensitivity and Output - see
// dsp/DynaCompStage for the researched circuit facts and compression
// model.
class DynaCompPedal : public Pedal
{
public:
    DynaCompPedal();

    juce::String getName() const override { return "Dyna Comp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<DynaCompStage> stages;

    PedalParameter sensitivity { "Sensitivity", 0.0f, 100.0f, 50.0f };
    PedalParameter output      { "Output",      0.0f, 100.0f, 60.0f };
};
