#pragma once

#include "MnaNetwork.h"
#include "Oversampler4x.h"

#include <array>

// The EarthQuaker Devices Hoof Fuzz, solved as the whole circuit it is: four
// common-emitter transistor stages, their coupling networks and the passive tone
// stack are ONE nonlinear system, not a chain of blocks, because in a Big Muff-
// family fuzz the stages load each other hard - the feedback stages' input
// impedance is a fraction of their 470k feedback resistor, the Sustain pot sits
// between two of them, and the tone stack hangs directly on a collector. A model
// that treated each stage as an isolated transfer function would get the gain and
// the tone wrong.
//
// SOURCES. The EQD production schematic is not public. This is the best documented
// equivalent: Kit Rae's trace of a Hoof ("Hoowf - Modified Violet Era Big Muff
// Clone"), cross-checked value for value against PedalPCB's Ungula clone BOM and
// two layout builds; all three agree. The values were read from those; the Hoof's
// differences from a stock Ram's Head Big Muff are the two GERMANIUM clipping
// transistors (2N1308 in the clones; MP38A reported in production), red LEDs
// instead of silicon diodes in the clipping feedback, a 50k Sustain pot, and a
// different tone stack (39k / 6.8nF low-pass, 6.8nF / variable-shunt high-pass) with
// the extra Shift control. (EQD's V2 only changed the switching to a relay; there is
// no clipping toggle.) An input jack shunt of 1M in Ungula's BOM is not on Kit Rae's
// schematic and is left out, and EQD's quoted 130k input impedance conflicts with
// the ~44k the schematic gives - the schematic is followed.
//
// SIGNAL PATH:
//   A  input booster (2N3904, silicon): 39k + 100nF in, 470k || 470pF collector-to-base
//      feedback, 15k collector load, 100 ohm emitter. ~+15dB, and it sets the input impedance.
//   Sustain: a 50k pot (a 2.2k resistor from its bottom to ground) between 100nF
//      coupling capacitors: attenuates from 0dB to about -27dB.
//   B, C  two clipping stages (2N1308 germanium): the same feedback network as A plus
//      100nF in series with two antiparallel red LEDs from collector to base - the
//      LEDs turn on at ~1.8V and hold the swing there. 100nF + 8.2k between stages.
//   Tone stack: 39k + 6.8nF low-pass, 6.8nF + (2.2k + Shift) high-pass, a 100k Tone
//      pot between them. A mid scoop whose depth and centre frequency move with Shift.
//   D  recovery stage (2N3904): 390k/100k bias, 10k collector, 2.2k emitter, then the
//      1M audio-taper Volume.
//
// DEVICE MODELS (the transistors' SPICE parameters are typical values, not read from
// the schematic; the sources give the part types only):
//   silicon NPN (2N3904): Is = 6.7fA, beta 250 (the typical measured range is 100-300)
//   germanium NPN (2N1308): Is = 20nA and beta 55 (Vbe ~0.25V at 0.3mA, hFE 30-100; the
//     forum-reported MP38's hFE is 50-55)
//   red LED: 1.8V at 1mA (Is = 1.2e-19A, ideality 1.9)
// All four transistors use the full Ebers-Moll model (forward and reverse junction), so a
// hard input - the rack can put several volts in front of it - saturates and cuts them off
// as a real transistor does instead of running the collectors past the supply.
// DC bias is not published anywhere - it is SOLVED from the netlist and the device
// models, and the capacitors start charged to it. The published-analysis estimates of
// the collector voltages (Q4 4.4-5.0V, Ge 3.2-5.4V) are what HoofStageTest checks it against.
//
// METHOD. All the linear parts - every resistor, capacitor and pot, the supply rails -
// are one MnaNetwork; the ten nonlinear device ports (each transistor's base-emitter and
// base-collector junctions, and the two LED pairs) are solved together each
// sample by Newton's method in the port voltages, the network's transfer resistances
// coupling them (the DK method, as NodalNetwork already does for tubes). It runs 4x
// oversampled.
//
// NOT MODELED: the transistors' Early effect and junction capacitances, the Ge leakage
// (ICBO) and temperature drift, the supply's sag, the bypass switching.
//
// LEVELS. A normalised 1.0 is 1 V at the guitar jack; the output is in volts.
// Framework-agnostic (no JUCE).
class HoofStage
{
public:
    struct Components
    {
        double supplyVolts = 9.0;

        // Input booster (stage A) - also the template for B and C's feedback network.
        double inputR = 39.0e3, inputC = 100.0e-9;
        double baseBiasR = 100.0e3;
        double feedbackR = 470.0e3, feedbackC = 470.0e-12;
        double collectorR = 15.0e3, emitterR = 100.0;

        // Sustain.
        double couplingC = 100.0e-9;
        double fuzzPot = 50.0e3, fuzzBottomR = 2.2e3;
        double interstageR = 8.2e3;

