#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "TunerEngine.h"

class TunerComponent : public juce::Component,
                        private juce::Timer
{
public:
    explicit TunerComponent(TunerEngine& engineToUse);
    ~TunerComponent() override;

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    TunerEngine& engine;
    std::vector<float> analysisBuffer;

    bool hasPitch = false;
    juce::String currentNoteName = "--";
    double currentFrequency = 0.0;
    float currentCents = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerComponent)
};
