#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <memory>
#include <vector>

#include "PedalParameter.h"

// Base interface for anything that can sit in the signal chain.
class Pedal
{
public:
    virtual ~Pedal() = default;

    virtual juce::String getName() const = 0;

    virtual void prepare(double sampleRate, int maximumBlockSize, int numChannels) = 0;

    // Processes in place. channelData/numChannels describe the active
    // output channels of the current audio block.
    virtual void process(float* const* channelData, int numChannels, int numSamples) = 0;

    // Knob-controllable parameters, for the UI to build controls from.
    // Empty by default - override if the pedal has any.
    virtual std::vector<PedalParameter*> getParameters() { return {}; }

    // Override for pedals with user-loadable impulse response slots (e.g. a
    // cab where each mic can be swapped from its built-in synthetic IR to a
    // real captured one). The Inspector shows a Load/Clear row per name
    // returned here. Empty by default - most pedals have none.
    virtual std::vector<juce::String> getImpulseResponseSlotNames() const { return {}; }
    virtual bool loadImpulseResponseFile(int /*slot*/, const juce::File&) { return false; }
    // Back to the slot's built-in (synthetic) IR.
    virtual void clearImpulseResponse(int /*slot*/) {}
    // The loaded file's name, or empty if the slot is using its built-in IR.
    virtual juce::String getImpulseResponseFileName(int /*slot*/) const { return {}; }

    // State a preset must save that isn't a PedalParameter (e.g. which IR
    // file each slot has loaded). A void var (the default) means "nothing
    // extra" and the preset omits it entirely, so pedals without any keep
    // writing exactly the files they always did. setExtraState() is called
    // AFTER the pedal is prepared and in the chain, with whatever
    // getExtraState() produced (or a void var for an older preset).
    virtual juce::var getExtraState() const { return {}; }
    virtual void setExtraState(const juce::var&) {}

    // Optional custom visual editor, shown in the Inspector window in
    // place of the generic knob grid for whichever parameters it manages
    // (e.g. a drawn cabinet with draggable mic markers instead of Cab/Mic
    // dropdown knobs). Null by default - most pedals are fine with plain
    // knobs. When non-null, getCustomEditorHandledParameters() must list
    // every PedalParameter the returned editor manages, so the Inspector
    // doesn't also render a redundant knob for it.
    virtual std::unique_ptr<juce::Component> createCustomEditor() { return nullptr; }
    virtual std::vector<PedalParameter*> getCustomEditorHandledParameters() { return {}; }

    std::atomic<bool> bypassed { false };

    // Peak output level from this pedal's most recent process() call,
    // 0..~1+ (a hot signal can exceed 1). Written by SignalChainComponent
    // right after calling process() - generically, from outside the
    // pedal, so every existing Pedal subclass gets a real, live VU meter
    // in the rack UI for free without touching any of their own process()
    // implementations. Relaxed atomic: the UI thread only ever reads it
    // for painting, never anything requiring ordering with other state.
    std::atomic<float> meterLevel { 0.0f };
};
