#pragma once

#include "CouplingHighpass.h"
#include "DiodeClipperStage.h"
#include "Oversampler4x.h"

#include <vector>

// A Klon Centaur-style transparent overdrive with two additions on top of
// the circuit it's modeled on: a series "Light Distortion" hard-clip stage
// and a post-clip "Depth" low-frequency peak.
//
// The Klon core follows two independent circuit teardowns (ElectroSmash's
// analysis and Coda Effects' schematic walk-through) - and the CCRMA Klon
// modeling paper's own block breakdown agrees on the shape of it:
//   - A dual-ganged Gain control that drives TWO paths in opposite
//     directions: a mid-boost path (a ~1kHz hump, 4.6x..25.8x per Coda's
//     calculation) into a shunt-to-ground GERMANIUM clipper (1N34A, ~0.35V
//     forward drop), and a CLEAN path that recedes as Gain rises but never
//     fully disappears. That inverse pairing is what keeps the pedal
//     dynamic and "amp at the edge of breakup" instead of a flat clipper.
//   - A summing stage with a ~495Hz single-pole low-pass (ElectroSmash)
//     that both paths pass through.
//   - A tone control that is an active HIGH SHELF with its corner near
//     408Hz spanning -8dB..+18.24dB (ElectroSmash) - mostly a treble boost.
// The clipper sits shunt-to-ground AFTER the gain stage (both teardowns
// agree), not in the op-amp's feedback loop as the CCRMA summary loosely
// worded it.
//
// Things this model calibrates rather than transcribes, since the
// original schematic image wasn't available to read (only a mirror of the
// ElectroSmash text): the clipper's series resistance, the hump's Q, the
// Gain-pot taper (dB-linear here), the clean-path floor, and the level
// scaling between digital samples and the circuit's volts. Each is a named
// constant in the .cpp. CentaurDriveStageTest measures the properties that
// matter (hump position, shelf range, clean-path survival, the diode drop)
// rather than trusting the numbers.
//
// The added stages:
//   - Light Distortion: a silicon (1N4148-style, ~0.6V) hard-clip stage in
//     SERIES after the Klon core, in the Distortion+/DS-1 mold (op-amp gain
//     into diodes to ground). Its gain and wet amount rise together with
//     the knob and at 0 it is skipped entirely, so 0 is a pure Klon path,
//     not just "very little distortion".
//   - Depth: a boost-only peak near 95Hz AFTER both clippers, so it adds
//     low-end body without feeding the clippers more bass to intermodulate.
//
// Both clippers run inside their own 4x oversampled block, per toolkit
// convention; the linear shaping around them runs at the base rate.
class CentaurDriveStage
{
public:
    explicit CentaurDriveStage(double sampleRate);

    void setSampleRate(double newSampleRate);

    void setDrive(float amount);            // 0..1: the Klon's dual-ganged Gain
    void setLightDistortion(float amount);  // 0..1: 0 = skipped (pure Klon path)
    void setDepth(float amount);            // 0..1: 0 = flat, 1 = full low peak
    void setTone(float amount);             // 0..1: real range -8dB..+18.24dB shelf

    void reset() noexcept;

    void processBlock(const float* input, float* output, int numSamples);

    // The shelf's boost/cut in dB for a given Tone knob position.
    static double toneGainDb(float amount) noexcept;

    // The two diode models, exposed so the test can check their forward
    // voltages against the real parts' documented drops.
    static DiodeClipperStage::Parameters germaniumDiodeParameters();
    static DiodeClipperStage::Parameters siliconDiodeParameters();

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        float process(float x) noexcept;
        void reset() noexcept;
    };

    static void designPeaking(Biquad& b, double sampleRate, double centerHz, double q, double gainDb);
    static void designHighShelf(Biquad& b, double sampleRate, double cornerHz, double gainDb);

    // A diode clipper wrapped so its small-signal gain is exactly unity and
    // its output is in volts - DiodeClipperStage normalises its output so a
    // reference drive lands near 1.0, which would otherwise silently bake a
    // large gain into the small-signal path.
    struct VoltClipper
    {
        explicit VoltClipper(const DiodeClipperStage::Parameters& p);

        float process(float volts) noexcept { return static_cast<float>(stage.processSample(volts) * unityCompensation); }
        void reset() noexcept { stage.reset(); }

        DiodeClipperStage stage;
        double unityCompensation = 1.0;
    };

    float clipGermanium(float x) noexcept;
    float clipSilicon(float x) noexcept;

    double sampleRate;

    float cleanLevel = 1.0f;
    float ldGain = 1.0f;
    float ldWet = 0.0f;
    float ldWetTrim = 1.0f;
    bool ldWasActive = false;

    CouplingHighpass inputHighpass; // the input buffer's DC-blocking coupling cap
    Biquad hump;                    // the gain stage's ~1kHz mid-hump
    double summingLpAlpha = 0.0;    // summing stage's ~495Hz single-pole low-pass
    double summingLpState = 0.0;
    Biquad depthPeak;
    Biquad toneShelf;

    VoltClipper germanium;
    VoltClipper silicon;
    Oversampler4x clipOversampler;
    Oversampler4x ldOversampler;

    std::vector<float> cleanBuffer;
    std::vector<float> clipBuffer;
    std::vector<float> ldBuffer;
};
