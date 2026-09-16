#pragma once

// A passive Fender/Marshall-style tone stack, simulated via nodal
// analysis rather than a hand-derived closed-form transfer function -
// deliberately, since a topology mistake here is something we can reason
// about circuit-node-by-node, whereas an algebra slip in a hand-reduced
// polynomial transfer function would be much harder to catch. Capacitors
// use a backward-Euler companion model (simple, stable, and accurate
// enough at audio sample rates for component values this small).
//
// Topology (the standard published "Fender-style" passive tone stack -
// see valvewizard.co.uk and similar amp-DIY references for the general
// form; exact component values below are typical blackface-era values,
// not transcribed from one specific factory schematic):
//
//   Vin --[C1 treble cap]-- A --[R1 treble pot, full value]-- Vm
//   Vin --[R4 slope resistor]--[C2 bass cap]-- Vm
//   Vm --[R2 * bassAmount, as a rheostat]-- ground
//   Vm --[R3 * midAmount, as a rheostat]-- ground
//   Vout = lerp(Vm, A, trebleAmount)   (treble pot wiper, assuming a
//                                       high-impedance load draws no
//                                       current from the wiper tap)
//
// Node A only has C1 and R1 attached (no other current path), so no
// current is lost there; the treble pot's own loading is absorbed into
// its full resistance appearing directly between A and Vm.
class FenderToneStack
{
public:
    struct Components
    {
        double trebleCapF = 250.0e-12;    // 250pF
        double bassCapF = 0.1e-6;         // 100nF
        double treblePotOhms = 250.0e3;   // 250k
        double bassPotOhms = 250.0e3;     // 250k, used as a rheostat
        double midPotOhms = 10.0e3;       // 10k, used as a rheostat - Fender's
                                           // mid range is characteristically
                                           // small next to a Marshall's
        double slopeResistorOhms = 100.0e3; // 100k
    };

    FenderToneStack(double sampleRate, Components componentsToUse = {});

    void setSampleRate(double newSampleRate);

    // 0..1 for each knob.
    void setControls(float treble, float bass, float mid);

    void reset();

    float processSample(float input) noexcept;

private:
    void updateConductances();

    Components components;
    double sampleRate;

    float trebleAmount = 0.5f, bassAmount = 0.5f, midAmount = 0.5f;

    // Cached conductances/resistances recomputed only when controls or
    // sample rate change, not per-sample.
    double g1 = 0.0;           // treble cap companion conductance (C1/T)
    double reqBassBranch = 0.0; // R4 + T/C2, the bass branch's effective series resistance
    double r1 = 0.0;           // treble pot full resistance
    double rBass = 0.0, rMid = 0.0; // rheostat resistances from knob positions

    // 2x2 system matrix inverse, cached alongside the conductances above.
    double invM00 = 0.0, invM01 = 0.0, invM10 = 0.0, invM11 = 0.0;

    // Capacitor state (backward-Euler companion model memory).
    double vC1Prev = 0.0; // voltage across C1 (Vin - A) last sample
    double vC2Prev = 0.0; // voltage across C2 alone last sample
};
