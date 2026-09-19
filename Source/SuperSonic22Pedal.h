#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/PowerAmpStage.h"
#include "dsp/SpringReverb.h"
#include "dsp/SuperSonic22Preamp.h"

#include <memory>
#include <vector>

// A complete Fender Super-Sonic 22 (Amps tab): the preamp traced from Fender's
// service diagram (dsp/SuperSonic22Preamp.h - read that header for the circuit
// and how well the model matches the diagram's printed voltages), then the
// spring reverb, then the 6V6 power stage. The knobs are the amp's own, in the
// order they sit on the panel:
//
//   Vintage channel: Vintage Volume, Vintage Treble, Vintage Bass, and the
//     Normal/Fat switch (Fat: a hotter second stage inside a feedback loop).
//   Burn channel:    Gain 1, Gain 2, Burn Treble, Burn Bass, Burn Mid, Burn
//     Volume.
//   Shared:          the Vintage/Burn switch and Reverb.
//
// There is deliberately no Master, Presence or Resonance: the real amp has
// none (its power stage is fixed, with a single fixed negative-feedback
// resistor pair). A level is set by the channel Volume, exactly as on the
// amp, and an amp with no Master gets loud by driving its power stage, which
// this one does - turn a Volume up and the 6V6s break up.
//
// LEVELS, from the same diagram. Signal in the rack is normalised (1.0 =
// full scale); the preamp works in volts, so 1.0 at the input is treated as
// 1V at the guitar jack. After the channel the diagram shows a mixer stage
// (V4-B, which also adds the reverb) that takes the 88mV at the channel
// output (TP23) to the 210mV at the power amp's input (TP35): a gain of 2.4,
// applied here. The power stage clips when its 12AT7 phase inverter can no
// longer drive the 6V6 grids: fixed bias is -43.7V, and V5-A's printed gain of
// about 11 gives a grid swing of about 4V peak at the amp's input before that
// happens - which the diagram's own "22 watts into 8 ohms" test agrees with
// (13.3V RMS out, at a closed-loop power-amp gain of 4.6, needs 2.9V RMS in).
// That 4.1V is what a normalised 1.0 is at the power stage, so full power
// arrives at about full scale and a bigger signal is genuine overdrive.
//
// JUDGMENT CALLS (not on the diagram): the power stage is the toolkit's
// generic push-pull PowerAmpStage, not a model of the 6V6s and output
// transformer, so its curve is a stand-in. Its Sag is low (the amp rectifies
// with silicon diodes, 1N4006s, and has no tube rectifier to droop) and its
// feedback light: the diagram's own numbers put the loop gain around 0.3-0.5
// (a 47/867 divider from the output back to the phase inverter, against a
// power-amp gain of only 4.6), far from a Twin's heavy feedback. Reverb is
// the toolkit's SpringReverb, not a model of the Fender reverb pan.
class SuperSonic22Pedal : public Pedal
{
public:
    SuperSonic22Pedal();

    juce::String getName() const override { return "Fender Super-Sonic 22"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    static constexpr int maxChannels = 2;

    std::vector<std::unique_ptr<SuperSonic22Preamp>> preamps;
    std::vector<std::unique_ptr<SpringReverb>> reverbs;
    std::vector<std::unique_ptr<PowerAmpStage>> powerAmps;

    // Switches first, in panel order, then the two channels' knobs.
    PedalParameter channel { "Channel", 0.0f, 1.0f, 0.0f, { "Vintage", "Burn" } };
    PedalParameter fat { "Fat", 0.0f, 1.0f, 0.0f, { "Normal", "Fat" } };

    PedalParameter vintageVolume { "Vintage Volume", 0.0f, 100.0f, 50.0f };
    PedalParameter vintageTreble { "Vintage Treble", 0.0f, 100.0f, 50.0f };
    PedalParameter vintageBass   { "Vintage Bass",   0.0f, 100.0f, 50.0f };

    // Burn is a high-gain channel: at guitar level even a modest Gain 1
    // saturates it (see the preamp's test), so its Gain 1 and Volume default
    // below 12 o'clock rather than to it. Gain 2 defaults to 12 o'clock but
    // does little until the top of its travel - a property of the traced
    // circuit (see the preamp header).
    PedalParameter gain1      { "Gain 1",      0.0f, 100.0f, 35.0f };
    PedalParameter gain2      { "Gain 2",      0.0f, 100.0f, 50.0f };
    PedalParameter burnTreble { "Burn Treble", 0.0f, 100.0f, 50.0f };
    PedalParameter burnBass   { "Burn Bass",   0.0f, 100.0f, 50.0f };
    PedalParameter burnMid    { "Burn Mid",    0.0f, 100.0f, 50.0f };
    PedalParameter burnVolume { "Burn Volume", 0.0f, 100.0f, 40.0f };

    PedalParameter reverb { "Reverb", 0.0f, 100.0f, 15.0f };

    // Guitar level: a normalised 1.0 is 1V at the input jack.
    static constexpr float guitarVoltsPerUnit = 1.0f;

    // Channel output -> power amp input (TP35 / TP23 = 210mV / 88mV), then
    // volts -> the normalised level the power stage runs at (see above).
    static constexpr float mixerGain = 210.0f / 88.0f;
    static constexpr float powerAmpFullScaleVolts = 4.1f;

    // The Reverb knob at full is this much wet in the SpringReverb's
    // dry/wet mix - a judgment call (the pan and its drive circuit aren't
    // modeled).
    static constexpr float maxReverbMix = 0.45f;

    static constexpr float sagAmount = 0.10f;
    static constexpr float feedbackAmount = 0.15f;
};
