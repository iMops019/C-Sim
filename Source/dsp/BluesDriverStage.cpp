#include "BluesDriverStage.h"

#include "PotTaper.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double thermalVoltage = 0.02585;

    // How long a knob takes to glide to a new position (about 2.2 x this to cover 90% of the way).
    constexpr double smoothingSeconds = 0.008;
}

// -------------------------------------------------------------------------
// Pots and small pieces
// -------------------------------------------------------------------------

double BluesDriverStage::gainRheostatOhms(const Components& c, double gain) noexcept
{
    return c.gainPot * potFraction(gain, c.gainPotMid);
}

double BluesDriverStage::levelUpperOhms(const Components& c, double level) noexcept
{
    return (1.0 - potFraction(level, c.levelPotMid)) * c.levelPot;
}

double BluesDriverStage::levelLowerOhms(const Components& c, double level) noexcept
{
    return potFraction(level, c.levelPotMid) * c.levelPot;
}

double BluesDriverStage::followerPiOhms(const Components& c) noexcept
{
    auto gm = c.followerAmps / thermalVoltage;
    return c.followerBeta / gm;
}

double BluesDriverStage::outputGain(const Components& c) noexcept
{
    auto shunt = 1.0 / (1.0 / c.outputShuntR + 1.0 / c.loadOhms);
    return shunt / (c.outputSeriesR + shunt);
}

// -------------------------------------------------------------------------
// The linear networks
// -------------------------------------------------------------------------

BluesDriverStage::Network BluesDriverStage::makeStackNetwork(const Components& c)
{
    Network n;
    auto& net = n.net;
    auto x = net.addNode(), m = net.addNode(), o = net.addNode(), p = net.addNode(), y = net.addNode();

    net.addResistor(MnaNetwork::source, x, c.r37);
    net.addCapacitor(x, o, c.c26);
    net.addResistor(MnaNetwork::source, m, c.r38);
    net.addCapacitor(m, o, c.c34);
    net.addCapacitor(m, p, c.c35);
    net.addResistor(o, p, c.r50);
    net.addResistor(p, MnaNetwork::ground, c.r51);

    // Stage 2's gate: a coupling capacitor and its bias resistor load the network.
    net.addCapacitor(o, y, c.c27);
    net.addResistor(y, MnaNetwork::ground, c.r35);
    n.out = y;
    return n;
}

BluesDriverStage::ToneNetwork BluesDriverStage::makeToneNetwork(const Components& c)
{
    ToneNetwork n;
    auto& net = n.net;
    auto b = net.addNode(), t = net.addNode(), w = net.addNode(), bottom = net.addNode(), l = net.addNode(), q = net.addNode();

    // The fixed shelf: R26 || C17 in series, C19 to ground.
    net.addResistor(MnaNetwork::source, b, c.r26);
    net.addCapacitor(MnaNetwork::source, b, c.c17);
    net.addCapacitor(b, MnaNetwork::ground, c.c19);

    // Tone: C100 into the top of the pot; the wiper is the output; the bottom of the pot goes through C101.
    net.addCapacitor(b, t, c.c100);
    n.toneUpper = net.addResistor(t, w, 1.0);
    n.toneLower = net.addResistor(w, bottom, 1.0);
    net.addCapacitor(bottom, MnaNetwork::ground, c.c101);

    // Level loads the Tone wiper; its wiper feeds the peak filter's coupling capacitor.
    n.levelUpper = net.addResistor(w, l, 1.0);
    n.levelLower = net.addResistor(l, MnaNetwork::ground, 1.0);
    net.addCapacitor(l, q, c.c10);
    net.addResistor(q, MnaNetwork::ground, c.r13);
    n.out = q;
    return n;
}

BluesDriverStage::Network BluesDriverStage::makePeakNetwork(const Components& c)
{
    Network n;
    auto& net = n.net;
    auto minus = net.addNode(), out = net.addNode(), j = net.addNode(), base = net.addNode(), emitter = net.addNode();

    // A non-inverting op-amp with R9 || C8 in its feedback...
    net.addResistor(out, minus, c.r9);
    net.addCapacitor(out, minus, c.c8);
    net.addIdealOpAmp(MnaNetwork::source, minus, out);

    // ...and, in its ground leg, C9 in series with the gyrator that behaves as a ~32H inductor:
    // a capacitor from node J to an emitter follower's base (biased by R10), R21 from J to its emitter.
    net.addCapacitor(minus, j, c.c9);
    net.addCapacitor(j, base, c.gyratorC);
    net.addResistor(base, MnaNetwork::ground, c.gyratorBiasR);
    net.addResistor(j, emitter, c.r21);
    net.addResistor(emitter, MnaNetwork::ground, c.emitterR);

    // The follower's small-signal model: base current through r_pi, beta times that into the emitter.
    auto rPi = followerPiOhms(c);
    net.addResistor(base, emitter, rPi);
    net.addTransconductance(MnaNetwork::ground, emitter, base, emitter, c.followerBeta / rPi);
    n.out = out;
    return n;
}

