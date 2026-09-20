#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/HoofStage.h"

#include <vector>

// The EarthQuaker Devices Hoof Fuzz (Pedals tab): a germanium/LED Big Muff-family
// fuzz, solved as its whole circuit - see dsp/HoofStage.h for the schematic, the
// sources and what is estimated rather than read. The knobs are the pedal's own:
//
//   Fuzz    the Sustain pot between the input booster and the clipping stages - from a
//           clean-ish boost at zero to a wall of sustain at full (about 27dB of range)
//   Tone    a passive low-pass / high-pass blend, as in any Big Muff: dark at zero,
//           bright at full, with the mid scoop in between
//   Level   the output Volume (an audio taper)
//   Shift   where the tone stack's high-pass leg is loaded: it moves the mid scoop
//           (counter-clockwise boosts the mids, clockwise deepens the scoop)
//
// Its input impedance is low (~44k, from the schematic), so it loads a passive
// pickup more than most pedals do.
//
// COST. The circuit is solved as one nonlinear system at 4x the host rate: roughly
// 15-20% of one core in a release build. A mono guitar is normally duplicated onto
// both channels, so bit-identical stereo input is processed once and copied.
class HoofPedal : public Pedal
{
public:
    HoofPedal();

    juce::String getName() const override { return "EQD Hoof Fuzz"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<HoofStage> stages;

    PedalParameter fuzz  { "Fuzz",  0.0f, 100.0f, 70.0f };
    PedalParameter tone  { "Tone",  0.0f, 100.0f, 50.0f };
    // Default Level is gain-staged: a strummed chord comes out ~3 dB louder than the guitar (RMS),
    // like the Tube Screamer. At 60 it was 13 dB hotter, enough to slam an amp into saturation.
    PedalParameter level { "Level", 0.0f, 100.0f, 32.0f };
    PedalParameter shift { "Shift", 0.0f, 100.0f, 40.0f };
};
