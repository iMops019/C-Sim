#include "SignalChainComponent.h"

class SignalChainComponent::SlotComponent : public juce::Component
{
public:
    SlotComponent(Pedal& pedalToShow, std::function<void()> onRemove)
        : pedal(pedalToShow)
    {
        nameLabel.setText(pedal.getName(), juce::dontSendNotification);
        nameLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(nameLabel);

        removeButton.setButtonText("x");
        removeButton.onClick = std::move(onRemove);
        addAndMakeVisible(removeButton);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colours::darkgrey);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colours::grey);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        removeButton.setBounds(area.removeFromTop(18).removeFromRight(18));
        nameLabel.setBounds(area);
    }

    Pedal& pedal;

private:
    juce::Label nameLabel;
    juce::TextButton removeButton;
};

SignalChainComponent::SignalChainComponent() = default;
SignalChainComponent::~SignalChainComponent() = default;

void SignalChainComponent::setAudioConfig(double sampleRate, int maximumBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;
    currentBlockSize = maximumBlockSize;
    currentNumChannels = numChannels;
}

void SignalChainComponent::addPedal(std::unique_ptr<Pedal> pedal)
{
    pedal->prepare(currentSampleRate, currentBlockSize, currentNumChannels);

    {
        const juce::ScopedLock lock(chainLock);
        chain.push_back(std::move(pedal));
    }

    rebuildSlotComponents();
}

void SignalChainComponent::removePedal(Pedal* pedalToRemove)
{
    {
        const juce::ScopedLock lock(chainLock);
        chain.erase(std::remove_if(chain.begin(), chain.end(),
                                    [pedalToRemove](const std::unique_ptr<Pedal>& p)
                                    { return p.get() == pedalToRemove; }),
                    chain.end());
    }

    rebuildSlotComponents();
}

void SignalChainComponent::rebuildSlotComponents()
{
    slots.clear();

    for (auto& pedal : chain)
    {
        auto* raw = pedal.get();
        auto* slot = slots.add(new SlotComponent(*raw, [this, raw] { removePedal(raw); }));
        addAndMakeVisible(slot);
    }

    resized();
    repaint();
}

void SignalChainComponent::processBlock(float* const* channelData, int numChannels, int numSamples)
{
    const juce::ScopedTryLock lock(chainLock);
    if (!lock.isLocked())
        return;

    for (auto& pedal : chain)
        if (!pedal->bypassed)
            pedal->process(channelData, numChannels, numSamples);
}

void SignalChainComponent::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::black.withAlpha(0.15f));
    g.drawRect(getLocalBounds(), 1);

    if (slots.isEmpty())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Signal Chain - empty. Add pedals from the Pedals tab below.",
                    getLocalBounds(), juce::Justification::centred);
    }
}

void SignalChainComponent::resized()
{
    auto area = getLocalBounds().reduced(10);
    constexpr int slotWidth = 110;
    constexpr int gap = 10;

    for (auto* slot : slots)
    {
        slot->setBounds(area.removeFromLeft(slotWidth));
        area.removeFromLeft(gap);
    }
}
