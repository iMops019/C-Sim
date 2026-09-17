#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Shared dark, flat colour palette for the whole app's "modern/sleek"
// look - one place to tweak the theme rather than hardcoded colours
// scattered through every Component's paint().
namespace ModernColours
{
    const juce::Colour background     { 0xff1b1d23 };
    const juce::Colour surface        { 0xff262a33 };
    const juce::Colour surfaceLight   { 0xff2f333d };
    const juce::Colour border         { 0xff3a3f4b };
    const juce::Colour accent         { 0xff5eead4 };
    const juce::Colour accentDim      { 0xff2f6f68 };
    const juce::Colour danger         { 0xfff87171 };
    const juce::Colour textPrimary    { 0xffe8eaed };
    const juce::Colour textSecondary  { 0xff9aa0ac };
}

// Applied once, globally (see Main.cpp), so every stock JUCE widget in the
// app - buttons, knobs, tabs, scrollbars, popup menus, alert windows -
// shares one cohesive dark theme instead of JUCE's default grey look.
class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;
};
