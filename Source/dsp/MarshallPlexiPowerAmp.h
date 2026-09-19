#pragma once

#include "AmpSpeakerLoad.h"
#include "KorenPentode.h"
#include "NodalNetwork.h"
#include "OutputLoad.h"
#include "TriodeSection.h"

// The Marshall 1959HW's power amp, traced from the same July 1970 Unicord
// drawing as the preamp (MarshallPlexiPreamp.h) and modeled as the circuit it
// is - a phase inverter, four EL34s, an output transformer, a speaker and a
// global negative-feedback loop that closes through all of them - rather than
// as a saturation curve with a "sag" knob. It takes the tone stack's output
// node and produces the speaker terminal voltage, in volts.
//
// THE CIRCUIT.
//  - V3, an ECC83, is a long-tailed pair phase inverter. Its two plate loads
//    are deliberately unequal (82k and 100k), so the two halves of the push-pull
//    pair are never quite balanced. The common cathode goes through 470 ohms and
//    10k to a junction J; grid 2 is coupled to J through .1uF (with a 1M leak),
//    so J's voltage - which carries the feedback - drives that grid AND the tail.
//  - J is where the negative feedback arrives: 47k from the speaker's terminals
//    (the manual: "the negative feedback ... is taken off the actual speaker
//    output itself", not the 8 ohm tap, so a lower impedance setting means less
//    feedback and less damping). J goes to ground through a 5k pot - PRESENCE -
//    whose wiper has a .1uF to ground: turned up, the cap shunts J at high
//    frequencies, removing feedback there and letting the top end open up.
//  - Each phase-inverter plate is coupled by .022uF to two EL34 grids (5.6k
//    stopper each, a 220k leak to a fixed negative bias rail). When the drive
//    is big enough the EL34 grids conduct, charge the .022uF caps, and shift the
//    bias negative for a while (the "blocking" that squashes a hard-driven
//    Plexi): that is modeled - the coupling cap, the leak and the grid's
//    conduction are one nonlinear node each.
//  - Four EL34s in push-pull-parallel (two per side): the Koren pentode
//    (KorenPentode.h), fixed bias, a 1k screen resistor each from B+.
//  - The output transformer and speaker (OutputLoad.h): Zaa 1.7k, three taps,
//    magnetizing and leakage inductance, and the speaker's own impedance curve.
//  - B+ is a supply with an internal resistance and a reservoir capacitor: it
//    droops when the tubes pull hard and recovers over Rs*C (about 8 ms) - the
//    sag.
//
// HOW IT IS SOLVED. Absolute voltages, sample by sample at the (oversampled)
// rate the caller runs it at. Two coupled nonlinear systems, each a small
// Newton iteration with analytic derivatives: (1) the phase inverter and its
// drivers - five unknowns (the two triodes' currents, the current the phase
// inverter's grid draws from the tone stack when it conducts, and the two
// driver-grid nodes); (2) the output stage - one unknown, the transformer's
// internal secondary voltage, which fixes both plate voltages and, through the
// linear speaker load, the current. The feedback loop closes through a one-
// sample delay (5us at 192kHz - far above the loop's bandwidth).
//
// JUDGMENT CALLS (not on the drawing): idle bias (35mA per tube, the usual
// 65-75% of the EL34's 25W plate dissipation - the drawing gives no figure; the
// fixed bias voltage follows from it); the supply's 130 ohm / 60uF; the
// transformer's winding resistance, leakage and magnetizing inductances; the
// grid-conduction knees and on-resistances; the ~1.7k Zaa (a forum-reported
// figure for the 1959, not from the drawing); and Koren's pentode parameters.
// The supply nodes (+460V B+, +375V at the phase inverter) are the c.1967
// drawing's printed figures.
class MarshallPlexiPowerAmp
{
public:
    // The fed-back voltage is band-limited (two poles) - see process().
    static constexpr double feedbackBandwidthHz = 10.0e3;

    // sampleRate is the rate this runs at - the OVERSAMPLED rate, when the
    // caller oversamples.
    explicit MarshallPlexiPowerAmp(double sampleRate);

    void setPresence(float position) noexcept;                     // 0..1
    void setImpedanceTap(double ohms) noexcept;                    // 4, 8 or 16
    void setSpeaker(const AmpSpeakerLoad::Speaker& speaker, double nominalOhms);

