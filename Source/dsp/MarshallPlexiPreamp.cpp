#include "MarshallPlexiPreamp.h"

#include <algorithm>
#include <cmath>

#include "PotTaper.h"

namespace
{
    using Net = NodalNetwork;
    namespace T = TriodeSection;

    float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // Every plate load in the preamp is 100k (V1 x2, V2-A). A plate is its
    // open-circuit voltage behind rp || 100k; the network it drives is what
    // loads it.
    constexpr double plateLoad = 100.0e3;
    constexpr double plateSource = T::parallel(plateLoad, T::rp);
    constexpr double stageGain = T::mu * plateLoad / (T::rp + plateLoad); // 61.5, unloaded

    // Miller capacitance at a 12AX7 grid (Cgp * (1 + gain) plus wiring): a
    // judgment call, not on the drawing. It is what makes Channel II's 470k
    // into V2-A's grid a ~4kHz low-pass, and what Channel I's 500pF bypasses.
    constexpr double gridCapacitance = 100.0e-12;

    // Supply nodes, from the c.1967 drawing (the July 1970 one prints none):
    // +270V at V1's plate loads, +300V at V2's.
    constexpr double v1Supply = 270.0;
    constexpr double v2Supply = 300.0;

    // The drawn cathode resistors and bypasses.
    constexpr double v1NormalCathodeOhms = 820.0;      // 250uF: inaudibly small corner, not modeled
    constexpr double v1HighTrebleCathodeOhms = 2.7e3;
    constexpr double v1HighTrebleBypassF = 0.68e-6;
    constexpr double v2aCathodeOhms = 820.0;
    constexpr double v2aBypassF = 0.68e-6;              // "1K" on later units

    // V2-B, the cathode follower: 100k cathode resistor, plate to the supply.
    // Unloaded it is a gain of mu*Rk / (rp + (mu+1)*Rk) behind rp/(mu+1) || Rk;
    // the tone stack that loads it is part of the network, so only the open-
    // circuit gain goes here. (Its output resistance is in MarshallToneStack's
    // Components::sourceOhms.)
    constexpr double followerCathodeOhms = 100.0e3;
    constexpr double followerOpenGain = T::mu * followerCathodeOhms / (T::rp + (T::mu + 1.0) * followerCathodeOhms);

    // The follower bootstraps its own grid: the cathode follows the grid with
    // a gain A, so the grid-to-cathode voltage only moves by (1 - A) times the
    // signal. Its grid starts to conduct when that reaches the conduction
    // margin above its resting grid bias. With the stack loading the cathode
    // (a typical ~40k), A is about 0.97:
    constexpr double followerLoadedOhms = T::parallel(followerCathodeOhms, 40.0e3);
    constexpr double followerLoadedGain = T::mu * followerLoadedOhms / (T::rp + (T::mu + 1.0) * followerLoadedOhms);
    constexpr double gridConductionMargin = 0.3;

    // 1M audio-taper pots: assumed 15% at 12 o'clock.
    constexpr double volumeTaper = 0.15;
}

MarshallPlexiPreamp::MarshallPlexiPreamp(double sampleRateToUse)
    : oversampler(sampleRateToUse),
      sampleRate(sampleRateToUse),
      oversampledRate(sampleRateToUse * 4.0),
      stack(sampleRateToUse * 4.0)
{
    operating.v1Normal = T::solveCathodeBiased(v1Supply, plateLoad, v1NormalCathodeOhms);
    operating.v1HighTreble = T::solveCathodeBiased(v1Supply, plateLoad, v1HighTrebleCathodeOhms);
    operating.v2a = T::solveCathodeBiased(v2Supply, plateLoad, v2aCathodeOhms);
    operating.cathodeFollower = T::solveCathodeFollower(v2Supply, operating.v2a.plateVolts, followerCathodeOhms);

    v1Normal.configure(operating.v1Normal, stageGain);
    v1HighTreble.configure(operating.v1HighTreble, stageGain);
    v2a.configure(operating.v2a, stageGain);

    followerGain = followerOpenGain;
    auto restingGrid = std::min(operating.cathodeFollower.gridBias, 0.0);
    followerKnee = std::min(60.0, std::max(8.0, (gridConductionMargin - restingGrid) / (1.0 - followerLoadedGain)));
    operating.followerKneeVolts = followerKnee;

    buildFront(frontI, true);
    buildFront(frontII, false);

    configureShelves();
    updateComponents();
    dirty = false;
}

// The mixer, as drawn, with the driven channel's plate as the source. The
// other channel's plate is grounded through its plate resistance - that is
// what its tube looks like to a signal from the other one.
void MarshallPlexiPreamp::buildFront(Front& front, bool drivesChannelI)
{
    auto& n = front.net;
    auto pI = n.addNode(), aI = n.addNode(), wI = n.addNode();
    auto pII = n.addNode(), aII = n.addNode(), wII = n.addNode();
    auto g = n.addNode();

    n.addResistor(drivesChannelI ? Net::source : Net::ground, pI, plateSource);
    n.addResistor(drivesChannelI ? Net::ground : Net::source, pII, plateSource);

    // Channel I (High Treble): .0022uF into the 1M Volume pot, a .005uF bright
    // cap across its upper half, the wiper through 470k || 500pF to the grid.
    n.addCapacitor(pI, aI, 0.0022e-6);
    front.volIUpper = n.addResistor(aI, wI, 500.0e3);
    front.volILower = n.addResistor(wI, Net::ground, 500.0e3);
    n.addCapacitor(aI, wI, 0.005e-6);
    n.addResistor(wI, g, 470.0e3);
    n.addCapacitor(wI, g, 500.0e-12);

    // Channel II (Normal): .022uF into its Volume pot, the wiper through 470k.
    n.addCapacitor(pII, aII, 0.022e-6);
    front.volIIUpper = n.addResistor(aII, wII, 500.0e3);
    front.volIILower = n.addResistor(wII, Net::ground, 500.0e3);
    n.addResistor(wII, g, 470.0e3);

    n.addCapacitor(g, Net::ground, gridCapacitance);

    front.plateI = pI; front.plateII = pII; front.out = g;
    n.prepare(oversampledRate);
}

