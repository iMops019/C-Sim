#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/MesaDiezelHybridPreamp.h"
#include "dsp/FenderToneStack.h"

// A standalone PREAMP block combining specific pieces of
// MesaRectifierPreamp and DiezelVH4Preamp (see
// dsp/MesaDiezelHybridPreamp for exactly which pieces and why) rather
// than a fixed all-in-one preset. Tone stack reuses FenderToneStack with
// the same researched Diezel component values DiezelVH4PreampPedal uses
// (this preamp's cascade structure leans heavily on Diezel's own), not
// FenderToneStack's own generic default (Fender-ish) values - those
// turned out to have dramatically more insertion loss at typical knob
// settings than either amp's own researched values (measured ~15dB
// worse, not just a little quieter), which is what made this pedal
// sound unexpectedly quiet next to its siblings.
class MesaDiezelHybridPreampPedal : public Pedal
{
public:
    MesaDiezelHybridPreampPedal();

    juce::String getName() const override { return "Mesa/Diezel Hybrid Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<MesaDiezelHybridPreamp> preampStages;
    std::vector<FenderToneStack> toneStacks;

    PedalParameter gain   { "Gain",   0.0f, 100.0f, 55.0f };
    PedalParameter deep   { "Deep",   0.0f, 100.0f, 30.0f };
    PedalParameter bass   { "Bass",   0.0f, 100.0f, 50.0f };
    PedalParameter mid    { "Mid",    0.0f, 100.0f, 55.0f };
    PedalParameter treble { "Treble", 0.0f, 100.0f, 55.0f };
    PedalParameter level  { "Level",  0.0f, 150.0f, 90.0f };
};
