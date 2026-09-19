#include "SuperSonic22Preamp.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.28318530717958647692;
    using Net = NodalNetwork;

    float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    constexpr double parallel(double a, double b) noexcept { return a * b / (a + b); }

    // ---- The tubes (12AX7A, all four sections). ----

    // Datasheet plate resistance - the project's usual number, not on the
    // schematic - and the schematic's 100k plate load (R3, R15, R26, R40).
    // A plate is modeled as its open-circuit voltage behind this resistance;
    // the network it drives is what loads it.
    constexpr double tubeMu = 100.0;
    constexpr double tubeRp = 62.5e3;
    constexpr double plateSource = parallel(100.0e3, tubeRp); // ~38.5k

    // Judgment call: the dynamic input capacitance a 12AX7 grid presents
    // (Miller effect at a gain near 50, plus wiring).
    constexpr double gridCapacitance = 100.0e-12;

    // The small-signal voltage gain of an unloaded section: mu * RL/(rp+RL)
    // = 61.5 from the datasheet numbers above. Not tuned - the diagram's
    // Vintage test points land within about 1.5dB with it (see the header).
    constexpr double stageGain = tubeMu * 100.0e3 / (tubeRp + 100.0e3);

    // Operating points, from the diagram's DC test points: the cathode
    // voltages (TP15 1.35V, TP17 1.35V, TP20 1.58V, TP22 1.50V) across the
    // 1.5k cathode resistors give the bias and the plate current, and the
    // rail each plate load hangs off (V1: +280V, V2: +317V) minus the drop
    // across 100k gives the plate voltage.
    struct Bias { double grid, plate; };
    constexpr Bias v1aBias { -1.35, 280.0 - 100.0e3 * (1.35 / 1.5e3) };
    constexpr Bias v1bBias { -1.35, 280.0 - 100.0e3 * (1.35 / 1.5e3) };
    constexpr Bias v2aBias { -1.58, 317.0 - 100.0e3 * (1.58 / 1.5e3) };
    constexpr Bias v2bBias { -1.50, 317.0 - 100.0e3 * (1.50 / 1.5e3) };

    // Where the grid starts to conduct, above the 0V a cathode-biased grid
    // reaches at the bias voltage: positive swings past about +0.3V are
    // clamped by grid current through the source impedance.
    constexpr double gridConductionMargin = 0.3;

    // ---- Cathode bypass. ----
    // 1.5k cathode resistor on all four sections. V1-A and V2-A have 22uF
    // (a corner below 5Hz - inaudible, not modeled). V1-B and V2-B have 2.2uF
    // (C8, C13), and a second 22uF (C19, C21) that Burn switches in parallel
    // (Vintage leaves it behind 100k, i.e. effectively out).
    constexpr double cathodeOhms = 1.5e3;
    constexpr double vintageCathodeFarads = 2.2e-6;
    constexpr double burnCathodeFarads = 2.2e-6 + 22.0e-6;

    // ---- Vintage output. ----
    // V2-B's plate load in Vintage: R109 (1M) || R28+R29 (100k+15k) ||
    // R24+R19 (470k+120k, the feedback resistor to ground through the
    // Normal-mode divider).
    constexpr double vintageLoad = parallel(1.0e6, parallel(115.0e3, 590.0e3));
    constexpr double vintageLoadFactor = vintageLoad / (vintageLoad + plateSource);
    constexpr double vintageDivider = 15.0e3 / (100.0e3 + 15.0e3); // R29 / (R28 + R29)
    // C14 (.22uF) into that load.
    constexpr double vintageC14Hz = 1.0 / (twoPi * (vintageLoad + plateSource) * 0.22e-6);

    // ---- Fat's feedback loop. ----
    // R24 (470k) feeds V2-B's plate back to the node where R20 (47k) brings
    // the signal in and R19 (120k) shunts it to ground; C12 (.01uF) couples
    // that node to the grid, whose leak is R21 (4.7M) || R25 (470k). Midband
    // (every coupling cap a short) the fraction of the plate voltage that
    // reaches the grid is:
    constexpr double fatSeries = plateSource + 47.0e3;
    constexpr double fatShunt = parallel(120.0e3, parallel(4.7e6, 470.0e3));
    constexpr double fatFeedback = (1.0 / 470.0e3) / (1.0 / fatSeries + 1.0 / 470.0e3 + 1.0 / fatShunt);

    // Tiny capacitance standing in for "switched out".
    constexpr double switchedOut = 1.0e-15;

    // A pot's resistance fraction at rotation p, for a taper that reaches
    // `mid` of its total at 12 o'clock: the standard exponential audio-taper
    // model. The schematic's codes are 15A (15% at 12 o'clock), 30A (30%)
    // and B (linear).
    double potFraction(double p, double mid) noexcept
    {
        p = std::min(1.0, std::max(0.0, p));
        if (mid >= 0.499)
            return p;
        auto a = std::pow(1.0 / mid - 1.0, 2.0);
        return (std::pow(a, p) - 1.0) / (a - 1.0);
    }

    // The Vintage stack, shared by both front ends (in Burn it stays
    // connected to V1-A's plate and loads it): V1-A's plate (through its
    // source resistance) -> 220pF shunt (Fat only) -> C4/R8 into the
    // blackface stack -> the treble pot's wiper at W. C2 and R5 (the Burn
    // path's first coupling) hang off the same plate in both channels.
    struct Stack { int plate, wiper, a0, c3, r9Upper, r9Lower, r10; };

    Stack addVintageStack(Net& net)
    {
        Stack s {};
        auto p = net.addNode(), slope = net.addNode(), t = net.addNode(), x = net.addNode(), y = net.addNode(), w = net.addNode();
        auto a0 = net.addNode();

        net.addResistor(Net::source, p, plateSource);
        s.c3 = net.addCapacitor(p, Net::ground, 220.0e-12);           // C3
        net.addCapacitor(p, t, 250.0e-12);                             // C4, treble cap
        net.addResistor(p, slope, 100.0e3);                            // R8, slope resistor
        net.addCapacitor(slope, x, 0.1e-6);                            // C5, bass cap
        net.addCapacitor(slope, y, 0.047e-6);                          // C6
        s.r9Upper = net.addResistor(t, w, 125.0e3);                    // R9, treble pot: top to wiper
        s.r9Lower = net.addResistor(w, x, 125.0e3);                    //                 wiper to bottom
        s.r10 = net.addResistor(x, y, 37.5e3);                         // R10, bass pot as a rheostat
        net.addResistor(y, Net::ground, 6.8e3);                        // R11, the fixed "mid" resistor
        net.addCapacitor(p, a0, 2.2e-9);                               // C2
        net.addResistor(a0, Net::ground, 1.0e6);                       // R5

        s.plate = p; s.wiper = w; s.a0 = a0;
        return s;
    }
}

