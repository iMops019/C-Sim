#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/FenderTwinReverbPreamp.h"
#include "dsp/FenderToneStack.h"

// A standalone PREAMP block modeled on the Fender Twin Reverb
// blackface (AB763) Vibrato channel (see dsp/FenderTwinReverbPreamp for
// the researched circuit facts: a genuinely clean, headroomy 3-stage
// cascade with a mid-cascade Volume/bright-cap interaction, the
// opposite design target of this app's metal-voiced preamps).
// Preamp-only, same as the other researched preamps here - pair it
// with a Power Amp block (Lab tab), ideally with low Sag or the
// Solid-State type, matching the real amp's stiff 4x6L6GC/diode
// rectifier supply. For the full authentic Vibrato-channel sound, chain
// this into the Lab tab's Spring Reverb and Bias Tremolo blocks
// afterward - this toolkit already has both, built from the same real
// circuit this amp is named for.
//
// Tone stack reuses FenderToneStack at its own DEFAULT component
// values (250pF treble cap, 0.1uF bass cap, 10k mid pot) - unlike the
// Marshall-family preamps in this toolkit, no override is needed here:
// those defaults already represent the typical blackface-era values
// this exact amp uses.
class FenderTwinReverbPreampPedal : public Pedal
{
public:
    FenderTwinReverbPreampPedal();

    juce::String getName() const override { return "Fender Twin Reverb Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    double currentSampleRate = 44100.0;
    std::vector<FenderTwinReverbPreamp> preampStages;
    std::vector<FenderToneStack> toneStacks;

    PedalParameter gain     { "Gain",     0.0f, 100.0f, 45.0f };
    PedalParameter volume   { "Volume",   0.0f, 100.0f, 60.0f };
    PedalParameter bass     { "Bass",     0.0f, 100.0f, 50.0f };
    PedalParameter mid      { "Mid",      0.0f, 100.0f, 40.0f }; // Fender's mid is characteristically small next to a Marshall's
    PedalParameter treble   { "Treble",   0.0f, 100.0f, 55.0f };
    PedalParameter presence { "Presence", 0.0f, 100.0f, 45.0f }; // power-amp negative-feedback control, ~5kHz on a real Twin
    PedalParameter level    { "Level",    0.0f, 150.0f, 90.0f };

    juce::IIRFilter presenceFilter[2];
};
