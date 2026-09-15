#include "KnobComponent.h"

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
    addAndMakeVisible(slider);

    label.setText(parameter.name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(12.0f));
    addAndMakeVisible(label);
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(14));
    slider.setBounds(area);
}
