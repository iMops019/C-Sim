#pragma once

#include <vector>

// A 4x oversampler (two cascaded 2x stages) for wrapping a nonlinear
// stage: harmonics a waveshaper generates above the original Nyquist get
// resolved at the higher rate instead of folding back down as aliasing,
// then the downsample filter removes them before returning to the
// original rate.
//
// Framework-agnostic (no JUCE dependency), matching the rest of this
// toolkit so it stays testable in isolation. A proper polyphase halfband
// FIR would be more CPU-efficient than these Butterworth biquad stages;
// this version favours being simple and easy to verify first - swap the
// filter design later without changing the interface if profiling calls
// for it.
class Oversampler4x
{
public:
    explicit Oversampler4x(double baseSampleRate);

    void reset();

    // Runs `process` once per oversampled sample over numSamples of audio
    // at the base sample rate (input/output both base-rate, length
    // numSamples). Not yet optimised for zero-allocation real-time use -
    // this is the verification version; see Peavey5150Pedal for the
    // juce::dsp::Oversampling-based approach already used in the app.
    template <typename ProcessSample>
    void processBlock(const float* input, float* output, int numSamples, ProcessSample&& process)
    {
        upsampleBuffer.resize(static_cast<size_t>(numSamples) * 4);
        upsample(input, numSamples, upsampleBuffer.data());

        for (auto& s : upsampleBuffer)
            s = process(s);

        downsample(upsampleBuffer.data(), numSamples, output);
    }

    double getOversampledRate() const noexcept { return baseRate * 4.0; }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        float process(float x) noexcept
        {
            auto in = static_cast<double>(x);
            auto out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return static_cast<float>(out);
        }

        void reset() noexcept { z1 = z2 = 0.0; }
    };

    // 8th-order Butterworth (4 cascaded biquads) for anti-imaging/
    // anti-aliasing - a 4th-order (2-biquad) version measurably under-
    // attenuated a harmonic landing less than an octave above the cutoff
    // (verified by OversamplerAliasingTest); 8th order gives a
    // meaningfully steeper transition band for the same cutoff choice.
    static constexpr int filterOrder = 4;

    struct Stage
    {
        Biquad up[filterOrder];
        Biquad down[filterOrder];

        void reset() noexcept
        {
            for (auto& b : up) b.reset();
            for (auto& b : down) b.reset();
        }
    };

    static void designRbjLowpass(Biquad& b, double cutoffHz, double sampleRate, double q);
    static void designButterworthLowpass(Biquad* stages, double cutoffHz, double sampleRate);

    void upsample(const float* input, int numSamples, float* output);
    void downsample(float* input, int numSamples, float* output);

    double baseRate;
    Stage stage1x2; // base rate <-> 2x
    Stage stage2x4; // 2x rate <-> 4x

    std::vector<float> upsampleBuffer; // 4x-rate buffer, reused across calls
    std::vector<float> midBuffer;      // 2x-rate buffer, reused across calls
};
