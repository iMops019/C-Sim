#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/MarshallPlexi1959Amp.h"

#include <cmath>
#include <memory>
#include <vector>

// The Marshall 1959HW Plexi Super Lead 100 (Amps tab): a complete amp, traced from
// the 1959's own drawings and modeled as the circuit it is - see
// dsp/MarshallPlexi1959Amp.h, MarshallPlexiPreamp.h and MarshallPlexiPowerAmp.h
// for the circuit, the sources and every judgment call. The knobs are the amp's
// own, in panel order:
//
//   Channel      the channel the guitar is plugged into: High Treble (Channel I,
//                the bright one), Normal (Channel II, the fuller one), or Jumped -
//                the classic cable from one channel's Low jack to the other's High
//                jack, which puts the guitar on both and mixes them.
//   Input        the High or Low sensitivity jack: Low is 6dB down AND darker (its
//                input impedance is 136k against ~1M, which damps the pickup's
//                resonance - modeled by the Pickup setting below).
//   Volume I / II, Treble, Middle, Bass, Presence
//   Impedance    the output impedance selector: 4, 8 or 16 ohms. The amp's negative
//                feedback is taken off the speaker output, so a lower setting means
//                less feedback and a looser, more resonant low end (Marshall's
//                manual calls this the most tonally significant variation in the
//                reissue). The cabinet is assumed to match the setting, as the
//                manual insists.
//   Pickup       the kind of guitar: Off (no correction), Single coil or Humbucker.
//                Only matters on the Low input, where the amp's input impedance is
//                low enough to load the pickup (see dsp/PickupLoading.h).
//   Output       a digital level trim AFTER the amp: 100 = 0 dB (unchanged), 0 = -40 dB.
//                The real amp has no master volume and, with a normal guitar, the
//                High Treble and Jumped channels are at or near full power at almost any
//                Volume (the drawn 5nF bright cap bleeds signal past the knob), so this is
//                how to bring the whole thing down WITHOUT touching the sound: it scales
//                the finished waveform, so the tone and the amount of distortion are
//                exactly what they were, only quieter. It sits ahead of the output tanh,
//                so turned down it also takes that safety ceiling out of play. A Cabinet
//                after the amp then sees a lower level, as behind a power attenuator (which
//                matters only if its Speaker Push is on).
//
// The speaker on the end of the amp is the Cabinet's, if one is in the chain (see
// SpeakerLoadLink.h): the feedback reacts to its impedance curve, so the same amp
// sounds different into a different cabinet. Otherwise a generic 12" model.
//
// LEVELS. A normalised 1.0 in is 1V at the guitar jack. The amp's output is the
// voltage at the speaker terminals - 100W into 16 ohms is a peak of ~57V - which is
// normalised by 64V, so full power comes out near full scale, and a final tanh only
// catches an extreme combination of knobs before it would clip digitally.
//
// COST. A circuit-level power stage is not cheap: roughly 35-45% of one core per
// channel in a release build (the amp runs at 4x the host rate). A mono guitar is
// normally duplicated onto both channels, so bit-identical stereo input is
// processed once and copied.
class MarshallPlexi1959Pedal : public Pedal
{
public:
    MarshallPlexi1959Pedal();

    juce::String getName() const override { return "Marshall 1959HW Plexi"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

    // The Output knob's gain: 100 is 0 dB (the amp exactly as it always was) and each step down
    // is 0.4 dB, to -40 dB at 0.
    static float outputTrimGain(float knobPercent) noexcept
    {
        return knobPercent >= 100.0f ? 1.0f : std::pow(10.0f, -(100.0f - knobPercent) * 0.4f / 20.0f);
    }

private:
    static constexpr int maxChannels = 2;

    std::vector<std::unique_ptr<MarshallPlexi1959Amp>> amps;

    // Panel order: the selectors, then the tone section.
    PedalParameter channel   { "Channel",   0.0f, 2.0f,   2.0f, { "High Treble", "Normal", "Jumped" } };
    PedalParameter input     { "Input",     0.0f, 1.0f,   0.0f, { "High", "Low" } };
    PedalParameter pickup    { "Pickup",    0.0f, 2.0f,   0.0f, { "Off", "Single coil", "Humbucker" } };

    PedalParameter volumeI   { "Volume I",  0.0f, 100.0f, 50.0f };
    PedalParameter volumeII  { "Volume II", 0.0f, 100.0f, 40.0f };
    PedalParameter treble    { "Treble",    0.0f, 100.0f, 60.0f };
    PedalParameter middle    { "Middle",    0.0f, 100.0f, 50.0f };
    PedalParameter bass      { "Bass",      0.0f, 100.0f, 50.0f };
    PedalParameter presence  { "Presence",  0.0f, 100.0f, 50.0f };
    PedalParameter impedance { "Impedance", 0.0f, 2.0f,   2.0f, { "4 ohm", "8 ohm", "16 ohm" } };
    PedalParameter output    { "Output",    0.0f, 100.0f, 100.0f };

    static constexpr float guitarVoltsPerUnit = 1.0f;
    static constexpr float speakerFullScaleVolts = 64.0f;

    AmpSpeakerLoad::Speaker drivenSpeaker;

    // The Output trim's smoothed gain. One value for the whole pedal: each processed channel
    // glides through the same ramp from where the last block ended, and the end point is kept.
    float trimNow = 1.0f;
    float trimAlpha = 0.0f;
};