// ---- Stage ----

void SuperSonic22Preamp::Stage::configure(double gridBias, double plateVolts, double smallSignalGain)
{
    KorenTriodeStage::Parameters p;
    p.mu = tubeMu;
    p.gridBias = gridBias;
    p.plateVoltage = plateVolts;
    p.inputToGridVolts = std::abs(gridBias); // unit input = the grid swung up to 0V
    koren.setParameters(p);

    swingVolts = std::abs(gridBias);
    clampVolts = std::abs(gridBias) + gridConductionMargin;

    // The Koren stage is normalised (unit input -> unit output); scale it so
    // its small-signal slope is exactly the stage's real voltage gain. The
    // curve's slope is negative (a plate stage inverts), so use its size.
    constexpr float eps = 0.01f;
    auto slope = (static_cast<double>(koren.processSample(eps)) - static_cast<double>(koren.processSample(-eps))) / (2.0 * static_cast<double>(eps));
    scaleVolts = smallSignalGain * swingVolts / std::abs(slope);
}

double SuperSonic22Preamp::Stage::process(double gridVolts) const noexcept
{
    auto v = gridVolts > 0.0 ? clampVolts * std::tanh(gridVolts / clampVolts) : gridVolts;
    return static_cast<double>(koren.processSample(static_cast<float>(v / swingVolts))) * scaleVolts;
}

// ---- CathodeShelf ----

