#pragma once

#include "MnaNetwork.h"
#include "Oversampler4x.h"

// The Boss BD-2 Blues Driver's audio path, from its service-manual schematic
// (the BD-2 MT board, read at full resolution; cross-checked against Analog Is
// Not Dead's redraw, Cobalt's breadboard analysis and Atomium's teardown).
//
// THE BD-2 HAS NO CLIPPING DIODES IN ITS AUDIO PATH TO SPEAK OF. Its overdrive is
// two cascaded gain stages built from discrete transistors - "op-amps" made of a
// JFET differential pair (2SK184GR) driving a PNP common-emitter output
// transistor with a Miller capacitor - whose open-loop gain is modest (~46dB,
// nothing like an IC's) and whose output stage runs into its rails: cutoff at
// the bottom, saturation at the top. So the pedal's gain is set by feedback but
// limited by the amplifier, and the overload is the amplifier running out of
// swing. A pair of 1SS133 diode stacks (~1.1V) sits after the first stage's
// network but is left out: in normal use (a 0.3V guitar, Gain at noon) they see
// under 1.1V and stay off, and only a hot low note at high Gain reaches them - by
// which point stage 2 is already >26dB into its own rails, so a clamp on its
// input changes nothing audible (BluesDriverStageTest checks both).
//
// SIGNAL PATH, in order (everything relative to the 4V bias Vref; every
// coupling capacitor is a high-pass):
//   1. Input: a JFET source follower (gain ~0.9, inferred) and two coupling
//      high-passes (3.4Hz and 7.2Hz).
//   2. Stage 1: non-inverting, gain 1 + (22k + Gain pot)/(1.5k + 1/(s*0.15uF)) -
//      15.7x..182x above its 707Hz corner (falling 6dB/octave below it), with
//      47pF rolling it off.
//      The Gain pot is a 250k audio-taper rheostat, ganged with stage 2's.
//   3. A fixed passive network (a Fender-style tone stack with no controls:
//      C26/R37, R38, C34, C35, R50, R51) that costs ~16dB and shapes the bass -
//      and stage 2's input coupling (2.2nF + 1M, a 72Hz high-pass) loads it.
//   4. Stage 2: same topology, 1 + (33k + Gain pot)/(2.2k + 1/(s*1uF)) -
//      72Hz corner, 16x..130x, 100pF roll-off.
//   5. A fixed shelf (R26 || C17, C19: -6dB above ~2.5kHz), then the Tone and
//      Level controls: Tone is a variable treble CUT (the wiper moves along a
//      10k pot between a 0.018uF series capacitor and a 0.018uF shunt), Level a
//      100k audio-taper pot loading it.
//   6. A peak filter on a real op-amp: unity gain with a +6..8dB bump near
//      120-150Hz, made by a ~32H simulated inductor (a gyrator: a capacitor, a
//      bias resistor and an emitter follower) in the feedback op-amp's ground
//      leg. The follower's finite output impedance is modeled, because an ideal
//      one would peak +11.7dB (its L-C resonance) instead of the published +6dB.
//   7. Output buffer (1k series, 100k shunt).
//
// WHAT IS CALIBRATED, NOT READ from the schematic:
//   - The amplifiers' open-loop gain (225, 47dB) and gain-bandwidth (4MHz).
//     The schematic gives topology, not transistor gains; 225 is set by the
//     published measurements (Cobalt: stage 1 "a little over 40dB" at max gain
//     against an ideal 45.2; end-to-end 52dB at max Gain against an ideal
//     ~62; AIND's simulation ~50dB), and the 4MHz is a typical figure. So
//     "the max gain matches the published numbers" is a calibration check, not
//     independent evidence.
//   - The rails: about 0V..7.8V on the 8V supply, i.e. -4.0V..+3.8V about Vref,
//     from the topology (PNP saturation at the top, cutoff at the bottom).
//     The two knees are given different sharpness - saturation abrupt (p=4),
//     cutoff softer (p=3) - because that is how the two mechanisms differ; the
//     exact asymmetry would need a SPICE run and is a judgment call.
//   - The Gain and Level taper (15% at noon) and the follower's beta/current.
// NOT MODELED: the JFET input follower's own distortion, the output buffer's,
// the protection diodes, soft switching, supply sag.
//
// LEVELS. A normalised 1.0 is 1 V, as in the other pedal and amp modules.
// Framework-agnostic (no JUCE).
class BluesDriverStage
{
public:
    struct Components
    {
        // Input (the JFET follower's ~0.9 gain is inferred, not read).
        double inputGain = 0.9;
        double inputCouplingC = 0.047e-6, inputBiasR = 1.0e6;   // 3.4Hz
        double stageCouplingC = 0.1e-6, stageBiasR = 220.0e3;   // 7.2Hz

        // The Gain pot: 250k audio taper, ganged across both stages.
        double gainPot = 250.0e3;
        double gainPotMid = 0.15;

        // Stage 1.
        double s1FeedbackR = 22.0e3;
        double s1LegR = 1.5e3, s1LegC = 0.15e-6;
        double s1FeedbackC = 47.0e-12;

        // Stage 2.
        double s2FeedbackR = 33.0e3;
        double s2LegR = 2.2e3, s2LegC = 1.0e-6;
        double s2FeedbackC = 100.0e-12;

        // The discrete amplifiers (calibrated - see the header).
        double openLoopGain = 225.0;
        double gainBandwidthHz = 4.0e6;
        double railTopVolts = 3.8;
        double railBottomVolts = 4.0;
        double kneeTop = 4.0;
        double kneeBottom = 3.0;

