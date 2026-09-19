#include "MarshallPlexiPowerAmp.h"

#include <algorithm>
#include <cmath>

namespace
{
    using Net = NodalNetwork;
    namespace T = TriodeSection;

    // ---- Supply. The +460V and +375V nodes are the c.1967 drawing's figures. ----
    constexpr double idleSupply = 460.0;
    constexpr double piSupply = 375.0;
    constexpr double piDropperOhms = 20.0e3;          // 20k/1W from B+ to the phase inverter's node
    constexpr double idleTubeAmps = 0.035;            // judgment call (see the header)
    constexpr double supplyOhms = 130.0;              // judgment: transformer + rectifier
    constexpr double reservoirFarads = 60.0e-6;       // judgment: the two 100uF cans in series plus the filter

    // ---- Phase inverter and drivers, from the drawing. ----
    constexpr double plateLoad1 = 82.0e3, plateLoad2 = 100.0e3;
    constexpr double couplingFarads = 0.022e-6;
    constexpr double gridLeakOhms = 220.0e3;
    constexpr double stopperOhms = 5.6e3;             // per EL34
    constexpr double screenOhms = 1.0e3;              // per EL34
    constexpr double piCathodeOhms = 470.0;
    constexpr double piTailOhms = 10.0e3;
    constexpr double cathodeToJ = piCathodeOhms + piTailOhms;
    constexpr double feedbackOhms = 47.0e3;           // 100k on later units
    constexpr double presencePotOhms = 5.0e3;
    constexpr double presenceFarads = 0.1e-6;
    constexpr double grid2CouplingFarads = 0.1e-6;
    constexpr double grid2LeakOhms = 1.0e6;

    // ---- Grid conduction (judgment calls): a soft knee at the cathode, then a
    // resistance. The phase inverter's ECC83 grid is a small-signal grid; the
    // EL34's, through its 5.6k stopper, a big one.
    constexpr double piGridOnOhms = 2.0e3, piGridKnee = 0.15;
    constexpr double outGridOnOhms = 1.0e3, outGridKnee = 0.3;

    // ---- Stray capacitance. Negligible for the audio itself, but it is what
    // rolls the feedback loop's gain off above ~20kHz: without it the loop has
    // gain at the sample rate's Nyquist frequency and, closed through a one-sample
    // delay, rings there. Judgment calls except the 47pF between the plates, which
    // is on the drawing; being between two plates that swing in antiphase it looks
    // like twice that to ground from each.
    // A Zobel network (R = the transformer tap's impedance, R*C = 50us) across the
    // speaker. A real amp is kept stable by parasitic poles this model does not list
    // one by one; without something taking their place the loop's gain RISES with
    // frequency (the voice coil's inductance climbs while the output tubes act as
    // current sources) and it rings near 20-30kHz - which the measured loop gain in
    // Tests/MarshallPlexiPowerAmpTest.cpp shows. The Zobel flattens the load above
    // ~3kHz and, with the feedback's own band limit, gives the loop margin. It is
    // sized to the TAP rather than the cabinet so that the high-frequency loop gain
    // (which then scales as sqrt(Zaa * tap)) is lowest at the low taps whatever is
    // plugged in: a 4 ohm tap on a 16 ohm cabinet - a mismatch, but one a user can
    // make - stays stable.
    constexpr double zobelSeconds = 50.0e-6;
    constexpr double plateFarads = 2.0 * 47.0e-12 + 10.0e-12;
    constexpr double driverGridFarads = 60.0e-12;      // two EL34 grids, Miller-multiplied

    double smoothPositive(double v, double width) noexcept
    {
        auto x = v / width;
        return x > 30.0 ? v : width * std::log1p(std::exp(x));
    }

    double sigmoid(double x) noexcept
    {
        return x > 30.0 ? 1.0 : (x < -30.0 ? 0.0 : 1.0 / (1.0 + std::exp(-x)));
    }

    // The plate-swing bound. In a push-pull stage the conducting tube's plate cannot
    // go below ~0V, so by transformer action the other plate cannot rise above
    // ~2*B+: the internal secondary voltage stays within +/- B+/(a/2). Inductive
    // energy (the core, the leakage, the voice coil) would otherwise let the model
    // exceed it - real amps arc over or absorb it in stray capacitance. A stiff
    // soft-knee shunt at the internal node enforces it: it draws (almost) nothing
    // below the limit and a great deal of current above.
    constexpr double clampSiemens = 0.2;