void SuperSonic22Preamp::CathodeShelf::configure(double sampleRate, double bypassFarads)
{
    // With the cathode resistor Rk bypassed by C, the stage gain is
    //   A(s) = A0 * (1 + s*Rk*C) / (N + s*Rk*C),   N = 1 + gm*Rk.
    // That equals A0 * (1 - (1 - 1/N) * LP(s)) with LP a one-pole low-pass
    // at N/(2*pi*Rk*C): full gain above it, 1/N of the gain far below.
    auto n = 1.0 + (tubeMu / tubeRp) * cathodeOhms;
    auto lowpassHz = n / (twoPi * cathodeOhms * bypassFarads);
    alpha = 1.0 - std::exp(-twoPi * lowpassHz / sampleRate);
    depth = 1.0 - 1.0 / n;
}

// ---- The amp ----

SuperSonic22Preamp::SuperSonic22Preamp(double sampleRateToUse)
    : oversampler(sampleRateToUse),
      sampleRate(sampleRateToUse),
      oversampledRate(sampleRateToUse * 4.0)
{
    v1a.configure(v1aBias.grid, v1aBias.plate, stageGain);
    v1b.configure(v1bBias.grid, v1bBias.plate, stageGain);
    v2a.configure(v2aBias.grid, v2aBias.plate, stageGain);
    v2b.configure(v2bBias.grid, v2bBias.plate, stageGain);

    vintageOutputHighpass.setCutoff(oversampledRate, vintageC14Hz);

    // Vintage front end: the stack, then the Volume pot with its bright
    // cap, loaded by V1-B's grid leak (R45) and grid capacitance.
    {
        auto stack = addVintageStack(vintageFront);
        auto wv = vintageFront.addNode();
        vfR12Upper = vintageFront.addResistor(stack.wiper, wv, 500.0e3);     // R12, Volume: top to wiper
        vfR12Lower = vintageFront.addResistor(wv, Net::ground, 500.0e3);     //              wiper to bottom
        vfC7 = vintageFront.addCapacitor(stack.wiper, wv, 47.0e-12);         // C7, bright cap (Normal only)
        vintageFront.addResistor(wv, Net::ground, 470.0e3);                  // R45
        vintageFront.addCapacitor(wv, Net::ground, gridCapacitance);
        vfP = stack.plate; vfOut = wv;
        vfC3 = stack.c3; vfR9Upper = stack.r9Upper; vfR9Lower = stack.r9Lower; vfR10 = stack.r10;
    }

    // Burn front end: the same stack still hanging off the plate (its Volume
    // pot's wiper is unconnected, leaving R12's full 1M to ground), and the
    // real Burn path: C2 -> R6 -> Gain 1 with C15 across it, into V1-B.
    {
        auto stack = addVintageStack(burnFront);
        burnFront.addResistor(stack.wiper, Net::ground, 1.0e6);              // R12, wiper open
        auto pg = burnFront.addNode(), wg = burnFront.addNode();
        burnFront.addResistor(stack.a0, pg, 15.0e3);                         // R6
        burnFront.addCapacitor(pg, Net::ground, 470.0e-12);                  // C15: a shunt to ground, not a bright cap
        bfR31Upper = burnFront.addResistor(pg, wg, 125.0e3);                 // R31, Gain 1
        bfR31Lower = burnFront.addResistor(wg, Net::ground, 125.0e3);
        burnFront.addResistor(wg, Net::ground, 470.0e3);                     // R45
        burnFront.addCapacitor(wg, Net::ground, gridCapacitance);
        bfP = stack.plate; bfOut = wg;
        bfC3 = stack.c3; bfR9Upper = stack.r9Upper; bfR9Lower = stack.r9Lower; bfR10 = stack.r10;
    }

    // Vintage, V1-B -> V2-B, Normal: C9 -> C10 -> the 3.3M/10pF over 270k
    // attenuator (R25 470k is V2-B's own grid leak).
    {
        auto p = interNormal.addNode(), c = interNormal.addNode(), n = interNormal.addNode(), q = interNormal.addNode();
        interNormal.addResistor(Net::source, p, plateSource);
        interNormal.addCapacitor(p, c, 0.22e-6);                             // C9
        interNormal.addResistor(c, Net::ground, 4.7e6);                      // R32
        interNormal.addCapacitor(c, n, 0.022e-6);                            // C10
        interNormal.addResistor(n, Net::ground, 10.0e6);                     // R18
        interNormal.addResistor(n, q, 3.3e6);                                // R22
        interNormal.addCapacitor(n, q, 10.0e-12);                            // C11
        interNormal.addResistor(q, Net::ground, 270.0e3);                    // R23
        interNormal.addResistor(q, Net::ground, 470.0e3);                    // R25
        interNormal.addCapacitor(q, Net::ground, gridCapacitance);
        inP = p; inOut = q;
    }

    // Vintage, V1-B -> V2-B, Fat: C9 -> R20 into the R19 shunt, C12 into the
    // grid. R24 is drawn here to ground - the feedback it carries is added
    // by the closed-loop table (see fatClosedLoop).
    {
        auto p = interFat.addNode(), c = interFat.addNode(), f = interFat.addNode(), g = interFat.addNode();
        interFat.addResistor(Net::source, p, plateSource);
        interFat.addCapacitor(p, c, 0.22e-6);                                // C9
        interFat.addResistor(c, Net::ground, 4.7e6);                         // R32
        interFat.addResistor(c, f, 47.0e3);                                  // R20
        interFat.addResistor(f, Net::ground, 120.0e3);                       // R19
        interFat.addResistor(f, Net::ground, 470.0e3);                       // R24, feedback source grounded
        interFat.addCapacitor(f, g, 0.01e-6);                                // C12
        interFat.addResistor(g, Net::ground, 4.7e6);                         // R21
        interFat.addResistor(g, Net::ground, 470.0e3);                       // R25
        interFat.addCapacitor(g, Net::ground, gridCapacitance);
        ifP = p; ifOut = g;
    }

    // Burn, V1-B -> V2-A: C9, C16 (.22uF each; R32 and R33 both 4.7M to
    // ground), Gain 2 with its R36/R38 bottom-end networks, R37 with the
    // 47pF bright cap C18 across the pot, R39 the grid stopper.
    {
        auto p = burnGain2.addNode(), c = burnGain2.addNode(), t2 = burnGain2.addNode(), w2 = burnGain2.addNode();
        auto b2 = burnGain2.addNode(), j = burnGain2.addNode(), g = burnGain2.addNode();
        burnGain2.addResistor(Net::source, p, plateSource);
        burnGain2.addCapacitor(p, c, 0.22e-6);                               // C9
        burnGain2.addResistor(c, Net::ground, 4.7e6);                        // R32
        burnGain2.addResistor(c, Net::ground, 4.7e6);                        // R33
        burnGain2.addCapacitor(c, t2, 0.22e-6);                              // C16
        g2R35Upper = burnGain2.addResistor(t2, w2, 125.0e3);                 // R35, Gain 2
        g2R35Lower = burnGain2.addResistor(w2, b2, 125.0e3);
        burnGain2.addResistor(b2, Net::ground, 20.0e3);                      // R36
        burnGain2.addResistor(w2, Net::ground, 10.0e3);                      // R38
        burnGain2.addResistor(w2, j, 110.0e3);                               // R37
        burnGain2.addCapacitor(t2, j, 47.0e-12);                             // C18
        burnGain2.addResistor(j, g, 1.5e3);                                  // R39
        burnGain2.addCapacitor(g, Net::ground, gridCapacitance);
        g2P = p; g2Out = g;
    }

    // Burn, V2-A -> V2-B: C20 into the 470k/100k divider (R25 the grid leak).
    {
        auto p = burnInter.addNode(), d = burnInter.addNode(), z = burnInter.addNode();
        burnInter.addResistor(Net::source, p, plateSource);
        burnInter.addCapacitor(p, d, 0.047e-6);                              // C20
        burnInter.addResistor(d, z, 470.0e3);                                // R42
        burnInter.addResistor(z, Net::ground, 100.0e3);                      // R43
        burnInter.addResistor(z, Net::ground, 470.0e3);                      // R25
        burnInter.addCapacitor(z, Net::ground, gridCapacitance);
        biP = p; biOut = z;
    }

    // Burn, V2-B -> the tone stack and Volume. C14 (.22uF) with R109/R122
    // (1M each) to ground, then the Marshall/Fender-family stack:
    //   input --C22-- treble pot top;   input --R46-- {C23 to the treble
    //   pot's bottom, C24 to the bass/mid junction};  bass pot from the
    //   treble pot's bottom down to the mid pot; the mid pot to ground.
    // The treble wiper goes through R50 (220k) into C25 (680pF) and the
    // Volume pot - a first-order low-pass near 2kHz whose exact corner
    // depends on the treble setting.
    {
        auto p = burnStack.addNode(), n0 = burnStack.addNode(), t = burnStack.addNode(), b = burnStack.addNode();
        auto m = burnStack.addNode(), p2 = burnStack.addNode(), wt = burnStack.addNode(), vv = burnStack.addNode();
        auto wo = burnStack.addNode();
        burnStack.addResistor(Net::source, p, plateSource);
        burnStack.addCapacitor(p, n0, 0.22e-6);                              // C14
        burnStack.addResistor(n0, Net::ground, 500.0e3);                     // R109 || R122
        burnStack.addCapacitor(n0, t, 150.0e-12);                            // C22
        burnStack.addResistor(n0, b, 120.0e3);                               // R46
        burnStack.addCapacitor(b, m, 0.15e-6);                               // C23
        burnStack.addCapacitor(b, p2, 0.022e-6);                             // C24
        bsR47Upper = burnStack.addResistor(t, wt, 125.0e3);                  // R47, Treble
        bsR47Lower = burnStack.addResistor(wt, m, 125.0e3);
        bsR48 = burnStack.addResistor(m, p2, 37.5e3);                        // R48, Bass
        bsR49 = burnStack.addResistor(p2, Net::ground, 12.5e3);              // R49, Mid
        burnStack.addResistor(wt, vv, 220.0e3);                              // R50
        burnStack.addCapacitor(vv, Net::ground, 680.0e-12);                  // C25
        bsR51Upper = burnStack.addResistor(vv, wo, 125.0e3);                 // R51, Volume
        bsR51Lower = burnStack.addResistor(wo, Net::ground, 125.0e3);
        burnStack.addResistor(wo, Net::ground, 1.0e6);                       // R30
        bsP = p; bsOut = wo;
    }

    for (auto* net : { &vintageFront, &burnFront, &interNormal, &interFat, &burnGain2, &burnInter, &burnStack })
        net->prepare(oversampledRate);

    configureCathodeShelves();
    buildFatTable();
    updateComponents();
    dirty = false;
}

