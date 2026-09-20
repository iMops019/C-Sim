#include "HoofStage.h"

#include "PotTaper.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double thermalVoltage = 0.02585;
    constexpr double maxExponent = 80.0;

    // How long a knob takes to glide to a new position (about 2.2 x this to cover 90% of the way).
    constexpr double smoothingSeconds = 0.008;
}

// -------------------------------------------------------------------------
// Construction: the netlist
// -------------------------------------------------------------------------

HoofStage::HoofStage(double sampleRateToUse, Components components)
    : comp(components), sampleRate(sampleRateToUse), oversampler(sampleRateToUse)
{
    build();
}

void HoofStage::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampler = Oversampler4x(sampleRate);
    build();
}

void HoofStage::build()
{
    net = MnaNetwork();
    auto& c = comp;
    auto gnd = MnaNetwork::ground;

    for (auto* node : { &n.x1, &n.ba, &n.ca, &n.ea, &n.p, &n.wp, &n.sb, &n.wc, &n.bb, &n.cb, &n.eb, &n.lb, &n.xb,
                        &n.bc, &n.cc, &n.ec, &n.lc, &n.tl, &n.th, &n.tw, &n.bd, &n.cd, &n.ed, &n.vt, &n.vw })
        *node = net.addNode();

    // Stage A - input booster.
    net.addResistor(MnaNetwork::source, n.x1, c.inputR);
    net.addCapacitor(n.x1, n.ba, c.inputC);
    net.addResistor(n.ba, gnd, c.baseBiasR);
    net.addResistor(n.ca, n.ba, c.feedbackR);
    net.addCapacitor(n.ca, n.ba, c.feedbackC);
    net.addBiasResistor(n.ca, c.collectorR, c.supplyVolts);
    net.addResistor(n.ea, gnd, c.emitterR);

    // Sustain: coupling capacitor, the pot (with its 2.2k to ground), coupling capacitor, 8.2k.
    net.addCapacitor(n.ca, n.p, c.couplingC);
    pot.fuzzTop = net.addResistor(n.p, n.wp, 1.0);
    pot.fuzzBottom = net.addResistor(n.wp, n.sb, 1.0);
    net.addResistor(n.sb, gnd, c.fuzzBottomR);
    net.addCapacitor(n.wp, n.wc, c.couplingC);
    net.addResistor(n.wc, n.bb, c.interstageR);

    // Each LED pair sits behind a capacitor, so its own DC voltage is set only by leakage - a red
    // LED's saturation current is so small that even the simulator's picoamp gmin would bias it
    // toward conduction. A 1G resistor across it pins that DC voltage at the zero a real, leakage-
    // -free circuit has, and is invisible at audio frequencies.
    net.addResistor(n.lb, n.bb, 1.0e9);
    net.addResistor(n.lc, n.bc, 1.0e9);

    // Stage B - first clipping stage (the LED pair joins lb to bb as a nonlinear port).
    net.addResistor(n.bb, gnd, c.baseBiasR);
    net.addResistor(n.cb, n.bb, c.feedbackR);
    net.addCapacitor(n.cb, n.bb, c.feedbackC);
    net.addBiasResistor(n.cb, c.collectorR, c.supplyVolts);
    net.addResistor(n.eb, gnd, c.emitterR);
    net.addCapacitor(n.cb, n.lb, c.clipC);
    net.addCapacitor(n.cb, n.xb, c.couplingC);
    net.addResistor(n.xb, n.bc, c.interstageR);

    // Stage C - second clipping stage.
    net.addResistor(n.bc, gnd, c.baseBiasR);
    net.addResistor(n.cc, n.bc, c.feedbackR);
    net.addCapacitor(n.cc, n.bc, c.feedbackC);
    net.addBiasResistor(n.cc, c.collectorR, c.supplyVolts);
    net.addResistor(n.ec, gnd, c.emitterR);
    net.addCapacitor(n.cc, n.lc, c.clipC);

    // Tone stack, straight off stage C's collector.
    net.addResistor(n.cc, n.tl, c.lowPassR);
    net.addCapacitor(n.tl, gnd, c.lowPassC);
    net.addCapacitor(n.cc, n.th, c.highPassC);
    pot.shift = net.addResistor(n.th, gnd, 1.0);
    pot.toneLow = net.addResistor(n.tl, n.tw, 1.0);
    pot.toneHigh = net.addResistor(n.tw, n.th, 1.0);
    net.addCapacitor(n.tw, n.bd, c.couplingC);

    // Stage D - recovery, then Volume.
    net.addBiasResistor(n.bd, c.recoveryBiasHighR, c.supplyVolts);
    net.addResistor(n.bd, gnd, c.recoveryBiasLowR);
    net.addBiasResistor(n.cd, c.recoveryCollectorR, c.supplyVolts);
    net.addResistor(n.ed, gnd, c.recoveryEmitterR);
    net.addCapacitor(n.cd, n.vt, c.couplingC);
    pot.volumeTop = net.addResistor(n.vt, n.vw, 1.0);
    pot.volumeBottom = net.addResistor(n.vw, gnd, 1.0);
    net.addResistor(n.vw, gnd, c.loadOhms);

    net.prepare(sampleRate * 4.0);

    // ---- The ten nonlinear device ports ----
    // Ports 2k and 2k+1 (k = 0..3): transistor k's base-emitter (forward) and base-collector
    // (reverse) junctions, for stages A, B, C and the recovery stage D.
    // Ports 8, 9: the two LED pairs.
    // Ebers-Moll: Ic = If - Ir (1 + 1/betaR), Ib = If/betaF + Ir/betaR, Ie = Ic + Ib.
    auto transistor = [&](int port, int collector, int base, int emitter, DeviceParameters params) {
        auto f = static_cast<size_t>(port), r = f + 1;
        portNodes[f] = { Pattern { base, 1.0 }, Pattern { emitter, -1.0 } };
        injection[f] = { Pattern { collector, -1.0 }, Pattern { base, -1.0 / params.beta }, Pattern { emitter, 1.0 + 1.0 / params.beta } };
        injectionCount[f] = 3;
        device[f] = params;
        kind[f] = 0;

        portNodes[r] = { Pattern { base, 1.0 }, Pattern { collector, -1.0 } };
        injection[r] = { Pattern { collector, 1.0 + 1.0 / reverseBeta }, Pattern { base, -1.0 / reverseBeta }, Pattern { emitter, -1.0 } };
        injectionCount[r] = 3;
        device[r] = params;
        kind[r] = 0;
    };
    transistor(0, n.ca, n.ba, n.ea, silicon());
    transistor(2, n.cb, n.bb, n.eb, germanium());
    transistor(4, n.cc, n.bc, n.ec, germanium());
    transistor(6, n.cd, n.bd, n.ed, silicon());

    auto led = [&](int port, int mid, int base) {
        auto i = static_cast<size_t>(port);
        portNodes[i] = { Pattern { mid, 1.0 }, Pattern { base, -1.0 } };
        injection[i] = { Pattern { mid, -1.0 }, Pattern { base, 1.0 }, Pattern { 0, 0.0 } };
        injectionCount[i] = 2;
        device[i] = redLed();
        kind[i] = 1;
    };
    led(8, n.lb, n.bb);
    led(9, n.lc, n.bc);

    for (size_t p = 0; p < numPorts; ++p)
    {
        portNVt[p] = device[p].ideality * thermalVoltage;
        portCritical[p] = portNVt[p] * std::log(portNVt[p] / (std::sqrt(2.0) * device[p].saturation));
    }

    smoothing = 1.0 - std::exp(-1.0 / (sampleRate * 4.0 * smoothingSeconds));
    snapKnobs();
    net.refresh();
    computeCoupling();
    reset();
}