    double clampCurrent(double x, double limit, double& slope) noexcept
    {
        auto width = 0.03 * limit;
        auto over = (std::abs(x) - limit) / width;
        if (over < -30.0)
        {
            slope = 0.0;
            return 0.0;
        }
        auto magnitude = over > 30.0 ? over * width : width * std::log1p(std::exp(over));
        slope = sigmoid(over);
        return std::copysign(clampSiemens * magnitude, x);
    }

    // Solve A d = b in place (Gaussian elimination, partial pivoting), 5x5.
    bool solve5(double a[5][5], double b[5]) noexcept
    {
        for (int col = 0; col < 5; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < 5; ++r)
                if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
                    pivot = r;
            if (std::abs(a[pivot][col]) < 1.0e-30)
                return false;
            if (pivot != col)
            {
                for (int c = 0; c < 5; ++c) std::swap(a[col][c], a[pivot][c]);
                std::swap(b[col], b[pivot]);
            }
            for (int r = col + 1; r < 5; ++r)
            {
                auto f = a[r][col] / a[col][col];
                for (int c = col; c < 5; ++c) a[r][c] -= f * a[col][c];
                b[r] -= f * b[col];
            }
        }
        for (int r = 4; r >= 0; --r)
        {
            auto s = b[r];
            for (int c = r + 1; c < 5; ++c) s -= a[r][c] * b[c];
            b[r] = s / a[r][r];
        }
        return true;
    }
}

MarshallPlexiPowerAmp::MarshallPlexiPowerAmp(double sampleRate)
    : fs(sampleRate), period(1.0 / sampleRate)
{
    // Grid 2's high-pass (1M x .1uF), bilinear.
    auto tau = grid2LeakOhms * grid2CouplingFarads;
    hpB0 = 2.0 * tau / (period + 2.0 * tau);
    hpA1 = (2.0 * tau - period) / (2.0 * tau + period);
    feedbackAlpha = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * feedbackBandwidthHz / sampleRate);

    // The feedback junction: 47k in from the speaker, a 5k Presence pot to
    // ground with a .1uF from its wiper to ground.
    jNode = jNet.addNode();
    auto wiper = jNet.addNode();
    jNet.addResistor(Net::source, jNode, feedbackOhms);
    presenceUpper = jNet.addResistor(jNode, wiper, presencePotOhms * 0.5);
    presenceLower = jNet.addResistor(wiper, Net::ground, presencePotOhms * 0.5);
    jNet.addCapacitor(wiper, Net::ground, presenceFarads);
    jNet.prepare(sampleRate);

    load.prepare(sampleRate);
    OutputLoad::Transformer transformer;
    transformer.windingFarads = 150.0e-12;   // judgment: plate-to-plate winding capacitance
    transformer.saturationVoltSeconds = 3.1; // judgment: core saturates near 30Hz at full power (see OutputLoad)
    load.setTransformer(transformer);
    setSpeaker(AmpSpeakerLoad::Speaker {}, 16.0);
    setImpedanceTap(16.0);

    computeOperatingPoint();
    updatePresence();
    resetState();
}

void MarshallPlexiPowerAmp::setPresence(float position) noexcept
{
    presence = std::min(1.0f, std::max(0.0f, position));
    if (! started)
    {
        // Nothing has run yet: apply it outright and let the junction settle to it.
        presenceNow = static_cast<double>(presence);
        updatePresence();
        settleJunction();
    }
}

// Wiper toward J (Presence up) puts the .1uF across J at high frequencies. A pot
// has a little contact resistance at its ends, which also keeps the junction's
// capacitor from being shorted through nothing.
void MarshallPlexiPowerAmp::updatePresence() noexcept
{
    constexpr double contactOhms = 20.0;
    jNet.setResistance(presenceUpper, presencePotOhms * (1.0 - presenceNow) + contactOhms);
    jNet.setResistance(presenceLower, presencePotOhms * presenceNow + contactOhms);
    jNet.refresh();
}

