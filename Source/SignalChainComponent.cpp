#include "SignalChainComponent.h"
#include "KnobComponent.h"

class SignalChainComponent::SlotComponent : public juce::Component
{
public:
    SlotComponent(Pedal& pedalToShow, std::function<void()> onRemove)
        : pedal(pedalToShow)
    {
        nameLabel.setText(pedal.getName(), juce::dontSendNotification);
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setFont(juce::Font(15.0f, juce::Font::bold));
        addAndMakeVisible(nameLabel);

        removeButton.setButtonText("x");
        removeButton.onClick = std::move(onRemove);
        addAndMakeVisible(removeButton);

        for (auto* param : pedal.getParameters())
        {
            auto* knob = knobs.add(new KnobComponent(*param));
            addAndMakeVisible(knob);
        }

        bool engaged = ! pedal.bypassed.load();
        footswitch.setClickingTogglesState(true);
        footswitch.setToggleState(engaged, juce::dontSendNotification);
        footswitch.setButtonText(engaged ? "ON" : "OFF");
        footswitch.setColour(juce::TextButton::buttonOnColourId, juce::Colours::limegreen);
        footswitch.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        footswitch.onClick = [this]
        {
            bool nowEngaged = footswitch.getToggleState();
            pedal.bypassed.store(! nowEngaged);
            footswitch.setButtonText(nowEngaged ? "ON" : "OFF");
        };
        addAndMakeVisible(footswitch);
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

        auto header = area.removeFromTop(20);
        removeButton.setBounds(header.removeFromRight(18));
        nameLabel.setBounds(header);

        area.removeFromTop(4);

        auto footswitchArea = area.removeFromBottom(30);
        footswitch.setBounds(footswitchArea.reduced(20, 0));

        area.removeFromBottom(6);

        juce::FlexBox fb;
        fb.flexWrap = juce::FlexBox::Wrap::wrap;
        fb.justifyContent = juce::FlexBox::JustifyContent::center;
        fb.alignContent = juce::FlexBox::AlignContent::flexStart;

        for (auto* knob : knobs)
            fb.items.add(juce::FlexItem(*knob).withWidth(52).withHeight(72).withMargin(2));

        fb.performLayout(area);
    }

    Pedal& pedal;

private:
    juce::Label nameLabel;
    juce::TextButton removeButton;
    juce::TextButton footswitch;
    juce::OwnedArray<KnobComponent> knobs;
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
        if (!pedal->bypassed.load(std::memory_order_relaxed))
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
    constexpr int slotWidth = 240;
    constexpr int gap = 10;

    for (auto* slot : slots)
    {
        slot->setBounds(area.removeFromLeft(slotWidth));
        area.removeFromLeft(gap);
    }
}
