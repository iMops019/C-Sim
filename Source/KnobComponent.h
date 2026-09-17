#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PedalParameter.h"

// A single rotary knob bound to one PedalParameter. Value flow is
// two-way: turning the knob writes to the parameter immediately, and a
// low-rate timer reads the parameter back into the knob so a value
// changed from elsewhere (e.g. loading a cab IR resets Mic Blend/Room -
// see CabinetPedal::loadImpulseResponseFile) doesn't leave this knob
// showing a stale position while its Inspector window happens to be open.
class KnobComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit KnobComponent(PedalParameter& parameterToControl);

    void resized() override;

private:
    void timerCallback() override;

    PedalParameter& parameter;
    juce::Slider slider;
    juce::Label label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobComponent)
};