void SuperSonic22Preamp::setChannel(Channel newChannel) noexcept
{
    if (channel == newChannel)
        return;
    channel = newChannel;
    configureCathodeShelves();
    resetNetworks();
    dirty = true;
}

void SuperSonic22Preamp::setFat(bool fatOn) noexcept
{
    if (fat == fatOn)
        return;
    fat = fatOn;
    interNormal.reset();
    interFat.reset();
    dirty = true;
}

// A pedal sets every knob on every audio block; only an actual change should
// cost a rebuild of the networks' matrices.
void SuperSonic22Preamp::setKnob(float& slot, float position) noexcept
{
    auto v = clamp01(position);
    if (slot != v)
    {
        slot = v;
        dirty = true;
    }
}

void SuperSonic22Preamp::setVintageVolume(float p) noexcept { setKnob(vintageVolume, p); }
void SuperSonic22Preamp::setVintageTreble(float p) noexcept { setKnob(vintageTreble, p); }
void SuperSonic22Preamp::setVintageBass(float p) noexcept   { setKnob(vintageBass, p); }
void SuperSonic22Preamp::setGain1(float p) noexcept         { setKnob(gain1, p); }
void SuperSonic22Preamp::setGain2(float p) noexcept         { setKnob(gain2, p); }
void SuperSonic22Preamp::setBurnTreble(float p) noexcept    { setKnob(burnTreble, p); }
void SuperSonic22Preamp::setBurnBass(float p) noexcept      { setKnob(burnBass, p); }
void SuperSonic22Preamp::setBurnMid(float p) noexcept       { setKnob(burnMid, p); }
void SuperSonic22Preamp::setBurnVolume(float p) noexcept    { setKnob(burnVolume, p); }

