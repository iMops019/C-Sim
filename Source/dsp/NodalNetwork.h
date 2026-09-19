#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// A small linear-circuit solver for the passive networks that sit between
// tube stages (tone stacks, gain pots, volume pots, coupling caps and the
// voltage dividers they form with the tubes' plate and grid impedances).
//
// Why a solver instead of hand-derived filters: those networks are
// interactive - the Bass pot loads the Treble pot, every pot loads the
// plate it hangs off, a bright cap only matters at some pot positions - and
// a topology slip in a hand-reduced transfer function is very hard to catch.
// Here the circuit is written down as the schematic shows it (each resistor
// and capacitor between two named nodes) and the interactions fall out.
//
// Method: modified nodal analysis with the capacitors replaced by their
// trapezoidal companion model (a conductance 2C/T in parallel with a history
// current source) - the same thing as the bilinear transform, so the digital
// network's response matches the analog one closely at audio frequencies
// (run it at the oversampled rate, as the amp modules do, and even the
// treble caps stay well away from the warp). The system matrix only changes
// when a component value does (a knob move), so its inverse is cached; a
// sample costs one small matrix-vector product plus a walk over the elements.
//
// Node 0 is ground and node 1 is the ideal voltage source driving the
// network. Every other node comes from addNode(). A tube's plate is modeled
// by the caller as a source (its open-circuit voltage) behind a resistor
// (plate load || rp) that is part of the network - so plate loading by the
// following stage is computed, not assumed.
//
// Framework-agnostic (no JUCE) like the rest of the toolkit. Nothing in
// process() allocates, and value changes are allocation-free too, so knob
// moves are safe on the audio thread.
class NodalNetwork
{
public:
    static constexpr int ground = 0;
    static constexpr int source = 1;
    static constexpr int maxUnknowns = 16;

    // Smallest resistance a pot section may take: a wiper at the end of its
    // track is a short, not a divide-by-zero.
    static constexpr double minOhms = 1.0;

    int addNode() noexcept { return nextNode++; }

    int addResistor(int a, int b, double ohms)
    {
        elements.push_back({ a, b, false, std::max(ohms, minOhms), 0.0, 0.0, 0.0 });
        dirty = true;
        return static_cast<int>(elements.size()) - 1;
    }

