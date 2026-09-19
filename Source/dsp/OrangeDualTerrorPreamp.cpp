#include "OrangeDualTerrorPreamp.h"

#include <cmath>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;

    float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // ---- Values read directly off the Dual Terror schematic (Rev.2). ----

    // 12AX7 plate resistance, datasheet typical - the source impedance a
    // plate presents alongside its load resistor. Not on the schematic.
    constexpr double tubeRp = 62.5e3;
    constexpr double parallel(double a, double b) noexcept { return a * b / (a + b); }

    // Tiny Terror: V1-A plate (100k load || rp) -> C1 1nF -> R4 68k ->
    // R6 470k || RV1-A 1M (pot's full resistance).
    constexpr double tinyC1Farads = 1.0e-9;
    constexpr double tinyPotLoad = parallel(470.0e3, 1.0e6);
    constexpr double tinyC1Resistance = parallel(100.0e3, tubeRp) + 68.0e3 + tinyPotLoad;
    constexpr double tinyC1CutoffHz = 1.0 / (twoPi * tinyC1Resistance * tinyC1Farads); // ~374Hz
    constexpr float tinyC1Passband = static_cast<float>(tinyPotLoad / tinyC1Resistance);

    // Tiny Terror: 100pF (C3) across RV1-A (1M).
    constexpr double tinyPotBrightHz = 1.0 / (twoPi * 1.0e6 * 100.0e-12); // ~1.6kHz

    // Tiny Terror: V1-B plate -> R8 100k -> C4 47nF -> RV1-B 1M || R10 220k.
    constexpr double tinyC4Resistance = parallel(100.0e3, tubeRp) + 100.0e3 + parallel(1.0e6, 220.0e3);
    constexpr double tinyC4CutoffHz = 1.0 / (twoPi * tinyC4Resistance * 47.0e-9); // ~10.6Hz
    constexpr float tinyC4Passband = static_cast<float>(parallel(1.0e6, 220.0e3) / tinyC4Resistance);

    // Fat: V3-B cathode follower (~1k out) -> R22 100k -> C21 68nF ->
    // RV4-B 1M || R24 220k.
    constexpr double fatC21Resistance = 1.0e3 + 100.0e3 + parallel(1.0e6, 220.0e3);
    constexpr double fatC21CutoffHz = 1.0 / (twoPi * fatC21Resistance * 68.0e-9); // ~8Hz
    constexpr float fatC21Passband = static_cast<float>(parallel(1.0e6, 220.0e3) / fatC21Resistance);

    // Tone: 500k pot in series with the bridging cap; source impedance is
    // the two splitter plates' own (load || rp), summed for a differential
    // path. Tiny Terror: 100k/100k plate loads + 2.2nF. Fat: 82k/100k + 4.7nF.
    constexpr double tonePotOhms = 500.0e3;
    constexpr double tinyToneSource = parallel(100.0e3, tubeRp) + parallel(100.0e3, tubeRp);
    constexpr double fatToneSource = parallel(82.0e3, tubeRp) + parallel(100.0e3, tubeRp);
    constexpr double tinyToneFarads = 2.2e-9;
    constexpr double fatToneFarads = 4.7e-9;

    // ---- Judgment calls (not on the schematic). ----

    // Tiny Terror's bright cap: how much the 100pF adds at Gain=0, fading
    // to none at Gain=1 (a bright cap across a pot matters most when the
    // pot's upper section is large, i.e. low Gain).
    constexpr float maxPotBrightAmount = 0.6f;

    // OR60 Bright switch: shimmer / neutral / bite.
    constexpr double brightSwitchHz = 2000.0;
    constexpr float brightAmounts[3] = { 0.35f, 0.0f, 1.0f };

    // Fixed drive into the very first tube (Tiny Terror V1-A) and the
    // makeup a Koren stage's sub-unity small-signal gain needs between
    // stages - same role as in this toolkit's other preamps.
    constexpr float firstStageDrive = 2.0f;
    constexpr float interStageMakeupGain = 3.0f;

    // Per-channel output makeup, calibrated with a throwaway diagnostic
    // (deleted after use): at the default Gain/Tone/Volume (0.5/0.6/0.5)
    // and a 0.3-amplitude sine, RMS gain averaged across 110Hz-1.76kHz
    // measured -1.6dB for the Tiny Terror and +3.9dB for the Fat channel
    // uncompensated. These bring both to ~0dB, so switching channels (or
    // engaging the add-on) doesn't jump level at default settings.
    constexpr float tinyOutputMakeup = 1.2f;  // +1.6dB
    constexpr float fatOutputMakeup = 0.64f;  // -3.9dB

    // Real grid conduction: once a grid swings positive it draws current
    // and stops following the input. Positive side only, same reasoning
    // as this toolkit's other cascades without a diode clipper.
    float gridConduction(float x) noexcept { return x > 0.0f ? std::tanh(x) : x; }

    // Gain pot tapers, log (audio) as on the schematic: rolled down they
    // pass little, rolled up the whole signal. Expressed as drive into the
    // following tube. Ranges are judgment calls sized so the top of the
    // knob is genuinely saturated.
    float tinyPotADrive(float g) noexcept { return 0.1f * std::pow(150.0f, g); }
    float tinyPotBDrive(float g) noexcept { return 0.3f * std::pow(30.0f, g); }
    float fatPotADrive(float g) noexcept { return 0.15f * std::pow(200.0f, g); }
    float fatPotBDrive(float g) noexcept { return 0.3f * std::pow(20.0f, g); }
}