    // Test hook: with the loop open the phase inverter sees no feedback at all
    // (the open-loop gain and distortion).
    void setFeedbackEnabled(bool enabled) noexcept { feedbackEnabled = enabled; }
    // Test hook: with the loop open, this voltage is applied at the feedback
    // input instead of nothing - measuring the loop gain is driving it and reading
    // the speaker terminals.
    void setInjectedFeedback(double volts) noexcept { feedbackInjected = volts; }

    void reset();

    // One sample. stackFreeVolts and stackOhms are the tone stack's output node
    // with nothing drawn from it, and its resistance at this instant
    // (MarshallToneStack::beginSample() / outputOhms()). Returns the current
    // the phase inverter's grid drew from that node (amps, out of it) - hand it
    // to MarshallToneStack::finishSample().
    double process(double stackFreeVolts, double stackOhms) noexcept;

    // The speaker's terminal voltage after the last process().
    double speakerVolts() const noexcept { return terminalVolts; }

    // State, for tests and the amp's meters.
    struct Operating
    {
        double tubeAmps = 0.0;        // per EL34 at idle
        double biasVolts = 0.0;       // the fixed grid bias (negative)
        double supplyVolts = 0.0;     // idle B+
        double piTailAmps = 0.0;      // both phase-inverter triodes, total
        double piCathodeVolts = 0.0;
        double piPlate1Volts = 0.0;   // the 82k side
        double piPlate2Volts = 0.0;   // the 100k side
    };
    const Operating& operating() const noexcept { return idle; }

    double supplyVolts() const noexcept { return vb; }
    double biasShiftVolts() const noexcept { return 0.5 * (vNode1 + vNode2) - idle.biasVolts; } // mean driver-grid shift from idle
    double tubeAmpsA() const noexcept { return lastAmpsA; }
    double tubeAmpsB() const noexcept { return lastAmpsB; }
    double transformerRatio() const noexcept { return load.turnsRatio(); }

private:
    void computeOperatingPoint();
    void resetState();
    void settleJunction() noexcept;
    void updatePresence() noexcept;

    double fs, period;
    OutputLoad load;
    bool feedbackEnabled = true;
    double feedbackInjected = 0.0;
    float presence = 0.5f;               // the target
    double presenceNow = 0.5;            // where the pot's resistances are: slewed toward the target
    bool started = false;                // has process() run since the last reset()?

    // The feedback junction J and the presence network: v_feedback -> 47k -> J,
    // J -> Ra -> W -> Rb -> ground, .1uF from W to ground.
    NodalNetwork jNet;
    int jNode = 0, presenceUpper = 0, presenceLower = 0;

    Operating idle;
    double gridDc = 0.0;            // the phase inverter's grid DC (both grids sit at the cathode bias through their leaks)
    double cathodeDc = 0.0;
    double supplyOpenVolts = 0.0;   // the supply's open-circuit voltage
    double otherAmps = 0.0;         // what the phase inverter and preamp draw from B+
    double amps1Dc = 0.0, amps2Dc = 0.0;

    // Solver state.
    double u[5] = {};               // i1, i2, gridAmps, vNode1, vNode2
    double vNode1 = 0.0, vNode2 = 0.0;
    double vcap1 = 0.0, icap1 = 0.0, vcap2 = 0.0, icap2 = 0.0;   // the two .022uF coupling caps
    double vpPrev1 = 0.0, icp1 = 0.0, vpPrev2 = 0.0, icp2 = 0.0; // the phase inverter's plate capacitances
    double vnPrev1 = 0.0, icn1 = 0.0, vnPrev2 = 0.0, icn2 = 0.0; // the driver grids' input capacitances
    double y2 = 0.0, vJPrev = 0.0;                                // grid 2's high-pass
    double xPrev = 0.0;
    double vb = 0.0;                                              // B+
    double vg2A = 0.0, vg2B = 0.0;                                // screen voltages
    double vgA = 0.0, vgB = 0.0;                                  // control-grid voltages of the output pairs
    double feedbackPrev = 0.0;
    double feedbackPole1 = 0.0, feedbackPole2 = 0.0, feedbackAlpha = 0.0;
    double terminalVolts = 0.0;
    double lastAmpsA = 0.0, lastAmpsB = 0.0;

    // The phase inverter grid 2's high-pass (1M x .1uF), bilinear.
    double hpA1 = 0.0, hpB0 = 0.0;
};