    int addCapacitor(int a, int b, double farads)
    {
        elements.push_back({ a, b, true, farads, 0.0, 0.0, 0.0 });
        dirty = true;
        return static_cast<int>(elements.size()) - 1;
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

    // Must be called once (after all nodes/elements are added) and again if
    // the sample rate changes.
    void prepare(double sampleRate)
    {
        period = 1.0 / sampleRate;
        numUnknowns = nextNode - 2;
        volts.assign(static_cast<size_t>(nextNode), 0.0);
        dirty = true;
        refresh();
        reset();
    }

    void reset() noexcept
    {
        for (auto& e : elements) { e.vPrev = 0.0; e.iPrev = 0.0; }
        std::fill(volts.begin(), volts.end(), 0.0);
    }

    // Advance one sample with the source at vin. Read results with voltage().
    void process(double vin) noexcept
    {
        solveFree(vin);
        advance();
    }

    // ---- Nonlinear loads (the DK method) ----
    // A tube's grid, a diode or any other nonlinear branch hanging off a node
    // of this network can be solved without putting it in the matrix: the
    // network is linear, so the voltage at a node is
    //     V = V_free + transferOhms(node, injectedAt) * I_injected
    // where V_free is what the network does with nothing injected. A caller
    // runs solveFree(), iterates on its own nonlinear equations using
    // freeVoltage() and transferOhms(), then calls commit() with the current
    // it settled on (positive = flowing INTO the node from outside).
    // solveFree() does not advance the capacitors; commit() does.
    void solveFree(double vin) noexcept
    {
        if (dirty)
            refresh();

        volts[source] = vin;

        std::array<double, maxUnknowns> rhs {};
        for (const auto& e : elements)
        {
            // Capacitor history current: injected into a, drawn from b.
            if (e.cap)
            {
                auto hist = e.g * e.vPrev + e.iPrev;
                if (e.a >= 2) rhs[static_cast<size_t>(e.a - 2)] += hist;
                if (e.b >= 2) rhs[static_cast<size_t>(e.b - 2)] -= hist;
            }

            // An element between an unknown node and a known one (ground or
            // the source) contributes its conductance times the known voltage.
            if (e.a >= 2 && e.b < 2) rhs[static_cast<size_t>(e.a - 2)] += e.g * volts[static_cast<size_t>(e.b)];
            if (e.b >= 2 && e.a < 2) rhs[static_cast<size_t>(e.b - 2)] += e.g * volts[static_cast<size_t>(e.a)];
        }

        for (int r = 0; r < numUnknowns; ++r)
        {
            double sum = 0.0;
            for (int c = 0; c < numUnknowns; ++c)
                sum += inverse[static_cast<size_t>(r * maxUnknowns + c)] * rhs[static_cast<size_t>(c)];
            volts[static_cast<size_t>(r + 2)] = sum;
        }
    }

    // The node voltages solveFree() found (before any injected current).
    double freeVoltage(int node) const noexcept { return volts[static_cast<size_t>(node)]; }

    // Volts at outNode per amp injected into inNode - the network's transfer
    // resistance at this instant (capacitors count as their companion
    // conductances, so this is the Thevenin resistance for THIS sample's step).
    double transferOhms(int outNode, int inNode) const noexcept
    {
        return inverse[static_cast<size_t>((outNode - 2) * maxUnknowns + (inNode - 2))];
    }

    // Finish the sample begun by solveFree(): `amps` flows into `node` from
    // outside, and the capacitors advance to the voltages that results in.
    void commit(int node, double amps) noexcept
    {
        if (amps != 0.0)
            for (int r = 0; r < numUnknowns; ++r)
                volts[static_cast<size_t>(r + 2)] += inverse[static_cast<size_t>(r * maxUnknowns + (node - 2))] * amps;
        advance();
    }

    double voltage(int node) const noexcept { return volts[static_cast<size_t>(node)]; }

    // Rebuild and invert the system matrix now (process() does this by
    // itself after a value change; exposed so a caller can move the cost off
    // the per-sample path).
    void refresh() noexcept
    {
        if (! dirty)
            return;
        dirty = false;

        std::array<double, maxUnknowns * maxUnknowns> a {};
        for (auto& e : elements)
        {
            e.g = e.cap ? 2.0 * e.value / period : 1.0 / e.value;

            if (e.a >= 2) a[static_cast<size_t>((e.a - 2) * maxUnknowns + (e.a - 2))] += e.g;
            if (e.b >= 2) a[static_cast<size_t>((e.b - 2) * maxUnknowns + (e.b - 2))] += e.g;
            if (e.a >= 2 && e.b >= 2)
            {
                a[static_cast<size_t>((e.a - 2) * maxUnknowns + (e.b - 2))] -= e.g;
                a[static_cast<size_t>((e.b - 2) * maxUnknowns + (e.a - 2))] -= e.g;
            }
        }

        invert(a);
    }

private:
    // Advance every capacitor to the voltages now in volts[].
    void advance() noexcept
    {
        for (auto& e : elements)
        {
            if (! e.cap)
                continue;
            auto v = volts[static_cast<size_t>(e.a)] - volts[static_cast<size_t>(e.b)];
            auto hist = e.g * e.vPrev + e.iPrev;
            e.iPrev = e.g * v - hist;
            e.vPrev = v;
        }
    }

    struct Element
    {
        int a, b;
        bool cap;
        double value;   // ohms or farads
        double g;       // conductance (resistor) or companion conductance 2C/T (capacitor)
        double vPrev;   // capacitor: voltage across it last sample (a - b)
        double iPrev;   // capacitor: current through it last sample (a -> b)
    };

    // Gauss-Jordan with partial pivoting on the leading numUnknowns block.
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
    std::vector<double> volts;
    std::array<double, maxUnknowns * maxUnknowns> inverse {};
    int nextNode = 2;
    int numUnknowns = 0;
    double period = 1.0;
    bool dirty = true;
};
