#include "CabinetVisualEditor.h"
#include "CabImpulseResponse.h"
#include "ModernLookAndFeel.h"

#include <cmath>

namespace
{
    constexpr float baseMarkerRadius = 11.0f;
    constexpr float micAYOffset = -7.0f; // keeps A and B from sitting exactly on top of each other at equal distances
    constexpr float micBYOffset = 7.0f;
    const juce::Colour micAColour = ModernColours::accent;
    const juce::Colour micBColour { 0xfff5a623 }; // warm amber - distinct from A's teal and from the danger red used elsewhere
    const juce::Colour heatColour { 0xffff6a3d };

    // One muted tint per CabImpulseResponse::CabType, so a mixed cab reads as
    // two colors. Kept low-saturation: the speakers are secondary to the
    // teal / amber mic markers drawn on top of them.
    juce::Colour tintForCab(int cabType)
    {
        static const juce::Colour tints[] = {
            juce::Colour(0xff6f86b8), // 4x12 V30            slate blue
            juce::Colour(0xffb08a5a), // 4x12 Greenback      warm brown
            juce::Colour(0xff8aa070), // 2x12 open back      olive
            juce::Colour(0xff9a86b0), // 1x12 combo          dusty violet
            juce::Colour(0xff7fb0a8), // Fender 1x10         sea green
            juce::Colour(0xff7aa8c8), // Fender 2x10         sky
            juce::Colour(0xffc08a9a), // Fender 1x12         rose
            juce::Colour(0xffb0a070), // Fender 4x12         sand
        };
        return tints[juce::jlimit(0, 7, cabType)];
    }

    juce::Font smallFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

CabinetVisualEditor::CabinetVisualEditor(CabinetPedal& ownerPedal, const Bindings& bindings)
    : pedal(ownerPedal), p(bindings)
{
    rebuildCabButtons();

    auto setUpMicTypeButton = [this](juce::TextButton& button, PedalParameter& typeParam)
    {
        auto index = juce::jlimit(0, (int) typeParam.valueLabels.size() - 1, (int) std::round(typeParam.get()));
        button.setButtonText(typeParam.valueLabels.empty() ? juce::String() : typeParam.valueLabels[(size_t) index]);
        button.setColour(juce::TextButton::buttonColourId, ModernColours::surfaceLight);
        button.onClick = [this, &button, &typeParam]
        {
            auto current = juce::jlimit(0, (int) typeParam.valueLabels.size() - 1, (int) std::round(typeParam.get()));
            auto next = (current + 1) % juce::jmax(1, (int) typeParam.valueLabels.size());
            typeParam.set((float) next);
            button.setButtonText(typeParam.valueLabels.empty() ? juce::String() : typeParam.valueLabels[(size_t) next]);
        };
        addAndMakeVisible(button);
    };
    setUpMicTypeButton(micTypeAButton, p.micTypeA);
    setUpMicTypeButton(micTypeBButton, p.micTypeB);
    micTypeAButton.setColour(juce::TextButton::textColourOffId, micAColour);
    micTypeBButton.setColour(juce::TextButton::textColourOffId, micBColour);

    // Discrete selectors: a dropdown per choice (items are the parameter's
    // own valueLabels, id = index + 1), and a linear slider for each mix.
    auto setUpCombo = [this](juce::ComboBox& box, PedalParameter& param)
    {
        for (int i = 0; i < (int) param.valueLabels.size(); ++i)
            box.addItem(param.valueLabels[(size_t) i], i + 1);
        box.setSelectedItemIndex(juce::jlimit(0, (int) param.valueLabels.size() - 1, (int) std::round(param.get())),
                                 juce::dontSendNotification);
        box.onChange = [this, &box, &param]
        {
            param.set((float) box.getSelectedItemIndex());
            syncSelectorControls();
            repaint();
        };
        addAndMakeVisible(box);
    };
    setUpCombo(cabRightBox, p.cabRight);
    setUpCombo(speaker2Box, p.speaker2);
    setUpCombo(speaker2RBox, p.speaker2R);

    auto setUpMixSlider = [this](juce::Slider& slider, PedalParameter& param)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 16);
        slider.setRange(0.0, 100.0, 1.0);
        slider.setNumDecimalPlacesToDisplay(0);
        slider.setValue(param.get(), juce::dontSendNotification);
        slider.setDoubleClickReturnValue(true, param.defaultValue);
        slider.onValueChange = [this, &slider, &param]
        {
            param.set((float) slider.getValue());
            repaint();
        };
        addAndMakeVisible(slider);
    };
    setUpMixSlider(speakerMixSlider, p.speakerMix);
    setUpMixSlider(speakerMixRSlider, p.speakerMixR);

    auto setUpCaption = [this](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(smallFont(12.0f));
        label.setColour(juce::Label::textColourId, ModernColours::textSecondary);
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    };
    setUpCaption(cabRightCaption, "Right cab");
    setUpCaption(speaker2Caption, "2nd speaker");
    setUpCaption(speaker2RCaption, "2nd spk (R)");

    syncSelectorControls();

    setSize(340, 378);
    startTimerHz(15);
}

