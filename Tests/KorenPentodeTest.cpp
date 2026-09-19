// Verify the tube evaluators the power stage is built on:
//
//  - KorenPentode with the EL34 parameters against the Philips/Mullard
//    datasheet's headline operating point (Va = Vg2 = 250V, Vg1 = -13.5V: 100mA
//    of plate current, gm about 11mA/V) - the check that caught the factor of
//    two in Koren's formula (his "+ |E1|^ex" term) that halves the current if
//    left out;
//  - the shape any pentode must have: cut off well below the bias, a large
//    current at Vg1 = 0, monotonic in both grid and plate voltage, and nearly
//    flat in plate voltage (that is what a pentode IS);
//  - the analytic partial derivatives the phase inverter's and output stage's
//    Newton solvers use, against central differences of the functions
//    themselves (a wrong derivative wouldn't make the answer wrong, only make
//    the solver slow or unstable, so it needs its own check);
//  - the 12AX7 evaluator agrees with the DC solver's current function.

#include "../Source/dsp/KorenPentode.h"
#include "../Source/dsp/TriodeSection.h"

#include <cstdio>

int main()
{
    bool allPassed = true;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "OK" : "FAILED", what); allPassed &= ok; };

    std::printf("=== EL34 against the datasheet ===\n");
    {
        auto d = KorenPentode::plate(-13.5, 250.0, 250.0);
        auto ig2 = KorenPentode::screen(-13.5, 250.0);
        std::printf("  Va = Vg2 = 250V, Vg1 = -13.5V: Ia %.1f mA (datasheet 100), gm %.1f mA/V (datasheet ~11), Ig2 %.1f mA, ra %.0f ohms\n",
                     d.i * 1e3, d.gm * 1e3, ig2 * 1e3, 1.0 / d.gp);
        check(d.i > 0.090 && d.i < 0.110, "plate current within 10% of the datasheet's 100mA");
        check(d.gm > 0.008 && d.gm < 0.014, "transconductance near the datasheet's 11mA/V");

        auto zero = KorenPentode::plate(0.0, 250.0, 250.0);
        auto cutoff = KorenPentode::plate(-40.0, 250.0, 250.0);
        std::printf("  at Vg1 = 0: %.0f mA; at Vg1 = -40V: %.2f mA\n", zero.i * 1e3, cutoff.i * 1e3);
        check(zero.i > 0.25 && zero.i < 0.40, "hundreds of mA at zero grid bias");
        check(cutoff.i < 0.001, "cut off (< 1mA) well below the bias");

        bool mono = true;
        double prev = -1.0;
        for (double vg = -60.0; vg <= 5.0; vg += 0.5)
        {
            auto i = KorenPentode::plate(vg, 300.0, 300.0).i;
            mono &= i >= prev;
            prev = i;
        }
        prev = -1.0;
        for (double vp = 0.0; vp <= 600.0; vp += 5.0)
        {
            auto i = KorenPentode::plate(-20.0, vp, 300.0).i;
            mono &= i >= prev;
            prev = i;
        }
        check(mono, "plate current is monotonic in grid voltage and in plate voltage");

        auto lowV = KorenPentode::plate(-20.0, 200.0, 300.0).i, highV = KorenPentode::plate(-20.0, 450.0, 300.0).i;
        std::printf("  Vg1 = -20V, Vg2 = 300V: Ia %.1f mA at Vp = 200V, %.1f mA at 450V (%.0f%% more for 2.25x the plate voltage)\n",
                     lowV * 1e3, highV * 1e3, 100.0 * (highV / lowV - 1.0));
        check(highV / lowV < 1.25, "a pentode: the plate current barely depends on plate voltage");

        // A Marshall's idle: ~450V on the plate, ~445V on the screen. The grid
        // bias that gives ~35mA is the classic -50V-ish of a 100W plexi.
        double lo = -120.0, hi = 0.0;
        for (int i = 0; i < 80; ++i)
        {
            auto mid = 0.5 * (lo + hi);
            (KorenPentode::plate(mid, 450.0, 445.0).i > 0.035 ? hi : lo) = mid;
        }
        std::printf("  the grid bias for 35mA at Va 450V / Vg2 445V: %.1f V\n", 0.5 * (lo + hi));
        check(0.5 * (lo + hi) < -35.0 && 0.5 * (lo + hi) > -70.0, "35mA at ~450V needs a fixed bias in the tens of volts negative (a plexi runs about -50V)");
    }
    std::printf("\n");

    std::printf("=== Analytic derivatives vs central differences ===\n");
    {
        double worstGm = 0.0, worstGp = 0.0;
        for (double vg : { -45.0, -30.0, -20.0, -10.0, -3.0, 0.0, 3.0 })
            for (double vp : { 30.0, 100.0, 250.0, 450.0 })
            {
                constexpr double h = 1.0e-4;
                auto a = KorenPentode::plate(vg, vp, 400.0);
                auto gmNum = (KorenPentode::plate(vg + h, vp, 400.0).i - KorenPentode::plate(vg - h, vp, 400.0).i) / (2.0 * h);
                auto gpNum = (KorenPentode::plate(vg, vp + h, 400.0).i - KorenPentode::plate(vg, vp - h, 400.0).i) / (2.0 * h);
                if (a.gm > 1.0e-6) worstGm = std::max(worstGm, std::abs(a.gm - gmNum) / a.gm);
                if (a.gp > 1.0e-7) worstGp = std::max(worstGp, std::abs(a.gp - gpNum) / a.gp);
            }
        std::printf("  EL34: worst relative error in gm %.2e, in gp %.2e\n", worstGm, worstGp);
        check(worstGm < 1.0e-4 && worstGp < 1.0e-4, "EL34 derivatives match central differences");

        namespace T = TriodeSection;
        double worstTgm = 0.0, worstTgp = 0.0, worstI = 0.0;
        for (double vg : { -4.0, -2.0, -1.0, -0.3, 0.0, 0.5 })
            for (double vp : { 20.0, 80.0, 150.0, 250.0 })
            {
                constexpr double h = 1.0e-5;
                auto a = T::plateCurrentEval(vg, vp);
                auto gmNum = (T::plateCurrent(vg + h, vp) - T::plateCurrent(vg - h, vp)) / (2.0 * h);
                auto gpNum = (T::plateCurrent(vg, vp + h) - T::plateCurrent(vg, vp - h)) / (2.0 * h);
                if (a.gm > 1.0e-6) worstTgm = std::max(worstTgm, std::abs(a.gm - gmNum) / a.gm);
                if (a.gp > 1.0e-7) worstTgp = std::max(worstTgp, std::abs(a.gp - gpNum) / a.gp);
                worstI = std::max(worstI, std::abs(a.i - T::plateCurrent(vg, vp)));
            }
        std::printf("  12AX7: worst relative error in gm %.2e, in gp %.2e; current differs from plateCurrent() by %.1e A\n", worstTgm, worstTgp, worstI);
        check(worstTgm < 1.0e-4 && worstTgp < 1.0e-4 && worstI < 1.0e-12, "12AX7 evaluator: derivatives match, and the current is the DC solver's");
    }
    std::printf("\n");

    std::printf(allPassed ? "ALL CHECKS PASSED\n" : "SOME CHECKS FAILED\n");
    return allPassed ? 0 : 1;
}
