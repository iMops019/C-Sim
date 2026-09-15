#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

#include "TunerEngine.h"

class TunerWindow : public juce::DocumentWindow
{
public:
    TunerWindow(TunerEngine& engineToUse, std::function<void()> onCloseCallback);

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerWindow)
};
