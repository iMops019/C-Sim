#pragma once

#include <algorithm>
#include <cmath>

// A pot's resistance fraction at rotation p (0..1), for a taper that reaches
// `mid` of its total at 12 o'clock: the standard exponential audio-taper model
// (mid = 0.10 for a "10% log" pot, 0.15 for a "15A", 0.5 for linear).
inline double potFraction(double p, double mid) noexcept
{
    p = std::min(1.0, std::max(0.0, p));
    if (mid >= 0.499)
        return p;
    auto a = std::pow(1.0 / mid - 1.0, 2.0);
    return (std::pow(a, p) - 1.0) / (a - 1.0);
}