void SuperSonic22Preamp::setCathodeBypassOverride(double farads) noexcept
{
    cathodeOverrideFarads = farads;
    configureCathodeShelves();
}

void SuperSonic22Preamp::configureCathodeShelves() noexcept
{
    auto farads = cathodeOverrideFarads > 0.0 ? cathodeOverrideFarads
                                               : (channel == Channel::Burn ? burnCathodeFarads : vintageCathodeFarads);
    shelfV1B.configure(oversampledRate, farads);
    shelfV2B.configure(oversampledRate, farads);
}

void SuperSonic22Preamp::updateComponents() noexcept
{
    // The Vintage stack (Burn's front end carries a copy: it loads V1-A's
    // plate whichever channel is selected).
    auto trebleWiper = potFraction(vintageTreble, 0.30);
    auto bassOhms = 250.0e3 * potFraction(vintageBass, 0.15);
    auto c3 = fat ? 220.0e-12 : switchedOut;

    vintageFront.setResistance(vfR9Upper, 250.0e3 * (1.0 - trebleWiper));
    vintageFront.setResistance(vfR9Lower, 250.0e3 * trebleWiper);
    vintageFront.setResistance(vfR10, bassOhms);
    vintageFront.setCapacitance(vfC3, c3);
    auto volume = potFraction(vintageVolume, 0.30);
    vintageFront.setResistance(vfR12Upper, 1.0e6 * (1.0 - volume));
    vintageFront.setResistance(vfR12Lower, 1.0e6 * volume);
    vintageFront.setCapacitance(vfC7, fat ? switchedOut : 47.0e-12); // bright cap is Normal-only

    burnFront.setResistance(bfR9Upper, 250.0e3 * (1.0 - trebleWiper));
    burnFront.setResistance(bfR9Lower, 250.0e3 * trebleWiper);
    burnFront.setResistance(bfR10, bassOhms);
    burnFront.setCapacitance(bfC3, c3);
    auto g1 = potFraction(gain1, 0.15);
    burnFront.setResistance(bfR31Upper, 250.0e3 * (1.0 - g1));
    burnFront.setResistance(bfR31Lower, 250.0e3 * g1);

    auto g2 = potFraction(gain2, 0.15);
    burnGain2.setResistance(g2R35Upper, 250.0e3 * (1.0 - g2));
    burnGain2.setResistance(g2R35Lower, 250.0e3 * g2);

    auto bt = static_cast<double>(burnTreble); // 250kB: linear
    burnStack.setResistance(bsR47Upper, 250.0e3 * (1.0 - bt));
    burnStack.setResistance(bsR47Lower, 250.0e3 * bt);
    burnStack.setResistance(bsR48, 250.0e3 * potFraction(burnBass, 0.15));
    burnStack.setResistance(bsR49, 25.0e3 * static_cast<double>(burnMid)); // 25kB: linear
    auto bv = potFraction(burnVolume, 0.30);
    burnStack.setResistance(bsR51Upper, 250.0e3 * (1.0 - bv));
    burnStack.setResistance(bsR51Lower, 250.0e3 * bv);

    for (auto* net : { &vintageFront, &burnFront, &burnGain2, &burnStack })
        net->refresh();
}

