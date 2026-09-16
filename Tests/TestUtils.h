#pragma once

// Shared helpers for the standalone dsp/ diagnostic tools in this
// directory - kept dependency-free (no JUCE) to match the toolkit itself.

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace TestUtils
{
    // Goertzel algorithm: magnitude of a real signal at one target
    // frequency, without computing a full FFT.
    inline double goertzelMagnitude(const std::vector<float>& samples, double targetFreq, double sampleRate)
    {
        auto n = samples.size();
        auto k = 0.5 + (static_cast<double>(n) * targetFreq / sampleRate);
        auto w = (2.0 * M_PI / static_cast<double>(n)) * k;
        auto cosw = std::cos(w);
        auto coeff = 2.0 * cosw;
        double q0 = 0.0, q1 = 0.0, q2 = 0.0;

        for (auto s : samples)
        {
            q0 = coeff * q1 - q2 + static_cast<double>(s);
            q2 = q1;
            q1 = q0;
        }

        auto real = q1 - q2 * cosw;
        auto imag = q2 * std::sin(w);
        return std::sqrt(real * real + imag * imag) / (static_cast<double>(n) / 2.0);
    }

    inline double toDb(double linear)
    {
        return 20.0 * std::log10(std::max(linear, 1.0e-12));
    }
}
