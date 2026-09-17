#include "PedalInspectorWindow.h"
#include "KnobComponent.h"
#include "ModernLookAndFeel.h"

namespace
{
    // The actual controls, in their own scrollable area so a pedal with a
    // lot of parameters (Cabinet has 13, Graphic EQ has 10) never clips a
    // knob off the bottom of the window regardless of how tall the window
    // ends up being.
    class InspectorContent : public juce::Component
    {
    public:
        InspectorContent(Pedal& pedalToShow, std::function<void()> onRemoveRequested)
            : pedal(pedalToShow)
        {
            for (auto* param : pedal.getParameters())
            {
                auto* knob = knobs.add(new KnobComponent(*param));
                knobArea.addAndMakeVisible(knob);
            }
            knobViewport.setViewedComponent(&knobArea, false);
            knobViewport.setScrollBarsShown(true, false);
            addAndMakeVisible(knobViewport);

            bool engaged = ! pedal.bypassed.load();
            footswitch.setClickingTogglesState(true);
            footswitch.setToggleState(engaged, juce::dontSendNotification);
            footswitch.setButtonText(engaged ? "ON" : "OFF");
            footswitch.setColour(juce::TextButton::buttonOnColourId, ModernColours::accent);
            footswitch.setColour(juce::TextButton::buttonColourId, ModernColours::danger);
            footswitch.onClick = [this]
            {
                bool nowEngaged = footswitch.getToggleState();
                pedal.bypassed.store(! nowEngaged);
                footswitch.setButtonText(nowEngaged ? "ON" : "OFF");
            };
            addAndMakeVisible(footswitch);

            if (pedal.supportsImpulseResponseFile())
            {
                loadIRButton.setButtonText("Load IR...");
                loadIRButton.onClick = [this] { openIRChooser(); };
                addAndMakeVisible(loadIRButton);
            }

            removeButton.setButtonText("Remove from Rack");
            removeButton.setColour(juce::TextButton::buttonColourId, ModernColours::danger);
            removeButton.onClick = std::move(onRemoveRequested);
            addAndMakeVisible(removeButton);

            setSize(340, 412);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(12);

            auto footswitchArea = area.removeFromBottom(36);
            footswitch.setBounds(footswitchArea.reduced(70, 0));
            area.removeFromBottom(8);

            auto removeArea = area.removeFromBottom(28);
            removeButton.setBounds(removeArea.reduced(60, 0));
            area.removeFromBottom(8);

            if (pedal.supportsImpulseResponseFile())
            {
                auto irArea = area.removeFromBottom(28);
                loadIRButton.setBounds(irArea.reduced(30, 0));
                area.removeFromBottom(8);
            }

            knobViewport.setBounds(area);
            layoutKnobArea();
        }

    private:
        static constexpr int knobWidth = 62;
        static constexpr int knobHeight = 82;
        static constexpr int knobMargin = 4;

        void layoutKnobArea()
        {
            auto width = juce::jmax(1, knobViewport.getWidth() - knobViewport.getScrollBarThickness());

            constexpr int itemWidth = knobWidth + knobMargin * 2;
            constexpr int itemHeight = knobHeight + knobMargin * 2;
            auto perRow = juce::jmax(1, width / itemWidth);
            auto rows = (knobs.size() + perRow - 1) / juce::jmax(1, perRow);
            auto requiredHeight = rows * itemHeight;

            knobArea.setSize(width, juce::jmax(knobViewport.getHeight(), requiredHeight));

            juce::FlexBox fb;
            fb.flexWrap = juce::FlexBox::Wrap::wrap;
            fb.justifyContent = juce::FlexBox::JustifyContent::center;
            fb.alignContent = juce::FlexBox::AlignContent::flexStart;

            for (auto* knob : knobs)
                fb.items.add(juce::FlexItem(*knob).withWidth((float) knobWidth).withHeight((float) knobHeight).withMargin((float) knobMargin));

            fb.performLayout(knobArea.getLocalBounds());
        }

        void openIRChooser()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Select a cabinet impulse response (.wav)", juce::File(), "*.wav;*.aif;*.aiff");

            auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

            fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
            {
                auto file = chooser.getResult();
                if (file.existsAsFile())
                    pedal.loadImpulseResponseFile(file);
            });
        }

        Pedal& pedal;
        juce::TextButton footswitch, loadIRButton, removeButton;
        std::unique_ptr<juce::FileChooser> fileChooser;

        juce::OwnedArray<KnobComponent> knobs;
        juce::Component knobArea;
        juce::Viewport knobViewport;
    };
}

PedalInspectorWindow::PedalInspectorWindow(Pedal& pedalToShow, std::function<void()> onCloseCallback,
                                            std::function<void()> onRemoveCallback)
    : DocumentWindow(pedalToShow.getName(), ModernColours::background, juce::DocumentWindow::closeButton),
      pedal(pedalToShow), onClose(std::move(onCloseCallback))
{
    setUsingNativeTitleBar(true);
    setContentOwned(new InspectorContent(pedal, std::move(onRemoveCallback)), true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void PedalInspectorWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}
