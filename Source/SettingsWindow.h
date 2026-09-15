#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow(juce::AudioDeviceManager& deviceManagerToUse,
                    std::function<void()> onCloseCallback);

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};
