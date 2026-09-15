#include "TunerWindow.h"
#include "TunerComponent.h"

TunerWindow::TunerWindow(TunerEngine& engineToUse, std::function<void()> onCloseCallback)
    : juce::DocumentWindow("Tuner",
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour(juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::closeButton),
      onClose(std::move(onCloseCallback))
{
    setUsingNativeTitleBar(true);
    setContentOwned(new TunerComponent(engineToUse), true);
    setResizable(false, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void TunerWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}
