#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// A small time-domain solver for linear circuits that need more than passive
// parts: it adds transconductances (a transistor's small-signal model) and
// ideal op-amps to what NodalNetwork does (resistors, capacitors, one driving
// voltage source), by full modified nodal analysis - the op-amp is one extra
// unknown (its output current) and one extra equation (v+ = v-).
//
// Same method as NodalNetwork: capacitors are replaced by their trapezoidal
// companion model (the bilinear transform), so the digital circuit's response
// matches the analog one, and the system matrix - which only changes when a
// component value does - is inverted once and cached. A sample costs one
// matrix-vector product plus a walk over the capacitors. Use it where a
// sub-circuit is linear (the tone stack and level pot, an equaliser built round
// an op-amp, a gyrator) and let hand-written code handle the nonlinear bits.
//
// Node 0 is ground and node 1 is the ideal voltage source driving the network;
// every other node comes from addNode(). Nothing in process() allocates.
//
// For circuits with nonlinear parts (transistors, diodes) it also supports the
// DK method: solveFree() finds the node voltages with nothing injected,
// inverseEntry() gives the network's transfer resistances, the caller solves its
// own nonlinear device equations against those, injectCurrent() adds the currents
// it settled on, and advance() steps the capacitors. addBiasResistor() ties a node
// to a constant rail (a supply) through a resistor, so a circuit can be simulated
// in absolute volts, and setCapacitorsOpen() + captureCapacitorVoltages() find its
// DC operating point and start the capacitors charged to it.
// Framework-agnostic (no JUCE).
class MnaNetwork
{
public:
    static constexpr int ground = 0;
    static constexpr int source = 1;
    static constexpr int maxUnknowns = 32;
    static constexpr double minOhms = 1.0;

    int addNode() noexcept { return nextNode++; }

    int addResistor(int a, int b, double ohms)
    {
        elements.push_back({ Kind::resistor, a, b, 0, 0, std::max(ohms, minOhms), 0.0, 0.0, 0.0 });
        dirty = true;
        return static_cast<int>(elements.size()) - 1;
    }

    // A resistor from `node` to a constant voltage (a supply rail), for simulating in absolute volts.
    int addBiasResistor(int node, double ohms, double railVolts)
    {
        elements.push_back({ Kind::resistor, node, ground, 0, 0, std::max(ohms, minOhms), 0.0, 0.0, 0.0, railVolts });
        dirty = true;
        return static_cast<int>(elements.size()) - 1;
    }

    int addCapacitor(int a, int b, double farads)
    {
        elements.push_back({ Kind::capacitor, a, b, 0, 0, farads, 0.0, 0.0, 0.0 });
        dirty = true;
        return static_cast<int>(elements.size()) - 1;
    }

    // A current gm * (v(controlPlus) - v(controlMinus)) flows through the element
    // from node `from` to node `to` (it leaves `from` and enters `to`).
    int addTransconductance(int from, int to, int controlPlus, int controlMinus, double gm)
    {
        elements.push_back({ Kind::transconductance, from, to, controlPlus, controlMinus, gm, 0.0, 0.0, 0.0 });
        dirty = true;
        return static_cast<int>(elements.size()) - 1;
    }

    // An ideal op-amp: it supplies whatever current into `out` keeps v(plus) == v(minus).
    void addIdealOpAmp(int plus, int minus, int out)
    {
        opAmps.push_back({ plus, minus, out });
        dirty = true;
    }

    void setResistance(int id, double ohms) noexcept
    {
        auto& e = elements[static_cast<size_t>(id)];
        ohms = std::max(ohms, minOhms);
        if (e.value != ohms) { e.value = ohms; dirty = true; }
    }

    void setCapacitance(int id, double farads) noexcept
    {
        auto& e = elements[static_cast<size_t>(id)];
        if (e.value != farads) { e.value = farads; dirty = true; }
    }

    void prepare(double sampleRate)
    {
        period = 1.0 / sampleRate;
        numNodeUnknowns = nextNode - 2;
        numUnknowns = numNodeUnknowns + static_cast<int>(opAmps.size());
        capacitorIndices.clear();
        for (size_t i = 0; i < elements.size(); ++i)
            if (elements[i].kind == Kind::capacitor)
                capacitorIndices.push_back(static_cast<int>(i));
        solution.assign(static_cast<size_t>(numUnknowns), 0.0);
        dirty = true;
        refresh();
        reset();
    }

    void reset() noexcept
    {
        for (auto& e : elements) { e.vPrev = 0.0; e.iPrev = 0.0; }
        std::fill(solution.begin(), solution.end(), 0.0);
        sourceVolts = 0.0;
    }