// -------------------------------------------------------------------------
// A discrete amplifier stage
// -------------------------------------------------------------------------

namespace
{
    struct Limited { double value, slope; };

    // x / (1 + (x/L)^p)^(1/p): linear near zero, a ceiling at L, the knee's sharpness set by
    // p. Monotonic, with slope 1 / ((1 + r)^(1/p) * (1 + r)) where r = (x/L)^p. The two
    // pedal knees are the integers 4 and 3, which get a root instead of a general pow (this
    // runs several times per sample at the 4x rate).
    Limited softLimit(double x, double top, double bottom, double kneeTop, double kneeBottom) noexcept
    {
        auto limitValue = x >= 0.0 ? top : bottom;
        auto p = x >= 0.0 ? kneeTop : kneeBottom;
        auto s = std::abs(x) / limitValue;

        double r, root; // root = (1 + r)^(1/p)
        if (p == 4.0)      { auto s2 = s * s; r = s2 * s2;   root = std::sqrt(std::sqrt(1.0 + r)); }
        else if (p == 3.0) { r = s * s * s;                  root = std::cbrt(1.0 + r); }
        else               { r = std::pow(s, p);             root = std::pow(1.0 + r, 1.0 / p); }
        return { x / root, 1.0 / (root * (1.0 + r)) };
    }
}

double BluesDriverStage::DiscreteStage::limit(double x, double top, double bottom, double kneeTop, double kneeBottom) noexcept
{
    return softLimit(x, top, bottom, kneeTop, kneeBottom).value;
}

void BluesDriverStage::DiscreteStage::prepare(double sampleRate, const Components& c, double legOhms, double legFarads, double feedbackFarads)
{
    auto period = 1.0 / sampleRate;
    g22 = 2.0 * legFarads / period;
    g23 = 2.0 * feedbackFarads / period;
    legR = legOhms;

    top = c.railTopVolts;
    bottom = c.railBottomVolts;
    kneeTop = c.kneeTop;
    kneeBottom = c.kneeBottom;

    // Single-pole open loop: A(s) = A0 / (1 + s/wp), with A0 * wp = 2*pi*GBW.
    openLoopGain = c.openLoopGain;
    auto wp = 2.0 * pi * c.gainBandwidthHz / c.openLoopGain;
    poleK = wp * period / 2.0;
    reset();
}

void BluesDriverStage::DiscreteStage::reset() noexcept
{
    legVPrev = legIPrev = 0.0;
    feedVPrev = feedIPrev = 0.0;
    outPrev = errorPrev = 0.0;
}

double BluesDriverStage::DiscreteStage::process(double u) noexcept
{
    // The inverting input v- is a node between the ground leg (R + C in series to
    // Vref) and the feedback network (R || C from the output). Both are linear, so
    // v- is an affine function of the output, v- = alpha*out + beta, with the
    // capacitors as trapezoidal companion models.
    auto yLeg = g22 / (1.0 + g22 * legR);
    auto hLeg = (g22 * legVPrev + legIPrev) / (1.0 + g22 * legR);
    auto yFeed = 1.0 / feedbackR + g23;
    auto hFeed = g23 * feedVPrev + feedIPrev;
    auto alpha = yFeed / (yFeed + yLeg);
    auto beta = (hLeg - hFeed) / (yFeed + yLeg);

    // The amplifier: out = limit(pole(A0 * (v+ - v-))). The single pole's trapezoidal
    // update gives  pole = c*error + history  with error = u - v-.
    auto c = poleK * openLoopGain / (1.0 + poleK);
    auto history = ((1.0 - poleK) * outPrev + poleK * openLoopGain * errorPrev) / (1.0 + poleK);
    auto k = c * (u - beta) + history;
    auto loop = c * alpha;

    // Solve  out = limit(k - loop * out).  F(out) = out - limit(...) is strictly
    // increasing (slope >= 1) and changes sign across the rails, so a safeguarded
    // Newton iteration inside that bracket always converges.
    auto lo = -bottom, hi = top;
    auto out = std::clamp((k) / (1.0 + loop), lo, hi);
    for (int i = 0; i < 40; ++i)
    {
        auto w = k - loop * out;
        auto limited = softLimit(w, top, bottom, kneeTop, kneeBottom);
        auto f = out - limited.value;
        if (f > 0.0) hi = out; else lo = out;

        auto slope = 1.0 + loop * limited.slope;
        auto next = out - f / slope;
        if (! (next > lo && next < hi))
            next = 0.5 * (lo + hi);
        auto step = std::abs(next - out);
        out = next;
        if (step < 1.0e-10)
            break;
    }

    // Advance the states.
    auto vMinus = alpha * out + beta;
    auto iLeg = yLeg * vMinus - hLeg;
    legVPrev = vMinus - legR * iLeg;
    legIPrev = iLeg;

    auto vFeed = out - vMinus;
    feedIPrev = g23 * vFeed - hFeed;
    feedVPrev = vFeed;

    outPrev = out;
    errorPrev = u - vMinus;
    return out;
}

