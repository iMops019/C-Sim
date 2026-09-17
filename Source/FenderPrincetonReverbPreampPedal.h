#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/FenderPrincetonReverbPreamp.h"
#include "dsp/FenderToneStack.h"

// A standalone PREAMP block modeled on the Fender Princeton Reverb
// blackface (AA1164) - see dsp/FenderPrincetonReverbPreamp for the
// researched circuit facts: a 2-stage cascade (the second stage a
// genuine 12AT7, lower-gain than a 12AX7) that breaks up noticeably
// earlier and "browner"/more mid-focused than
// FenderTwinReverbPreampPedal. Preamp-only, same as the other
// researched preamps here - pair it with a Tube Power Amp block (Lab
// tab) at meaningfully HIGHER Sag than you'd use for the Twin, matching
// the real amp's sagging tube rectifier (5U4GB) and its lack of the
// Twin's stiff solid-state supply.
//
// Deliberately only Bass and Treble knobs: the stock blackface
// Princeton Reverb genuinely has NO Mid control ("did unfortunately
// come without a mid pot" - fenderguru.com) and no Presence control at
// all, unlike every other researched preamp in this toolkit. Reusing
// FenderToneStack here with the Mid knob wired to a fixed constant (not
// exposed as a parameter) reflects the real amp's fixed resistor in
// that spot rather than inventing a control the hardware doesn't have.
class FenderPrincetonReverbPreampPedal : public Pedal
{
public:
    FenderPrincetonReverbPreampPedal();

    juce::String getName() const override { return "Fender Princeton Reverb Preamp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    double currentSampleRate = 44100.0;
    std::vector<FenderPrincetonReverbPreamp> preampStages;
    std::vector<FenderToneStack> toneStacks;

    PedalParameter gain   { "Gain",   0.0f, 100.0f, 40.0f };
    PedalParameter volume { "Volume", 0.0f, 100.0f, 65.0f };
    PedalParameter bass   { "Bass",   0.0f, 100.0f, 50.0f };
    PedalParameter treble { "Treble", 0.0f, 100.0f, 55.0f };
    PedalParameter level  { "Level",  0.0f, 150.0f, 90.0f };
};
