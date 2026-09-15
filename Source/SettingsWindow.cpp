#include "SettingsWindow.h"

SettingsWindow::SettingsWindow(juce::AudioDeviceManager& deviceManagerToUse,
                                std::function<void()> onCloseCallback)
    : juce::DocumentWindow("Audio Settings",
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour(juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::closeButton),
      onClose(std::move(onCloseCallback))
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManagerToUse,
        0, 2,   // min/max input channels
        0, 2,   // min/max output channels
        false,  // show MIDI input options
        false,  // show MIDI output selector
        true,   // show channels as stereo pairs
        false); // hide advanced options by default

    selector->setSize(500, 450);

    setUsingNativeTitleBar(true);
    setContentOwned(selector, true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void SettingsWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}