void CabinetVisualEditor::rebuildCabButtons()
{
    cabButtons.clear();
    for (auto& labelText : p.cab.valueLabels)
    {
        auto* button = cabButtons.add(new juce::TextButton(labelText));
        auto index = cabButtons.size() - 1;
        button->onClick = [this, index]
        {
            p.cab.set((float) index);
            updateCabButtonHighlights();
        };
        addAndMakeVisible(button);
    }
    updateCabButtonHighlights();
}

void CabinetVisualEditor::updateCabButtonHighlights()
{
    auto current = (int) std::round(p.cab.get());
    for (int i = 0; i < cabButtons.size(); ++i)
        cabButtons[i]->setColour(juce::TextButton::buttonColourId,
                                  i == current ? ModernColours::accentDim : ModernColours::surfaceLight);
    repaint();
}

// Bring the dropdowns/sliders in line with the parameters (a preset load or
// any other outside change), and grey out what doesn't apply right now.
void CabinetVisualEditor::syncSelectorControls()
{
    auto syncCombo = [](juce::ComboBox& box, PedalParameter& param)
    {
        auto index = juce::jlimit(0, (int) param.valueLabels.size() - 1, (int) std::round(param.get()));
        if (box.getSelectedItemIndex() != index)
            box.setSelectedItemIndex(index, juce::dontSendNotification);
    };
    auto syncSlider = [](juce::Slider& slider, PedalParameter& param)
    {
        if (! slider.isMouseButtonDown() && std::abs(slider.getValue() - (double) param.get()) > 0.001)
            slider.setValue(param.get(), juce::dontSendNotification);
    };

    syncCombo(cabRightBox, p.cabRight);
    syncCombo(speaker2Box, p.speaker2);
    syncCombo(speaker2RBox, p.speaker2R);
    syncSlider(speakerMixSlider, p.speakerMix);
    syncSlider(speakerMixRSlider, p.speakerMixR);

    auto rightHasOwnCab = (int) std::round(p.cabRight.get()) > 0;
    auto setApplies = [](juce::Component& c, bool applies)
    {
        c.setEnabled(applies);
        c.setAlpha(applies ? 1.0f : 0.4f);
    };
    setApplies(speakerMixSlider, (int) std::round(p.speaker2.get()) > 0);
    setApplies(speaker2RBox, rightHasOwnCab);
    setApplies(speaker2RCaption, rightHasOwnCab);
    setApplies(speakerMixRSlider, rightHasOwnCab && (int) std::round(p.speaker2R.get()) > 0);
}

bool CabinetVisualEditor::dualActive() const
{
    return (int) std::round(p.cabRight.get()) > 0 || pedal.isSlotLoaded(2) || pedal.isSlotLoaded(3);
}

int CabinetVisualEditor::rightCabType() const
{
    auto index = (int) std::round(p.cabRight.get());
    return index > 0 ? index - 1 : (int) std::round(p.cab.get());
}

// "Same as Cab" mirrors the WHOLE left cab, second speaker and mix included
// (the same rule the DSP uses), so the right side reads the left's values.
int CabinetVisualEditor::secondSpeakerType(bool rightSide) const
{
    auto ownRight = rightSide && (int) std::round(p.cabRight.get()) > 0;
    return (int) std::round((ownRight ? p.speaker2R : p.speaker2).get()) - 1;
}

