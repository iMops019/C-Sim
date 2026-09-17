#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/PitchShifter.h"

#include <vector>

// Hear the rig as if it were tuned down (or up) without retuning the
// guitar - dial in a drop-D/drop-C feel, or transpose up for
// experimentation. See dsp/PitchShifter for how the shift is done and
// its trade-offs. Defaults a whole step down and fully wet, since the
// point is to hear the transposed pitch, not blend it in subtly.
class TransposePedal : public Pedal
{
public:
    TransposePedal();

    juce::String getName() const override { return "Transpose"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<PitchShifter> shifters;

    PedalParameter semitones { "Semitones", -12.0f, 12.0f, -2.0f };
    PedalParameter mix       { "Mix",         0.0f, 100.0f, 100.0f };
};
