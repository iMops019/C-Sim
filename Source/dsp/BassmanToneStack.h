#pragma once

// The Fender Bassman 5F6-A ("tweed") tone stack, simulated via nodal
// analysis (same technique as FenderToneStack, backward-Euler companion
// capacitor models solved via KCL) - a genuinely different topology from
// FenderToneStack's later blackface-era circuit, not just different
// component values. Traced from the real Dec-1984-era... no, from the
// actual 5F6-A factory schematic (Kuehnel's "Circuit Analysis of a
// Legendary Tube Amplifier") and cross-checked against Fenton's published
// mesh-analysis paper ("Modelling of the Fender Bassman 5F6-A Tone
// Stack", 2015) for the shared-branch structure between controls.
//
// Topology:
//
//   Vin --[C1 treble cap]-- A --[R1 treble pot, full value]-- B
//   Vin --[R4 slope resistor]-- B                    (parallel path)
//   B --[C2 bass cap]--[Rb * bassAmount, a rheostat]-- D   (bass has NO
//        separate output tap - it only shapes how much reaches D)
//   B --[C3 mid cap]-- D                              (parallel path)
//   D --[R3 mid pot, full value]-- ground, wiper tap feeds Vout directly
//        scaled by midAmount - a real 3-terminal use, unlike Bass's
//        2-terminal rheostat (which end is "up" is a labeling choice,
//        calibrated so that turning the knob up audibly adds more mids,
//        verified in BassmanToneStackTest)
//   Vout = lerp(B, A, trebleAmount) + D * midAmount
//
// This is the literal circuit Marshall cloned into the JTM45 (hence the
// shared "mid-forward, less scooped than blackface" character) - see
// FenderStyleAmpPedal's own header for why this one was worth building as
// its own module instead of reusing FenderToneStack.
class BassmanToneStack
{
public:
    struct Components
    {
        double trebleCapF = 250.0e-12;      // 250pF
        double treblePotOhms = 250.0e3;     // 250k
        double slopeResistorOhms = 56.0e3;  // 56k
        double bassCapF = 0.02e-6;          // 0.02uF
        double bassPotOhms = 1.0e6;         // 1M, used as a rheostat
        double midCapF = 0.02e-6;           // 0.02uF
        double midPotOhms = 25.0e3;         // 25k, used as a real 3-terminal pot
    };

    BassmanToneStack(double sampleRate, Components componentsToUse = {});

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

    // Cached conductances, recomputed only when controls or sample rate
    // change, not per-sample.
    double gTreb = 0.0;    // treble cap companion conductance
    double gTrebPot = 0.0; // 1 / treble pot full resistance
    double gSlope = 0.0;   // 1 / slope resistor
    double gBassBranch = 0.0; // 1 / (rheostat resistance + bass cap companion resistance)
    double gMidCap = 0.0;    // mid cap companion conductance (pure cap, no series resistor)
    double gMidPot = 0.0;    // 1 / mid pot full resistance
    double reqBassBranch = 0.0; // rheostat resistance + bass cap companion resistance

    // 3x3 system matrix inverse for unknowns [A, B, D], cached alongside
    // the conductances above.
    double inv[3][3] = {};

    // Capacitor state (backward-Euler companion model memory).
    double vTrebPrev = 0.0; // voltage across the treble cap (Vin - A) last sample
    double vBassPrev = 0.0; // voltage across the bass cap ALONE last sample
    double vMidPrev = 0.0;  // voltage across the mid cap (B - D) last sample
};