float CabinetVisualEditor::speakerMixFraction(bool rightSide) const
{
    auto ownRight = rightSide && (int) std::round(p.cabRight.get()) > 0;
    return juce::jlimit(0.0f, 1.0f, (ownRight ? p.speakerMixR : p.speakerMix).get() / 100.0f);
}

void CabinetVisualEditor::resized()
{
    auto area = getLocalBounds();

    auto buttonRows = area.removeFromTop(58);
    juce::FlexBox fb;
    fb.flexWrap = juce::FlexBox::Wrap::wrap;
    fb.justifyContent = juce::FlexBox::JustifyContent::center;
    fb.alignContent = juce::FlexBox::AlignContent::flexStart;
    for (auto* button : cabButtons)
        fb.items.add(juce::FlexItem(*button).withWidth(74.0f).withHeight(26.0f).withMargin(2.0f));
    fb.performLayout(buttonRows);

    area.removeFromTop(4);

    // Three selector rows: caption | dropdown | (mix slider).
    auto layoutRow = [&area](juce::Label& caption, juce::ComboBox& box, juce::Slider* mix)
    {
        auto row = area.removeFromTop(26);
        area.removeFromTop(2);
        caption.setBounds(row.removeFromLeft(78).reduced(4, 0));
        if (mix != nullptr)
        {
            box.setBounds(row.removeFromLeft(128).reduced(2, 1));
            mix->setBounds(row.reduced(2, 0));
        }
        else
        {
            box.setBounds(row.reduced(2, 1).withWidth(128));
        }
    };
    layoutRow(cabRightCaption, cabRightBox, nullptr);
    layoutRow(speaker2Caption, speaker2Box, &speakerMixSlider);
    layoutRow(speaker2RCaption, speaker2RBox, &speakerMixRSlider);

    area.removeFromTop(2);

    // Reserved for the status line - see paint(). Always reserved (even when
    // not currently shown) so the layout doesn't jump around as state changes.
    statusArea = area.removeFromTop(16);
    area.removeFromTop(2);

    auto micTypeRow = area.removeFromBottom(26);
    micTypeAButton.setBounds(micTypeRow.removeFromLeft(micTypeRow.getWidth() / 2).reduced(4, 0));
    micTypeBButton.setBounds(micTypeRow.reduced(4, 0));

    area.removeFromBottom(6);
    cabDrawArea = area;
}

juce::Rectangle<float> CabinetVisualEditor::cabRect(bool rightSide) const
{
    auto area = cabDrawArea.toFloat();
    if (! dualActive())
        return area;

    constexpr float gap = 6.0f;
    auto half = (area.getWidth() - gap) * 0.5f;
    return rightSide ? juce::Rectangle<float>(area.getX() + half + gap, area.getY(), half, area.getHeight())
                     : juce::Rectangle<float>(area.getX(), area.getY(), half, area.getHeight());
}

std::vector<juce::Rectangle<float>> CabinetVisualEditor::layoutSpeakers(juce::Rectangle<float> cabArea, int count) const
{
    auto area = cabArea.reduced(12.0f, 16.0f);

    std::vector<juce::Rectangle<float>> result;

    if (count <= 1)
    {
        auto d = juce::jmin(area.getWidth() * 0.55f, area.getHeight() * 0.85f);
        result.push_back(juce::Rectangle<float>(d, d).withCentre(area.getCentre()));
    }
    else if (count == 2)
    {
        auto d = juce::jmin(area.getWidth() * 0.42f, area.getHeight() * 0.85f);
        auto gap = area.getWidth() * 0.08f;
        auto totalW = d * 2.0f + gap;
        auto startCx = area.getCentreX() - totalW * 0.5f + d * 0.5f;
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx, area.getCentreY() }));
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx + d + gap, area.getCentreY() }));
    }
    else // 4
    {
        auto d = juce::jmin(area.getWidth() * 0.44f, area.getHeight() * 0.44f);
        auto gapX = area.getWidth() * 0.06f;
        auto gapY = area.getHeight() * 0.06f;
        auto totalW = d * 2.0f + gapX;
        auto totalH = d * 2.0f + gapY;
        auto startCx = area.getCentreX() - totalW * 0.5f + d * 0.5f;
        auto startCy = area.getCentreY() - totalH * 0.5f + d * 0.5f;
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx, startCy }));
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx + d + gapX, startCy }));
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx, startCy + d + gapY }));
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx + d + gapX, startCy + d + gapY }));
    }

    return result;
}

