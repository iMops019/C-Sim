#pragma once

#include "Oversampler4x.h"

#include <complex>

// The Ibanez Tube Screamer's audio path, modeled as the circuit it is rather
// than as a filter-plus-waveshaper: the op-amp clipping stage is solved as its
// real feedback network (a resistor, a capacitor and two antiparallel diodes in
// parallel around the op-amp), and the tone stage is derived from its netlist.
// Written for the TS808 (Components::ts808()); the TS9 (Components::ts9())
// differs in the published schematics only in its two output resistors (470 and
// 100k against 100 and 10k) and its op-amp type, which is not modeled - so the
// two pedals measure almost identically here (a 0.05dB level step and a lower
// output high-pass corner). Whatever else players hear between them is the
// op-amp's own behaviour and component tolerance, which a schematic can't give.
//
// SOURCES. Every component value below was read off the TS808 schematics in
// ElectroSmash's "Tube Screamer Analysis", R.G. Keen's Geofex "The Technology of
// the Tube Screamer", and Steve Cerutti's hand-redraw of the original TS-808
// (schematicheaven.net); Yeh/Abel/Smith (DAFx-07) covered the tone stage. The
// three agree on the values; ElectroSmash's designators (R7, R8, R12...) are
// inconsistent between its own text and images, so the Components fields are
// named for what the part does, not for a designator.
//
// SIGNAL PATH, in order (all AC signals are relative to the 4.5V bias Vr, so the
// DC operating points drop out and every coupling capacitor is a high-pass):
//   1. Input buffer: 0.02uF into ~446k (ElectroSmash's input impedance), a
//      15-18Hz high-pass. The 2SC1815 emitter follower itself is unity gain
//      and is left linear (at guitar levels its 10k emitter load only limits
//      swings past ~2V).
//   2. Clipping stage: a non-inverting op-amp. The inverting leg is 4.7k in
//      series with 0.047uF to ground (the 720Hz high-pass that makes the
//      "mid hump" - bass gets unity gain, mids and treble get up to 118x). The
//      feedback network is (51k + the 500k Drive pot) || 51pF, with the two
//      silicon diodes DIRECTLY across it - not in series with the resistors -
//      so once the feedback voltage reaches a diode drop the diodes take over
//      and the stage's gain collapses toward 1 for that half-cycle. That is a
//      soft, symmetric clipper whose knee moves with Drive, and it is the whole
//      character of the pedal.
//   3. Tone stage: a second op-amp behind a 1k/0.22uF low-pass, with the 20k
//      Tone pot straddling its inputs and a 220 ohm + 0.22uF shunt on the wiper.
//      It is a first-order treble-lift network cascaded with the pre-filter, and
//      the pot loads the pre-filter (the two interact), so it is solved as one
//      second-order section rather than two independent filters.
//   4. Level pot and the output buffer's divider.
//
// WHAT IS NOT MODELED, deliberately: the op-amp's finite gain-bandwidth and slew
// rate (the diodes and the 5.6-61kHz feedback pole dominate the loop; a 4558
// datasheet wasn't among the sources), the two transistor buffers' nonlinearity,
// the JFET bypass switch (the rack's bypass button does that job), and the
// output coupling capacitors C7/C8 (1.6Hz and 3.1Hz - inaudible).
//
// LEVELS. A normalised 1.0 is 1 V, the same convention as the amp modules, so
// the pedal's output lands at the level a real Tube Screamer feeds an amp.
// Framework-agnostic (no JUCE).
class TubeScreamerStage
{
public:
    struct Components
    {
        // Input buffer.
        double c1 = 0.02e-6;
        double inputOhms = 446.0e3;      // ElectroSmash: 1k + 510k || the transistor's base impedance
        double c2 = 1.0e-6;              // coupling into the clipping stage
        double r5 = 10.0e3;              // that stage's bias resistor (the coupling cap's load)

        // Clipping stage.
        double r4 = 4.7e3;
        double c3 = 0.047e-6;
        double r6 = 51.0e3;              // feedback resistor in series with the Drive pot
        double drivePot = 500.0e3;
        double driveTaperMid = 0.15;     // "500K A"/log: 15% of its resistance at 12 o'clock (calibrated)
        double c4 = 51.0e-12;

        // The two feedback diodes (1N914 / 1S1588 / MA150 class). The SPICE
        // model's saturation current and ideality; the diodes are matched.
        double diodeSaturation = 2.52e-9;
        double diodeIdeality = 1.752;

        // Tone stage.
        double r7 = 1.0e3;
        double c5 = 0.22e-6;
        double r9 = 10.0e3;
        double r11 = 1.0e3;
        double tonePot = 20.0e3;         // treated as linear (sources disagree on the marking)
        double r8 = 220.0;
        double c6 = 0.22e-6;

