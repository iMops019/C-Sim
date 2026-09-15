#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <vector>

// A rolling buffer of the most recent raw guitar samples. The audio thread
// pushes samples every block (lock-free, no allocation); the tuner window's
// timer periodically copies out the latest window for pitch analysis.
class TunerEngine
{
public:
    explicit TunerEngine(int capacityInSamples = 8192);

    // Audio thread only.
    void pushSamples(const float* samples, int numSamples) noexcept;

    // Message thread only. Fills 'dest' with the most recent windowSize
    // samples in chronological order. Returns false if not enough audio
    // has been captured yet to fill the window.
    bool copyLatest(float* dest, int windowSize) const noexcept;

    void setSampleRate(double newSampleRate) noexcept { sampleRate.store(newSampleRate, std::memory_order_relaxed); }
    double getSampleRate() const noexcept { return sampleRate.load(std::memory_order_relaxed); }

private:
    std::vector<float> buffer;
    const int capacity;
    std::atomic<int64_t> writeIndex { 0 };
    std::atomic<double> sampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerEngine)
};
