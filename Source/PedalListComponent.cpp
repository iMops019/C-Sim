#include "PedalListComponent.h"
#include "ModernLookAndFeel.h"

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
        g.setColour(ModernColours::surface);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
        g.setColour(ModernColours::border);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
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

// The actual scrollable content: as many catalog entries as the list has,
// laid out at their natural height rather than clipped to whatever the
// current tab area happens to be - PedalListComponent puts this inside a
// Viewport so a catalog can grow past a handful of entries (which the Lab
// and Pedals tabs both now have) without silently hiding the rest.
class PedalListComponent::Content : public juce::Component
{
public:
    void addEntry(const juce::String& name, std::function<void()> onAdd)
    {
        auto* entry = entries.add(new CatalogEntry(name, std::move(onAdd)));
        addAndMakeVisible(entry);
    }

    int getRequiredHeight() const noexcept
    {
        if (entries.isEmpty())
            return margin * 2;

        return entries.size() * (entryHeight + gap) - gap + margin * 2;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(margin);
        for (auto* entry : entries)
        {
            entry->setBounds(area.removeFromTop(entryHeight));
            area.removeFromTop(gap);
        }
    }

private:
    static constexpr int entryHeight = 32;
    static constexpr int gap = 6;
    static constexpr int margin = 10;

    juce::OwnedArray<CatalogEntry> entries;
};

PedalListComponent::PedalListComponent(SignalChainComponent& signalChainToUse, std::vector<CatalogItem> catalogToShow)
    : signalChain(signalChainToUse), content(std::make_unique<Content>())
{
    for (auto& item : catalogToShow)
    {
        auto create = item.create;
        content->addEntry(item.name, [this, create] { signalChain.addPedal(create()); });
    }

    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

PedalListComponent::~PedalListComponent() = default;

void PedalListComponent::resized()
{
    viewport.setBounds(getLocalBounds());

    auto width = juce::jmax(1, viewport.getWidth() - viewport.getScrollBarThickness());
    auto height = juce::jmax(viewport.getHeight(), content->getRequiredHeight());
    content->setSize(width, height);

    // setSize() is a no-op (and skips calling resized()) if the size
    // didn't actually change - call it directly so entries are always
    // laid out. Harmless today since this catalog is static, but the same
    // bug just bit the dynamically-changing Signal Chain board.
    content->resized();
}