        // Output buffer - the only audio-path difference between TS808 and TS9.
        double rb = 100.0;               // series resistor after the buffer (TS9: 470)
        double rc = 10.0e3;              // shunt resistor after the output cap (TS9: 100k)
        double c9 = 10.0e-6;
        double loadOhms = 1.0e6;         // what the next stage presents: an amp's ~1M input
    };

    static Components ts808() { return {}; }

    static Components ts9()
    {
        Components c;
        c.rb = 470.0;
        c.rc = 100.0e3;
        return c;
    }

    explicit TubeScreamerStage(double sampleRate, Components components = ts808());

    void setSampleRate(double newSampleRate);

    void setDrive(float amount);   // 0..1
    void setTone(float amount);    // 0..1: 0 = bass end, 1 = treble end
    void setLevel(float amount);   // 0..1, linear pot

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

    // ---- Exposed for the test: the pieces the audio path is made of ----

    // The op-amp clipping stage on its own. process() takes the voltage at the
    // op-amp's non-inverting input and returns the op-amp's output (volts,
    // relative to Vr); lastFeedbackVolts() is the voltage across the diodes.
    class ClippingStage
    {
    public:
        void prepare(double sampleRate, const Components& c);
        void setFeedbackResistance(double ohms) noexcept { rFeedback = ohms; }
        void reset() noexcept;
        double process(double u) noexcept;
        double lastFeedbackVolts() const noexcept { return vFeedbackPrev; }

    private:
        double g3 = 0.0, g4 = 0.0;             // trapezoidal companion conductances 2C/T
        double r4 = 0.0, rFeedback = 51.0e3;
        double diodeIs = 0.0, diodeNVt = 0.0;
        double vc3Prev = 0.0, i3Prev = 0.0;    // the 0.047uF: voltage and current last sample
        double vFeedbackPrev = 0.0, i4Prev = 0.0; // the 51pF
    };

    // Solves  gLinear*V + Is*(exp(V/nVt) - exp(-V/nVt)) = b  for V (Newton's
    // method from a bound guaranteed to be on the convergent side).
    static double solveDiodePair(double gLinear, double b, double saturation, double nVt) noexcept;

    // The tone stage as an analog transfer function in s:
    //   H(s) = (n0 + n1 s) / (d0 + d1 s + d2 s^2)
    struct ToneStage { double n0, n1, d0, d1, d2; };
    static ToneStage toneStage(const Components& c, double tone) noexcept;

    // The whole small-signal response of everything the audio path implements,
    // as a complex gain at frequencyHz. Even at a tiny level the two diodes are
    // not absent: their small-signal conductance 2*Is/(n*Vt) (~9M ohms) sits in
    // parallel with the feedback resistor, which at Drive max (551k) is worth
    // about half a dB of gain. diodeLeakage = false leaves them out, giving the
    // "ideal diodes-off" figures the schematic analyses quote.
    static std::complex<double> smallSignalResponse(const Components& c, double drive, double tone, double level, double frequencyHz, bool diodeLeakage = true);

    static double driveFeedbackOhms(const Components& c, double drive) noexcept;

private:
    struct HighPass
    {
        double a = 0.0, p = 0.0, x1 = 0.0, y1 = 0.0;
        void design(double sampleRate, double tau) noexcept;
        void reset() noexcept { x1 = y1 = 0.0; }
        double process(double x) noexcept
        {
            auto y = p * y1 + a * (x - x1);
            x1 = x;
            y1 = y;
            return y;
        }
    };

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
        void reset() noexcept { z1 = z2 = 0.0; }
        double process(double x) noexcept
        {
            auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void configure();
    void designTone(double toneAmount);
    float processOversampledSample(float x) noexcept;

    Components comp;
    double sampleRate;

    // The knobs' target values, and the smoothed values the circuit actually runs on.
    // A knob is read once per audio block; snapping to its new value at the block
    // boundary would step the gain (and, on a clipper, the whole waveform's shape) at the
    // block rate - zipper noise - so each glides to its target over ~8ms instead.
    double drive = 0.4, tone = 0.5, level = 0.7;
    double feedbackTarget = 0.0, feedbackNow = 0.0;
    double toneNow = 0.5, levelNow = 0.7;
    double smoothing = 0.0;
    int toneUpdateCounter = 0;
    double outputGain = 1.0;

    HighPass inputHighPass, couplingHighPass, outputHighPass;
    ClippingStage clipper;
    Biquad toneFilter;
    Oversampler4x oversampler;
};