    // Advance one sample with the source at vin. Read results with voltage().
    void process(double vin) noexcept
    {
        solveFree(vin);
        advance();
    }

    // ---- The DK-method interface, for a caller with nonlinear devices ----

    // The node voltages with nothing injected (capacitors' history and the supply
    // rails included). Does not advance the capacitors.
    void solveFree(double vin) noexcept
    {
        if (dirty)
            refresh();
        sourceVolts = vin;

        double rhs[maxUnknowns] = {};
        const double* constants = constantRhs.data();
        const double* coupling = sourceCoupling.data();
        for (int r = 0; r < numUnknowns; ++r)
            rhs[r] = constants[r] - coupling[r] * vin;

        if (! capsOpen)
        {
            const Element* el = elements.data();
            for (int idx : capacitorIndices)
            {
                const auto& e = el[idx];
                // Companion current source: leaves node b, enters node a.
                auto hist = e.g * e.vPrev + e.iPrev;
                if (e.a >= 2) rhs[e.a - 2] += hist;
                if (e.b >= 2) rhs[e.b - 2] -= hist;
            }
        }

        const double* inv = inverse.data();
        double* sol = solution.data();
        for (int r = 0; r < numUnknowns; ++r)
        {
            const double* row = inv + r * maxUnknowns;
            double sum = 0.0;
            for (int c = 0; c < numUnknowns; ++c)
                sum += row[c] * rhs[c];
            sol[r] = sum;
        }
    }

    // Volts at outNode per amp injected INTO injNode from outside: the network's
    // transfer resistance at this instant (capacitors count as their companion
    // conductances). Both must be real nodes (>= 2).
    double inverseEntry(int outNode, int injNode) const noexcept
    {
        return inverse[static_cast<size_t>((outNode - 2) * maxUnknowns + (injNode - 2))];
    }

    // Adds the effect of `amps` flowing into `node` from outside to the solution.
    void injectCurrent(int node, double amps) noexcept
    {
        if (amps == 0.0)
            return;
        const double* inv = inverse.data() + (node - 2);
        double* sol = solution.data();
        for (int r = 0; r < numUnknowns; ++r)
            sol[r] += inv[r * maxUnknowns] * amps;
    }

    // Steps every capacitor to the voltages now in the solution.
    void advance() noexcept
    {
        Element* el = elements.data();
        for (int idx : capacitorIndices)
        {
            auto& e = el[idx];
            auto v = nodeVoltage(e.a) - nodeVoltage(e.b);
            auto hist = e.g * e.vPrev + e.iPrev;
            e.iPrev = e.g * v - hist;
            e.vPrev = v;
        }
    }

    // DC analysis: with the capacitors open the network has no memory. After solving
    // it that way, captureCapacitorVoltages() charges each capacitor to the voltage it
    // has in the solution (zero current), and setCapacitorsOpen(false) resumes normal
    // operation from that operating point.
    void setCapacitorsOpen(bool open) noexcept
    {
        if (capsOpen != open)
        {
            capsOpen = open;
            dirty = true;
        }
    }

    void captureCapacitorVoltages() noexcept
    {
        for (auto& e : elements)
            if (e.kind == Kind::capacitor)
            {
                e.vPrev = nodeVoltage(e.a) - nodeVoltage(e.b);
                e.iPrev = 0.0;
            }
    }

    double voltage(int node) const noexcept { return nodeVoltage(node); }

