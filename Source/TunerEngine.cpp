#include "TunerEngine.h"

TunerEngine::TunerEngine(int capacityInSamples)
    : buffer(static_cast<size_t>(capacityInSamples), 0.0f), capacity(capacityInSamples)
{
}

void TunerEngine::pushSamples(const float* samples, int numSamples) noexcept
{
    auto index = writeIndex.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        buffer[static_cast<size_t>(index % capacity)] = samples[i];
        ++index;
    }

    writeIndex.store(index, std::memory_order_relaxed);
}

bool TunerEngine::copyLatest(float* dest, int windowSize) const noexcept
{
    auto index = writeIndex.load(std::memory_order_relaxed);

    if (index < static_cast<int64_t>(windowSize) || windowSize > capacity)
        return false;

    for (int i = 0; i < windowSize; ++i)
    {
        auto sourceIndex = (index - windowSize + i) % capacity;
        dest[i] = buffer[static_cast<size_t>(sourceIndex)];
    }

    return true;
}
