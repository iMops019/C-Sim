#include "ModernLookAndFeel.h"

ModernLookAndFeel::ModernLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, ModernColours::background);

    setColour(juce::TextButton::buttonColourId, ModernColours::surfaceLight);
    setColour(juce::TextButton::buttonOnColourId, ModernColours::accent);
    setColour(juce::TextButton::textColourOffId, ModernColours::textPrimary);
    setColour(juce::TextButton::textColourOnId, ModernColours::background);

    setColour(juce::ToggleButton::textColourId, ModernColours::textPrimary);

    setColour(juce::Slider::rotarySliderFillColourId, ModernColours::accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, ModernColours::border);
    setColour(juce::Slider::thumbColourId, ModernColours::accent);
    setColour(juce::Slider::trackColourId, ModernColours::border);
    setColour(juce::Slider::textBoxTextColourId, ModernColours::textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, ModernColours::surface);
    setColour(juce::Slider::textBoxOutlineColourId, ModernColours::border);

    setColour(juce::Label::textColourId, ModernColours::textPrimary);

    setColour(juce::TabbedButtonBar::tabOutlineColourId, ModernColours::border);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, ModernColours::accent);
    setColour(juce::TabbedButtonBar::tabTextColourId, ModernColours::textSecondary);
    setColour(juce::TabbedButtonBar::frontTextColourId, ModernColours::textPrimary);
    setColour(juce::TabbedComponent::backgroundColourId, ModernColours::background);
    setColour(juce::TabbedComponent::outlineColourId, ModernColours::border);

    setColour(juce::ScrollBar::thumbColourId, ModernColours::border);
    setColour(juce::ScrollBar::trackColourId, ModernColours::surface);

    setColour(juce::PopupMenu::backgroundColourId, ModernColours::surface);
    setColour(juce::PopupMenu::textColourId, ModernColours::textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ModernColours::accentDim);
    setColour(juce::PopupMenu::highlightedTextColourId, ModernColours::textPrimary);

    setColour(juce::AlertWindow::backgroundColourId, ModernColours::surface);
    setColour(juce::AlertWindow::textColourId, ModernColours::textPrimary);
    setColour(juce::AlertWindow::outlineColourId, ModernColours::border);

    setColour(juce::TextEditor::backgroundColourId, ModernColours::surfaceLight);
    setColour(juce::TextEditor::textColourId, ModernColours::textPrimary);
    setColour(juce::TextEditor::outlineColourId, ModernColours::border);
    setColour(juce::TextEditor::focusedOutlineColourId, ModernColours::accent);
}

void ModernLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                          juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(4.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    constexpr float trackThickness = 3.5f;
    auto arcRadius = radius - trackThickness;

    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(ModernColours::border);
    g.strokePath(backgroundArc, juce::PathStrokeType(trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, angle, true);
    g.setColour(slider.isEnabled() ? ModernColours::accent : ModernColours::textSecondary);
    g.strokePath(valueArc, juce::PathStrokeType(trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto knobRadius = juce::jmax(2.0f, arcRadius - trackThickness * 2.5f);
    g.setColour(ModernColours::surfaceLight);
    g.fillEllipse(centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
    g.setColour(ModernColours::border);
    g.drawEllipse(centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.0f);

    juce::Path pointer;
    auto pointerLength = knobRadius * 0.75f;
    constexpr float pointerThickness = 2.5f;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -knobRadius, pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(ModernColours::accent);
    g.fillPath(pointer);
}
