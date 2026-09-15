#include "TunerComponent.h"
#include "PitchDetector.h"

#include <cmath>

namespace
{
    constexpr int windowSize = 4096;
    constexpr float silenceThreshold = 0.01f;

    const char* const noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

TunerComponent::TunerComponent(TunerEngine& engineToUse)
    : engine(engineToUse)
{
    analysisBuffer.resize(static_cast<size_t>(windowSize));
    setSize(320, 260);
    startTimerHz(25);
}

TunerComponent::~TunerComponent()
{
    stopTimer();
}

void TunerComponent::timerCallback()
{
    if (! engine.copyLatest(analysisBuffer.data(), windowSize))
    {
        hasPitch = false;
        repaint();
        return;
    }

    float peak = 0.0f;
    for (auto s : analysisBuffer)
        peak = juce::jmax(peak, std::abs(s));

    if (peak < silenceThreshold)
    {
        hasPitch = false;
        repaint();
        return;
    }

    auto frequency = PitchDetector::detectPitchYin(analysisBuffer.data(), windowSize, engine.getSampleRate());

    if (frequency <= 0.0)
    {
        hasPitch = false;
        repaint();
        return;
    }

    auto midiNote = 69.0 + 12.0 * std::log2(frequency / 440.0);
    auto roundedNote = std::round(midiNote);

    auto noteIndex = ((static_cast<int>(roundedNote) % 12) + 12) % 12;
    auto octave = static_cast<int>(roundedNote / 12.0) - 1;

    currentNoteName = juce::String(noteNames[noteIndex]) + juce::String(octave);
    currentFrequency = frequency;
    currentCents = static_cast<float>((midiNote - roundedNote) * 100.0);
    hasPitch = true;

    repaint();
}

void TunerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    auto bounds = getLocalBounds();

    if (! hasPitch)
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(20.0f));
        g.drawText("Play a note...", bounds, juce::Justification::centred);
        return;
    }

    auto noteArea = bounds.removeFromTop(bounds.getHeight() / 2);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(64.0f, juce::Font::bold));
    g.drawText(currentNoteName, noteArea, juce::Justification::centred);

    auto freqArea = bounds.removeFromTop(28);
    g.setFont(juce::Font(16.0f));
    g.setColour(juce::Colours::lightgrey);
    g.drawText(juce::String(currentFrequency, 1) + " Hz", freqArea, juce::Justification::centred);

    bounds.removeFromTop(10);
    auto meterArea = bounds.removeFromTop(40).reduced(20, 0).toFloat();

    g.setColour(juce::Colours::darkgrey);
    g.fillRoundedRectangle(meterArea, 4.0f);

    auto centreX = meterArea.getCentreX();
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawLine(centreX, meterArea.getY(), centreX, meterArea.getBottom(), 2.0f);

    auto clampedCents = juce::jlimit(-50.0f, 50.0f, currentCents);
    auto needleX = juce::jmap(clampedCents, -50.0f, 50.0f, meterArea.getX(), meterArea.getRight());

    bool inTune = std::abs(currentCents) < 5.0f;
    g.setColour(inTune ? juce::Colours::limegreen : juce::Colours::orange);
    g.fillRoundedRectangle(needleX - 3.0f, meterArea.getY() - 6.0f, 6.0f, meterArea.getHeight() + 12.0f, 3.0f);

    bounds.removeFromTop(50);
    auto centsArea = bounds.removeFromTop(24);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(14.0f));
    juce::String centsText = (currentCents >= 0.0f ? "+" : juce::String()) + juce::String(currentCents, 0) + " cents";
    g.drawText(centsText, centsArea, juce::Justification::centred);
}