OrangeDualTerrorPreamp::OrangeDualTerrorPreamp(double sampleRateToUse)
    : oversampler(sampleRateToUse),
      sampleRate(sampleRateToUse)
{
    auto os = sampleRateToUse * 4.0;
    brightSwitchHighpass.setCutoff(os, brightSwitchHz);
    c1Highpass.setCutoff(os, tinyC1CutoffHz);
    potBrightHighpass.setCutoff(os, tinyPotBrightHz);
    c4Highpass.setCutoff(os, tinyC4CutoffHz);
    fatDcBlock.setCutoff(os, fatC21CutoffHz);

    KorenTriodeStage::Parameters gainStage;
    gainStage.inputToGridVolts = 5.0;
    gainStage.gridBias = -1.5; // 1.5k cathode resistor
    for (auto& s : tinyStages) s.setParameters(gainStage);
    fatStage.setParameters(gainStage);

    updateTone();
}

void OrangeDualTerrorPreamp::setChannel(Channel newChannel) noexcept { channel = newChannel; updateTone(); }
void OrangeDualTerrorPreamp::setGain(float amount) noexcept          { gain = clamp01(amount); }
void OrangeDualTerrorPreamp::setTone(float amount) noexcept          { tone = clamp01(amount); updateTone(); }
void OrangeDualTerrorPreamp::setVolume(float amount) noexcept        { volume = clamp01(amount); }
void OrangeDualTerrorPreamp::setBright(int position) noexcept        { bright = position < 0 ? 0 : (position > 2 ? 2 : position); }

float OrangeDualTerrorPreamp::getPotBrightAmount() const noexcept
{
    return maxPotBrightAmount * (1.0f - gain);
}

void OrangeDualTerrorPreamp::updateTone() noexcept
{
    auto isTiny = channel == Channel::TinyTerror;
    auto farads = isTiny ? tinyToneFarads : fatToneFarads;
    auto source = isTiny ? tinyToneSource : fatToneSource;

    // Audio-taper pot: resistance in series with the cap. Tone=1 is the
    // brightest (pot fully in series, the cap barely bleeds anything);
    // Tone=0 shorts the pot out so the cap bridges the plates directly.
    auto rPot = tonePotOhms * static_cast<double>(tone) * static_cast<double>(tone);

    // H(s) = (1 + s*tz) / (1 + s*tp), exact for a cap+pot bridging a
    // source of impedance `source`.
    auto tz = rPot * farads;
    auto tp = (rPot + source) * farads;

    toneZeroHz = tz > 0.0 ? static_cast<float>(1.0 / (twoPi * tz)) : 0.0f;
    tonePoleHz = static_cast<float>(1.0 / (twoPi * tp));

    auto k = 2.0 * sampleRate; // bilinear
    auto d = 1.0 + k * tp;
    tb0 = (1.0 + k * tz) / d;
    tb1 = (1.0 - k * tz) / d;
    ta1 = (1.0 - k * tp) / d;
}

void OrangeDualTerrorPreamp::reset() noexcept
{
    brightSwitchHighpass.reset();
    c1Highpass.reset();
    potBrightHighpass.reset();
    c4Highpass.reset();
    fatDcBlock.reset();
    oversampler.reset();
    toneX1 = toneY1 = 0.0;
}

float OrangeDualTerrorPreamp::processTinyTerror(float x) noexcept
{
    // V1-A, then the 1nF coupling (the ~374Hz "punchy" highpass) and its
    // voltage divider.
    auto signal = tinyStages[0].processSample(gridConduction(x * firstStageDrive));
    signal = c1Highpass.processSample(signal) * tinyC1Passband * interStageMakeupGain;

    // RV1-A with its 100pF bright cap, then V1-B.
    signal += getPotBrightAmount() * potBrightHighpass.processSample(signal);
    signal *= tinyPotADrive(gain);
    signal = tinyStages[1].processSample(gridConduction(signal));

    // R8/C4 into RV1-B, then the splitter.
    signal = c4Highpass.processSample(signal) * tinyC4Passband * interStageMakeupGain;
    signal *= tinyPotBDrive(gain);
    return std::tanh(signal); // long-tailed-pair splitter: symmetric
}

float OrangeDualTerrorPreamp::processFat(float x) noexcept
{
    // Gain pot at the input, straight into V3-A. V3-B is a cathode
    // follower direct-coupled off V3-A's plate: unity gain, so it adds
    // no DSP here - what it changes is that nothing strips bass.
    auto signal = fatStage.processSample(gridConduction(x * fatPotADrive(gain)));
    signal *= interStageMakeupGain;

    // R22/C21 into RV4-B (68nF: lows pass untouched), then the splitter.
    signal = fatDcBlock.processSample(signal) * fatC21Passband * interStageMakeupGain;
    signal *= fatPotBDrive(gain);
    return std::tanh(signal);
}

float OrangeDualTerrorPreamp::processOversampledSample(float x) noexcept
{
    // OR60 Bright switch, ahead of the first tube.
    x += brightAmounts[bright] * brightSwitchHighpass.processSample(x);

    return channel == Channel::TinyTerror ? processTinyTerror(x) : processFat(x);
}

void OrangeDualTerrorPreamp::processBlock(const float* input, float* output, int numSamples) noexcept
{
    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });

    auto makeup = channel == Channel::TinyTerror ? tinyOutputMakeup : fatOutputMakeup;

    for (int n = 0; n < numSamples; ++n)
    {
        // Tone: first-order shelf, y = b0*x + b1*x1 - a1*y1.
        auto x = static_cast<double>(output[n]);
        auto y = tb0 * x + tb1 * toneX1 - ta1 * toneY1;
        toneX1 = x;
        toneY1 = y;

        output[n] = static_cast<float>(y) * volume * makeup;
    }
}
