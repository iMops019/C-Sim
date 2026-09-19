#include "KnobComponent.h"

#include <cmath>

KnobComponent::KnobComponent(PedalParameter& parameterToControl)
    : parameter(parameterToControl)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 16);
    slider.setNumDecimalPlacesToDisplay(0);
    slider.setRange(parameter.range.getStart(), parameter.range.getEnd(), 1.0);
    slider.setValue(parameter.get(), juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, parameter.defaultValue);
    slider.onValueChange = [this] { parameter.set((float) slider.getValue()); };

    if (! parameter.valueLabels.empty())
    {
        slider.textFromValueFunction = [this](double v)
        {
            auto index = juce::jlimit(0, (int) parameter.valueLabels.size() - 1, (int) std::round(v));
            return parameter.valueLabels[(size_t) index];
        };
        // Refresh the text box with the new formatter. NOT setValue() with the
        // value it already has: JUCE skips the update when the value is
        // unchanged, which left every labeled knob (Live/Studio, Polarity,
        // ...) showing a raw number until the user touched it.
        slider.updateText();
    }

    addAndMakeVisible(slider);

    label.setText(parameter.name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(12.0f));
    addAndMakeVisible(label);

    startTimerHz(15); // cheap enough to not matter, fast enough to feel instant
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(14));
    slider.setBounds(area);
}

void KnobComponent::timerCallback()
{
    if (slider.isMouseButtonDown())
        return; // don't fight the user's own drag

    auto current = parameter.get();
    if (std::abs(current - (float) slider.getValue()) > 0.0001f)
        slider.setValue(current, juce::dontSendNotification);
}
