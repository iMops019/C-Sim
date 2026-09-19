#include "MarshallToneStack.h"

#include "PotTaper.h"

MarshallToneStack::MarshallToneStack(double sampleRate, Components componentsToUse)
    : c(componentsToUse)
{
    using Net = NodalNetwork;

    auto vin = net.addNode(), top = net.addNode(), n1 = net.addNode(), n2 = net.addNode();
    auto n3 = net.addNode(), mw = net.addNode();
    wiper = net.addNode();
    out = net.addNode();

    net.addResistor(Net::source, vin, c.sourceOhms);
    net.addCapacitor(vin, top, c.trebleCapF);                 // C1
    net.addResistor(vin, n1, c.slopeOhms);                    // R4
    trebleUpper = net.addResistor(top, wiper, c.trebleOhms);  // Treble pot: T to wiper
    trebleLower = net.addResistor(wiper, n2, c.trebleOhms);   //             wiper to N2
    net.addCapacitor(n1, n2, c.bassCapF);                     // C2
    bass = net.addResistor(n2, n3, c.bassOhms);               // Bass, a rheostat
    midUpper = net.addResistor(n3, mw, c.midOhms);            // Middle pot: N3 to wiper
    midLower = net.addResistor(mw, Net::ground, c.midOhms);   //             wiper to ground
    net.addCapacitor(n1, mw, c.midCapF);                      // C3

    net.addCapacitor(wiper, out, c.couplingF);
    net.addResistor(out, Net::ground, c.loadOhms);
    net.addCapacitor(out, Net::ground, c.loadF);

    net.prepare(sampleRate);
    update();
}

void MarshallToneStack::setControls(float trebleIn, float bassIn, float middleIn) noexcept
{
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    trebleIn = clamp01(trebleIn); bassIn = clamp01(bassIn); middleIn = clamp01(middleIn);
    if (trebleIn != treble || bassIn != bassKnob || middleIn != middle)
    {
        treble = trebleIn; bassKnob = bassIn; middle = middleIn;
        dirty = true;
    }
}

void MarshallToneStack::update() noexcept
{
    auto t = static_cast<double>(treble);
    net.setResistance(trebleUpper, c.trebleOhms * (1.0 - t));
    net.setResistance(trebleLower, c.trebleOhms * t);
    net.setResistance(bass, c.bassOhms * potFraction(bassKnob, c.bassTaperAtNoon));
    auto m = potFraction(middle, c.midTaperAtNoon);
    net.setResistance(midUpper, c.midOhms * (1.0 - m));
    net.setResistance(midLower, c.midOhms * m);
    net.refresh();
    dirty = false;
}

void MarshallToneStack::reset() noexcept
{
    net.reset();
}

double MarshallToneStack::processSample(double volts) noexcept
{
    if (dirty)
        update();
    net.process(volts);
    return net.voltage(out);
}