void MarshallPlexiPreamp::setRouting(Routing newRouting) noexcept { routing = newRouting; }
void MarshallPlexiPreamp::setLowInput(bool useLowJack) noexcept { lowInput = useLowJack; }

// A pedal sets every knob on every audio block; only an actual change should
// cost a rebuild of the networks' matrices.
void MarshallPlexiPreamp::setKnob(float& slot, float position) noexcept
{
    auto v = clamp01(position);
    if (slot != v)
    {
        slot = v;
        dirty = true;
    }
}

void MarshallPlexiPreamp::setVolumeI(float p) noexcept  { setKnob(volumeI, p); }
void MarshallPlexiPreamp::setVolumeII(float p) noexcept { setKnob(volumeII, p); }
void MarshallPlexiPreamp::setTreble(float p) noexcept   { setKnob(treble, p); }
void MarshallPlexiPreamp::setMiddle(float p) noexcept   { setKnob(middle, p); }
void MarshallPlexiPreamp::setBass(float p) noexcept     { setKnob(bass, p); }

void MarshallPlexiPreamp::setV2aBypassFarads(double farads) noexcept
{
    v2aBypassOverride = farads;
    configureShelves();
}

void MarshallPlexiPreamp::configureShelves() noexcept
{
    shelfHighTreble.configure(oversampledRate, v1HighTrebleCathodeOhms, v1HighTrebleBypassF);
    shelfV2a.configure(oversampledRate, v2aCathodeOhms, v2aBypassOverride > 0.0 ? v2aBypassOverride : v2aBypassF);
}

void MarshallPlexiPreamp::updateComponents() noexcept
{
    auto volI = potFraction(volumeI, volumeTaper);
    auto volII = potFraction(volumeII, volumeTaper);

    for (auto* f : { &frontI, &frontII })
    {
        f->net.setResistance(f->volIUpper, 1.0e6 * (1.0 - volI));
        f->net.setResistance(f->volILower, 1.0e6 * volI);
        f->net.setResistance(f->volIIUpper, 1.0e6 * (1.0 - volII));
        f->net.setResistance(f->volIILower, 1.0e6 * volII);
        f->net.refresh();
    }

    stack.setControls(treble, bass, middle);
}

void MarshallPlexiPreamp::reset() noexcept
{
    frontI.net.reset();
    frontII.net.reset();
    stack.reset();
    shelfHighTreble.reset();
    shelfV2a.reset();
    oversampler.reset();
}

float MarshallPlexiPreamp::processOversampledSample(float xIn) noexcept
{
    processToStack(static_cast<double>(xIn));
    return static_cast<float>(finishStack(0.0));
}

double MarshallPlexiPreamp::processToStack(double x) noexcept
{
    auto track = [this](double& peak, double v) { if (testPointsEnabled) peak = std::max(peak, std::abs(v)); };

    // Grid voltages at the two V1 halves. The input networks' 68k stoppers and
    // 1M leaks are invisible at these frequencies; what shows is the Low
    // jack's 6dB pad.
    auto jack = lowInput ? 0.5 : 1.0;
    double gridI = 0.0, gridII = 0.0;
    switch (routing)
    {
        case Routing::ChannelI:   gridI = x * jack; break;
        case Routing::ChannelII:  gridII = x * jack; break;
        case Routing::Jumpered:   gridI = x * jack; gridII = x; break;
    }

    auto plateI = v1HighTreble.process(shelfHighTreble.process(gridI));
    auto plateII = v1Normal.process(gridII);

    frontI.net.process(plateI);
    frontII.net.process(plateII);
    track(testPoints.plateI, frontI.net.voltage(frontI.plateI));
    track(testPoints.plateII, frontII.net.voltage(frontII.plateII));

    auto grid = frontI.net.voltage(frontI.out) + frontII.net.voltage(frontII.out);
    track(testPoints.v2aGrid, grid);

    auto plateA = v2a.process(shelfV2a.process(grid));
    track(testPoints.v2aPlate, plateA);

    // V2-B: the cathode follower's grid is V2-A's plate. The tone stack's output
    // node is the caller's to finish.
    return stack.beginSample(followerTransfer(plateA));
}

double MarshallPlexiPreamp::finishStack(double ampsDrawn) noexcept
{
    auto out = stack.finishSample(ampsDrawn);
    if (testPointsEnabled)
        testPoints.stackOutput = std::max(testPoints.stackOutput, std::abs(out));
    return out;
}

void MarshallPlexiPreamp::refreshControls() noexcept
{
    if (dirty)
    {
        updateComponents();
        dirty = false;
    }
}

void MarshallPlexiPreamp::processBlock(const float* input, float* output, int numSamples) noexcept
{
    refreshControls();

    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
