#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/DiezelVH4Preamp.h"
#include "dsp/FenderToneStack.h"

// A standalone PREAMP block modeled on the Diezel VH4's Mega/Lead
// channel (see dsp/DiezelVH4Preamp for the researched circuit facts: a
// tighter, more bass-limited 4-stage tube cascade than Mesa or Fortin,
// restorable with a real Deep control). Preamp-only, same as the other
// researched preamps here - pair it with a Power Amp block (Lab tab)
// rather than a fixed all-in-one preset.
//
// Tone stack reuses FenderToneStack (same passive nodal-analysis
// machinery FortinMeshuggahPreampPedal uses) with component values
// traced from a published pedal clone of the VH4's actual Mega/Lead
// channel network - 680pF treble cap, 22nF mid/bass caps, a 39k slope
// resistor, 250k treble pot, 1M bass pot, 25k mid pot - cross-checked
// against independent schematic-tracing discussion of the real amp
// before being used here.
class DiezelVH4PreampPedal : public Pedal
{
public:
    DiezelVH4PreampPedal();

    juce::String getName() const override { return "Diezel VH4 Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    double currentSampleRate = 44100.0;
    std::vector<DiezelVH4Preamp> preampStages;
    std::vector<FenderToneStack> toneStacks;

    PedalParameter gain     { "Gain",     0.0f, 100.0f, 55.0f };
    PedalParameter deep     { "Deep",     0.0f, 100.0f, 30.0f };
    PedalParameter bass     { "Bass",     0.0f, 100.0f, 50.0f };
    PedalParameter mid      { "Mid",      0.0f, 100.0f, 55.0f };
    PedalParameter treble   { "Treble",   0.0f, 100.0f, 55.0f };
    PedalParameter presence { "Presence", 0.0f, 100.0f, 50.0f }; // ~4kHz shelf, matches the real amp's power-amp presence control
    PedalParameter level    { "Level",    0.0f, 150.0f, 90.0f };

    juce::IIRFilter presenceFilter[2];
};