void SuperSonic22Preamp::resetNetworks() noexcept
{
    for (auto* net : { &vintageFront, &burnFront, &interNormal, &interFat, &burnGain2, &burnInter, &burnStack })
        net->reset();
    shelfV1B.reset();
    shelfV2B.reset();
    vintageOutputHighpass.reset();
}

void SuperSonic22Preamp::reset() noexcept
{
    resetNetworks();
    oversampler.reset();
}

// Fat's V2-B, with R24 closing a feedback loop from its plate to its own
// input. In the amp that is a linear network problem plus a nonlinear tube:
//     G = G' + h * Vp,        Vp = load * tube(G)
// where G is the grid voltage, G' what the network would deliver with the
// feedback source grounded (the interFat network computes exactly that, with
// its full frequency response), h the fraction of plate voltage that reaches
// the grid (fatFeedback) and Vp the loaded plate voltage. The tube inverts,
// so h > 0 is NEGATIVE feedback: it cuts the gain and, more to the point,
// flattens the tube's curvature - Fat distorts later and more gently than a
// bare V2-B driven equally hard. Solving that per sample would need an
// iteration inside the oversampled loop; instead it is solved once, for a
// grid of G' values, and read back by interpolation. (The loop's own
// frequency dependence - the coupling caps inside it - is the one thing this
// leaves out; the network in front keeps all of its.)
void SuperSonic22Preamp::buildFatTable()
{
    fatTableRange = 80.0;
    fatTable.assign(static_cast<size_t>(fatTableSize), 0.0);

    for (int i = 0; i < fatTableSize; ++i)
    {
        auto gp = -fatTableRange + 2.0 * fatTableRange * static_cast<double>(i) / static_cast<double>(fatTableSize - 1);

        // G - h*Vp(G) increases with G (Vp falls as G rises), so a bracket
        // around G' converges by bisection; |h*Vp| stays well under 20V.
        auto lo = gp - 20.0, hi = gp + 20.0;
        for (int it = 0; it < 48; ++it)
        {
            auto mid = 0.5 * (lo + hi);
            auto residual = mid - fatFeedback * vintageLoadFactor * v2b.process(mid) - gp;
            (residual > 0.0 ? hi : lo) = mid;
        }
        fatTable[static_cast<size_t>(i)] = vintageLoadFactor * v2b.process(0.5 * (lo + hi));
    }
}

