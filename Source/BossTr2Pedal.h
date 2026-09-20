#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/BossTr2Stage.h"

#include <vector>

// The Boss TR-2 Tremolo (Pedals tab), modeled from its service-manual schematic - see
// dsp/BossTr2Stage.h for the circuit, the sources and what is assumed. The knobs are
// the pedal's own three:
//
//   Rate    the LFO speed, about 1Hz to 12.7Hz (a linear pot in the oscillator's
//           integrator - real relaxation-oscillator timing, not a table)
//   Depth   how far the gain dips: at zero the pedal is a unity-gain pass-through; at
//           full it goes from full level down to about a third (triangle) or to almost
//           silence (square)
//   Wave    shapes the tremolo from a smooth triangle to a near-square chop, by driving
//           the LFO into a clipping amplifier with 1.2x to 11.2x gain. The square's
//           on-time is lopsided (about 73%) because the LFO's triangle is off-centre.
//
// This is not an optical tremolo: the TR-2's gain element is a VCA chip whose gain is
// linear in its control voltage, so there is none of an opto's lag or memory - the only
// smoothing is a ~10ms RC on the control voltage, the same rising and falling.
// The whole path is wet (Boss's electronic bypass takes the dry signal out when the
// effect is on), so there is no mix control and no dry leak.
//
// Cheap enough not to matter: one LFO and one multiplier per channel at the host rate.
// Both channels start their LFO in the same place and are driven identically, so a mono
// guitar duplicated onto two channels stays a mono tremolo.
class BossTr2Pedal : public Pedal
{
public:
    BossTr2Pedal();

    juce::String getName() const override { return "Boss TR-2 Tremolo"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<BossTr2Stage> stages;

    PedalParameter rate  { "Rate",  0.0f, 100.0f, 45.0f };
    PedalParameter depth { "Depth", 0.0f, 100.0f, 60.0f };
    PedalParameter wave  { "Wave",  0.0f, 100.0f, 25.0f };
};
