#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/PowerAmpStage.h"
#include "dsp/SolidStatePowerAmpStage.h"

#include <vector>

// A standalone power amp stage for the rack, with a real Type choice
// between Tube (push-pull, sag, negative feedback - see PowerAmpStage)
// and Solid-State (much more headroom before breakup, no sag, a harder
// clipping onset once pushed past it - see SolidStatePowerAmpStage).
// Deliberately no watts knob anywhere: what actually shapes the tone is
// how hard this stage is driven into its own breakup (Drive) and, for
// tube, how much its supply sags under that demand (Sag) - a wattage
// spec is a hardware rating, not something a DSP stage can meaningfully
// take as an input.
//
// Sag and Feedback only apply to the Tube engine - a stiff solid-state
// rail doesn't sag the way a tube B+ supply does, and this pedal
// doesn't model output-stage feedback for Solid-State (see
// SolidStatePowerAmpStage's own header for that scope choice). Both
// knobs stay visible either way rather than disabling them, matching
// how the rest of this app's pedals work; they're simply inert while
// Solid-State is selected.
class PowerAmpPedal : public Pedal
{
public:
    PowerAmpPedal();

    juce::String getName() const override { return "Power Amp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<PowerAmpStage> tubeStages;
    std::vector<SolidStatePowerAmpStage> solidStateStages;

    PedalParameter type     { "Type",     0.0f, 1.0f,   0.0f, { "Tube", "Solid-State" } };
    PedalParameter drive    { "Drive",    0.0f, 100.0f, 40.0f };
    PedalParameter sag      { "Sag",      0.0f, 100.0f, 40.0f };
    PedalParameter feedback { "Feedback", 0.0f, 100.0f, 30.0f };
    PedalParameter level    { "Level",    0.0f, 150.0f, 100.0f };
};
