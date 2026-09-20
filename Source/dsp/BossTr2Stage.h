#pragma once

#include <algorithm>
#include <cmath>

// The Boss TR-2 Tremolo, from its service-manual schematic (the V1 board, PCB
// 70901390 - read at full resolution, cross-checked against the V3 redraw on
// freestompboxes, a measured scope study of the pedal, and the M5207L01 datasheet).
//
// THERE IS NO OPTOCOUPLER. The TR-2 does not modulate with an LED and a light-
// dependent resistor, and so has none of that family's lag and memory. The gain
// element is a Mitsubishi M5207L01, a linear-control VCA IC: its gain is
//     |gain| = Vc * Ro / (2 Ri) = Vc * 22k / 20k = 1.1 * Vc
// for a control voltage Vc measured from the chip's 4.5V common - LINEAR in
// amplitude (not in dB), unipolar, valid from 0V (-100dB) up past 1V. The tremolo
// is that control voltage moving, and everything else here is how the LFO makes it
// move. Boss's electronic bypass puts the dry signal on a JFET switch, so with the
// effect on the output is the wet path alone (no dry mix).
//
// THE LFO is a real relaxation oscillator: a NON-INVERTING SCHMITT TRIGGER (IC4B)
// and an INVERTING INTEGRATOR (IC4A) chasing each other.
//   - The Schmitt's positive input sums the integrator's output (33k), its own
//     output (47k) and a bias divider (330k to 9V, 100k to ground) against a 4.5V
//     reference; solving that node for the trip point gives Vt = 8.693 - 0.7021*Vsq,
//     i.e. the integrator's output flips the trigger at 3.217V while the trigger is
//     high (7.8V) and at 7.850V while it is low (1.2V). Those come out of the
//     resistor values; the only assumption is the M5218's 1.2V..7.8V output swing.
//   - The integrator (56k into 0.5uF - two 1uF electrolytics back to back) is fed
//     the trigger's square wave through the RATE pot: a 100k pot with a 10k resistor
//     on its cold end, the integrator's 56k loading the wiper. That gives a ramp
//     of 3.3V / (0.5uF * Reff) with Reff = 56k (1 + Ra/Rp), Ra the pot's resistance
//     from the square-wave end to the wiper and Rp = 56k || (110k - Ra). The period
//     follows: T = 1.404us * Reff - 0.99Hz at the slow end, 12.7Hz at the fast (the
//     pedal is measured at 1.0 - 11.1Hz; the top is ~14% high, most likely the
//     electrolytics' tolerance).
//   - The triangle is off-centre (its middle is 5.53V, not the 4.5V everything else
//     is referenced to), which is why the waveform gets its lopsided duty cycle.
//
// FROM LFO TO CONTROL VOLTAGE:
//   IC3A is an inverting amplifier around 4.5V with gain -(12k + Wave)/10k = -1.2 to
//   -11.2 (Wave is a 100k audio-taper rheostat), clipping at the rails - so Wave turns
//   the triangle into a trapezoid and, turned up, nearly a square wave. IC3B inverts it
//   again (gain -0.15 about a 4.9V bias). The DEPTH pot then mixes that between a fixed
//   5.5V and IC3B's output and feeds the control node through 100k; a 1M resistor to
//   5.5V and 0.1uF to ground sit on that node. The result, from the resistor values:
//       Vc = 1 + 0.123 d - 0.136 d V3A,   V3A = the IC3A output in volts
//   with an RC smoothing of tau = 0.1uF * ((100k + pot) || 1M) ~ 9-11ms, the same
//   rise and fall (no opto lag: this is the only smoothing there is).
//
// WHAT IS ASSUMED, not read: the M5218's output swing (1.2V..7.8V on 9V), the "hollow
// arrow" +5.5V rail (from the datasheet's Vc = 1V for 0dB), the JFET input buffer's
// ~0.9 gain (which makes the Depth-0 gain 0.9 x 1.1 = 0.99: unity, as a bypass-matched
// pedal should be), and the Wave pot's direction and taper.
// NOT MODELED: the supply's effect on the control voltage (it tracks the battery), the
// VCA's high-frequency roll-off above 20kHz, soft-switching.
//
// LEVELS. A normalised 1.0 is 1 V, as in the other pedal and amp modules.
// Framework-agnostic (no JUCE).
class BossTr2Stage
{
public:
    struct Components
    {
        // Input JFET buffer (inferred ~0.9) and the output filter/buffer.
        double inputGain = 0.9;
        double outputCouplingC = 0.027e-6, outputBiasR = 100.0e3;   // a 59Hz high-pass after the VCA's amplifier
        double outputGain = 0.99;                                    // 1k series, 100k shunt