    void refresh() noexcept
    {
        if (! dirty)
            return;
        dirty = false;

        std::array<double, maxUnknowns * maxUnknowns> m {};
        sourceCoupling.assign(static_cast<size_t>(numUnknowns), 0.0);
        constantRhs.assign(static_cast<size_t>(numUnknowns), 0.0);

        // Adds coefficient `value` to the equation `row` for the voltage at `node`
        // (a known node's voltage moves to the right-hand side).
        auto stamp = [&](int row, int node, double value) {
            if (row < 2)
                return;
            auto r = row - 2;
            if (node >= 2)
                m[static_cast<size_t>(r * maxUnknowns + (node - 2))] += value;
            else if (node == source)
                sourceCoupling[static_cast<size_t>(r)] += value;
        };

        for (auto& e : elements)
        {
            if (e.kind == Kind::transconductance)
            {
                // KCL as "currents leaving the node sum to zero".
                stamp(e.a, e.c1, e.value);
                stamp(e.a, e.c2, -e.value);
                stamp(e.b, e.c1, -e.value);
                stamp(e.b, e.c2, e.value);
                continue;
            }

            e.g = e.kind == Kind::capacitor ? (capsOpen ? 0.0 : 2.0 * e.value / period) : 1.0 / e.value;
            if (e.kind == Kind::resistor && e.bias != 0.0 && e.a >= 2)
                constantRhs[static_cast<size_t>(e.a - 2)] += e.g * e.bias;
            stamp(e.a, e.a, e.g);
            stamp(e.a, e.b, -e.g);
            stamp(e.b, e.b, e.g);
            stamp(e.b, e.a, -e.g);
        }

        // A vanishing conductance from every node to ground (SPICE's gmin) keeps a node that is
        // only reached through capacitors solvable when they are open.
        for (int n = 0; n < numNodeUnknowns; ++n)
            m[static_cast<size_t>(n * maxUnknowns + n)] += 1.0e-12;

        for (size_t k = 0; k < opAmps.size(); ++k)
        {
            auto row = numNodeUnknowns + static_cast<int>(k);
            const auto& o = opAmps[k];
            // The op-amp's output current enters `out`: it leaves that node's KCL as -i.
            if (o.out >= 2)
                m[static_cast<size_t>((o.out - 2) * maxUnknowns + row)] -= 1.0;
            // Constraint row: v(plus) - v(minus) = 0.
            auto put = [&](int node, double value) {
                if (node >= 2)
                    m[static_cast<size_t>(row * maxUnknowns + (node - 2))] += value;
                else if (node == source)
                    sourceCoupling[static_cast<size_t>(row)] += value;
            };
            put(o.plus, 1.0);
            put(o.minus, -1.0);
        }

        invert(m);
    }

private:
    enum class Kind { resistor, capacitor, transconductance };

    struct Element
    {
        Kind kind;
        int a, b;        // nodes (transconductance: from, to)
        int c1, c2;      // transconductance control nodes
        double value;    // ohms, farads or siemens
        double g;        // conductance, or the capacitor's companion conductance 2C/T
        double vPrev;    // capacitor: voltage across it last sample (a - b)
        double iPrev;    // capacitor: current through it last sample (a -> b)
        double bias = 0.0; // resistor to a rail: the rail's voltage (0 = an ordinary resistor)
    };

    struct OpAmp { int plus, minus, out; };

    double nodeVoltage(int node) const noexcept
    {
        if (node == ground)
            return 0.0;
        if (node == source)
            return sourceVolts;
        return solution[static_cast<size_t>(node - 2)];
    }

    void invert(std::array<double, maxUnknowns * maxUnknowns> m) noexcept
    {
        inverse.fill(0.0);
        for (int i = 0; i < numUnknowns; ++i)
            inverse[static_cast<size_t>(i * maxUnknowns + i)] = 1.0;

        auto at = [](std::array<double, maxUnknowns * maxUnknowns>& arr, int r, int c) -> double& {
            return arr[static_cast<size_t>(r * maxUnknowns + c)];
        };

        for (int col = 0; col < numUnknowns; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < numUnknowns; ++r)
                if (std::abs(at(m, r, col)) > std::abs(at(m, pivot, col)))
                    pivot = r;
            if (pivot != col)
                for (int c = 0; c < numUnknowns; ++c)
                {
                    std::swap(at(m, col, c), at(m, pivot, c));
                    std::swap(at(inverse, col, c), at(inverse, pivot, c));
                }

            auto p = at(m, col, col);
            for (int c = 0; c < numUnknowns; ++c)
            {
                at(m, col, c) /= p;
                at(inverse, col, c) /= p;
            }
            for (int r = 0; r < numUnknowns; ++r)
            {
                if (r == col)
                    continue;
                auto f = at(m, r, col);
                if (f == 0.0)
                    continue;
                for (int c = 0; c < numUnknowns; ++c)
                {
                    at(m, r, c) -= f * at(m, col, c);
                    at(inverse, r, c) -= f * at(inverse, col, c);
                }
            }
        }
    }

    std::vector<Element> elements;
    std::vector<OpAmp> opAmps;
    std::vector<double> solution;       // node voltages (nodes 2..), then op-amp currents
    std::vector<double> sourceCoupling; // per equation: coefficient of the source voltage
    std::vector<double> constantRhs;    // per equation: current from the supply rails
    std::vector<int> capacitorIndices;  // which elements are capacitors
    std::array<double, maxUnknowns * maxUnknowns> inverse {};
    int nextNode = 2;
    int numNodeUnknowns = 0;
    int numUnknowns = 0;
    double period = 1.0;
    double sourceVolts = 0.0;
    bool dirty = true;
    bool capsOpen = false;
};
