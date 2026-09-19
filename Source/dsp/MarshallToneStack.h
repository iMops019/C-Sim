#pragma once

#include "NodalNetwork.h"

// The passive Treble/Middle/Bass network every Marshall of the JTM45 / Plexi /
// JCM800 family shares, solved as a circuit (NodalNetwork.h) so the three pots
// load each other the way they do in the amp. Default component values are the
// Marshall 1959 Super Lead's, from the July 1970 Unicord drawing of the 1959
// (500pF, 33k, .022uF, .022uF, 250k Treble, 1M Bass, 25k Middle); the 1988
// "1959 STD" drawing has the same topology with 470pF / 220k / 22k.
//
// TOPOLOGY, as drawn (Vin is the cathode follower ahead of it):
//
//   Vin --C1-- T                    T   = top of the Treble pot
//   Vin --R4-- N1                   N1  = the slope-resistor node
//   Treble pot: T -- W -- N2        W   = its wiper (the output)
//   N1 --C2-- N2
//   Bass:   N2 -- N3, a rheostat    (the wiper tied to one end)
//   Middle pot: N3 -- MW -- ground  MW  = its wiper
//   N1 --C3-- MW
//
// then W drives the next stage through a coupling cap into its grid leak and
// input capacitance (the phase inverter's, here). Turning a knob up means:
// Treble - wiper toward T; Bass - MORE rheostat resistance (N2 is the node the
// bass caps feed, and the resistance to ground is what lets its signal build);
// Middle - MORE resistance below the wiper (a Middle at 0 grounds N3 through
// the whole 25k, the deepest scoop).
//
// Checked against the closed-form polynomial in the literature (Yeh & Smith's
// Marshall stack): the first- and second-order coefficients of the traced
// netlist's transfer function match it to four digits at every setting tried,
// which pins down the wiring; see Tests/MarshallToneStackTest.cpp.
class MarshallToneStack
{
public:
    struct Components
    {
        double trebleCapF = 500.0e-12;   // C1
        double bassCapF = 22.0e-9;       // C2
        double midCapF = 22.0e-9;        // C3
        double slopeOhms = 33.0e3;       // R4
        double trebleOhms = 250.0e3;     // linear
        double bassOhms = 1.0e6;         // audio taper
        double midOhms = 25.0e3;
        // Resistance fraction at 12 o'clock: the Bass pot is a 1M log
        // (assumed 15%, a common audio taper); the 1959HW's Middle is a "10%
        // log" pot per Marshall's manual (the standard production 1959 uses a
        // linear one, 0.5).
        double bassTaperAtNoon = 0.15;
        double midTaperAtNoon = 0.10;

        // The cathode follower in front: a Thevenin source, volts and ohms.
        double sourceOhms = 615.0;
        // What the wiper drives: coupling cap, grid leak, input capacitance.
        double couplingF = 22.0e-9;
        double loadOhms = 1.0e6;
        double loadF = 40.0e-12;
    };

    MarshallToneStack(double sampleRate, Components componentsToUse = {});

    // Knob rotations, 0..1 (7 o'clock to 5 o'clock).
    void setControls(float treble, float bass, float middle) noexcept;

    void reset() noexcept;

    // One sample in volts (the source's open-circuit voltage); returns the
    // voltage at the far side of the coupling cap - the next grid.
    double processSample(double volts) noexcept;

    // The wiper itself, before the coupling cap, after the last sample.
    double wiperVoltage() const noexcept { return net.voltage(wiper); }

    // The same sample in two halves, for a next stage that loads the output
    // node with a current of its own (a triode grid that conducts). beginSample()
    // returns the output voltage with nothing drawn from it; outputOhms() is how
    // much that voltage moves per amp drawn (this instant's Thevenin resistance);
    // finishSample() takes the current the next stage actually drew (amps OUT of
    // the node) and returns the resulting output voltage.
    double beginSample(double volts) noexcept
    {
        if (dirty)
            update();
        net.solveFree(volts);
        return net.freeVoltage(out);
    }
    double outputOhms() const noexcept { return net.transferOhms(out, out); }
    double finishSample(double ampsDrawn) noexcept
    {
        net.commit(out, -ampsDrawn);
        return net.voltage(out);
    }

private:
    void update() noexcept;

    Components c;
    NodalNetwork net;
    int wiper = 0, out = 0;
    int trebleUpper = 0, trebleLower = 0, bass = 0, midUpper = 0, midLower = 0;
    float treble = 0.5f, bassKnob = 0.5f, middle = 0.5f;
    bool dirty = true;
};
