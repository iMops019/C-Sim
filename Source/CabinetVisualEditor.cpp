#include "CabinetVisualEditor.h"
#include "CabImpulseResponse.h"
#include "ModernLookAndFeel.h"

#include <cmath>

namespace
{
    constexpr float markerRadius = 11.0f;
    constexpr float micAYOffset = -12.0f;
    constexpr float micBYOffset = 12.0f;
    const juce::Colour micAColour = ModernColours::accent;
    const juce::Colour micBColour { 0xfff5a623 }; // warm amber - distinct from A's teal and from the danger red used elsewhere
}

CabinetVisualEditor::CabinetVisualEditor(PedalParameter& cabParam,
                                          PedalParameter& micTypeAParam, PedalParameter& micPositionAParam,
                                          PedalParameter& micTypeBParam, PedalParameter& micPositionBParam)
    : cab(cabParam), micTypeA(micTypeAParam), micPositionA(micPositionAParam),
      micTypeB(micTypeBParam), micPositionB(micPositionBParam)
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
    setUpMicTypeButton(micTypeAButton, micTypeA);
    setUpMicTypeButton(micTypeBButton, micTypeB);
    micTypeAButton.setColour(juce::TextButton::textColourOffId, micAColour);
    micTypeBButton.setColour(juce::TextButton::textColourOffId, micBColour);

    setSize(320, 260);
    startTimerHz(15);
}

void CabinetVisualEditor::rebuildCabButtons()
{
    cabButtons.clear();
    for (auto& labelText : cab.valueLabels)
    {
        auto* button = cabButtons.add(new juce::TextButton(labelText));
        auto index = cabButtons.size() - 1;
        button->onClick = [this, index]
        {
            cab.set((float) index);
            updateCabButtonHighlights();
        };
        addAndMakeVisible(button);
    }
    updateCabButtonHighlights();
}

void CabinetVisualEditor::updateCabButtonHighlights()
{
    auto current = (int) std::round(cab.get());
    for (int i = 0; i < cabButtons.size(); ++i)
        cabButtons[i]->setColour(juce::TextButton::buttonColourId,
                                  i == current ? ModernColours::accentDim : ModernColours::surfaceLight);
    repaint();
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

    area.removeFromTop(6);

    auto micTypeRow = area.removeFromBottom(26);
    micTypeAButton.setBounds(micTypeRow.removeFromLeft(micTypeRow.getWidth() / 2).reduced(4, 0));
    micTypeBButton.setBounds(micTypeRow.reduced(4, 0));

    area.removeFromBottom(6);
    cabDrawArea = area;
}

std::vector<juce::Rectangle<float>> CabinetVisualEditor::layoutSpeakers() const
{
    auto area = cabDrawArea.toFloat().reduced(16.0f);
    auto count = CabImpulseResponse::speakerCount((int) std::round(cab.get()));

    std::vector<juce::Rectangle<float>> result;

    if (count <= 1)
    {
        auto d = juce::jmin(area.getWidth() * 0.5f, area.getHeight() * 0.85f);
        result.push_back(juce::Rectangle<float>(d, d).withCentre(area.getCentre()));
    }
    else if (count == 2)
    {
        auto d = juce::jmin(area.getWidth() * 0.38f, area.getHeight() * 0.85f);
        auto gap = area.getWidth() * 0.08f;
        auto totalW = d * 2.0f + gap;
        auto startCx = area.getCentreX() - totalW * 0.5f + d * 0.5f;
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx, area.getCentreY() }));
        result.push_back(juce::Rectangle<float>(d, d).withCentre({ startCx + d + gap, area.getCentreY() }));
    }
    else // 4
    {
        auto d = juce::jmin(area.getWidth() * 0.42f, area.getHeight() * 0.42f);
        auto gapX = area.getWidth() * 0.06f;
        auto gapY = area.getHeight() * 0.08f;
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
    return cabDrawArea.toFloat().reduced(24.0f, 0.0f);
}