juce::Rectangle<float> CabinetVisualEditor::dragTrackBounds() const
{
    return cabRect(false).reduced(14.0f, 12.0f);
}

// x: position on the cone (0 = edge, 1 = centre), mirrored about the track's
// centre by `side`. y: the mic's distance, top (close) to bottom (far).
juce::Point<float> CabinetVisualEditor::markerPositionFor(float positionFraction, float distanceKnob, int side, float yOffset) const
{
    auto track = dragTrackBounds();
    auto halfWidth = track.getWidth() * 0.5f;
    auto x = track.getCentreX() + static_cast<float>(side) * (1.0f - positionFraction) * halfWidth;
    auto y = track.getY() + juce::jlimit(0.0f, 100.0f, distanceKnob) / 100.0f * track.getHeight() + yOffset;
    return { x, y };
}

float CabinetVisualEditor::xToPositionFraction(float x) const
{
    auto track = dragTrackBounds();
    auto halfWidth = juce::jmax(1.0f, track.getWidth() * 0.5f);
    auto dist = std::abs(x - track.getCentreX());
    return juce::jlimit(0.0f, 1.0f, 1.0f - dist / halfWidth);
}

float CabinetVisualEditor::yToDistanceKnob(float y, float yOffset) const
{
    auto track = dragTrackBounds();
    return juce::jlimit(0.0f, 100.0f, (y - yOffset - track.getY()) / juce::jmax(1.0f, track.getHeight()) * 100.0f);
}

void CabinetVisualEditor::drawCab(juce::Graphics& g, juce::Rectangle<float> area, int cabType, int secondType, float mix,
                                  const juce::String& caption) const
{
    // Cab body - a simple dark box, distinct from the window's own
    // background so it reads as "a physical object".
    g.setColour(ModernColours::surface);
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(ModernColours::border);
    g.drawRoundedRectangle(area, 8.0f, 1.5f);

    auto count = CabImpulseResponse::speakerCount(cabType);
    auto speakers = layoutSpeakers(area, count);
    auto push = juce::jlimit(0.0f, 1.0f, p.speakerPush.get() / 100.0f);

    // Which speakers are the second type. This is a picture of the MIX, not
    // a spatial model (the DSP doesn't place individual speakers): with
    // several speakers, that many of them (rounded, at least one while the
    // mix is above 0) take the second tint; a single speaker blends the two.
    auto mixed = secondType >= 0 && secondType != cabType && mix > 0.0f;
    auto secondCount = 0;
    if (mixed && count > 1)
    {
        secondCount = juce::jlimit(0, count, juce::roundToInt(mix * (float) count));
        if (secondCount == 0) secondCount = 1;
        if (secondCount == count && mix < 1.0f) secondCount = count - 1;
    }

    for (size_t i = 0; i < speakers.size(); ++i)
    {
        auto& speaker = speakers[i];

        juce::Colour tint = tintForCab(cabType);
        if (mixed)
        {
            if (count > 1)
                tint = ((int) i >= count - secondCount) ? tintForCab(secondType) : tintForCab(cabType);
            else
                tint = tintForCab(cabType).interpolatedWith(tintForCab(secondType), mix);
        }

        // Speaker push: a heat glow around the cone, growing with Push.
        if (push > 0.02f)
        {
            g.setColour(heatColour.withAlpha(0.08f + 0.22f * push));
            g.fillEllipse(speaker.expanded(1.5f + 3.5f * push));
            g.setColour(heatColour.withAlpha(0.35f + 0.55f * push));
            g.drawEllipse(speaker.expanded(0.5f), 1.0f + 2.0f * push);
        }

        // Basket, cone, and dust cap as three concentric circles.
        g.setColour(ModernColours::background);
        g.fillEllipse(speaker);
        g.setColour(ModernColours::surfaceLight.interpolatedWith(tint, 0.30f));
        g.fillEllipse(speaker.reduced(speaker.getWidth() * 0.12f));
        g.setColour(tint.darker(0.6f));
        g.fillEllipse(speaker.reduced(speaker.getWidth() * 0.40f));
        g.setColour(ModernColours::border);
        g.drawEllipse(speaker, 1.5f);
    }

    g.setColour(ModernColours::textSecondary);
    g.setFont(smallFont(10.0f));
    g.drawFittedText(caption, area.reduced(6.0f, 3.0f).removeFromBottom(26.0f).toNearestInt(),
                     juce::Justification::bottomLeft, 2, 1.0f);
}

