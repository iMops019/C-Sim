#pragma once

#include <array>

// A 10-band graphic EQ spanning the useful guitar/amp tone-shaping range,
// 100Hz-10kHz - below that is mostly cabinet/room rumble, above it is
// fret and pick noise on a guitar signal, not tone. Wide enough to carve
// a scooped metal mid or a bright lead boost, or just experiment.
//
// Each band is a fixed-frequency peaking filter (RBJ Audio EQ Cookbook
// formula), which is exactly unity gain at 0dB - the peaking b/a
// coefficients are identical when the requested gain is 0dB, so a flat
// EQ is a true bypass, not a filter shaped to look flat. Centers are
// spaced ~0.74 octave apart (a fixed ratio across the whole range, not
// ISO thirds) with a shared Q chosen so adjacent bands cross close to
// -3dB - narrow enough that a boost is felt where you put it, wide
// enough that bands don't fight each other or leave gaps between them.
//
// Framework-agnostic, matching the rest of the toolkit.
class GraphicEQ
{
public:
    static constexpr int numBands = 10;

    // Fixed center frequencies in Hz, band 0 (100Hz) to band 9 (10kHz).
    static const std::array<double, numBands>& getBandFrequencies() noexcept;

    explicit GraphicEQ(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setBandGainDb(int band, float gainDb); // -15..+15, 0 = flat/bypass for that band

    void reset() noexcept;

    float processSample(float input) noexcept;
    void processBlock(const float* input, float* output, int numSamples) noexcept;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        float process(float x) noexcept;
        void reset() noexcept;
    };

    void updateBand(int band);
    static void designPeaking(Biquad& b, double freqHz, double gainDb, double q, double sampleRate);

    double sampleRate;
    std::array<float, numBands> gainsDb {};
    std::array<Biquad, numBands> biquads;
};
