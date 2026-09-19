#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "CabinetPedal.h"
#include "PedalParameter.h"

// A drawn cabinet with draggable mic markers, replacing CabinetPedal's
// old Cab/Mic Type/Position dropdown-style knobs with something that
// actually looks like a cab and lets you place mics on it, the way most
// commercial cab-sim plugins do. Bound directly to the same
// PedalParameters those knobs used to control - same two-way value flow
// convention as KnobComponent (drag writes immediately, a low-rate timer
// reads back so an external reset, e.g. loading a real IR, doesn't leave
// this editor showing a stale picture).
//
// Cab type is picked via a row of small labeled buttons (one per
// CabImpulseResponse::CabType, using PedalParameter::valueLabels so this
// never goes out of sync with the enum) rather than clicking the cab
// image itself - keeps every option visible at a glance.
//
// Mic dragging is deliberately horizontal-only: the DSP model
// (CabImpulseResponse::renderMicChannel) only has one continuous
// position axis per mic (0=cone edge, 1=cone centre - see its own
// comment), so a 2D drag would let you move somewhere that has no
// audible effect. Both mics drag across the SAME reference speaker (the
// first one in the cab's layout) since the model doesn't distinguish
// between physical speakers spatially - only how close to that cone's
// edge or centre the capsule is aimed.
//
// Real bug report this editor used to give zero feedback about ("I move
// [the mics] around and I don't hear anything"): the first real IR loaded
// into a cab resets Mic Blend to 0 by design (so a real capture isn't
// heard comb-filtered against an invented synthetic Mic B), which means
// Mic B's marker can be dragged anywhere with zero audible effect until
// Mic Blend is raised again - and a mic whose slot holds a real IR ignores
// its Position entirely (that position is physically baked into the
// capture). Both are real, by-design behavior, not bugs - so this editor
// shows a status line for them and dims the affected marker.
class CabinetVisualEditor : public juce::Component,
                             private juce::Timer
{
public:
    CabinetVisualEditor(CabinetPedal& ownerPedal, PedalParameter& cabParam,
                         PedalParameter& micTypeAParam, PedalParameter& micPositionAParam,
                         PedalParameter& micTypeBParam, PedalParameter& micPositionBParam,
                         PedalParameter& micBlendParam);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void rebuildCabButtons();
    void updateCabButtonHighlights();

    std::vector<juce::Rectangle<float>> layoutSpeakers() const;

    // The full horizontal range a mic marker can be dragged across - the
    // whole cab face, not just one speaker's own small circle. A first
    // version confined dragging to a single speaker's radius (~40px),
    // which looked and felt broken: both mics ended up stuck clustered
    // in one small corner of the cab with most of the drawn cabinet
    // completely unreachable. The DSP model itself only has one
    // abstract 0..1 edge<->centre axis per mic (see
    // CabImpulseResponse::renderMicChannel) with no real spatial
    // "which speaker" concept, so mapping the FULL cab width to that
    // axis is just as valid a choice as one speaker's width, and is the
    // one that actually looks and feels like placing a mic on a cab.
    juce::Rectangle<float> dragTrackBounds() const;

    // side: -1 = rendered/dragged left of the track's centre, +1 = right
    // of it. Purely a rendering convenience (see .cpp) - the DSP
    // parameter itself only stores the 0..1 edge<->centre fraction, not
    // which side.
    juce::Point<float> markerPositionFor(float positionFraction, int side) const;
    float xToPositionFraction(float x) const;

    CabinetPedal& pedal;
    PedalParameter& cab;
    PedalParameter& micTypeA;
    PedalParameter& micPositionA;
    PedalParameter& micTypeB;
    PedalParameter& micPositionB;
    PedalParameter& micBlend;

    juce::OwnedArray<juce::TextButton> cabButtons;
    juce::TextButton micTypeAButton, micTypeBButton;

    juce::Rectangle<int> cabDrawArea;
    juce::Rectangle<int> statusArea;
    int micASide = -1;
    int micBSide = 1;

    enum class DragTarget { none, micA, micB };
    DragTarget activeDrag = DragTarget::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabinetVisualEditor)
};
