#include "TubeScreamerStage.h"

#include "PotTaper.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;

    // The op-amp's output can't leave its supply. A 4558 on 9V has roughly +-3V
    // to give around the 4.5V bias; the exact figure wasn't in the sources, so
    // this is a calibration - it is linear up to the knee and only matters for
    // an input far hotter than a guitar (the diodes hold the output to about
    // the input plus 0.6V otherwise).
    constexpr double railKneeVolts = 2.5;
    constexpr double railCeilingVolts = 3.5;

    double railLimit(double v) noexcept
    {
        auto a = std::abs(v);
        if (a <= railKneeVolts)
            return v;
        auto over = railCeilingVolts - railKneeVolts;
        return std::copysign(railKneeVolts + over * std::tanh((a - railKneeVolts) / over), v);
    }

    // Both Newton's exponentials and the tone pot's end stops need a floor.
    constexpr double minOhms = 1.0;

    // How long a knob takes to glide to a new position (about 2.2 x this to cover 90% of the way).
    constexpr double smoothingSeconds = 0.008;
}

// -------------------------------------------------------------------------
// The diode network
// -------------------------------------------------------------------------

double TubeScreamerStage::solveDiodePair(double gLinear, double b, double saturation, double nVt) noexcept
{
    // f(V) = gLinear*V + 2*Is*sinh(V/nVt) - b is odd and monotonic, and convex
    // for V > 0, so Newton started to the right of the root (for b > 0) steps
    // monotonically down onto it. Two upper bounds on the root are cheap: the
    // linear part alone (b/gLinear) and the diodes alone (nVt*asinh(b/2Is));
    // the smaller is the start.
    auto magnitude = std::abs(b);
    auto v = std::min(magnitude / gLinear, nVt * std::asinh(magnitude / (2.0 * saturation)));

    for (int i = 0; i < 40; ++i)
    {
        auto e = std::exp(v / nVt);
        auto einv = 1.0 / e;
        auto f = gLinear * v + saturation * (e - einv) - magnitude;
        auto df = gLinear + saturation / nVt * (e + einv);
        auto step = f / df;
        v -= step;
        if (v < 0.0)
            v = 0.0;
        if (std::abs(step) < 1.0e-12)
            break;
    }

    return std::copysign(v, b);
}

// -------------------------------------------------------------------------
// The clipping stage
// -------------------------------------------------------------------------

void TubeScreamerStage::ClippingStage::prepare(double sampleRate, const Components& c)
{
    auto period = 1.0 / sampleRate;
    g3 = 2.0 * c.c3 / period;
    g4 = 2.0 * c.c4 / period;
    r4 = c.r4;
    diodeIs = c.diodeSaturation;
    diodeNVt = c.diodeIdeality * 0.02585;
    reset();
}

void TubeScreamerStage::ClippingStage::reset() noexcept
{
    vc3Prev = i3Prev = 0.0;
    vFeedbackPrev = i4Prev = 0.0;
}

double TubeScreamerStage::ClippingStage::process(double u) noexcept
{
    // The op-amp holds its inverting input at u (the non-inverting one), so
    // the 4.7k + 0.047uF leg to ground carries a current set by u alone. The
    // capacitor is the trapezoidal companion model: i = g*(v - vPrev) - iPrev.
    auto iLeg = (g3 * (u - vc3Prev) - i3Prev) / (1.0 + g3 * r4);
    vc3Prev = u - r4 * iLeg;
    i3Prev = iLeg;

    // That same current has to come through the feedback network - the resistor
    // (the 51k plus the Drive pot), the 51pF and the two diodes, in parallel.
    // Kirchhoff on the network's voltage V: V/R + C dV/dt + Id(V) = iLeg,
    // with the capacitor's history moved to the right-hand side.
    auto gLinear = 1.0 / rFeedback + g4;
    auto b = iLeg + g4 * vFeedbackPrev + i4Prev;
    auto v = solveDiodePair(gLinear, b, diodeIs, diodeNVt);

    i4Prev = g4 * (v - vFeedbackPrev) - i4Prev;
    vFeedbackPrev = v;

    return u + v;
}

