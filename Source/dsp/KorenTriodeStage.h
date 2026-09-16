#pragma once

// A memoryless waveshaper derived from Norman Koren's SPICE triode model,
// evaluated at a fixed nominal plate voltage - i.e. the grid-voltage
// nonlinearity only. This is the "playable curve" version: it does NOT
// model the plate/coupling-cap feedback into current draw that a real
// circuit has (grid, plate and coupling cap all interact) - that's the
// full Dempwolf/WDF circuit stage, a separate stretch-goal module. This
// stage is deliberately framework-agnostic (no JUCE dependency) so it can
// be unit tested in complete isolation before it's wired into anything.
//
// Reference: Norman Koren, "Improved SPICE Models for Vacuum Tubes".
// Default parameters are the commonly published 12AX7 fit.
class KorenTriodeStage
{
public:
    struct Parameters
    {
        double mu = 100.0;    // amplification factor
        double ex = 1.4;      // exponent
        double kg1 = 1060.0;
        double kp = 600.0;
        double kvb = 300.0;

        double plateVoltage = 250.0;    // fixed nominal Vp (volts)
        double gridBias = -1.5;         // DC operating point (volts)
        double inputToGridVolts = 6.0;  // grid-voltage swing for a unit-amplitude input
    };

    explicit KorenTriodeStage(Parameters parametersToUse = {});

    void setParameters(const Parameters& newParams);
    const Parameters& getParameters() const noexcept { return params; }

    // Processes one sample of a normalised (roughly -1..1) audio signal.
    float processSample(float input) const noexcept;

private:
    static double plateCurrent(double vg, double vp, const Parameters& p) noexcept;

    Parameters params;
    double quiescentCurrent = 0.0;   // Ip at Vg = gridBias - the DC point a real coupling cap removes
    double outputNormalisation = 1.0;
};