// The junction at its DC steady state: the tail current into 47k || the Presence
// pot, with the .1uF charged to match.
void MarshallPlexiPowerAmp::settleJunction() noexcept
{
    jNet.reset();
    for (int i = 0, n = static_cast<int>(0.05 * fs); i < n; ++i)
    {
        jNet.solveFree(0.0);
        jNet.commit(jNode, idle.piTailAmps);
    }
    vJPrev = jNet.voltage(jNode);
}

void MarshallPlexiPowerAmp::setImpedanceTap(double ohms) noexcept
{
    load.setTap(ohms);
    load.setZobel(ohms, zobelSeconds / ohms);
}

// The connected cabinet (the Zobel network is sized to the transformer's tap, not
// to the cabinet: see setImpedanceTap).
void MarshallPlexiPowerAmp::setSpeaker(const AmpSpeakerLoad::Speaker& speaker, double nominalOhms)
{
    load.setSpeaker(speaker, nominalOhms);
}

// The DC operating point of everything: the bias that gives the EL34s their idle
// current, the phase inverter's tail current and plate voltages, and the supply's
// open-circuit voltage that makes B+ come out at +460V under the idle load.
void MarshallPlexiPowerAmp::computeOperatingPoint()
{
    // Output tubes: the grid bias for idleTubeAmps at +460V (the screen sits a
    // hair below that: Ig2 is ~0 at this bias).
    double lo = -150.0, hi = 0.0;
    for (int i = 0; i < 80; ++i)
    {
        auto mid = 0.5 * (lo + hi);
        (KorenPentode::plate(mid, idleSupply, idleSupply).i > idleTubeAmps ? hi : lo) = mid;
    }
    idle.biasVolts = 0.5 * (lo + hi);
    idle.tubeAmps = KorenPentode::plate(idle.biasVolts, idleSupply, idleSupply).i;
    idle.supplyVolts = idleSupply;

    // Phase inverter: both grids sit at the cathode's bias through their leaks,
    // i.e. -470 ohms * the tail current below the cathode; the tail current flows
    // through 470 + 10k + (47k || the 5k Presence pot) to ground.
    auto rDc = cathodeToJ + 1.0 / (1.0 / feedbackOhms + 1.0 / presencePotOhms);
    auto triodeAmps = [&](double gridToCathode, double cathodeVolts, double plateLoad) {
        double a = 0.0, b = (piSupply - cathodeVolts) / plateLoad;
        for (int i = 0; i < 80; ++i)
        {
            auto mid = 0.5 * (a + b);
            (T::plateCurrent(gridToCathode, piSupply - cathodeVolts - plateLoad * mid) > mid ? a : b) = mid;
        }
        return 0.5 * (a + b);
    };
    double a = 0.0, b = 0.01;
    for (int i = 0; i < 80; ++i)
    {
        auto tail = 0.5 * (a + b);
        auto total = triodeAmps(-piCathodeOhms * tail, rDc * tail, plateLoad1) + triodeAmps(-piCathodeOhms * tail, rDc * tail, plateLoad2);
        (total > tail ? a : b) = tail;
    }
    idle.piTailAmps = 0.5 * (a + b);
    idle.piCathodeVolts = rDc * idle.piTailAmps;
    cathodeDc = idle.piCathodeVolts;
    gridDc = cathodeDc - piCathodeOhms * idle.piTailAmps;
    amps1Dc = triodeAmps(-piCathodeOhms * idle.piTailAmps, cathodeDc, plateLoad1);
    amps2Dc = triodeAmps(-piCathodeOhms * idle.piTailAmps, cathodeDc, plateLoad2);
    idle.piPlate1Volts = piSupply - plateLoad1 * amps1Dc;
    idle.piPlate2Volts = piSupply - plateLoad2 * amps2Dc;

    // The supply: what the phase inverter and preamp draw through the 20k dropper,
    // plus four tubes' idle current, times the supply's resistance, above +460V.
    otherAmps = (idleSupply - piSupply) / piDropperOhms;
    supplyOpenVolts = idleSupply + supplyOhms * (4.0 * idle.tubeAmps + otherAmps);
}

