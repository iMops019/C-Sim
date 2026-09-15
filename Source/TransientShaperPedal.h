#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"

// A dual-envelope transient shaper (SPL Transient Designer style): tracks
// a fast envelope (the instantaneous transient) and a slow envelope (the
// settled/sustained level), then boosts or cuts gain based on how far
// apart they are. For a chuggy rhythm tone, boost Attack (punchier pick
// attack) and cut Sustain (tighter, faster decay, less mud) - which are
// this pedal's defaults. Placing it before the amp gives the distortion
// stage a punchier transient to dig into.
class TransientShaperPedal : public Pedal
{
public:
    TransientShaperPedal();

    juce::String getName() const override { return "Transient Shaper"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    double currentSampleRate = 44100.0;

    static constexpr int maxChannels = 2;
    float fastEnvelope[maxChannels] = { 0.0f, 0.0f };
    float slowEnvelope[maxChannels] = { 0.0f, 0.0f };

    float fastAttackCoeff = 0.0f;
    float fastReleaseCoeff = 0.0f;
    float slowAttackCoeff = 0.0f;
    float slowReleaseCoeff = 0.0f;

    PedalParameter attack  { "Attack",  -100.0f, 100.0f, 40.0f };
    PedalParameter sustain { "Sustain", -100.0f, 100.0f, -30.0f };
};
