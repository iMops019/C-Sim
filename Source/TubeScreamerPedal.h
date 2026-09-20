#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/TubeScreamerStage.h"

#include <vector>

// The Ibanez Tube Screamer TS808 and TS9 (Pedals tab), modeled from their
// schematics - see dsp/TubeScreamerStage.h for the circuit, the sources and
// what is left out. One class serves both: they share the circuit and differ
// only in the output-stage resistors, so the model picks the component set and
// the name (which is also the preset key).
//
// The knobs are the pedal's own three: Drive (the 500k pot in the op-amp's
// feedback: 12x to 118x of gain, and where the diodes start to clip), Tone (a
// treble-lift control that moves the mid hump between about 525Hz and 1.6kHz)
// and Level.
//
// Meant to be used as its makers meant it: in front of an amp, with Drive low
// and Level up as a boost that tightens the low end and pushes the amp's own
// gain stage, or with Drive up as a mid-focused overdrive on its own.
class TubeScreamerPedal : public Pedal
{
public:
    enum class Model { TS808, TS9 };

    explicit TubeScreamerPedal(Model modelToUse);

    juce::String getName() const override { return model == Model::TS808 ? "Tube Screamer TS808" : "Tube Screamer TS9"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    Model model;
    std::vector<TubeScreamerStage> stages;

    PedalParameter drive { "Drive", 0.0f, 100.0f, 35.0f };
    PedalParameter tone  { "Tone",  0.0f, 100.0f, 50.0f };
    PedalParameter level { "Level", 0.0f, 100.0f, 70.0f };
};
