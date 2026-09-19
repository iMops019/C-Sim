#include "CentaurDriveStage.h"

#include <algorithm>
#include <cmath>

namespace
{
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

    // --- Klon core: sourced from the ElectroSmash / Coda Effects teardowns ---
    constexpr double humpHz = 1000.0;          // gain stage mid-hump centre
    constexpr double humpMinGain = 4.6;        // op-amp gain at Gain=0 (Coda)
    constexpr double humpMaxGain = 25.8;       // op-amp gain at Gain=1 (Coda)
    constexpr double summingLowpassHz = 495.0; // summing stage single-pole LPF (ElectroSmash)
    constexpr double toneShelfHz = 408.0;      // tone control shelf corner (ElectroSmash)
    constexpr double toneMinGain = 0.4;        // -8dB
    constexpr double toneMaxGain = 8.16;       // +18.24dB
    constexpr double inputHighpassHz = 1.5;    // input buffer's coupling-cap corner (ElectroSmash)

    // --- Calibrated, not transcribed (see the header comment) ---
    // The real gain stage's response is a broad plateau (a high-pass corner
    // from its 2.2uF coupling cap, then a feedback-cap roll-off), not a narrow
    // peak - a low Q reproduces that, so low guitar notes get real gain too
    // instead of only 1kHz content driving the clipper.
    constexpr double humpQ = 0.35;
    constexpr float cleanPathFloor = 0.2f;     // clean level left at Gain=1 - never fully gone
    constexpr double voltsPerUnit = 0.5;       // 1.0 sample = 500mV in the circuit's voltage domain
    constexpr float outputTrim = 0.3f;         // lands default settings near unity loudness

    // --- Added stages ---
    constexpr double depthHz = 95.0;
    constexpr double depthQ = 0.9;
    constexpr double depthMaxDb = 10.0;
    constexpr double ldMaxGainDb = 30.0;       // Distortion+/DS-1-class op-amp gain range

    double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }
    double linearToDb(double lin) { return 20.0 * std::log10(lin); }
}

// -------------------------------------------------------------------------
// Biquad + filter designs
// -------------------------------------------------------------------------

float CentaurDriveStage::Biquad::process(float x) noexcept
{
    auto in = static_cast<double>(x);
    auto out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    return static_cast<float>(out);
}

void CentaurDriveStage::Biquad::reset() noexcept
{
    z1 = z2 = 0.0;
}

void CentaurDriveStage::designPeaking(Biquad& b, double sr, double centerHz, double q, double gainDb)
{
    // RBJ Audio EQ Cookbook peaking EQ.
    auto a = std::pow(10.0, gainDb / 40.0);
    auto w0 = 2.0 * M_PI * centerHz / sr;
    auto cosw0 = std::cos(w0);
    auto alpha = std::sin(w0) / (2.0 * q);

    auto b0 = 1.0 + alpha * a;
    auto b1 = -2.0 * cosw0;
    auto b2 = 1.0 - alpha * a;
    auto a0 = 1.0 + alpha / a;
    auto a1 = -2.0 * cosw0;
    auto a2 = 1.0 - alpha / a;

    b.b0 = b0 / a0;
    b.b1 = b1 / a0;
    b.b2 = b2 / a0;
    b.a1 = a1 / a0;
    b.a2 = a2 / a0;
}