double SuperSonic22Preamp::v2bNormalTransfer(double gridVolts) const noexcept
{
    return vintageLoadFactor * v2b.process(gridVolts);
}

double SuperSonic22Preamp::fatClosedLoop(double gridVolts) const noexcept
{
    auto pos = (gridVolts + fatTableRange) * (static_cast<double>(fatTableSize - 1) / (2.0 * fatTableRange));
    pos = std::min(static_cast<double>(fatTableSize - 1) - 1.0e-9, std::max(0.0, pos));
    auto i = static_cast<size_t>(pos);
    auto frac = pos - static_cast<double>(i);
    return fatTable[i] + frac * (fatTable[i + 1] - fatTable[i]);
}

float SuperSonic22Preamp::processOversampledSample(float xIn) noexcept
{
    auto track = [this](double& peak, double v) { if (testPointsEnabled) peak = std::max(peak, std::abs(v)); };

    // V1-A, shared by both channels. (The R1 10k grid stopper and R2 1M
    // input load are invisible at these frequencies.)
    auto vA = v1a.process(static_cast<double>(xIn));

    if (channel == Channel::Vintage)
    {
        vintageFront.process(vA);
        track(testPoints.v1aPlate, vintageFront.voltage(vfP));

        auto vB = v1b.process(shelfV1B.process(vintageFront.voltage(vfOut)));

        double plate;
        if (! fat)
        {
            interNormal.process(vB);
            track(testPoints.v1bPlate, interNormal.voltage(inP));
            plate = vintageLoadFactor * v2b.process(shelfV2B.process(interNormal.voltage(inOut)));
        }
        else
        {
            interFat.process(vB);
            track(testPoints.v1bPlate, interFat.voltage(ifP));
            plate = fatClosedLoop(shelfV2B.process(interFat.voltage(ifOut)));
        }
        track(testPoints.v2bPlate, plate);

        auto out = vintageOutputHighpass.processSample(static_cast<float>(plate * vintageDivider));
        track(testPoints.output, static_cast<double>(out));
        return out;
    }

    burnFront.process(vA);
    track(testPoints.v1aPlate, burnFront.voltage(bfP));

    auto vB = v1b.process(shelfV1B.process(burnFront.voltage(bfOut)));

    burnGain2.process(vB);
    track(testPoints.v1bPlate, burnGain2.voltage(g2P));

    auto vC = v2a.process(burnGain2.voltage(g2Out));

    burnInter.process(vC);
    track(testPoints.v2aPlate, burnInter.voltage(biP));

    auto vD = v2b.process(shelfV2B.process(burnInter.voltage(biOut)));

    burnStack.process(vD);
    track(testPoints.v2bPlate, burnStack.voltage(bsP));

    auto out = burnStack.voltage(bsOut);
    track(testPoints.output, out);
    return static_cast<float>(out);
}

void SuperSonic22Preamp::processBlock(const float* input, float* output, int numSamples) noexcept
{
    if (dirty)
    {
        updateComponents();
        dirty = false;
    }

    oversampler.processBlock(input, output, numSamples,
                              [this](float x) { return processOversampledSample(x); });
}
