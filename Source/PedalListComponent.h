#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "Pedal.h"
#include "SignalChainComponent.h"

// Content of the "Pedals" tab: a catalog of available pedal types the user
// can add to the Signal Chain themselves. Nothing here is added
// automatically - each entry only acts when its Add button is clicked.
class PedalListComponent : public juce::Component
{
public:
    explicit PedalListComponent(SignalChainComponent& signalChainToUse);
    ~PedalListComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class CatalogEntry;

    SignalChainComponent& signalChain;
    juce::OwnedArray<CatalogEntry> entries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalListComponent)
};
