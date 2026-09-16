#pragma once

#include <algorithm>
#include <vector>

// A delay-line (Schroeder) allpass filter: y[n] = -g*x[n] + x[n-M] + g*y[n-M].
// Unlike a single-sample allpass, a delay-based one has a resonant comb
// structure while still being unity-gain at every frequency (only phase
// changes) - cascading several with different delay lengths is the
// standard building block for a dispersive delay line (spring reverb,
// diffusion networks), since different lengths give different frequencies
// a different effective group delay.
class DelayAllpass
{
public:
    DelayAllpass() = default;

    void setup(int delaySamples, float g)
    {
        delayLength = std::max(1, delaySamples);
        gain = g;
        buffer.assign(static_cast<size_t>(delayLength), 0.0f);
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    float processSample(float x) noexcept
    {
        auto delayed = buffer[writeIndex];
        auto y = -gain * x + delayed;
        buffer[writeIndex] = x + gain * y;
        writeIndex = (writeIndex + 1) % static_cast<size_t>(delayLength);
        return y;
    }

private:
    std::vector<float> buffer;
    size_t writeIndex = 0;
    int delayLength = 1;
    float gain = 0.0f;
};
