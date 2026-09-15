#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PedalParameter.h"

// A single rotary knob bound to one PedalParameter.
class KnobComponent : public juce::Component
{
public:
    explicit KnobComponent(PedalParameter& parameterToControl);

    void resized() override;

private:
    PedalParameter& parameter;
    juce::Slider slider;
    juce::Label label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobComponent)
};