// -------------------------------------------------------------------------
// The stage
// -------------------------------------------------------------------------

BluesDriverStage::BluesDriverStage(double sampleRateToUse, Components components)
    : comp(components), sampleRate(sampleRateToUse), oversampler(sampleRateToUse)
{
    configure();
}

void BluesDriverStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampler = Oversampler4x(sampleRate);
    configure();
}

void BluesDriverStage::configure()
{
    auto rate = sampleRate * 4.0; // the whole circuit runs at the oversampled rate

    inputHighPass.design(rate, comp.inputCouplingC * comp.inputBiasR);
    stageHighPass.design(rate, comp.stageCouplingC * comp.stageBiasR);

    stage1.prepare(rate, comp, comp.s1LegR, comp.s1LegC, comp.s1FeedbackC);
    stage2.prepare(rate, comp, comp.s2LegR, comp.s2LegC, comp.s2FeedbackC);

    stack = makeStackNetwork(comp);
    stack.net.prepare(rate);
    toneNet = makeToneNetwork(comp);
    toneNet.net.prepare(rate);
    peak = makePeakNetwork(comp);
    peak.net.prepare(rate);

    outGain = outputGain(comp);

    smoothing = 1.0 - std::exp(-1.0 / (rate * smoothingSeconds));
    rheostatTarget = gainRheostatOhms(comp, gain);
    reset();
}

void BluesDriverStage::setGain(float amount)
{
    gain = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    rheostatTarget = gainRheostatOhms(comp, gain);
}

void BluesDriverStage::setTone(float amount)
{
    tone = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

void BluesDriverStage::setLevel(float amount)
{
    level = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

void BluesDriverStage::applyTone(double toneAmount) noexcept
{
    toneNet.net.setResistance(toneNet.toneUpper, toneUpperOhms(comp, toneAmount));
    toneNet.net.setResistance(toneNet.toneLower, toneLowerOhms(comp, toneAmount));
}

void BluesDriverStage::applyLevel(double levelAmount) noexcept
{
    toneNet.net.setResistance(toneNet.levelUpper, levelUpperOhms(comp, levelAmount));
    toneNet.net.setResistance(toneNet.levelLower, levelLowerOhms(comp, levelAmount));
}

void BluesDriverStage::reset() noexcept
{
    inputHighPass.reset();
    stageHighPass.reset();
    stage1.reset();
    stage2.reset();
    stack.net.reset();
    toneNet.net.reset();
    peak.net.reset();
    oversampler.reset();

    // Start on the knobs' values, not gliding toward them.
    rheostatNow = rheostatTarget;
    stage1.setFeedbackResistance(comp.s1FeedbackR + rheostatNow);
    stage2.setFeedbackResistance(comp.s2FeedbackR + rheostatNow);
    toneNow = tone;
    levelNow = level;
    applyTone(toneNow);
    applyLevel(levelNow);
    networkUpdateCounter = 0;
}

float BluesDriverStage::processOversampledSample(float x) noexcept
{
    // Glide the knobs toward their targets. Gain is a multiply-add per sample; Tone and Level
    // change the tone network's resistors, which re-solves it, so that is done every 32nd sample
    // (about 6kHz) and only while one of them is moving.
    rheostatNow += smoothing * (rheostatTarget - rheostatNow);
    stage1.setFeedbackResistance(comp.s1FeedbackR + rheostatNow);
    stage2.setFeedbackResistance(comp.s2FeedbackR + rheostatNow);
    if (toneNow != tone || levelNow != level)
    {
        toneNow += smoothing * (tone - toneNow);
        levelNow += smoothing * (level - levelNow);
        if (std::abs(tone - toneNow) < 1.0e-6) toneNow = tone;
        if (std::abs(level - levelNow) < 1.0e-6) levelNow = level;
        if (++networkUpdateCounter >= 32 || (toneNow == tone && levelNow == level))
        {
            networkUpdateCounter = 0;
            applyTone(toneNow);
            applyLevel(levelNow);
        }
    }

    auto v = static_cast<double>(x) * comp.inputGain; // 1.0 = 1V at the jack
    v = inputHighPass.process(v);
    v = stageHighPass.process(v);

    v = stage1.process(v);
    stack.net.process(v);
    v = stage2.process(stack.net.voltage(stack.out));

    toneNet.net.process(v);
    peak.net.process(toneNet.net.voltage(toneNet.out));
    return static_cast<float>(peak.net.voltage(peak.out) * outGain);
}

void BluesDriverStage::processBlock(const float* input, float* output, int numSamples)
{
    oversampler.processBlock(input, output, numSamples,
                             [this](float x) { return processOversampledSample(x); });
}