        // The M5207L01 VCA.
        double vcaGainPerVolt = 22.0e3 / (2.0 * 10.0e3);
        double vcaInputLimitVolts = 6.1;    // where its distortion reaches ~1% at the datasheet's 2.3Vrms

        // The LFO.
        double supplyVolts = 9.0, referenceVolts = 4.5;
        double railLowVolts = 1.2, railHighVolts = 7.8;             // M5218 swing (assumed)
        double integratorR = 56.0e3, integratorC = 0.5e-6;
        double ratePot = 100.0e3, rateColdR = 10.0e3;
        double schmittIntegratorR = 33.0e3, schmittFeedbackR = 47.0e3, schmittUpperR = 330.0e3, schmittLowerR = 100.0e3;

        // The Wave stage (IC3A) and IC3B.
        double waveFixedR = 12.0e3, waveInputR = 10.0e3, wavePot = 100.0e3, wavePotMid = 0.15;
        double ic3bBiasVolts = 4.9, ic3bInputR = 10.0e3, ic3bFeedbackR = 1.5e3;

        // Depth and the control node.
        double depthEndVolts = 5.5, depthPot = 100.0e3;
        double controlSeriesR = 100.0e3, controlBiasR = 1.0e6, controlC = 0.1e-6;
        double commonVolts = 4.5;
    };

    explicit BossTr2Stage(double sampleRate, Components components = {});

    void setSampleRate(double newSampleRate);

    void setRate(float amount);    // 0..1
    void setDepth(float amount);   // 0..1
    void setWave(float amount);    // 0..1

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

    // ---- Exposed for the test: the circuit's own numbers ----
    static double lfoFrequencyHz(const Components& c, double rate);
    struct Thresholds { double low, high; };
    static Thresholds lfoThresholds(const Components& c);   // where the integrator's output flips the trigger
    static double smoothingSeconds(const Components& c, double depth);
    static double waveGain(const Components& c, double wave);

    // The control voltage's most recent value (after smoothing), volts above the common.
    double controlVolts() const noexcept { return smoothedControl; }
    // The control voltage the smoothing is heading for, given the LFO where it is now.
    double controlTargetVolts() const noexcept { return controlTarget(); }
    double lfoVolts() const noexcept { return integrator; }
    double vcaGain() const noexcept { return c.vcaGainPerVolt * std::max(smoothedControl, 0.0); }

private:
    void configure();
    void updateRamp() noexcept;
    void stepLfo() noexcept;
    static double softLimit(double x, double limit) noexcept;

    Components c;
    double sampleRate;
    double rate = 0.4, depth = 0.6, wave = 0.3;

    double controlTarget() const noexcept;

    double integrator = 0.0;    // IC4A's output (the triangle), volts
    bool triggerHigh = true;    // IC4B's output state
    double smoothedControl = 1.0;

    // Derived from the knobs.
    Thresholds thresholds {};
    double rampDown = 0.0, rampUp = 0.0;   // volts/second while the trigger is high / low
    double waveStageGain = 1.2;
    double controlAlpha = 0.0;

    double hpA = 0.0, hpP = 0.0, hpX1 = 0.0, hpY1 = 0.0;
};