void CentaurDriveStage::designHighShelf(Biquad& b, double sr, double cornerHz, double gainDb)
{
    // RBJ Audio EQ Cookbook high-shelf, shelf slope S=1.
    auto a = std::pow(10.0, gainDb / 40.0);
    auto w0 = 2.0 * M_PI * cornerHz / sr;
    auto cosw0 = std::cos(w0);
    auto sinw0 = std::sin(w0);
    constexpr double shelfSlope = 1.0;
    auto alpha = (sinw0 / 2.0) * std::sqrt((a + 1.0 / a) * (1.0 / shelfSlope - 1.0) + 2.0);
    auto twoSqrtAAlpha = 2.0 * std::sqrt(a) * alpha;

    auto b0 = a * ((a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha);
    auto b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw0);
    auto b2 = a * ((a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha);
    auto a0 = (a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha;
    auto a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw0);
    auto a2 = (a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha;

    b.b0 = b0 / a0;
    b.b1 = b1 / a0;
    b.b2 = b2 / a0;
    b.a1 = a1 / a0;
    b.a2 = a2 / a0;
}

// -------------------------------------------------------------------------
// Diodes
// -------------------------------------------------------------------------

DiodeClipperStage::Parameters CentaurDriveStage::germaniumDiodeParameters()
{
    // 1N34A-style germanium: high saturation current and a soft knee
    // (ideality folded into the thermal voltage) put the forward drop near
    // the ~0.35V Bill Finnegan's part is documented at. The 1k series
    // resistor is a calibration - the schematic value wasn't readable.
    DiodeClipperStage::Parameters p;
    p.seriesResistance = 1000.0;
    p.saturationCurrent = 2.0e-7;
    p.thermalVoltage = 0.0388;
    p.inputScale = 1.0; // input already in volts
    return p;
}

DiodeClipperStage::Parameters CentaurDriveStage::siliconDiodeParameters()
{
    // 1N4148-style silicon: SPICE Is=2.52nA with ideality 1.752 gives the
    // familiar ~0.6V drop. (DiodeClipperStage's own default keeps n=1 and
    // so clips lower than a real 1N4148 - fine for its purpose, but this
    // stage wants the real drop.)
    DiodeClipperStage::Parameters p;
    p.seriesResistance = 10000.0;
    p.saturationCurrent = 2.52e-9;
    p.thermalVoltage = 0.02585 * 1.752;
    p.inputScale = 1.0;
    return p;
}

CentaurDriveStage::VoltClipper::VoltClipper(const DiodeClipperStage::Parameters& p)
    : stage(p)
{
    constexpr double probeVolts = 1.0e-4;
    DiodeClipperStage probe(p);
    auto smallSignalGain = static_cast<double>(probe.processSample(static_cast<float>(probeVolts))) / probeVolts;
    unityCompensation = smallSignalGain > 1.0e-9 ? 1.0 / smallSignalGain : 1.0;
}

// -------------------------------------------------------------------------
// The stage
// -------------------------------------------------------------------------

double CentaurDriveStage::toneGainDb(float amount) noexcept
{
    auto t = static_cast<double>(std::clamp(amount, 0.0f, 1.0f));
    auto lo = linearToDb(toneMinGain);
    auto hi = linearToDb(toneMaxGain);
    return lo + t * (hi - lo);
}

CentaurDriveStage::CentaurDriveStage(double sampleRateToUse)
    : sampleRate(sampleRateToUse),
      germanium(germaniumDiodeParameters()),
      silicon(siliconDiodeParameters()),
      clipOversampler(sampleRateToUse),
      ldOversampler(sampleRateToUse)
{
    inputHighpass.setCutoff(sampleRate, inputHighpassHz);
    summingLpAlpha = 1.0 - std::exp(-2.0 * M_PI * summingLowpassHz / sampleRate);

    setDrive(0.5f);
    setLightDistortion(0.0f);
    setDepth(0.0f);
    setTone(0.5f);
}

void CentaurDriveStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    clipOversampler = Oversampler4x(sampleRate);
    ldOversampler = Oversampler4x(sampleRate);
    inputHighpass.setCutoff(sampleRate, inputHighpassHz);
    summingLpAlpha = 1.0 - std::exp(-2.0 * M_PI * summingLowpassHz / sampleRate);
    reset();
}

void CentaurDriveStage::setDrive(float amount)
{
    auto g = static_cast<double>(std::clamp(amount, 0.0f, 1.0f));

    // Dual-ganged pot: one half raises the hump path's gain, the other
    // lowers the clean path. dB-linear interpolation between the sourced
    // 4.6x and 25.8x endpoints, so equal knob steps feel like equal loudness
    // steps.
    auto humpDb = linearToDb(humpMinGain) + g * (linearToDb(humpMaxGain) - linearToDb(humpMinGain));
    designPeaking(hump, sampleRate, humpHz, humpQ, humpDb);

    cleanLevel = static_cast<float>(1.0 - (1.0 - cleanPathFloor) * g);
}

void CentaurDriveStage::setLightDistortion(float amount)
{
    auto l = static_cast<double>(std::clamp(amount, 0.0f, 1.0f));
    ldGain = static_cast<float>(dbToLinear(l * ldMaxGainDb));
    // Wet amount rises faster than the gain so the first part of the knob
    // is already audible, while 0 stays a true skip.
    ldWet = static_cast<float>(1.0 - (1.0 - l) * (1.0 - l));
    // Hard clipping pins the wet path near the diode drop no matter how much
    // gain went in, which would make the pedal jump in level as the knob
    // rises; back the wet path off as its gain climbs.
    ldWetTrim = static_cast<float>(1.0 / std::pow(ldGain, 0.35));
}

void CentaurDriveStage::setDepth(float amount)
{
    auto d = static_cast<double>(std::clamp(amount, 0.0f, 1.0f));
    designPeaking(depthPeak, sampleRate, depthHz, depthQ, d * depthMaxDb);
}

void CentaurDriveStage::setTone(float amount)
{
    designHighShelf(toneShelf, sampleRate, toneShelfHz, toneGainDb(amount));
}

void CentaurDriveStage::reset() noexcept
{
    inputHighpass.reset();
    hump.reset();
    summingLpState = 0.0;
    depthPeak.reset();
    toneShelf.reset();
    germanium.reset();
    silicon.reset();
    clipOversampler.reset();
    ldOversampler.reset();
    ldWasActive = false;
}

float CentaurDriveStage::clipGermanium(float x) noexcept
{
    return static_cast<float>(germanium.process(static_cast<float>(x * voltsPerUnit)) / voltsPerUnit);
}

float CentaurDriveStage::clipSilicon(float x) noexcept
{
    auto volts = static_cast<double>(x) * static_cast<double>(ldGain) * voltsPerUnit;
    return static_cast<float>(silicon.process(static_cast<float>(volts)) / voltsPerUnit * static_cast<double>(ldWetTrim));
}

void CentaurDriveStage::processBlock(const float* input, float* output, int numSamples)
{
    auto n = static_cast<size_t>(numSamples);
    cleanBuffer.resize(n);
    clipBuffer.resize(n);

    // 1. Input buffer, then split: the clean path as-is, the other path
    //    through the gain stage's mid-hump into the germanium clipper.
    for (int i = 0; i < numSamples; ++i)
    {
        auto a = inputHighpass.processSample(input[i]);
        cleanBuffer[static_cast<size_t>(i)] = a;
        clipBuffer[static_cast<size_t>(i)] = hump.process(a);
    }

    clipOversampler.processBlock(clipBuffer.data(), clipBuffer.data(), numSamples,
                                 [this](float x) { return clipGermanium(x); });

    // 2. Summing stage: recombine both paths, then the ~495Hz low-pass.
    for (int i = 0; i < numSamples; ++i)
    {
        auto mixed = static_cast<double>(cleanBuffer[static_cast<size_t>(i)]) * cleanLevel
                     + static_cast<double>(clipBuffer[static_cast<size_t>(i)]);
        summingLpState += summingLpAlpha * (mixed - summingLpState);
        output[i] = static_cast<float>(summingLpState);
    }

    // 3. Light Distortion, in series. Skipped outright at 0 so the pedal is
    //    a pure Klon path there.
    if (ldWet > 0.0f)
    {
        if (!ldWasActive)
        {
            ldOversampler.reset();
            silicon.reset();
            ldWasActive = true;
        }

        ldBuffer.assign(output, output + numSamples);
        ldOversampler.processBlock(ldBuffer.data(), ldBuffer.data(), numSamples,
                                   [this](float x) { return clipSilicon(x); });
        for (int i = 0; i < numSamples; ++i)
            output[i] += ldWet * (ldBuffer[static_cast<size_t>(i)] - output[i]);
    }
    else
    {
        ldWasActive = false;
    }

    // 4. Depth (post-clip low peak), then the Klon's tone shelf.
    for (int i = 0; i < numSamples; ++i)
    {
        auto y = depthPeak.process(output[i]);
        y = toneShelf.process(y);
        output[i] = y * outputTrim;
    }
}
