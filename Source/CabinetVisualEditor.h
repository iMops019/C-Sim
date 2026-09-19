#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "CabinetPedal.h"
#include "PedalParameter.h"

// The Cabinet's visual editor: a drawn cab (or two) with draggable mic
// markers, plus real controls for the discrete choices that used to be
// awkward knobs. It is bound directly to the same PedalParameters those
// knobs control - same two-way value flow as KnobComponent (a control writes
// immediately, a low-rate timer reads back so a preset load or any other
// outside change doesn't leave this editor showing a stale picture).
//
// What it shows and controls:
//  - Cab: a row of labeled buttons (one per CabImpulseResponse::CabType,
//    from PedalParameter::valueLabels so it never drifts out of sync with
//    the enum) - the user's own choice over click-to-cycle.
//  - Right cab / 2nd speaker / 2nd speaker (right): dropdowns, each with a
//    mix slider where one applies. Nine-way selectors are miserable as
//    knobs (they showed a bare number). The right-cab rows grey out while
//    Cab R is "Same as Cab", because then the right cab simply mirrors the
//    left one.
//  - The cab itself: one cab, or TWO side by side (left / right) while dual-
//    cab mode is on (Cab R names a cab, or a Right IR slot holds a real
//    file). Speakers are tinted per speaker type, so a mixed cab (Spk 2 +
//    Spk Mix) reads as two colors in proportion to the mix - a picture of
//    the mix, not a spatial model: the DSP doesn't place individual
//    speakers (see CabImpulseResponse::speakerCount). Spk Push shows as a
//    heat glow around the speakers.
//  - Two mic markers, dragged in TWO dimensions: horizontally is the
//    position on the cone (edge <-> centre - the one abstract axis
//    CabImpulseResponse::renderMicChannel has), vertically is the mic's
//    DISTANCE from the cab (top = close, bottom = far - see
//    dsp/MicPlacement.h). The marker also shrinks with distance, as
//    something further away would look. While two cabs are shown the mics
//    are dragged on the left one and ghosted on the right (they are shared).
//
// Both mics drag across the SAME reference speaker's width (the DSP has no
// concept of "which speaker" - only how close to that cone's edge or
// centre the capsule is aimed), which is why a first version confined to a
// single speaker's own small circle looked broken.
//
// Feedback it gives that it once did not ("I move [the mics] around and I
// don't hear anything"): the first real IR loaded into a cab resets Mic
// Blend to 0 by design (so a real capture isn't heard comb-filtered against
// an invented synthetic Mic B), so Mic B can be dragged with no audible
// effect until Blend is raised; and a mic whose slot holds a real IR ignores
// its POSITION entirely (that is baked into the capture) - though its
// distance still delays it. A status line explains both and the affected
// marker is dimmed.
class CabinetVisualEditor : public juce::Component,
                             private juce::Timer
{
public:
    // Every parameter this editor manages - CabinetPedal lists the same set
    // in getCustomEditorHandledParameters() so the Inspector doesn't also
    // show them as knobs.
    struct Bindings
    {
        PedalParameter& cab;
        PedalParameter& micTypeA;
        PedalParameter& micPositionA;
        PedalParameter& micTypeB;
        PedalParameter& micPositionB;
        PedalParameter& micBlend;
        PedalParameter& distanceA;
        PedalParameter& distanceB;
        PedalParameter& cabRight;
        PedalParameter& speaker2;
        PedalParameter& speakerMix;
        PedalParameter& speaker2R;
        PedalParameter& speakerMixR;
        PedalParameter& speakerPush;
    };

    CabinetVisualEditor(CabinetPedal& ownerPedal, const Bindings& bindings);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    // Where a mic's marker is currently drawn, in this component's own
    // coordinates - what a mouse-down has to hit to grab it (used by the
    // editor's test to drag a marker without hard-coding pixel positions).
    juce::Point<float> getMicMarkerCentre(bool micB) const;

private:
    void timerCallback() override;
    void rebuildCabButtons();
    void updateCabButtonHighlights();
    void syncSelectorControls();

    bool dualActive() const;
    int rightCabType() const;                       // the cab type drawn on the right (Cab R, or the left cab if "Same")
    int secondSpeakerType(bool rightSide) const;    // -1 = none
    float speakerMixFraction(bool rightSide) const; // 0..1

    // The rectangle a cab is drawn in: the whole draw area for one cab; the
    // left / right half while two are shown.
    juce::Rectangle<float> cabRect(bool rightSide) const;
    std::vector<juce::Rectangle<float>> layoutSpeakers(juce::Rectangle<float> cabArea, int speakerCount) const;
    void drawCab(juce::Graphics& g, juce::Rectangle<float> area, int cabType, int secondType, float mix, const juce::String& caption) const;

    // The area mic markers are dragged in: the left cab (or the only cab),
    // inset so a marker at the extreme still sits inside the drawing.
    juce::Rectangle<float> dragTrackBounds() const;

    // side: -1 = rendered/dragged left of the track's centre, +1 = right of
    // it. Purely a rendering convenience - the DSP parameter only stores the
    // 0..1 edge<->centre fraction, not which side.
    juce::Point<float> markerPositionFor(float positionFraction, float distanceKnob, int side, float yOffset) const;
    float xToPositionFraction(float x) const;
    float yToDistanceKnob(float y, float yOffset) const;

    CabinetPedal& pedal;
    Bindings p;

    juce::OwnedArray<juce::TextButton> cabButtons;
    juce::TextButton micTypeAButton, micTypeBButton;

    juce::Label cabRightCaption, speaker2Caption, speaker2RCaption;
    juce::ComboBox cabRightBox, speaker2Box, speaker2RBox;
    juce::Slider speakerMixSlider, speakerMixRSlider;

    juce::Rectangle<int> cabDrawArea;
    juce::Rectangle<int> statusArea;
    int micASide = -1;
    int micBSide = 1;

    enum class DragTarget { none, micA, micB };
    DragTarget activeDrag = DragTarget::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabinetVisualEditor)
};