// -------------------------------------------------------------------------
// The tone stage
// -------------------------------------------------------------------------

TubeScreamerStage::ToneStage TubeScreamerStage::toneStage(const Components& c, double tone) noexcept
{
    // Netlist: the clipper's output feeds R7 into node P, the second op-amp's
    // non-inverting input. P also has C5 and R9 to ground (AC) and the Tone pot
    // from P to its wiper W; the wiper reaches ground through R8 + C6; the pot's
    // other end is the inverting input N, which R11 ties back to the output.
    // With an ideal op-amp (vN = vP) the wiper node gives
    //     vW = vP * G / (G + Yz)     G = 1/Rl + 1/Rr,  Yz = 1/(R8 + 1/sC6)
    // and the pot's own current is an extra load on P, so the pre-filter and the
    // treble lift are one interacting system. Working it through (the common
    // first-order factor (G + s*C6*b) cancels):
    //     b  = 1 + G*R8
    //     n0 = G/R7                         n1 = C6*(b + R11/Rr)/R7
    //     d0 = g0*G                         g0 = 1/R7 + 1/R9
    //     d1 = g0*C6*b + C5*G + C6/Rl       d2 = C5*C6*b
    // At tone = 1 (wiper at N, Rr -> 0) this becomes the closed form
    // 1 + R11*sC6/(1 + sC6*R8), a 593Hz zero and 3.3kHz pole; at tone = 0 the
    // op-amp is a plain buffer of the low-passed input.
    auto rl = std::max(tone * c.tonePot, minOhms);
    auto rr = std::max((1.0 - tone) * c.tonePot, minOhms);
    auto g = 1.0 / rl + 1.0 / rr;
    auto b = 1.0 + g * c.r8;
    auto g0 = 1.0 / c.r7 + 1.0 / c.r9;

    ToneStage t;
    t.n0 = g / c.r7;
    t.n1 = c.c6 * (b + c.r11 / rr) / c.r7;
    t.d0 = g0 * g;
    t.d1 = g0 * c.c6 * b + c.c5 * g + c.c6 / rl;
    t.d2 = c.c5 * c.c6 * b;
    return t;
}

// -------------------------------------------------------------------------
// Small pieces
// -------------------------------------------------------------------------

double TubeScreamerStage::driveFeedbackOhms(const Components& c, double drive) noexcept
{
    return c.r6 + c.drivePot * potFraction(drive, c.driveTaperMid);
}

void TubeScreamerStage::HighPass::design(double sampleRate, double tau) noexcept
{
    // Bilinear transform of s*tau / (1 + s*tau).
    auto k = 2.0 * tau * sampleRate;
    a = k / (k + 1.0);
    p = (k - 1.0) / (k + 1.0);
}

std::complex<double> TubeScreamerStage::smallSignalResponse(const Components& c, double drive, double tone, double level, double frequencyHz, bool diodeLeakage)
{
    using cd = std::complex<double>;
    auto s = cd(0.0, twoPi * frequencyHz);

    auto highPass = [&](double tau) { return (s * tau) / (1.0 + s * tau); };

    auto rFeedback = driveFeedbackOhms(c, drive);
    auto gDiodes = diodeLeakage ? 2.0 * c.diodeSaturation / (c.diodeIdeality * 0.02585) : 0.0;
    auto zf = 1.0 / (1.0 / rFeedback + gDiodes + s * c.c4);
    auto zg = c.r4 + 1.0 / (s * c.c3);
    auto clip = 1.0 + zf / zg;

    auto t = toneStage(c, tone);
    auto toneH = (t.n0 + t.n1 * s) / (t.d0 + t.d1 * s + t.d2 * s * s);

    auto rOut = 1.0 / (1.0 / c.rc + 1.0 / c.loadOhms);
    auto divider = rOut / (c.rb + rOut);

    return highPass(c.c1 * c.inputOhms) * highPass(c.c2 * c.r5) * clip * toneH
         * (level * divider) * highPass(c.c9 * (c.rb + rOut));
}