        // Clipping branch: capacitor in series with the LED pair.
        double clipC = 100.0e-9;

        // Tone stack.
        double lowPassR = 39.0e3, lowPassC = 6.8e-9;
        double highPassC = 6.8e-9, shiftFixedR = 2.2e3, shiftPot = 25.0e3;
        double tonePot = 100.0e3;

        // Recovery stage and Volume.
        double recoveryBiasHighR = 390.0e3, recoveryBiasLowR = 100.0e3;
        double recoveryCollectorR = 10.0e3, recoveryEmitterR = 2.2e3;
        double volumePot = 1.0e6, volumeMid = 0.15;
        double loadOhms = 1.0e6;
    };

    struct DeviceParameters
    {
        double saturation, ideality, beta;
    };
    static DeviceParameters silicon() { return { 6.7e-15, 1.0, 250.0 }; }
    static DeviceParameters germanium() { return { 2.0e-8, 1.0, 55.0 }; }
    static DeviceParameters redLed() { return { 1.2e-19, 1.9, 0.0 }; }
    static constexpr double reverseBeta = 5.0; // the transistors' reverse current gain (base-collector junction)

    explicit HoofStage(double sampleRate, Components components = {});

    void setSampleRate(double newSampleRate);

    void setFuzz(float amount);    // 0..1: Sustain
    void setTone(float amount);    // 0..1: 0 = low-pass (dark), 1 = high-pass (bright)
    void setShift(float amount);   // 0..1
    void setLevel(float amount);   // 0..1: the Volume pot

    void reset();

    void processBlock(const float* input, float* output, int numSamples);

    // ---- Exposed for the test ----
    struct OperatingPoint
    {
        // Stage order A, B, C, D (recovery).
        std::array<double, 4> baseVolts, collectorVolts, emitterVolts, collectorAmps;
    };
    OperatingPoint operatingPoint() const { return dcPoint; }

    // How well the last samples' Newton solves converged: the worst residual of
    // v - vFree - K j(v) over the ports (volts), and the most iterations any one
    // sample needed, and the highest voltage either LED pair reached. Reset by the
    // caller between measurements.
    struct SolverStats { double worstResidual = 0.0; int mostIterations = 0; double peakLedVolts = 0.0; };
    SolverStats solverStats() const { return stats; }
    void resetSolverStats() { stats = {}; }

    static double fuzzWiperFraction(float amount) { return std::clamp(static_cast<double>(amount), 0.0, 1.0); }
    static double volumeFraction(const Components& c, double level);

private:
    struct Nodes
    {
        int x1, ba, ca, ea, p, wp, sb, wc, bb, cb, eb, lb, xb, bc, cc, ec, lc, tl, th, tw, bd, cd, ed, vt, vw;
    };
    struct PotIds { int fuzzTop, fuzzBottom, toneLow, toneHigh, shift, volumeTop, volumeBottom; };

    static constexpr int numPorts = 10;
    using Vec = std::array<double, numPorts>;
    using Mat = std::array<std::array<double, numPorts>, numPorts>;

    struct Pattern { int node; double amount; };

    void build();
    void applyPots(double fuzzAmount, double toneAmount, double shiftAmount, double levelAmount);
    void snapKnobs();
    void computeCoupling();
    void solvePorts(int maxIterations, double tolerance) noexcept;
    void solveDc();
    float processOversampledSample(float x) noexcept;

    Components comp;
    double sampleRate;
    // The knobs' target values and the smoothed values the circuit runs on. A knob is read once
    // per audio block; snapping to its new value at the block boundary would step the circuit
    // there (a click, once per block while a knob is moving), so each glides over ~8ms. The pots
    // re-solve the whole 25-node network, so they are pushed in every 64th sample, and only
    // while a knob is actually moving.
    double fuzz = 0.7, tone = 0.5, shift = 0.4, level = 0.6;
    double fuzzNow = 0.7, toneNow = 0.5, shiftNow = 0.4, levelNow = 0.6;
    double smoothing = 0.0;
    int potUpdateCounter = 0;

    MnaNetwork net;
    Nodes n {};
    PotIds pot {};

    // Per device port: the node differences that make up its voltage (+/-), and the
    // pattern of node currents one unit of its device current injects.
    std::array<std::array<Pattern, 2>, numPorts> portNodes {};
    std::array<std::array<Pattern, 3>, numPorts> injection {};
    std::array<int, numPorts> injectionCount {};
    std::array<DeviceParameters, numPorts> device {};
    std::array<int, numPorts> kind {}; // 0 = junction, 1 = LED pair
    std::array<double, numPorts> portNVt {}, portCritical {}; // for the SPICE-style junction step limiting

    Mat coupling {};        // K: port voltage per unit device current
    Vec portVolts {}, freeVolts {}, deviceCurrents {};

    OperatingPoint dcPoint {};
    SolverStats stats {};
    Oversampler4x oversampler;
};
