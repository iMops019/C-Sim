#pragma once

// A complex-frequency circuit solver for the tests: resistors, capacitors,
// transconductances (a transistor's small-signal model) and ideal op-amps, solved
// by modified nodal analysis at one frequency at a time. It shares nothing with
// MnaNetwork (the time-domain solver the modules use), so a test can transcribe a
// netlist from the schematic, solve it here, and compare the module against that -
// two independent implementations of the same circuit.
//
// Node 0 is ground and node 1 the 1V source; other nodes come from node().

#include <cmath>
#include <complex>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace TestCircuit
{
    using cd = std::complex<double>;

    // ---- A complex-frequency circuit solver, independent of MnaNetwork ----
    class Circuit
    {
    public:
        static constexpr int ground = 0, source = 1;
        int node() { return nextNode++; }
        void r(int a, int b, double ohms) { parts.push_back({ 0, a, b, 0, 0, ohms }); }
        void c(int a, int b, double farads) { parts.push_back({ 1, a, b, 0, 0, farads }); }
        void gm(int from, int to, int cp, int cn, double siemens) { parts.push_back({ 2, from, to, cp, cn, siemens }); }
        void opamp(int plus, int minus, int out) { ops.push_back({ plus, minus, out }); }

        cd solve(double freq, int outNode) const
        {
            auto s = cd(0.0, 2.0 * M_PI * freq);
            int nn = nextNode - 2, n = nn + static_cast<int>(ops.size());
            std::vector<std::vector<cd>> m(static_cast<size_t>(n), std::vector<cd>(static_cast<size_t>(n + 1), 0.0));
            auto add = [&](int row, int col, cd v) {
                if (row < 2) return;
                if (col >= 2) m[static_cast<size_t>(row - 2)][static_cast<size_t>(col - 2)] += v;
                else if (col == source) m[static_cast<size_t>(row - 2)][static_cast<size_t>(n)] -= v;
            };
            for (auto& p : parts)
            {
                if (p.kind == 2)
                {
                    add(p.a, p.c1, p.v); add(p.a, p.c2, -p.v);
                    add(p.b, p.c1, -p.v); add(p.b, p.c2, p.v);
                    continue;
                }
                cd y = p.kind == 0 ? cd(1.0 / p.v) : s * p.v;
                add(p.a, p.a, y); add(p.a, p.b, -y); add(p.b, p.b, y); add(p.b, p.a, -y);
            }
            for (size_t k = 0; k < ops.size(); ++k)
            {
                auto row = nn + static_cast<int>(k);
                if (ops[k].out >= 2) m[static_cast<size_t>(ops[k].out - 2)][static_cast<size_t>(row)] -= 1.0;
                auto put = [&](int node, double v) {
                    if (node >= 2) m[static_cast<size_t>(row)][static_cast<size_t>(node - 2)] += v;
                    else if (node == source) m[static_cast<size_t>(row)][static_cast<size_t>(n)] -= v;
                };
                put(ops[k].plus, 1.0);
                put(ops[k].minus, -1.0);
            }
            for (int col = 0; col < n; ++col)
            {
                int piv = col;
                for (int row = col + 1; row < n; ++row)
                    if (std::abs(m[static_cast<size_t>(row)][static_cast<size_t>(col)]) > std::abs(m[static_cast<size_t>(piv)][static_cast<size_t>(col)]))
                        piv = row;
                std::swap(m[static_cast<size_t>(col)], m[static_cast<size_t>(piv)]);
                for (int row = 0; row < n; ++row)
                {
                    if (row == col) continue;
                    auto f = m[static_cast<size_t>(row)][static_cast<size_t>(col)] / m[static_cast<size_t>(col)][static_cast<size_t>(col)];
                    for (int k = col; k <= n; ++k)
                        m[static_cast<size_t>(row)][static_cast<size_t>(k)] -= f * m[static_cast<size_t>(col)][static_cast<size_t>(k)];
                }
            }
            auto idx = static_cast<size_t>(outNode - 2);
            return m[idx][static_cast<size_t>(n)] / m[idx][idx];
        }

    private:
        struct Part { int kind, a, b, c1, c2; double v; };
        struct Op { int plus, minus, out; };
        std::vector<Part> parts;
        std::vector<Op> ops;
        int nextNode = 2;
    };


}
