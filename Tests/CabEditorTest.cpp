// Functional check of the Cabinet's visual editor (CabinetVisualEditor) with
// a real pedal and the real component - no window, no screen: it drives the
// controls and the mouse through JUCE's own API and reads the parameters
// back. (EditorSnapshot is the tool for LOOKING at it; this is for whether
// it WORKS.)
//
//  1. the dropdowns and mix sliders write to the parameters they name;
//  2. a change from outside (a preset load) flows back into the controls;
//  3. the right-cab controls are disabled while Cab R is "Same as Cab", and
//     the mix sliders only apply once a second speaker is chosen;
//  4. dragging a mic marker moves its POSITION horizontally and its
//     DISTANCE vertically, each without disturbing the other, and only the
//     marker that was grabbed moves.

#include "../Source/CabinetPedal.h"
#include "../Source/CabinetVisualEditor.h"
#include "../Source/KnobComponent.h"
#include "../Source/ModernLookAndFeel.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace
{
    PedalParameter* param(CabinetPedal& p, const char* name)
    {
        for (auto* q : p.getParameters())
            if (q->name == name)
                return q;
        return nullptr;
    }

    template <typename T>
    void collect(juce::Component& root, std::vector<T*>& out)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* match = dynamic_cast<T*>(child))
                out.push_back(match);
            collect(*child, out);
        }
    }

    // Lets JUCE's timers run for a while (the editor re-reads its parameters
    // on a 15 Hz timer).
    void runMessageLoopFor(int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
    }

    juce::MouseEvent mouseEventAt(juce::Component& component, juce::Point<float> position)
    {
        auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(),
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                juce::MouseInputSource::defaultTiltY, &component, &component, now, position, now, 1, true);
    }

    void drag(CabinetVisualEditor& editor, juce::Point<float> from, juce::Point<float> to)
    {
        editor.mouseDown(mouseEventAt(editor, from));
        editor.mouseDrag(mouseEventAt(editor, to));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ModernLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    CabinetPedal pedal;
    pedal.prepare(48000.0, 512, 2);
    auto component = pedal.createCustomEditor();
    auto& editor = static_cast<CabinetVisualEditor&>(*component);

    std::vector<juce::ComboBox*> combos;
    std::vector<juce::Slider*> sliders;
    collect(editor, combos);
    collect(editor, sliders);

    std::printf("=== The editor has the controls it should ===\n");
    check(combos.size() == 3 && sliders.size() == 2, "three dropdowns (Cab R, Spk 2, Spk 2 R) and two mix sliders");
    if (combos.size() != 3 || sliders.size() != 2)
        return 1;
    // Creation order in the editor: Cab R, Spk 2, Spk 2 R / Spk Mix, Spk Mix R.
    auto& cabRightBox = *combos[0];
    auto& speaker2Box = *combos[1];
    auto& speaker2RBox = *combos[2];
    auto& mixSlider = *sliders[0];
    auto& mixRSlider = *sliders[1];

    {
        auto handled = pedal.getCustomEditorHandledParameters();
        auto isHandled = [&](const char* name) { return std::find(handled.begin(), handled.end(), param(pedal, name)) != handled.end(); };
        check(isHandled("Cab R") && isHandled("Spk 2") && isHandled("Spk Mix") && isHandled("Spk 2 R") && isHandled("Spk Mix R")
                  && isHandled("Distance A") && isHandled("Distance B"),
              "the parameters the editor controls are handed off, so the Inspector doesn't duplicate them as knobs");
        check(! isHandled("Spk Push") && ! isHandled("Mic Blend"), "Spk Push and Mic Blend stay as knobs (the editor only reads them)");
        check(cabRightBox.getNumItems() == 9 && speaker2Box.getNumItems() == 9, "the dropdowns list 'Same as Cab'/'None' plus all 8 cabs");
    }

    std::printf("\n=== The controls write to the parameters ===\n");
    {
        cabRightBox.setSelectedItemIndex(5, juce::sendNotificationSync);
        check(std::round(param(pedal, "Cab R")->get()) == 5.0f, "choosing 'Fender 2x10' in the Right cab dropdown sets Cab R");
        speaker2Box.setSelectedItemIndex(3, juce::sendNotificationSync);
        check(std::round(param(pedal, "Spk 2")->get()) == 3.0f, "the 2nd speaker dropdown sets Spk 2");
        speaker2RBox.setSelectedItemIndex(2, juce::sendNotificationSync);
        check(std::round(param(pedal, "Spk 2 R")->get()) == 2.0f, "the right 2nd speaker dropdown sets Spk 2 R");
        mixSlider.setValue(35.0, juce::sendNotificationSync);
        mixRSlider.setValue(80.0, juce::sendNotificationSync);
        check(std::round(param(pedal, "Spk Mix")->get()) == 35.0f && std::round(param(pedal, "Spk Mix R")->get()) == 80.0f,
              "the mix sliders set Spk Mix and Spk Mix R");
    }

    std::printf("\n=== Changes from outside flow back in (a preset load) ===\n");
    {
        param(pedal, "Cab R")->set(7.0f);
        param(pedal, "Spk 2")->set(1.0f);
        param(pedal, "Spk Mix")->set(65.0f);
        runMessageLoopFor(400);
        check(cabRightBox.getSelectedItemIndex() == 7, "the Right cab dropdown follows Cab R");
        check(speaker2Box.getSelectedItemIndex() == 1, "the 2nd speaker dropdown follows Spk 2");
        check(std::round(mixSlider.getValue()) == 65.0, "the mix slider follows Spk Mix");
    }

    std::printf("\n=== Controls that don't apply right now are disabled ===\n");
    {
        param(pedal, "Cab R")->set(0.0f);
        param(pedal, "Spk 2")->set(0.0f);
        param(pedal, "Spk 2 R")->set(0.0f);
        runMessageLoopFor(400);
        check(! speaker2RBox.isEnabled() && ! mixRSlider.isEnabled(), "right-side 2nd speaker controls are off while Cab R is 'Same as Cab'");
        check(! mixSlider.isEnabled(), "the mix slider is off while the 2nd speaker is None");

        param(pedal, "Cab R")->set(3.0f);
        param(pedal, "Spk 2")->set(2.0f);
        param(pedal, "Spk 2 R")->set(4.0f);
        runMessageLoopFor(400);
        check(speaker2RBox.isEnabled() && mixRSlider.isEnabled(), "they switch on once Cab R names a cab (and a 2nd speaker is chosen)");
        check(mixSlider.isEnabled(), "and the mix slider switches on once a 2nd speaker is chosen");
    }

    std::printf("\n=== Dragging a mic marker: horizontal = position, vertical = distance ===\n");
    {
        param(pedal, "Cab R")->set(0.0f);
        param(pedal, "Position A")->set(30.0f);
        param(pedal, "Distance A")->set(50.0f);
        param(pedal, "Position B")->set(70.0f);
        param(pedal, "Distance B")->set(50.0f);

        auto startA = editor.getMicMarkerCentre(false);
        auto posA = [&] { return param(pedal, "Position A")->get(); };
        auto distA = [&] { return param(pedal, "Distance A")->get(); };
        auto posB = [&] { return param(pedal, "Position B")->get(); };
        auto distB = [&] { return param(pedal, "Distance B")->get(); };

        // Mic A starts left of the centre; toward the centre is toward the cone's middle.
        drag(editor, startA, { startA.x + 25.0f, startA.y });
        auto towardCentre = posA();
        check(towardCentre > 30.0f && std::abs(distA() - 50.0f) < 1.0f,
              "dragging Mic A sideways toward the centre raises its position and leaves its distance alone");

        param(pedal, "Position A")->set(30.0f);
        param(pedal, "Distance A")->set(50.0f);
        startA = editor.getMicMarkerCentre(false);
        drag(editor, startA, { startA.x - 25.0f, startA.y });
        check(posA() < 30.0f && std::abs(distA() - 50.0f) < 1.0f, "dragging it toward the edge lowers its position, distance unchanged");

        param(pedal, "Position A")->set(30.0f);
        param(pedal, "Distance A")->set(50.0f);
        startA = editor.getMicMarkerCentre(false);
        drag(editor, startA, { startA.x, startA.y - 30.0f });
        auto closer = distA();
        check(closer < 50.0f && std::abs(posA() - 30.0f) < 1.0f, "dragging Mic A UP brings it closer (lower Distance) without changing its position");

        param(pedal, "Distance A")->set(50.0f);
        startA = editor.getMicMarkerCentre(false);
        drag(editor, startA, { startA.x, startA.y + 30.0f });
        check(distA() > 50.0f && std::abs(posA() - 30.0f) < 1.0f, "dragging it DOWN moves it further away (higher Distance)");

        // The extremes clamp to the ends of each axis.
        startA = editor.getMicMarkerCentre(false);
        drag(editor, startA, { -500.0f, -500.0f });
        check(distA() < 0.5f && posA() < 0.5f, "dragging far up-left clamps to closest distance and the cone's edge");
        startA = editor.getMicMarkerCentre(false);
        drag(editor, startA, { editor.getWidth() * 0.5f + 0.0f, 5000.0f });
        check(distA() > 99.5f, "dragging far down clamps to the greatest distance");

        // Grabbing one marker moves only that one.
        param(pedal, "Position A")->set(30.0f);
        param(pedal, "Distance A")->set(50.0f);
        param(pedal, "Position B")->set(70.0f);
        param(pedal, "Distance B")->set(50.0f);
        auto startB = editor.getMicMarkerCentre(true);
        drag(editor, startB, { startB.x, startB.y + 25.0f });
        check(distB() > 50.0f && std::abs(distA() - 50.0f) < 0.01f && std::abs(posA() - 30.0f) < 0.01f,
              "dragging Mic B changes Mic B and leaves Mic A exactly where it was");

        // Clicking empty space grabs nothing.
        param(pedal, "Distance A")->set(50.0f);
        param(pedal, "Distance B")->set(50.0f);
        drag(editor, { 5.0f, 5.0f }, { 200.0f, 200.0f });
        check(std::abs(distA() - 50.0f) < 0.01f && std::abs(distB() - 50.0f) < 0.01f, "clicking and dragging on empty space moves neither mic");
    }

    std::printf("\n=== Labeled knobs show their names, not a bare number ===\n");
    {
        // Regression: KnobComponent installed its label formatter but then
        // "refreshed" the text with setValue(), which JUCE skips when the value
        // hasn't changed - so every labeled knob (Polarity, Live/Studio, ...)
        // showed a raw number until touched.
        auto shownTexts = [](PedalParameter& parameter)
        {
            KnobComponent knob(parameter);
            knob.setSize(62, 82);
            std::vector<juce::Label*> labels;
            collect(knob, labels);
            std::vector<juce::String> texts;
            for (auto* label : labels)
                texts.push_back(label->getText());
            return texts;
        };
        auto contains = [](const std::vector<juce::String>& texts, const char* wanted)
        {
            return std::find(texts.begin(), texts.end(), juce::String(wanted)) != texts.end();
        };

        param(pedal, "Polarity A")->set(0.0f);
        param(pedal, "Live/Studio")->set(1.0f);
        check(contains(shownTexts(*param(pedal, "Polarity A")), "Normal"), "Polarity A shows 'Normal' straight away");
        check(contains(shownTexts(*param(pedal, "Live/Studio")), "Studio"), "Live/Studio shows 'Studio' straight away");
        param(pedal, "Polarity A")->set(1.0f);
        check(contains(shownTexts(*param(pedal, "Polarity A")), "Invert"), "and a knob created at a different value shows that value's name");
        param(pedal, "Polarity A")->set(0.0f);
    }

    std::printf("\n%s\n", allPassed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allPassed ? 0 : 1;
}
