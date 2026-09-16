#pragma once

// A diode clipper: a series resistor into a node loaded by two
// antiparallel diodes to ground - the classic circuit behind countless
// distortion pedals, and the "harder edge" clipping style Mesa
// Rectifier-style amps mix in alongside their tube gain stages. Meant to
// be blended in with a tube cascade (see MetalPreampChain's diode blend),
// not used alone.
//
// Unlike the tube waveshaper, a diode's exponential I-V relationship
// makes this circuit's node voltage transcendental (no closed form), so
// it's solved with Newton-Raphson each sample - a smaller, more
// tractable place to use the same implicit-solve technique the full
// circuit tube stage (a further stretch goal) will need too.
class DiodeClipperStage
{
public:
    struct Parameters
    {
        double seriesResistance = 10000.0;  // ohms
        double saturationCurrent = 2.52e-9; // typical small-signal silicon diode Is
        double thermalVoltage = 0.02585;    // Vt at room temperature
        double inputScale = 3.0;            // volts of drive for a unit-amplitude input
    };

    explicit DiodeClipperStage(Parameters parametersToUse = {});

    void reset() noexcept { vOutPrev = 0.0; }

    float processSample(float input) noexcept;

private:
    static double solve(double vin, double initialGuess, const Parameters& p) noexcept;

    Parameters params;
    double vOutPrev = 0.0;
    double outputNormalisation = 1.0;
};
