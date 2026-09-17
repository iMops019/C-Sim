#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/FortinMeshuggahPreamp.h"
#include "dsp/FenderToneStack.h"

// A standalone PREAMP block modeled on the Fortin Meshuggah amp (see
// dsp/FortinMeshuggahPreamp for the researched circuit facts: a Marshall
// JCM800 + "Jose mod" tube cascade with two independent gain stages and
// a pre-tone-stack diode-clipping Master stage). Preamp-only, same as
// MesaRectifierPreampPedal - pair it with a Power Amp block (Lab tab)
// for the full amp rather than a fixed all-in-one preset.
//
// "The rest is stock JCM800" per the real amp's own circuit (see the dsp
// module's header) - modeled here by reusing the existing FenderToneStack
// nodal-analysis passive stack with Marshall-typical component values,
// rather than the independent shelf/peak IIR filters MesaRectifierPreampPedal
// and Peavey5150Pedal use. This is deliberately the more "real" of the
// two tone-stack approaches in this app (an actual interactive passive
// network, not three independent bands) - a fitting choice for the amp
// whose whole design point is authenticity to a real, specific circuit.
class FortinMeshuggahPreampPedal : public Pedal
{
public:
    FortinMeshuggahPreampPedal();

    juce::String getName() const override { return "Fortin Meshuggah Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<FortinMeshuggahPreamp> preampStages;
    std::vector<FenderToneStack> toneStacks;

    PedalParameter gain1  { "Gain 1", 0.0f, 100.0f, 55.0f };
    PedalParameter gain2  { "Gain 2", 0.0f, 100.0f, 55.0f };
    PedalParameter master { "Master", 0.0f, 100.0f, 45.0f };
    PedalParameter bass   { "Bass",   0.0f, 100.0f, 50.0f };
    PedalParameter mid    { "Mid",    0.0f, 100.0f, 60.0f };
    PedalParameter treble { "Treble", 0.0f, 100.0f, 55.0f };
    PedalParameter level  { "Level",  0.0f, 150.0f, 90.0f };
};
