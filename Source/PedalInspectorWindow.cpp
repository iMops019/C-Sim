#include "PedalInspectorWindow.h"
#include "KnobComponent.h"
#include "ModernLookAndFeel.h"

#include <algorithm>

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
            customEditor = pedal.createCustomEditor();
            auto handledParams = pedal.getCustomEditorHandledParameters();

            if (customEditor != nullptr)
                addAndMakeVisible(*customEditor);

            for (auto* param : pedal.getParameters())
            {
                if (std::find(handledParams.begin(), handledParams.end(), param) != handledParams.end())
                    continue; // already shown by the custom editor above - don't duplicate it as a knob

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

            // One Load/Clear row per IR slot the pedal exposes (Cabinet has
            // one per mic on each cab).
            auto slotNames = pedal.getImpulseResponseSlotNames();
            for (int slot = 0; slot < (int) slotNames.size(); ++slot)
            {
                auto* row = irRows.add(new IRRow());
                row->label.setText(slotNames[(size_t) slot], juce::dontSendNotification);
                row->label.setFont(juce::Font(juce::FontOptions(13.0f)));
                row->loadButton.onClick = [this, slot] { openIRChooser(slot); };
                row->clearButton.setButtonText("Clear");
                row->clearButton.onClick = [this, slot] { pedal.clearImpulseResponse(slot); refreshIRRows(); };
                addAndMakeVisible(row->label);
                addAndMakeVisible(row->loadButton);
                addAndMakeVisible(row->clearButton);
            }
            refreshIRRows();

            removeButton.setButtonText("Remove from Rack");
            removeButton.setColour(juce::TextButton::buttonColourId, ModernColours::danger);
            removeButton.onClick = std::move(onRemoveRequested);
            addAndMakeVisible(removeButton);

            // A custom editor (e.g. CabinetVisualEditor) needs its own
            // fixed-height area above the knob grid, and typically wants
            // a wider window than the plain knob grid does on its own.
            auto width = 340;
            auto height = 412 + irRows.size() * irRowHeight;
            if (customEditor != nullptr)
            {
                width = juce::jmax(width, customEditor->getWidth() + 24);
                height += customEditor->getHeight() + 8;
            }
            setSize(width, height);
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

            if (irRows.size() > 0)
            {
                for (int i = irRows.size() - 1; i >= 0; --i)
                {
                    auto rowArea = area.removeFromBottom(irRowHeight).reduced(0, 2);
                    auto* row = irRows[i];
                    row->label.setBounds(rowArea.removeFromLeft(88));
                    row->clearButton.setBounds(rowArea.removeFromRight(52));
                    rowArea.removeFromRight(4);
                    row->loadButton.setBounds(rowArea);
                }
                area.removeFromBottom(8);
            }

            if (customEditor != nullptr)
            {
                auto customArea = area.removeFromTop(customEditor->getHeight());
                customEditor->setBounds(customArea.withSizeKeepingCentre(
                    juce::jmin(customArea.getWidth(), customEditor->getWidth()), customArea.getHeight()));
                area.removeFromTop(8);
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

        void openIRChooser(int slot)
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Select an impulse response (.wav) for " + pedal.getImpulseResponseSlotNames()[(size_t) slot],
                juce::File(), "*.wav;*.aif;*.aiff");

            auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

            fileChooser->launchAsync(chooserFlags, [this, slot](const juce::FileChooser& chooser)
            {
                auto file = chooser.getResult();
                if (file.existsAsFile())
                    pedal.loadImpulseResponseFile(slot, file);
                refreshIRRows();
            });
        }

        // Shows each slot's loaded file name (or "Load..." if it's using
        // the built-in IR), and only lets you Clear a slot that has one.
        void refreshIRRows()
        {
            for (int slot = 0; slot < irRows.size(); ++slot)
            {
                auto name = pedal.getImpulseResponseFileName(slot);
                irRows[slot]->loadButton.setButtonText(name.isEmpty() ? juce::String("Load...") : name);
                irRows[slot]->clearButton.setEnabled(name.isNotEmpty());
            }
        }

        struct IRRow
        {
            juce::Label label;
            juce::TextButton loadButton, clearButton;
        };
        static constexpr int irRowHeight = 30;

        Pedal& pedal;
        juce::OwnedArray<IRRow> irRows;
        juce::TextButton footswitch, removeButton;
        std::unique_ptr<juce::FileChooser> fileChooser;
        std::unique_ptr<juce::Component> customEditor;

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
