#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

#include "Pedal.h"

// A floating window showing one pedal's full controls - bypass, IR
// loading, and every knob. Opened by clicking that pedal's tile on the
// Signal Chain board, so the board itself stays a compact, glanceable
// list of what's in the chain and in what order, instead of every knob
// for every pedal competing for space on one screen at once.
class PedalInspectorWindow : public juce::DocumentWindow
{
public:
    // onRemoveCallback is expected to remove the pedal from the Signal
    // Chain, which in turn closes this window (see
    // SignalChainComponent::removePedal/closeInspector) - this window
    // doesn't need to separately close itself in response.
    PedalInspectorWindow(Pedal& pedalToShow, std::function<void()> onCloseCallback,
                          std::function<void()> onRemoveCallback);

    void closeButtonPressed() override;

    Pedal& getPedal() const noexcept { return pedal; }

private:
    Pedal& pedal;
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalInspectorWindow)
};