        // The fixed passive network between the stages, and stage 2's input.
        double c26 = 220.0e-12, r37 = 330.0e3;
        double r38 = 100.0e3;
        double c34 = 0.1e-6, c35 = 0.047e-6;
        double r50 = 1.0e6, r51 = 15.0e3;
        double c27 = 2.2e-9, r35 = 1.0e6;

        // Fixed shelf, then Tone and Level.
        double r26 = 5.6e3, c17 = 5.6e-9, c19 = 5.6e-9;
        double c100 = 0.018e-6, tonePot = 10.0e3, c101 = 0.018e-6;
        double levelPot = 100.0e3, levelPotMid = 0.15;
        double c10 = 0.047e-6, r13 = 470.0e3;

        // The bass peak filter.
        double r9 = 6.8e3, c8 = 2.2e-9;
        double c9 = 0.056e-6, gyratorC = 0.056e-6, gyratorBiasR = 470.0e3;
        double r21 = 1.2e3, emitterR = 10.0e3;
        double followerBeta = 200.0, followerAmps = 0.33e-3;

        // Output buffer: 1k series, 100k shunt, into an amp's ~1M input.
        double outputSeriesR = 1.0e3, outputShuntR = 100.0e3, loadOhms = 1.0e6;
    };

    explicit BluesDriverStage(double sampleRate, Components components = {});

    void setSampleRate(double newSampleRate);

    void setGain(float amount);    // 0..1
    void setTone(float amount);    // 0..1: 0 = dark (treble cut), 1 = bright
    void setLevel(float amount);   // 0..1

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

    // ---- Exposed for the test: the pieces the audio path is made of ----

    // The Gain rheostat (both stages' feedback add this to their fixed resistor).
    static double gainRheostatOhms(const Components& c, double gain) noexcept;
    // The Tone pot's two halves: wiper-to-C100 side, wiper-to-C101 side (tone 1 = wiper at the C100 end).
    static double toneUpperOhms(const Components& c, double tone) noexcept { return (1.0 - tone) * c.tonePot; }
    static double toneLowerOhms(const Components& c, double tone) noexcept { return tone * c.tonePot; }
    // The Level pot: resistance above and below the wiper (top = the Tone pot's wiper).
    static double levelUpperOhms(const Components& c, double level) noexcept;
    static double levelLowerOhms(const Components& c, double level) noexcept;
    static double followerPiOhms(const Components& c) noexcept;
    static double outputGain(const Components& c) noexcept;

    // One of the two discrete amplifiers with its feedback network: a finite-gain,
    // single-pole amplifier whose output stops at the rails. process() takes the
    // non-inverting input (volts about Vref) and returns the output.
    class DiscreteStage
    {
    public:
        void prepare(double sampleRate, const Components& c, double legR, double legC, double feedbackC);
        void setFeedbackResistance(double ohms) noexcept { feedbackR = ohms; }
        void reset() noexcept;
        double process(double u) noexcept;

        // The rails' soft limit and its slope, exposed for the test.
        static double limit(double x, double top, double bottom, double kneeTop, double kneeBottom) noexcept;

    private:
        double top = 3.8, bottom = 4.0, kneeTop = 4.0, kneeBottom = 3.0;
        double openLoopGain = 225.0, poleK = 0.0;
        double g22 = 0.0, g23 = 0.0, legR = 0.0, feedbackR = 100.0e3;
        double legVPrev = 0.0, legIPrev = 0.0;  // the leg's capacitor: voltage and current last sample
        double feedVPrev = 0.0, feedIPrev = 0.0; // the feedback capacitor
        double outPrev = 0.0, errorPrev = 0.0;   // the amplifier's pole state
    };

    // The linear networks, built from the schematic. Each has the ideal source
    // at MnaNetwork::source; `out` is the node the next stage reads.
    struct Network { MnaNetwork net; int out = 0; };
    struct ToneNetwork : Network { int toneUpper = 0, toneLower = 0, levelUpper = 0, levelLower = 0; };

    static Network makeStackNetwork(const Components& c);       // stage-1 output -> stage-2 gate
    static ToneNetwork makeToneNetwork(const Components& c);    // stage-2 output -> peak filter input
    static Network makePeakNetwork(const Components& c);        // peak filter input -> its output

private:
    struct HighPass
    {
        double a = 0.0, p = 0.0, x1 = 0.0, y1 = 0.0;
        void design(double sampleRate, double tau) noexcept
        {
            auto k = 2.0 * tau * sampleRate;
            a = k / (k + 1.0);
            p = (k - 1.0) / (k + 1.0);
        }
        void reset() noexcept { x1 = y1 = 0.0; }
        double process(double x) noexcept
        {
            auto y = p * y1 + a * (x - x1);
            x1 = x;
            y1 = y;
            return y;
        }
    };

    void configure();
    float processOversampledSample(float x) noexcept;

    Components comp;
    double sampleRate;
    // The knobs' target values and the smoothed values the circuit runs on. A knob is read once
    // per audio block; snapping to its new value at the block boundary would step the circuit
    // there (a click, once per block while a knob is moving), so each glides over ~8ms. Gain glides
    // as the rheostat's resistance, Tone and Level as pot positions pushed into the networks every
    // 32nd sample while they are moving.
    void applyTone(double toneAmount) noexcept;
    void applyLevel(double levelAmount) noexcept;

    double gain = 0.4, tone = 0.5, level = 0.6;
    double rheostatTarget = 0.0, rheostatNow = 0.0;
    double toneNow = 0.5, levelNow = 0.6;
    double smoothing = 0.0;
    int networkUpdateCounter = 0;
    double outGain = 1.0;

    HighPass inputHighPass, stageHighPass;
    DiscreteStage stage1, stage2;
    Network stack, peak;
    ToneNetwork toneNet;
    Oversampler4x oversampler;
};