juce::Point<float> CabinetVisualEditor::markerPositionFor(float positionFraction, int side) const
{
    auto track = dragTrackBounds();
    auto halfWidth = track.getWidth() * 0.5f;
    auto x = track.getCentreX() + static_cast<float>(side) * (1.0f - positionFraction) * halfWidth;
    return { x, track.getCentreY() };
}

float CabinetVisualEditor::xToPositionFraction(float x) const
{
    auto track = dragTrackBounds();
    auto halfWidth = juce::jmax(1.0f, track.getWidth() * 0.5f);
    auto dist = std::abs(x - track.getCentreX());
    return juce::jlimit(0.0f, 1.0f, 1.0f - dist / halfWidth);
}

void CabinetVisualEditor::paint(juce::Graphics& g)
{
    auto area = cabDrawArea.toFloat();

    // Cab body - a simple dark carpeted-box look, distinct from the
    // window's own background so it reads as "a physical object" rather
    // than blending into the surrounding controls.
    g.setColour(ModernColours::surface);
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(ModernColours::border);
    g.drawRoundedRectangle(area, 8.0f, 1.5f);

    // Speakers - basket, cone, and dust cap as three concentric circles.
    for (auto& speaker : layoutSpeakers())
    {
        g.setColour(ModernColours::background);
        g.fillEllipse(speaker);
        g.setColour(ModernColours::surfaceLight);
        g.fillEllipse(speaker.reduced(speaker.getWidth() * 0.12f));
        g.setColour(ModernColours::background);
        g.fillEllipse(speaker.reduced(speaker.getWidth() * 0.42f));
        g.setColour(ModernColours::border);
        g.drawEllipse(speaker, 1.5f);
    }

    // Mic markers, positioned from the live parameter values - drawn
    // last so they sit on top of the cab/speaker artwork.
    auto drawMic = [&g](juce::Point<float> pos, float yOffset, const juce::Colour& colour, const juce::String& label)
    {
        auto centre = pos.translated(0.0f, yOffset);
        auto bounds = juce::Rectangle<float>(markerRadius * 2.0f, markerRadius * 2.0f).withCentre(centre);

        g.setColour(colour.withAlpha(0.25f));
        g.fillEllipse(bounds.expanded(4.0f));
        g.setColour(colour);
        g.fillEllipse(bounds);
        g.setColour(ModernColours::background);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(label, bounds, juce::Justification::centred);
    };

    auto micAPos = markerPositionFor(micPositionA.get() / 100.0f, micASide);
    auto micBPos = markerPositionFor(micPositionB.get() / 100.0f, micBSide);
    drawMic(micAPos, micAYOffset, micAColour, "A");
    drawMic(micBPos, micBYOffset, micBColour, "B");
}

void CabinetVisualEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.position;
    auto micAPos = markerPositionFor(micPositionA.get() / 100.0f, micASide).translated(0.0f, micAYOffset);
    auto micBPos = markerPositionFor(micPositionB.get() / 100.0f, micBSide).translated(0.0f, micBYOffset);

    auto distA = pos.getDistanceFrom(micAPos);
    auto distB = pos.getDistanceFrom(micBPos);

    constexpr float hitRadius = markerRadius + 6.0f;
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
        micPositionA.set(fraction * 100.0f);
    }
    else if (activeDrag == DragTarget::micB)
    {
        micBSide = side;
        micPositionB.set(fraction * 100.0f);
    }

    repaint();
}

void CabinetVisualEditor::timerCallback()
{
    if (activeDrag != DragTarget::none)
        return; // don't fight the user's own drag

    updateCabButtonHighlights();

    auto typeIndexOf = [](PedalParameter& p) { return juce::jlimit(0, (int) p.valueLabels.size() - 1, (int) std::round(p.get())); };
    micTypeAButton.setButtonText(micTypeA.valueLabels.empty() ? juce::String() : micTypeA.valueLabels[(size_t) typeIndexOf(micTypeA)]);
    micTypeBButton.setButtonText(micTypeB.valueLabels.empty() ? juce::String() : micTypeB.valueLabels[(size_t) typeIndexOf(micTypeB)]);

    repaint();
}