void CabinetVisualEditor::paint(juce::Graphics& g)
{
    // Status line. Slots 0/1 are the left cab's Mic A/B (the ones this
    // editor's markers move). A slot holding a real IR ignores Position -
    // it's baked into the capture - though Distance still delays it - so its
    // marker is dimmed the same way an unheard Mic B is, and the line says so.
    auto micALoaded = pedal.isSlotLoaded(0);
    auto micBLoaded = pedal.isSlotLoaded(1);
    auto micBInactive = p.micBlend.get() < 1.0f; // Mic Blend reads ~0% - moving Mic B currently does nothing audible

    if (micALoaded || micBLoaded)
    {
        g.setColour(ModernColours::accent);
        g.setFont(smallFont(12.0f, true));
        juce::String msg = micALoaded && micBLoaded ? "Real IRs in Mic A and B - position doesn't apply"
                                                     : (micALoaded ? "Real IR in Mic A - its position doesn't apply"
                                                                   : "Real IR in Mic B - its position doesn't apply");
        if (micBInactive && ! micBLoaded)
            msg += "; Mic B inactive (Blend 0%)";
        g.drawText(msg, statusArea.toFloat(), juce::Justification::centred);
    }
    else if (micBInactive)
    {
        g.setColour(ModernColours::textSecondary);
        g.setFont(smallFont(12.0f));
        g.drawText("Mic B inactive (Blend 0%) - raise Mic Blend to hear it",
                   statusArea.toFloat(), juce::Justification::centred);
    }

    // The cab(s).
    auto dual = dualActive();
    auto describe = [this](int cabType, int secondType, float mix)
    {
        juce::String name = p.cab.valueLabels[(size_t) juce::jlimit(0, (int) p.cab.valueLabels.size() - 1, cabType)];
        if (secondType >= 0 && secondType != cabType && mix > 0.0f)
            name += " + " + p.cab.valueLabels[(size_t) juce::jlimit(0, (int) p.cab.valueLabels.size() - 1, secondType)]
                    + " " + juce::String(juce::roundToInt(mix * 100.0f)) + "%";
        return name;
    };

    auto leftType = (int) std::round(p.cab.get());
    drawCab(g, cabRect(false), leftType, secondSpeakerType(false), speakerMixFraction(false),
            (dual ? "L: " : juce::String()) + describe(leftType, secondSpeakerType(false), speakerMixFraction(false)));
    if (dual)
        drawCab(g, cabRect(true), rightCabType(), secondSpeakerType(true), speakerMixFraction(true),
                "R: " + describe(rightCabType(), secondSpeakerType(true), speakerMixFraction(true)));

    // Mic markers, positioned from the live parameter values and drawn last
    // so they sit on top of the cab artwork. Dimmed to a flat outline when it
    // currently has no audible effect (Mic B while Blend reads ~0%, or a
    // mic whose slot holds a real IR); shrinking with distance.
    auto drawMic = [&g](juce::Point<float> pos, float radius, const juce::Colour& colour, const juce::String& label, bool inactive, bool ghost)
    {
        auto bounds = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(pos);

        if (ghost || inactive)
        {
            g.setColour(colour.withAlpha(ghost ? 0.30f : 0.35f));
            g.drawEllipse(bounds, 1.5f);
            g.setColour(colour.withAlpha(ghost ? 0.40f : 0.5f));
            g.setFont(smallFont(juce::jmax(9.0f, radius * 1.15f), true));
            g.drawText(label, bounds, juce::Justification::centred);
            return;
        }

        g.setColour(colour.withAlpha(0.25f));
        g.fillEllipse(bounds.expanded(4.0f));
        g.setColour(colour);
        g.fillEllipse(bounds);
        g.setColour(ModernColours::background);
        g.setFont(smallFont(juce::jmax(9.0f, radius * 1.15f), true));
        g.drawText(label, bounds, juce::Justification::centred);
    };

    auto radiusFor = [](float distanceKnob) { return baseMarkerRadius * (1.15f - 0.5f * juce::jlimit(0.0f, 100.0f, distanceKnob) / 100.0f); };
    auto micAPos = markerPositionFor(p.micPositionA.get() / 100.0f, p.distanceA.get(), micASide, micAYOffset);
    auto micBPos = markerPositionFor(p.micPositionB.get() / 100.0f, p.distanceB.get(), micBSide, micBYOffset);

    if (dual) // the mics are shared: ghost them on the right cab, where they can't be dragged
    {
        auto shift = cabRect(true).getX() - cabRect(false).getX();
        drawMic(micAPos.translated(shift, 0.0f), radiusFor(p.distanceA.get()), micAColour, "A", false, true);
        drawMic(micBPos.translated(shift, 0.0f), radiusFor(p.distanceB.get()), micBColour, "B", false, true);
    }
    drawMic(micAPos, radiusFor(p.distanceA.get()), micAColour, "A", micALoaded, false);
    drawMic(micBPos, radiusFor(p.distanceB.get()), micBColour, "B", micBInactive || micBLoaded, false);

    // A quiet hint for the vertical axis, so dragging a marker up or down
    // isn't a mystery: it is the mic's distance from the cab.
    auto track = dragTrackBounds();
    g.setColour(ModernColours::textSecondary.withAlpha(0.55f));
    g.setFont(smallFont(9.0f));
    g.drawText("close", juce::Rectangle<float>(track.getRight() - 40.0f, track.getY() - 10.0f, 44.0f, 11.0f), juce::Justification::centredRight);
    g.drawText("far", juce::Rectangle<float>(track.getRight() - 40.0f, track.getBottom() - 1.0f, 44.0f, 11.0f), juce::Justification::centredRight);
}

