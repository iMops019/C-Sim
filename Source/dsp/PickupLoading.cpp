#include "PickupLoading.h"

#include <algorithm>
#include <cmath>

namespace
{
    // The pickup's characteristic polynomial into a load resistance rl:
    //   n2 s^2 + n1 s + n0.
    struct Poly { double s2, s1, s0; };

    Poly polyInto(const PickupLoading::Setup& setup, double loadOhms) noexcept
    {
        auto ct = setup.pickup.farads + setup.cableFarads;
        return { setup.pickup.henries * ct,
                 setup.pickup.ohms * ct + setup.pickup.henries / loadOhms,
                 1.0 + setup.pickup.ohms / loadOhms };
    }

    // The load the amp actually sees at the guitar's output: the guitar's own
    // pots in parallel with the amp's input.
    double loadOf(double potsOhms, double inputOhms) noexcept
    {
        return potsOhms * inputOhms / (potsOhms + inputOhms);
    }
}

std::complex<double> PickupLoading::response(const Setup& setup, double frequencyHz) noexcept
{
    auto amp = polyInto(setup, loadOf(setup.potsOhms, setup.ampInputOhms));
    auto ref = polyInto(setup, loadOf(setup.potsOhms, setup.referenceInputOhms));
    std::complex<double> s(0.0, 2.0 * 3.14159265358979323846 * frequencyHz);
    return (ref.s2 * s * s + ref.s1 * s + ref.s0) / (amp.s2 * s * s + amp.s1 * s + amp.s0);
}

void PickupLoading::prepare(double sampleRateHz) noexcept
{
    sampleRate = sampleRateHz;
    reset();
}

void PickupLoading::configure(const Setup& setup) noexcept
{
    // No boosting: a load at or above the reference is "no change".
    if (setup.ampInputOhms >= setup.referenceInputOhms * 0.999)
    {
        active = false;
        return;
    }

    // H_corr = P_ref / P_amp (see the header).
    auto amp = polyInto(setup, loadOf(setup.potsOhms, setup.ampInputOhms));
    auto ref = polyInto(setup, loadOf(setup.potsOhms, setup.referenceInputOhms));

    // Numerator = reference polynomial, denominator = amp polynomial; both
    // second order with the same s^2 term, so the high-frequency gain is 1.
    // Bilinear transform, s = K (1 - z^-1) / (1 + z^-1).
    auto k = 2.0 * sampleRate;
    auto coefficients = [k](const Poly& p, double& c0, double& c1, double& c2) {
        c0 = p.s2 * k * k + p.s1 * k + p.s0;
        c1 = 2.0 * (p.s0 - p.s2 * k * k);
        c2 = p.s2 * k * k - p.s1 * k + p.s0;
    };
    double n0, n1, n2, d0, d1, d2;
    coefficients(ref, n0, n1, n2);
    coefficients(amp, d0, d1, d2);

    b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0;
    a1 = d1 / d0; a2 = d2 / d0;

    // Its DC gain should be the analog one; the bilinear transform preserves it.
    active = true;
}
