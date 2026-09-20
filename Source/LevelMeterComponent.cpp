#include "LevelMeterComponent.h"

#include "ModernLookAndFeel.h"

#include <cmath>

LevelMeterComponent::LevelMeterComponent(juce::String labelText) : label(std::move(labelText))
{
    setTooltip("Peak level in dBFS. Turns red and latches CLIP if it reaches digital full scale - click to reset.");
    startTimerHz(30);
}

LevelMeterComponent::~LevelMeterComponent()
{
    stopTimer();
}

void LevelMeterComponent::pushPeak(float peak) noexcept
{
    // A lock-free "store the maximum since the UI last looked".
    auto current = pendingPeak.load(std::memory_order_relaxed);
    while (peak > current && ! pendingPeak.compare_exchange_weak(current, peak, std::memory_order_relaxed))
    {
    }
}

float LevelMeterComponent::toDb(float linear) noexcept
{
    return linear <= 1.0e-6f ? floorDb : std::max(floorDb, 20.0f * std::log10(linear));
}

void LevelMeterComponent::advance(double seconds, float blockPeak)
{
    auto db = toDb(blockPeak);

    // The bar jumps up to a new peak and falls back smoothly.
    barDb = db > barDb ? db : std::max(db, barDb - barFallDbPerSecond * static_cast<float>(seconds));

    // The hold tick sits on the highest recent peak, then drifts down.
    if (db >= heldDb)
    {
        heldDb = db;
        holdRemaining = holdSeconds;
    }
    else if (holdRemaining > 0.0)
    {
        holdRemaining -= seconds;
    }
    else
    {
        heldDb = std::max({ db, barDb, heldDb - holdFallDbPerSecond * static_cast<float>(seconds) });
    }

    if (blockPeak >= clipThreshold)
        clipLatched = true;
}

void LevelMeterComponent::timerCallback()
{
    advance(1.0 / 30.0, takePendingPeak());
    repaint();
}

void LevelMeterComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(ModernColours::surface);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(ModernColours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    auto inner = bounds.reduced(5.0f, 3.0f);
    g.setColour(ModernColours::textSecondary);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(label, inner.removeFromLeft(28.0f), juce::Justification::centredLeft);

    // Readout / CLIP flag on the right.
    auto readout = inner.removeFromRight(46.0f);
    if (clipLatched)
    {
        g.setColour(ModernColours::danger);
        g.fillRoundedRectangle(readout, 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("CLIP", readout, juce::Justification::centred);
    }
    else
    {
        g.setColour(ModernColours::textPrimary);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(heldDb <= floorDb ? juce::String("--") : juce::String(heldDb, 1), readout, juce::Justification::centredRight);
    }
    inner.removeFromRight(4.0f);

    // The bar.
    auto bar = inner.reduced(0.0f, 2.0f);
    g.setColour(ModernColours::background);
    g.fillRoundedRectangle(bar, 2.0f);

    auto fraction = [](float db) { return juce::jlimit(0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb)); };
    auto fill = bar.withWidth(bar.getWidth() * fraction(barDb));
    auto colourFor = [](float db) { return db >= -3.0f ? ModernColours::danger : (db >= -12.0f ? juce::Colour(0xfffbbf24) : ModernColours::accent); };
    g.setColour(colourFor(barDb));
    g.fillRoundedRectangle(fill, 2.0f);

    auto holdX = bar.getX() + bar.getWidth() * fraction(heldDb);
    g.setColour(colourFor(heldDb));
    g.fillRect(holdX - 1.0f, bar.getY(), 2.0f, bar.getHeight());
}
