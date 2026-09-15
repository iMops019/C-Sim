#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

// A single knob-controllable value on a pedal. The audio thread reads
// get() every block; the GUI thread writes via set() from a knob - no
// locking needed since it's a plain atomic float.
class PedalParameter
{
public:
    PedalParameter(juce::String parameterName, float minValue, float maxValue, float defaultVal)
        : name(std::move(parameterName)), range(minValue, maxValue), defaultValue(defaultVal), value(defaultVal)
    {
    }

    float get() const noexcept { return value.load(std::memory_order_relaxed); }
    void set(float newValue) noexcept { value.store(range.clipValue(newValue), std::memory_order_relaxed); }

    const juce::String name;
    const juce::Range<float> range;
    const float defaultValue;

private:
    std::atomic<float> value;
};
