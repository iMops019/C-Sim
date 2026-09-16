#pragma once

// A single-pole RC high-pass, modeling the real coupling capacitor + grid
// leak resistor that sits between every pair of stages in an actual tube
// amp. The standard digital "DC blocker" one-pole form:
//   y[n] = alpha * (y[n-1] + x[n] - x[n-1])
class CouplingHighpass
{
public:
    CouplingHighpass() = default;
    CouplingHighpass(double sampleRate, double cutoffHz) { setCutoff(sampleRate, cutoffHz); }

    void setCutoff(double sampleRate, double cutoffHz)
    {
        constexpr double twoPi = 6.28318530717958647692;
        auto rc = 1.0 / (twoPi * cutoffHz);
        auto dt = 1.0 / sampleRate;
        alpha = rc / (rc + dt);
    }

    void reset() noexcept { prevInput = 0.0; prevOutput = 0.0; }

    float processSample(float input) noexcept
    {
        auto x = static_cast<double>(input);
        auto y = alpha * (prevOutput + x - prevInput);
        prevInput = x;
        prevOutput = y;
        return static_cast<float>(y);
    }

private:
    double alpha = 0.0;
    double prevInput = 0.0;
    double prevOutput = 0.0;
};
