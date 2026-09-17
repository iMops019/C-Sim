#pragma once

#include "PedalListComponent.h"

#include <memory>

// The full catalog of pedal/amp/cab/lab types the app knows how to
// construct. MainComponent uses the four tab-specific lists to build its
// catalog tabs; PresetManager uses createByName() to reconstruct an
// arbitrary saved chain, regardless of which tab each pedal originally
// came from - a preset just stores each pedal's getName() string.
namespace PedalCatalog
{
    std::vector<PedalListComponent::CatalogItem> pedals();
    std::vector<PedalListComponent::CatalogItem> amps();
    std::vector<PedalListComponent::CatalogItem> cabs();
    std::vector<PedalListComponent::CatalogItem> lab();

    // nullptr if no known type matches `name` (e.g. a preset saved by a
    // build that had a pedal this one no longer does).
    std::unique_ptr<Pedal> createByName(const juce::String& name);
}
