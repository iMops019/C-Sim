#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

#include "Pedal.h"
#include "PedalInspectorWindow.h"

// The rack: a literal 19"-style stage rack rendering of the ordered
// chain of pedals the user has added, and the only thing the audio
// callback processes through. Starts empty - nothing is added here
// automatically. Each entry is a full-width rack-unit strip (order
// number, name, a lit power/bypass LED, a live VU meter, remove,
// move-earlier/move-later) stacked top to bottom like units screwed
// into a real rack frame; clicking a strip opens a separate
// PedalInspectorWindow with that pedal's actual knobs, rather than
// showing every knob for every pedal in the rack at once.
class SignalChainComponent : public juce::Component
{
public:
    SignalChainComponent();
    ~SignalChainComponent() override;

    void setAudioConfig(double sampleRate, int maximumBlockSize, int numChannels);

    // Message-thread only. Prepares the pedal, then adds it to the chain.
    void addPedal(std::unique_ptr<Pedal> pedal);
    void removePedal(Pedal* pedalToRemove);
    void clear();

    // Message-thread only. Replaces the chain's order with newOrder - a
    // permutation of the pedals already in the chain (e.g. from a drag
    // reorder in the rack UI) - leaving move-earlier/move-later's simpler
    // by-one-step reordering in place as a mouse-drag-free alternative.
    // Any pointer in newOrder no longer actually in the chain is ignored;
    // any pedal in the chain but missing from newOrder is appended at the
    // end, preserving its old relative order - defensive against a stale
    // snapshot, though the rack UI never actually produces one.
    void reorderPedals(const std::vector<Pedal*>& newOrder);

    // Message-thread only. A snapshot of the current chain in order, for
    // inspection (e.g. saving a preset) - not for holding onto, since a
    // pedal can be removed at any time from the message thread.
    std::vector<Pedal*> getPedalsInOrder() const;

    // Audio-thread only. Skips processing (leaves passthrough audio as-is)
    // if the chain is currently being modified rather than blocking.
    void processBlock(float* const* channelData, int numChannels, int numSamples);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class BoardTile;
    class Content;

    void rebuildSlotComponents();
    void openInspector(Pedal* pedalToInspect);
    void closeInspector(Pedal* pedalToClose);
    void moveExistingPedal(Pedal* pedalToMove, int delta);

    juce::CriticalSection chainLock;
    std::vector<std::unique_ptr<Pedal>> chain; // guarded by chainLock

    // Both of these reference pedals owned by `chain` without owning them
    // - declared after `chain` so they're destroyed first (reverse
    // declaration order), never left holding a dangling Pedal&.
    std::vector<std::unique_ptr<PedalInspectorWindow>> openInspectors;
    std::unique_ptr<Content> content;
    juce::Viewport viewport;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentNumChannels = 2;

    static constexpr int railWidth = 8; // rack frame's mounting-rail width, either side

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalChainComponent)
};