void MarshallPlexiPowerAmp::resetState()
{
    vb = idleSupply;
    vg2A = vg2B = idleSupply;
    vNode1 = vNode2 = vgA = vgB = idle.biasVolts;

    u[0] = amps1Dc; u[1] = amps2Dc; u[2] = 0.0; u[3] = idle.biasVolts; u[4] = idle.biasVolts;

    vcap1 = idle.piPlate1Volts - idle.biasVolts; icap1 = 0.0;
    vcap2 = idle.piPlate2Volts - idle.biasVolts; icap2 = 0.0;
    vpPrev1 = idle.piPlate1Volts; vpPrev2 = idle.piPlate2Volts;
    icp1 = icp2 = 0.0;
    vnPrev1 = vnPrev2 = idle.biasVolts;
    icn1 = icn2 = 0.0;

    started = false;
    presenceNow = static_cast<double>(presence);
    updatePresence();
    settleJunction();
    y2 = 0.0;

    load.reset();
    load.rebuildStep();
    xPrev = 0.0;
    feedbackPrev = 0.0;
    feedbackPole1 = feedbackPole2 = 0.0;
    terminalVolts = 0.0;
    lastAmpsA = lastAmpsB = 2.0 * idle.tubeAmps;
}

void MarshallPlexiPowerAmp::reset() { resetState(); }

