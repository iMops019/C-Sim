#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

#include "Pedal.h"
#include "SignalChainComponent.h"

// Content of a catalog tab (e.g. "Pedals" or "Amps"): lists available
// types the user can add to the Signal Chain themselves. Nothing here is
// added automatically - each entry only acts when its Add button is clicked.
class PedalListComponent : public juce::Component
{
public:
    struct CatalogItem
    {
        juce::String name;
        std::function<std::unique_ptr<Pedal>()> create;
    };

    PedalListComponent(SignalChainComponent& signalChainToUse, std::vector<CatalogItem> catalogToShow);
    ~PedalListComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class CatalogEntry;

    SignalChainComponent& signalChain;
    juce::OwnedArray<CatalogEntry> entries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalListComponent)
};
