#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Pedal.h"
#include "CabImpulseResponse.h"
#include "dsp/MicPlacement.h"
#include "dsp/SpeakerCompression.h"
#include "SpeakerLoadLink.h"

#include <vector>

// A full cab + mic simulator, replacing the old single-cab FourByTwelveCabPedal:
// pick a cab/speaker, dial in two independently-typed/positioned mics and
// blend between them, add a touch of a Live or Studio room mic, then trim
// with Low Cut/High Cut/Resonance and stereo Width. Everything runs
// through real juce::dsp::Convolution against procedurally synthesized
// IRs (see CabImpulseResponse). Every mic engine also has its own IR slot
// (see the IR slots comment below): any of them can be swapped for a real
// captured .wav independently of the others.
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
    ~CabinetPedal() override;

    juce::String getName() const override { return "Cabinet"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

    // IR slots - one per convolution engine that carries a mic:
    //   0 = Left Mic A, 1 = Left Mic B, 2 = Right Mic A, 3 = Right Mic B
    // ("Right" is the second cab from dual-cab mode - see Cab R). Each slot
    // is independently either synthetic (driven by Cab/Mic Type/Position as
    // usual) or a real loaded file. A loaded slot STAYS loaded until Clear
    // is pressed: its Type/Position/Cab are physically baked into the
    // capture, so nudging those knobs no longer throws the file away (they
    // simply don't apply to that slot). Loading into a Right slot switches
    // dual-cab mode on by itself, even with Cab R at "Same as Cab" - in
    // which case that side's remaining synthetic mic uses the left cab.
    // File paths are saved in presets (getExtraState) and reloaded; a file
    // that's gone missing quietly falls back to the synthetic cab.
    std::vector<juce::String> getImpulseResponseSlotNames() const override
    {
        return { "Left Mic A", "Left Mic B", "Right Mic A", "Right Mic B" };
    }
    bool loadImpulseResponseFile(int slot, const juce::File& file) override;
    void clearImpulseResponse(int slot) override;
    juce::String getImpulseResponseFileName(int slot) const override;

    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;

    // A drawn cab with draggable mic markers in place of the Cab/Mic
    // Type/Position knobs - see CabinetVisualEditor.
    std::unique_ptr<juce::Component> createCustomEditor() override;
    std::vector<PedalParameter*> getCustomEditorHandledParameters() override;

    // Exposed for CabinetVisualEditor, so it can show the user WHY moving
    // a mic marker might not do anything: Mic B while Mic Blend reads near
    // 0%, or a mic whose slot holds a real IR (its Position is baked into
    // the capture) - neither of which was visible in the UI at all before,
    // which is exactly what made this feel broken rather than just quiet.
    bool isSlotLoaded(int slot) const noexcept
    {
        return slot >= 0 && slot < numIRSlots && slotLoaded[slot].load();
    }

private:
    static constexpr int maxChannels = 2;
    static constexpr int numIRSlots = 4;

    void reloadMicIRIfNeeded(int mic, bool cabChanged);
    void reloadRoomIRIfNeeded(bool liveStudioChanged);
    void reloadRightCabIRsIfNeeded();
    void loadMicIR(juce::dsp::Convolution& conv, int cabType, int secondCabType, int mixStep, int micType, int positionStep);
    juce::dsp::Convolution& engineForSlot(int slot) noexcept;
    bool applyIRFile(int slot, const juce::File& file);
    bool anySlotLoaded() const noexcept;

    juce::dsp::Convolution convA, convB, convRoom;
    juce::AudioBuffer<float> scratchA, scratchB, scratchRoom;

    // Dual-cab mode's second cab (see the Cab R parameter): its own pair of
    // mic engines, same mic Types/Positions as the left cab's. Only run
    // while dual mode is on (Cab R names a cab, or a Right IR slot is
    // loaded), so the default single-cab path costs nothing extra.
    juce::dsp::Convolution convA2, convB2;
    juce::AudioBuffer<float> scratchA2, scratchB2;
    int lastRightSigA = -1, lastRightSigB = -1;

    // Mic placement (see dsp/MicPlacement.h): per convolution engine (same
    // order as the IR slots), per channel, a proximity-effect shelf and an
    // arrival-time delay applied to that mic's output right after its
    // convolution. At the default (both mics the same distance) the shelf
    // is exactly identity and the delay exactly zero - the sound is
    // unchanged until a Distance knob moves. A slot holding a real IR
    // skips the shelf (that capture already contains its own proximity
    // effect - applying it again would double-count) but keeps the delay,
    // where it acts as a phase-alignment control between two real captures.
    struct MicPlacementState
    {
        MicPlacement::ProximityShelf shelf[maxChannels];
        MicPlacement::FractionalDelay delay[maxChannels];
    };
    MicPlacementState placement[numIRSlots];

    // Speaker compression/breakup (see dsp/SpeakerCompression.h and the
    // Spk Push parameter): one processor per channel per cab (left cab,
    // right cab), applied to the dry signal BEFORE the convolution engines
    // so the cone's distortion is then shaped by the cab and mic, and so a
    // real loaded IR (linear, static) gets the dynamic behavior it lacks.
    // Always run, so their filters/envelopes are warm the moment Push is
    // raised; at Push = 0 they hand back their input unchanged, exactly.
    SpeakerCompression::Processor speakerProcessors[2][maxChannels];
    juce::AudioBuffer<float> pushedLeft, pushedRight;

    juce::IIRFilter lowCutFilters[maxChannels], highCutFilters[maxChannels], resonanceFilters[maxChannels];
    juce::IIRFilter airFilters[maxChannels];

    double currentSampleRate = 44100.0;
    int currentMaxBlockSize = 512;
    int currentNumChannels = 2;

    // Cached last-applied (quantized) settings, so a kernel reload only
    // happens when one of these actually changes.
    int lastCab = -1, lastMicTypeA = -1, lastMicTypeB = -1, lastLiveStudio = -1;
    int lastMicPositionAStep = -1, lastMicPositionBStep = -1;

    // Per-slot IR state - see the IR slots comment above. slotLoaded and
    // pendingResynth are touched from both the GUI thread (load/clear) and
    // the audio thread (process), hence atomic; slotFiles is GUI-thread
    // only (it exists for getExtraState/the Inspector's file-name display).
    // pendingResynth is set by Clear: the audio thread notices it and
    // resynthesizes that engine's IR even if none of its own knobs moved.
    std::atomic<bool> slotLoaded[numIRSlots] {};
    std::atomic<bool> pendingResynth[numIRSlots] {};
    juce::File slotFiles[numIRSlots];

    PedalParameter cab          { "Cab",         0.0f, 7.0f,   0.0f,
                                   { "4x12 V30", "4x12 Greenback", "2x12 Open Back", "1x12 Combo",
                                     "Fender 1x10", "Fender 2x10", "Fender 1x12", "Fender 4x12" } };
    PedalParameter micTypeA     { "Mic A",       0.0f, 1.0f,   0.0f, { "Dynamic", "Ribbon" } };
    PedalParameter micPositionA { "Position A",  0.0f, 100.0f, 30.0f };
    PedalParameter micTypeB     { "Mic B",       0.0f, 1.0f,   1.0f, { "Dynamic", "Ribbon" } };
    PedalParameter micPositionB { "Position B",  0.0f, 100.0f, 70.0f };
    PedalParameter micBlend     { "Mic Blend",   0.0f, 100.0f, 30.0f };
    // Flip either mic's polarity in the blend - see dsp/DualCabMix.h.
    PedalParameter polarityA    { "Polarity A",  0.0f, 1.0f,   0.0f, { "Normal", "Invert" } };
    PedalParameter polarityB    { "Polarity B",  0.0f, 1.0f,   0.0f, { "Normal", "Invert" } };
    // Pan the two mics apart: 0 (default) = both mics blended into both sides
    // (unchanged); 100 = Mic A only on the left, Mic B only on the right - a
    // real stereo pair with genuine width. See DualCabMix::mixMicsWithSpread.
    PedalParameter micSpread    { "Mic Spread",  0.0f, 100.0f, 0.0f };
    // How far each mic sits from the cab: 0 = 1 inch, 100 = 18 inches. Sets
    // that mic's proximity-effect bass boost AND (via the difference
    // between the two mics) their relative arrival time - see
    // dsp/MicPlacement.h. 50 is the neutral reference for both, so the
    // default changes nothing. Shared by both cabs in dual-cab mode.
    PedalParameter distanceA    { "Distance A",  0.0f, 100.0f, 50.0f };
    PedalParameter distanceB    { "Distance B",  0.0f, 100.0f, 50.0f };
    // Dual-cab: a second cabinet on the right side. Index 0 = "Same as
    // Cab" (dual mode off - exactly the single-cab behavior - unless a
    // Right IR slot is loaded, which turns it on by itself); index n+1 =
    // the same cab types Cab offers, so the two lists stay in step. Spread
    // is how far apart the two sit: 0 = both centered (they sum), 100 =
    // hard left/right. Mics and Room are shared by both cabs.
    PedalParameter cabRight     { "Cab R",       0.0f, 8.0f,   0.0f,
                                   { "Same as Cab", "4x12 V30", "4x12 Greenback", "2x12 Open Back", "1x12 Combo",
                                     "Fender 1x10", "Fender 2x10", "Fender 1x12", "Fender 4x12" } };
    PedalParameter spread       { "Spread",      0.0f, 100.0f, 100.0f };
    // Mixed speakers: a second speaker type blended into the cab (e.g. a
    // 4x12 V30 with some Greenback in with it) - see
    // CabImpulseResponse::generateMixedMicIR. Spk 2 = "None" (the default)
    // is a plain single-speaker cab, exactly as before; Spk Mix is the
    // balance the mic hears (0 = all the Cab's speaker, 100 = all Spk 2).
    // The "R" pair does the same for the right cab in dual-cab mode; with
    // Cab R at "Same as Cab" the right cab mirrors the left completely,
    // mix included, and the R pair is ignored.
    PedalParameter speaker2     { "Spk 2",       0.0f, 8.0f,   0.0f,
                                   { "None", "4x12 V30", "4x12 Greenback", "2x12 Open Back", "1x12 Combo",
                                     "Fender 1x10", "Fender 2x10", "Fender 1x12", "Fender 4x12" } };
    PedalParameter speakerMix   { "Spk Mix",     0.0f, 100.0f, 50.0f };
    PedalParameter speaker2R    { "Spk 2 R",     0.0f, 8.0f,   0.0f,
                                   { "None", "4x12 V30", "4x12 Greenback", "2x12 Open Back", "1x12 Combo",
                                     "Fender 1x10", "Fender 2x10", "Fender 1x12", "Fender 4x12" } };
    PedalParameter speakerMixR  { "Spk Mix R",   0.0f, 100.0f, 50.0f };
    // How hard the speaker is being pushed: 0 (default) = off, an exact
    // bypass; up = more bass squash + harmonics (cone excursion limiting)
    // and slow power compression. Level-dependent, and each cab's headroom
    // follows its speaker's rated power (a 25 W Greenback breaks up sooner
    // than a 60 W V30) - see dsp/SpeakerCompression.h.
    PedalParameter speakerPush  { "Spk Push",    0.0f, 100.0f, 0.0f };
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
