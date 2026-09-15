#include "PedalListComponent.h"
#include "ReverbPedal.h"

class PedalListComponent::CatalogEntry : public juce::Component
{
public:
    CatalogEntry(const juce::String& pedalName, std::function<void()> onAdd)
    {
        nameLabel.setText(pedalName, juce::dontSendNotification);
        addAndMakeVisible(nameLabel);

        addButton.setButtonText("Add");
        addButton.onClick = std::move(onAdd);
        addAndMakeVisible(addButton);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        addButton.setBounds(area.removeFromRight(60));
        nameLabel.setBounds(area);
    }

private:
    juce::Label nameLabel;
    juce::TextButton addButton;
};

PedalListComponent::PedalListComponent(SignalChainComponent& signalChainToUse)
    : signalChain(signalChainToUse)
{
    auto* reverbEntry = entries.add(new CatalogEntry("Reverb", [this]
    {
        signalChain.addPedal(std::make_unique<ReverbPedal>());
    }));
    addAndMakeVisible(reverbEntry);
}

PedalListComponent::~PedalListComponent() = default;

void PedalListComponent::paint(juce::Graphics&)
{
}

void PedalListComponent::resized()
{
    auto area = getLocalBounds().reduced(10);
    constexpr int entryHeight = 32;
    constexpr int gap = 6;

    for (auto* entry : entries)
    {
        entry->setBounds(area.removeFromTop(entryHeight));
        area.removeFromTop(gap);
    }
}