// -------------------------------------------------------------------------
// Pots
// -------------------------------------------------------------------------

double HoofStage::volumeFraction(const Components& c, double level)
{
    return potFraction(level, c.volumeMid);
}

void HoofStage::applyPots(double fuzzAmount, double toneAmount, double shiftAmount, double levelAmount)
{
    auto f = fuzzWiperFraction(static_cast<float>(fuzzAmount));
    net.setResistance(pot.fuzzTop, (1.0 - f) * comp.fuzzPot);
    net.setResistance(pot.fuzzBottom, f * comp.fuzzPot);

    net.setResistance(pot.toneLow, toneAmount * comp.tonePot);
    net.setResistance(pot.toneHigh, (1.0 - toneAmount) * comp.tonePot);
    net.setResistance(pot.shift, comp.shiftFixedR + shiftAmount * comp.shiftPot);

    auto v = volumeFraction(comp, levelAmount);
    net.setResistance(pot.volumeTop, (1.0 - v) * comp.volumePot);
    net.setResistance(pot.volumeBottom, v * comp.volumePot);
}

void HoofStage::snapKnobs()
{
    fuzzNow = fuzz;
    toneNow = tone;
    shiftNow = shift;
    levelNow = level;
    potUpdateCounter = 0;
    applyPots(fuzzNow, toneNow, shiftNow, levelNow);
}