// -------------------------------------------------------------------------
// The stage
// -------------------------------------------------------------------------

TubeScreamerStage::TubeScreamerStage(double sampleRateToUse, Components components)
    : comp(components), sampleRate(sampleRateToUse), oversampler(sampleRateToUse)
{
    configure();
}

void TubeScreamerStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampler = Oversampler4x(sampleRate);
    configure();
}

void TubeScreamerStage::configure()
{
    auto rate = sampleRate * 4.0; // the whole circuit runs at the oversampled rate

    inputHighPass.design(rate, comp.c1 * comp.inputOhms);
    couplingHighPass.design(rate, comp.c2 * comp.r5);

    auto rOut = 1.0 / (1.0 / comp.rc + 1.0 / comp.loadOhms);
    outputHighPass.design(rate, comp.c9 * (comp.rb + rOut));
    outputGain = rOut / (comp.rb + rOut);

    clipper.prepare(rate, comp);
    smoothing = 1.0 - std::exp(-1.0 / (rate * smoothingSeconds));
    feedbackTarget = driveFeedbackOhms(comp, drive);
    reset();
}

void TubeScreamerStage::setDrive(float amount)
{
    drive = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    feedbackTarget = driveFeedbackOhms(comp, drive);
}

void TubeScreamerStage::setTone(float amount)
{
    tone = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

void TubeScreamerStage::setLevel(float amount)
{
    level = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

void TubeScreamerStage::designTone(double toneAmount)
{
    // Bilinear transform of (n0 + n1 s)/(d0 + d1 s + d2 s^2), s = k(1-z^-1)/(1+z^-1).
    auto t = toneStage(comp, toneAmount);
    auto k = 2.0 * sampleRate * 4.0;
    auto k2 = k * k;

    auto a0 = t.d0 + t.d1 * k + t.d2 * k2;
    toneFilter.b0 = (t.n0 + t.n1 * k) / a0;
    toneFilter.b1 = (2.0 * t.n0) / a0;
    toneFilter.b2 = (t.n0 - t.n1 * k) / a0;
    toneFilter.a1 = (2.0 * t.d0 - 2.0 * t.d2 * k2) / a0;
    toneFilter.a2 = (t.d0 - t.d1 * k + t.d2 * k2) / a0;
}

void TubeScreamerStage::reset() noexcept
{
    inputHighPass.reset();
    couplingHighPass.reset();
    outputHighPass.reset();
    clipper.reset();
    toneFilter.reset();
    oversampler.reset();

    // Start on the knobs' values, not gliding toward them.
    feedbackNow = feedbackTarget;
    clipper.setFeedbackResistance(feedbackNow);
    toneNow = tone;
    levelNow = level;
    toneUpdateCounter = 0;
    designTone(toneNow);
}

float TubeScreamerStage::processOversampledSample(float x) noexcept
{
    // Glide the knobs toward their targets. Drive and Level are one multiply-add each; Tone
    // needs its filter redesigned, which is done every 16th sample while it is moving.
    feedbackNow += smoothing * (feedbackTarget - feedbackNow);
    clipper.setFeedbackResistance(feedbackNow);
    levelNow += smoothing * (level - levelNow);
    if (toneNow != tone)
    {
        toneNow += smoothing * (tone - toneNow);
        if (std::abs(tone - toneNow) < 1.0e-6)
            toneNow = tone;
        if (++toneUpdateCounter >= 16 || toneNow == tone)
        {
            toneUpdateCounter = 0;
            designTone(toneNow);
        }
    }

    auto v = static_cast<double>(x); // 1.0 = 1V at the jack
    v = inputHighPass.process(v);
    v = couplingHighPass.process(v);

    v = railLimit(clipper.process(v));
    v = railLimit(toneFilter.process(v));

    v = outputHighPass.process(v * levelNow * outputGain);
    return static_cast<float>(v);
}

void TubeScreamerStage::processBlock(const float* input, float* output, int numSamples)
{
    oversampler.processBlock(input, output, numSamples,
                             [this](float x) { return processOversampledSample(x); });
}