double MarshallPlexiPowerAmp::process(double stackFree, double stackOhms) noexcept
{
    const double gc = 2.0 * couplingFarads * fs;                 // the coupling caps' companion conductance
    const double gcp = 2.0 * plateFarads * fs;                   // ... the plate capacitance's
    const double gcn = 2.0 * driverGridFarads * fs;              // ... the driver grids'
    const double hist1 = gc * vcap1 + icap1;
    const double hist2 = gc * vcap2 + icap2;
    const double histP1 = gcp * vpPrev1 + icp1, histP2 = gcp * vpPrev2 + icp2;
    const double histN1 = gcn * vnPrev1 + icn1, histN2 = gcn * vnPrev2 + icn2;
    const double leak = 1.0 / gridLeakOhms;
    const double outOn = outGridOnOhms + stopperOhms;
    const double biasRail = idle.biasVolts;

    // ---- Presence, slewed: a knob move is a change of the pot's resistances, and
    // the junction's capacitor must follow it continuously. ----
    started = true;
    if (presenceNow != static_cast<double>(presence))
    {
        auto step = period / 0.02;                       // a full sweep takes 20ms
        auto target = static_cast<double>(presence);
        presenceNow += std::min(step, std::max(-step, target - presenceNow));
        updatePresence();
    }

    // ---- The feedback junction, as it stands before this sample's tail current. ----
    jNet.solveFree(feedbackEnabled ? feedbackPrev : feedbackInjected);
    const double vJfree = jNet.freeVoltage(jNode);
    const double zJJ = jNet.transferOhms(jNode, jNode);
    const double rk = cathodeToJ + zJJ;

    // ---- Phase inverter and drivers: Newton on (i1, i2, gridAmps, vNode1, vNode2). ----
    struct Side { double icc, diccDi, diccDn, vp, dvpDi, dvpDn; };
    auto couple = [&](double i, double n, double plateLoad, double hist, double histPlate) {
        // The plate node: B+ through the load = the tube's current i + the plate
        // capacitance's + the coupling cap's, which runs on to the driver-grid
        // node n:  (B - vp)/RL = i + (gcp vp - histPlate) + (gc (vp - n) - hist).
        auto gt = 1.0 / plateLoad + gc + gcp;
        Side s;
        s.vp = (piSupply / plateLoad - i + gc * n + hist + histPlate) / gt;
        s.dvpDi = -1.0 / gt;
        s.dvpDn = gc / gt;
        s.icc = gc * (s.vp - n) - hist;
        s.diccDi = gc * s.dvpDi;
        s.diccDn = gc * (s.dvpDn - 1.0);
        return s;
    };

    double i1 = u[0], i2 = u[1], ig = u[2], n1 = u[3], n2 = u[4];
    for (int iter = 0; iter < 20; ++iter)
    {
        // Not converged after ten tries (a large kick put the guess somewhere the
        // tubes' curves are flat): start over from the operating point.
        if (iter == 10)
        {
            i1 = amps1Dc; i2 = amps2Dc; ig = 0.0; n1 = n2 = idle.biasVolts;
        }
        auto ik = i1 + i2;
        auto vk = vJfree + rk * ik;
        auto vJ = vJfree + zJJ * ik;
        auto vG1 = gridDc + stackFree - stackOhms * ig;
        auto vG2 = gridDc + hpA1 * y2 + hpB0 * (vJ - vJPrev);
        auto s1 = couple(i1, n1, plateLoad1, hist1, histP1);
        auto s2 = couple(i2, n2, plateLoad2, hist2, histP2);
        auto t1 = T::plateCurrentEval(vG1 - vk, s1.vp - vk);
        auto t2 = T::plateCurrentEval(vG2 - vk, s2.vp - vk);

        auto vgk1 = vG1 - vk;
        auto gridConduction = smoothPositive(vgk1, piGridKnee) / piGridOnOhms;
        auto dGridConduction = sigmoid(vgk1 / piGridKnee) / piGridOnOhms;
        auto igp1 = 2.0 * smoothPositive(n1, outGridKnee) / outOn, digp1 = 2.0 * sigmoid(n1 / outGridKnee) / outOn;
        auto igp2 = 2.0 * smoothPositive(n2, outGridKnee) / outOn, digp2 = 2.0 * sigmoid(n2 / outGridKnee) / outOn;

        double f[5] = { i1 - t1.i,
                        i2 - t2.i,
                        ig - gridConduction,
                        s1.icc - (n1 - biasRail) * leak - igp1 - (gcn * n1 - histN1),
                        s2.icc - (n2 - biasRail) * leak - igp2 - (gcn * n2 - histN2) };

        auto worst = 0.0;
        for (auto v : f) worst = std::max(worst, std::abs(v));
        if (worst < 1.0e-10)
            break;

        double g2 = hpB0 * zJJ;   // d(vG2)/d(ik)
        double a[5][5] = {
            { 1.0 - (t1.gm * -rk + t1.gp * (s1.dvpDi - rk)), -(t1.gm * -rk + t1.gp * -rk), t1.gm * stackOhms, -t1.gp * s1.dvpDn, 0.0 },
            { -(t2.gm * (g2 - rk) + t2.gp * -rk), 1.0 - (t2.gm * (g2 - rk) + t2.gp * (s2.dvpDi - rk)), 0.0, 0.0, -t2.gp * s2.dvpDn },
            { dGridConduction * rk, dGridConduction * rk, 1.0 + dGridConduction * stackOhms, 0.0, 0.0 },
            { s1.diccDi, 0.0, 0.0, s1.diccDn - leak - digp1 - gcn, 0.0 },
            { 0.0, s2.diccDi, 0.0, 0.0, s2.diccDn - leak - digp2 - gcn } };
        double rhs[5] = { -f[0], -f[1], -f[2], -f[3], -f[4] };
        if (! solve5(a, rhs))
            break;

        // Limit the step (a few mA, tens of volts) so a bad guess can't fling the
        // iteration off the tubes' curves.
        double scale = 1.0;
        for (int k = 0; k < 3; ++k) scale = std::min(scale, 4.0e-3 / std::max(std::abs(rhs[k]), 1.0e-30));
        for (int k = 3; k < 5; ++k) scale = std::min(scale, 60.0 / std::max(std::abs(rhs[k]), 1.0e-30));
        i1 = std::max(0.0, i1 + scale * rhs[0]);
        i2 = std::max(0.0, i2 + scale * rhs[1]);
        ig += scale * rhs[2];
        n1 += scale * rhs[3];
        n2 += scale * rhs[4];
    }
    u[0] = i1; u[1] = i2; u[2] = ig; u[3] = n1; u[4] = n2;

    // Commit the phase-inverter stage's memory.
    {
        auto ik = i1 + i2;
        auto vJ = vJfree + zJJ * ik;
        auto s1 = couple(i1, n1, plateLoad1, hist1, histP1);
        auto s2 = couple(i2, n2, plateLoad2, hist2, histP2);
        jNet.commit(jNode, ik);
        y2 = hpA1 * y2 + hpB0 * (vJ - vJPrev);
        vJPrev = vJ;
        vcap1 = s1.vp - n1; icap1 = s1.icc;
        vcap2 = s2.vp - n2; icap2 = s2.icc;
        icp1 = gcp * s1.vp - histP1; vpPrev1 = s1.vp;
        icp2 = gcp * s2.vp - histP2; vpPrev2 = s2.vp;
        icn1 = gcn * n1 - histN1; vnPrev1 = n1;
        icn2 = gcn * n2 - histN2; vnPrev2 = n2;
        vNode1 = n1; vNode2 = n2;

        // The output tubes' control grids: the node less the drop across the stopper.
        vgA = n1 - stopperOhms * (smoothPositive(n1, outGridKnee) / outOn);
        vgB = n2 - stopperOhms * (smoothPositive(n2, outGridKnee) / outOn);
    }

    // ---- Output stage: Newton on the transformer's internal voltage x. ----
    const double half = 0.5 * load.turnsRatio();
    const double dInt = load.internalD(), hInt = load.internalH();
    const double xLimit = vb / half;   // see clampCurrent()
    double x = xPrev, iA = 0.0, iB = 0.0, iLoad = 0.0;
    for (int iter = 0; iter < 40; ++iter)
    {
        auto pa = KorenPentode::plate(vgA, vb + half * x, vg2A);
        auto pb = KorenPentode::plate(vgB, vb - half * x, vg2B);
        iA = 2.0 * pa.i;
        iB = 2.0 * pb.i;
        double clampSlope = 0.0;
        iLoad = -half * (iA - iB) - load.shuntCurrent(x) - clampCurrent(x, xLimit, clampSlope);
        auto residual = x - (dInt * iLoad + hInt);
        auto slope = 1.0 + dInt * (2.0 * half * half * (pa.gp + pb.gp) + load.shuntSlope(x) + clampSlope);
        auto dx = -residual / slope;
        // Step limit: the stiff clamp and the tubes' curves make an undamped step
        // overshoot into the clamp's far side. A fraction of the plate-swing bound
        // per iteration is plenty (the solution is within it) and cannot run away.
        auto maxStep = 0.5 * xLimit + 5.0;
        dx = std::min(maxStep, std::max(-maxStep, dx));
        x += dx;
        if (std::abs(dx) < 1.0e-9)
            break;
    }
    {
        // The last update moved x a hair: refresh the currents at the settled point.
        auto pa = KorenPentode::plate(vgA, vb + half * x, vg2A);
        auto pb = KorenPentode::plate(vgB, vb - half * x, vg2B);
        iA = 2.0 * pa.i;
        iB = 2.0 * pb.i;
        double unused = 0.0;
        iLoad = -half * (iA - iB) - load.shuntCurrent(x) - clampCurrent(x, xLimit, unused);
    }

    terminalVolts = load.terminalD() * iLoad + load.terminalH();
    load.commit(iLoad, x);
    xPrev = x;

    // The feedback voltage, band-limited (two poles). The tube pair is nearly a
    // current source, so into an inductive speaker the loop gain RISES with
    // frequency until stray capacitance, the transformer's winding capacitance and
    // the like bring it down - poles this model does not list one by one. Closed
    // through a one-sample delay the loop needs them, so they are represented by
    // this filter.
    feedbackPole1 += feedbackAlpha * (terminalVolts - feedbackPole1);
    feedbackPole2 += feedbackAlpha * (feedbackPole1 - feedbackPole2);
    feedbackPrev = feedbackPole2;
    lastAmpsA = iA;
    lastAmpsB = iB;

    // ---- Screens and supply. ----
    auto screenA = KorenPentode::screen(vgA, vg2A), screenB = KorenPentode::screen(vgB, vg2B);
    vg2A = vb - screenOhms * screenA;
    vg2B = vb - screenOhms * screenB;
    auto supplyAmps = iA + iB + 2.0 * (screenA + screenB) + otherAmps;
    vb += (period / reservoirFarads) * ((supplyOpenVolts - vb) / supplyOhms - supplyAmps);

    return ig;
}