void HoofStage::setFuzz(float amount)
{
    fuzz = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

void HoofStage::setTone(float amount)
{
    tone = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

void HoofStage::setShift(float amount)
{
    shift = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

void HoofStage::setLevel(float amount)
{
    level = std::clamp(static_cast<double>(amount), 0.0, 1.0);
}

// -------------------------------------------------------------------------
// The nonlinear ports
// -------------------------------------------------------------------------

void HoofStage::computeCoupling()
{
    // K[p][q] = the voltage port p sees per unit of device current q: the sum, over the nodes
    // whose voltages make up port p and the nodes device current q injects into, of the
    // network's transfer resistance (with the injection's amount and the port's +/- sign).
    for (size_t p = 0; p < numPorts; ++p)
        for (size_t q = 0; q < numPorts; ++q)
        {
            double k = 0.0;
            for (const auto& pn : portNodes[p])
                for (int i = 0; i < injectionCount[q]; ++i)
                    k += pn.amount * injection[q][static_cast<size_t>(i)].amount
                         * net.inverseEntry(pn.node, injection[q][static_cast<size_t>(i)].node);
            coupling[p][q] = k;
        }
}

void HoofStage::solvePorts(int maxIterations, double tolerance) noexcept
{
    constexpr int np = numPorts;

    // Node voltages with no device current, seen through each port.
    double vFree[np];
    for (int p = 0; p < np; ++p)
        vFree[p] = portNodes[static_cast<size_t>(p)][0].amount * net.voltage(portNodes[static_cast<size_t>(p)][0].node)
                 + portNodes[static_cast<size_t>(p)][1].amount * net.voltage(portNodes[static_cast<size_t>(p)][1].node);

    double v[np], j[np], s[np];
    for (int p = 0; p < np; ++p)
        v[p] = portVolts[static_cast<size_t>(p)];

    int used = 0;
    for (int it = 0; it < maxIterations; ++it)
    {
        used = it + 1;
        // The device currents and their slopes at the current port voltages: one exp() per device.
        for (int p = 0; p < np; ++p)
        {
            const auto& d = device[static_cast<size_t>(p)];
            auto nVt = d.ideality * thermalVoltage;
            auto e = std::exp(std::clamp(v[p] / nVt, -maxExponent, maxExponent));
            if (kind[static_cast<size_t>(p)] == 0)
            {
                j[p] = d.saturation * (e - 1.0);
                s[p] = d.saturation / nVt * e;
            }
            else
            {
                auto inverse = 1.0 / e;
                j[p] = d.saturation * (e - inverse);
                s[p] = d.saturation / nVt * (e + inverse);
            }
        }

        // F(v) = v - vFree - K j(v) = 0, J = I - K diag(s).
        double a[np][np], rhs[np];
        for (int p = 0; p < np; ++p)
        {
            double f = v[p] - vFree[p];
            for (int q = 0; q < np; ++q)
            {
                auto k = coupling[static_cast<size_t>(p)][static_cast<size_t>(q)];
                f -= k * j[q];
                a[p][q] = (p == q ? 1.0 : 0.0) - k * s[q];
            }
            rhs[p] = -f;
        }

        // Gaussian elimination with partial pivoting.
        for (int col = 0; col < np; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < np; ++r)
                if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
                    pivot = r;
            if (pivot != col)
            {
                for (int c2 = 0; c2 < np; ++c2)
                    std::swap(a[col][c2], a[pivot][c2]);
                std::swap(rhs[col], rhs[pivot]);
            }
            for (int r = col + 1; r < np; ++r)
            {
                auto factor = a[r][col] / a[col][col];
                for (int c2 = col; c2 < np; ++c2)
                    a[r][c2] -= factor * a[col][c2];
                rhs[r] -= factor * rhs[col];
            }
        }
        double delta[np];
        for (int r = np - 1; r >= 0; --r)
        {
            auto sum = rhs[r];
            for (int c2 = r + 1; c2 < np; ++c2)
                sum -= a[r][c2] * delta[c2];
            delta[r] = sum / a[r][r];
        }

        // Newton's proposed new port voltages, limited the way SPICE does (pnjlim): near the
        // solution the step is taken as is, but a big jump for an exponential device is tamed
        // on a log scale - otherwise a step down an exponential moves only nVt, and one up
        // overshoots by volts. The fixed point is unchanged; only the path to it is.
        double step[np];
        double biggest = 0.0;
        for (int p = 0; p < np; ++p)
        {
            auto pi = static_cast<size_t>(p);
            auto nVt = portNVt[pi];
            auto sign = 1.0;
            auto vNew = v[p] + delta[p], vOld = v[p];
            if (kind[pi] == 1 && vNew < 0.0) // the LED pair is symmetric: limit its magnitude
            {
                sign = -1.0;
                vNew = -vNew;
                vOld = -vOld;
            }
            if (vNew > portCritical[pi] && std::abs(vNew - vOld) > 2.0 * nVt)
            {
                if (vOld > 0.0)
                {
                    auto arg = 1.0 + (vNew - vOld) / nVt;
                    vNew = arg > 0.0 ? vOld + nVt * std::log(arg) : portCritical[pi];
                }
                else
                    vNew = nVt * std::log(vNew / nVt);
            }
            step[p] = sign * vNew - v[p];
            biggest = std::max(biggest, std::abs(step[p]));
        }
        for (int p = 0; p < np; ++p)
            v[p] += step[p];

        // Converged: Newton's error after a step of size d is about d^2 / (2 n Vt), so once a
        // step is under `tolerance` (100uV for audio) the port voltages are good to ~0.2uV - far
        // below any signal that matters - and the confirming iteration can be skipped. The
        // currents are brought up to the new voltages with the slopes already in hand.
        if (biggest < tolerance)
        {
            for (int p = 0; p < np; ++p)
                j[p] += s[p] * step[p];
            break;
        }
    }

    for (int p = 0; p < np; ++p)
    {
        portVolts[static_cast<size_t>(p)] = v[p];
        deviceCurrents[static_cast<size_t>(p)] = j[p];
    }

    // Diagnostics: how far the final voltages are from satisfying v = vFree + K j.
    double worst = 0.0;
    for (int p = 0; p < np; ++p)
    {
        double f = v[p] - vFree[p];
        for (int q = 0; q < np; ++q)
            f -= coupling[static_cast<size_t>(p)][static_cast<size_t>(q)] * j[q];
        worst = std::max(worst, std::abs(f));
    }
    stats.worstResidual = std::max(stats.worstResidual, worst);
    stats.mostIterations = std::max(stats.mostIterations, used);
    stats.peakLedVolts = std::max(stats.peakLedVolts, std::max(std::abs(v[8]), std::abs(v[9])));
}

// -------------------------------------------------------------------------
// DC operating point, and processing
// -------------------------------------------------------------------------

void HoofStage::solveDc()
{
    snapKnobs();
    net.reset();
    net.setCapacitorsOpen(true);
    net.refresh();
    computeCoupling();

    portVolts = { 0.65, -4.0, 0.22, -4.0, 0.22, -4.0, 0.65, -4.0, 0.0, 0.0 };
    net.solveFree(0.0);
    solvePorts(500, 1.0e-13);

    // Put the device currents into the network to read the node voltages.
    for (size_t q = 0; q < numPorts; ++q)
        for (int i = 0; i < injectionCount[q]; ++i)
            net.injectCurrent(injection[q][static_cast<size_t>(i)].node, injection[q][static_cast<size_t>(i)].amount * deviceCurrents[q]);

    auto volts = [&](int node) { return net.voltage(node); };
    dcPoint.baseVolts = { volts(n.ba), volts(n.bb), volts(n.bc), volts(n.bd) };
    dcPoint.collectorVolts = { volts(n.ca), volts(n.cb), volts(n.cc), volts(n.cd) };
    dcPoint.emitterVolts = { volts(n.ea), volts(n.eb), volts(n.ec), volts(n.ed) };
    for (size_t k = 0; k < 4; ++k)
        dcPoint.collectorAmps[k] = deviceCurrents[2 * k] - deviceCurrents[2 * k + 1] * (1.0 + 1.0 / reverseBeta);

    net.captureCapacitorVoltages();
    net.setCapacitorsOpen(false);
    net.refresh();
    computeCoupling();
}

void HoofStage::reset()
{
    oversampler.reset();
    solveDc();
}

float HoofStage::processOversampledSample(float x) noexcept
{
    // Glide the knobs toward their targets; re-solve the network every 64th sample while moving.
    if (fuzzNow != fuzz || toneNow != tone || shiftNow != shift || levelNow != level)
    {
        auto glide = [this](double& now, double target) {
            now += smoothing * (target - now);
            if (std::abs(target - now) < 1.0e-6)
                now = target;
        };
        glide(fuzzNow, fuzz);
        glide(toneNow, tone);
        glide(shiftNow, shift);
        glide(levelNow, level);
        auto arrived = fuzzNow == fuzz && toneNow == tone && shiftNow == shift && levelNow == level;
        if (++potUpdateCounter >= 64 || arrived)
        {
            potUpdateCounter = 0;
            applyPots(fuzzNow, toneNow, shiftNow, levelNow);
            net.refresh();
            computeCoupling();
        }
    }

    net.solveFree(static_cast<double>(x));
    solvePorts(60, 1.0e-4);

    // Aggregate the device currents per node, then inject once per node.
    std::array<double, 40> injected {};
    for (size_t q = 0; q < numPorts; ++q)
        for (int i = 0; i < injectionCount[q]; ++i)
            injected[static_cast<size_t>(injection[q][static_cast<size_t>(i)].node)] += injection[q][static_cast<size_t>(i)].amount * deviceCurrents[q];
    for (int node : { n.ca, n.ba, n.ea, n.cb, n.bb, n.eb, n.lb, n.cc, n.bc, n.ec, n.lc, n.cd, n.bd, n.ed })
        net.injectCurrent(node, injected[static_cast<size_t>(node)]);

    net.advance();
    return static_cast<float>(net.voltage(n.vw));
}

void HoofStage::processBlock(const float* input, float* output, int numSamples)
{
    oversampler.processBlock(input, output, numSamples,
                             [this](float x) { return processOversampledSample(x); });
}
