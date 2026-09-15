#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

#include "Pedal.h"

// The centre "pedalboard" area: shows the ordered chain of pedals the user
// has manually added, and is the only thing the audio callback processes
// through. Starts empty - nothing is added here automatically.
class SignalChainComponent : public juce::Component
{
public:
    SignalChainComponent();
    ~SignalChainComponent() override;

    void setAudioConfig(double sampleRate, int maximumBlockSize, int numChannels);

    // Message-thread only. Prepares the pedal, then adds it to the chain.
    void addPedal(std::unique_ptr<Pedal> pedal);
    void removePedal(Pedal* pedalToRemove);

    // Audio-thread only. Skips processing (leaves passthrough audio as-is)
    // if the chain is currently being modified rather than blocking.
    void processBlock(float* const* channelData, int numChannels, int numSamples);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class SlotComponent;

    void rebuildSlotComponents();

    juce::CriticalSection chainLock;
    std::vector<std::unique_ptr<Pedal>> chain; // guarded by chainLock

    juce::OwnedArray<SlotComponent> slots; // message-thread only, mirrors chain

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentNumChannels = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalChainComponent)
};
