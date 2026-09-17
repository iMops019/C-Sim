#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Pedal.h"
#include "CabImpulseResponse.h"

#include <vector>

// A full cab + mic simulator, replacing the old single-cab FourByTwelveCabPedal:
// pick a cab/speaker, dial in two independently-typed/positioned mics and
// blend between them, add a touch of a Live or Studio room mic, then trim
// with Low Cut/High Cut/Resonance and stereo Width. Everything runs
// through real juce::dsp::Convolution against procedurally synthesized
// IRs (see CabImpulseResponse) - a real captured IR .wav can still be
// loaded in to override Mic A, exactly as the old cab supported.
//
// Three convolution engines run in parallel (Mic A, Mic B, Room), each
// against its own scratch copy of the dry signal, then their outputs are
// mixed with plain real-time gain blends (Mic Blend, Room amount) - since
// convolution is linear, this is mathematically identical to blending the
// impulse responses themselves, but lets those two knobs move smoothly in
// real time without reloading (and re-partitioning) a convolution kernel
// on every tweak. A kernel reload only happens when something that's
// actually baked into an IR changes: Cab, a Mic's Type/Position, or
// Live/Studio - and even then, only when the (quantized) setting actually
// changed since last block, not on every call.
class CabinetPedal : public Pedal
{
public:
    CabinetPedal();

    juce::String getName() const override { return "Cabinet"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

    bool supportsImpulseResponseFile() const override { return true; }
    bool loadImpulseResponseFile(const juce::File& file) override;

    // A drawn cab with draggable mic markers in place of the Cab/Mic
    // Type/Position knobs - see CabinetVisualEditor.
    std::unique_ptr<juce::Component> createCustomEditor() override;
    std::vector<PedalParameter*> getCustomEditorHandledParameters() override;

private:
    static constexpr int maxChannels = 2;

    void reloadMicIRIfNeeded(int mic, bool cabChanged);
    void reloadRoomIRIfNeeded(bool liveStudioChanged);

    juce::dsp::Convolution convA, convB, convRoom;
    juce::AudioBuffer<float> scratchA, scratchB, scratchRoom;

    juce::IIRFilter lowCutFilters[maxChannels], highCutFilters[maxChannels], resonanceFilters[maxChannels];
    juce::IIRFilter airFilters[maxChannels];

    double currentSampleRate = 44100.0;
    int currentMaxBlockSize = 512;
    int currentNumChannels = 2;

    // Cached last-applied (quantized) settings, so a kernel reload only
    // happens when one of these actually changes.
    int lastCab = -1, lastMicTypeA = -1, lastMicTypeB = -1, lastLiveStudio = -1;
    int lastMicPositionAStep = -1, lastMicPositionBStep = -1;

    // True while Mic A is a real, user-loaded IR file rather than a
    // synthetic one - see loadImpulseResponseFile()/reloadMicIRIfNeeded()
    // and the makeup-gain comment in process().
    bool usingLoadedIR = false;

    PedalParameter cab          { "Cab",         0.0f, 7.0f,   0.0f,
                                   { "4x12 V30", "4x12 Greenback", "2x12 Open Back", "1x12 Combo",
                                     "Fender 1x10", "Fender 2x10", "Fender 1x12", "Fender 4x12" } };
    PedalParameter micTypeA     { "Mic A",       0.0f, 1.0f,   0.0f, { "Dynamic", "Ribbon" } };
    PedalParameter micPositionA { "Position A",  0.0f, 100.0f, 30.0f };
    PedalParameter micTypeB     { "Mic B",       0.0f, 1.0f,   1.0f, { "Dynamic", "Ribbon" } };
    PedalParameter micPositionB { "Position B",  0.0f, 100.0f, 70.0f };
    PedalParameter micBlend     { "Mic Blend",   0.0f, 100.0f, 30.0f };
    PedalParameter liveStudio   { "Live/Studio", 0.0f, 1.0f,   1.0f, { "Live", "Studio" } };
    PedalParameter roomAmount   { "Room",        0.0f, 100.0f, 15.0f };
    PedalParameter lowCut       { "Low Cut",     40.0f, 500.0f, 80.0f };
    PedalParameter highCut      { "High Cut",    2000.0f, 12000.0f, 8000.0f };
    PedalParameter resonance    { "Resonance",   -12.0f, 12.0f, 0.0f };
    // Boost-only high shelf above the IR's own natural rolloff - real
    // captured cab IRs measure MUCH darker above ~3-4kHz than the
    // "explosive"/hyped top end a commercial plugin's own post-processing
    // adds on top of its own IRs. A modest default (rather than 0) keeps
    // the synthetic cabs' own already-tuned voicing from changing much;
    // loadImpulseResponseFile() bumps this higher specifically when a
    // real IR loads, since that's where the extra help is actually
    // needed (see its own comment).
    PedalParameter air          { "Air",         0.0f, 100.0f, 15.0f };
    PedalParameter width        { "Width",       0.0f, 200.0f, 100.0f };
    PedalParameter level        { "Level",       0.0f, 150.0f, 100.0f };
};
