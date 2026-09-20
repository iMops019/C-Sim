#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>

// A small horizontal peak meter for the top bar: a bar (-60..0 dBFS), a peak-hold
// tick, the held peak in dB, and a latched red CLIP flag that stays lit until it is
// clicked. It exists to answer one question - "is something actually hitting digital
// full scale, and WHERE" - so MainComponent puts one on the guitar input (is the
// audio interface itself clipping?) and one on the final output (is the chain?).
// The per-pedal rack meters can't say: an amp's output sits near full scale by design.
//
// Thread model: the audio thread calls pushPeak() once per block (lock-free); the
// message-thread timer reads it and animates. advance() is the animation step,
// public so it can be tested without a timer.
class LevelMeterComponent : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit LevelMeterComponent(juce::String labelText);
    ~LevelMeterComponent() override;

    // Audio thread: report the absolute peak (1.0 = full scale) of a block.
    void pushPeak(float peak) noexcept;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override { clearClip(); }

    // ---- the animation, and its state, for the test ----
    void advance(double seconds, float blockPeak);
    void clearClip() { clipLatched = false; repaint(); }

    static constexpr float floorDb = -60.0f;
    static constexpr float clipThreshold = 0.999f;   // at or above this the block counts as clipped

    // The highest peak pushed since the last call (and resets it) - what the timer feeds advance().
    float takePendingPeak() noexcept { return pendingPeak.exchange(0.0f, std::memory_order_relaxed); }

    static float toDb(float linear) noexcept;
    float getBarDb() const noexcept { return barDb; }
    float getHeldDb() const noexcept { return heldDb; }
    bool isClipLatched() const noexcept { return clipLatched; }

private:
    void timerCallback() override;

    juce::String label;
    std::atomic<float> pendingPeak { 0.0f };

    float barDb = floorDb;        // the bar: jumps up, falls smoothly
    float heldDb = floorDb;       // the peak-hold tick and readout
    double holdRemaining = 0.0;   // seconds the hold stays put before it starts to fall
    bool clipLatched = false;

    static constexpr float barFallDbPerSecond = 30.0f;
    static constexpr float holdFallDbPerSecond = 20.0f;
    static constexpr double holdSeconds = 1.5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeterComponent)
};