juce::Point<float> CabinetVisualEditor::getMicMarkerCentre(bool micB) const
{
    return micB ? markerPositionFor(p.micPositionB.get() / 100.0f, p.distanceB.get(), micBSide, micBYOffset)
                : markerPositionFor(p.micPositionA.get() / 100.0f, p.distanceA.get(), micASide, micAYOffset);
}

void CabinetVisualEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.position;
    auto micAPos = markerPositionFor(p.micPositionA.get() / 100.0f, p.distanceA.get(), micASide, micAYOffset);
    auto micBPos = markerPositionFor(p.micPositionB.get() / 100.0f, p.distanceB.get(), micBSide, micBYOffset);

    auto distA = pos.getDistanceFrom(micAPos);
    auto distB = pos.getDistanceFrom(micBPos);

    constexpr float hitRadius = baseMarkerRadius + 7.0f;
    if (distA <= hitRadius && distA <= distB)
        activeDrag = DragTarget::micA;
    else if (distB <= hitRadius)
        activeDrag = DragTarget::micB;
    else
        activeDrag = DragTarget::none;
}

void CabinetVisualEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (activeDrag == DragTarget::none)
        return;

    auto track = dragTrackBounds();
    auto side = (e.position.x >= track.getCentreX()) ? 1 : -1;
    auto fraction = xToPositionFraction(e.position.x);

    if (activeDrag == DragTarget::micA)
    {
        micASide = side;
        p.micPositionA.set(fraction * 100.0f);
        p.distanceA.set(yToDistanceKnob(e.position.y, micAYOffset));
    }
    else if (activeDrag == DragTarget::micB)
    {
        micBSide = side;
        p.micPositionB.set(fraction * 100.0f);
        p.distanceB.set(yToDistanceKnob(e.position.y, micBYOffset));
    }

    repaint();
}

void CabinetVisualEditor::timerCallback()
{
    if (activeDrag != DragTarget::none)
        return; // don't fight the user's own drag

    updateCabButtonHighlights();
    syncSelectorControls();

    auto typeIndexOf = [](PedalParameter& q) { return juce::jlimit(0, (int) q.valueLabels.size() - 1, (int) std::round(q.get())); };
    micTypeAButton.setButtonText(p.micTypeA.valueLabels.empty() ? juce::String() : p.micTypeA.valueLabels[(size_t) typeIndexOf(p.micTypeA)]);
    micTypeBButton.setButtonText(p.micTypeB.valueLabels.empty() ? juce::String() : p.micTypeB.valueLabels[(size_t) typeIndexOf(p.micTypeB)]);

    repaint();
}
