#include "MarshallPlexi1959Amp.h"

namespace
{
    // The impedance a jack presents to the guitar, from the drawing: High is the
    // 1M leak on the input node; Low is a 68k series resistor into a second 68k
    // that shunts the grid to ground through the unused High jack's contact.
    constexpr double highJackOhms = 1.0e6;
    constexpr double lowJackOhms = 136.0e3;
}

MarshallPlexi1959Amp::MarshallPlexi1959Amp(double sampleRate)
    : oversampler(sampleRate),
      oversampledRate(sampleRate * 4.0),
      preamp(sampleRate),
      power(sampleRate * 4.0)
{
    pickupLoading.prepare(oversampledRate);
    configurePickup();
}

void MarshallPlexi1959Amp::setRouting(MarshallPlexiPreamp::Routing routing) noexcept { preamp.setRouting(routing); }

void MarshallPlexi1959Amp::setLowInput(bool useLowJack) noexcept
{
    if (lowInput == useLowJack)
        return;
    lowInput = useLowJack;
    preamp.setLowInput(useLowJack);
    configurePickup();
}

void MarshallPlexi1959Amp::setPickup(Pickup newPickup) noexcept
{
    if (pickup == newPickup)
        return;
    pickup = newPickup;
    configurePickup();
}

void MarshallPlexi1959Amp::configurePickup() noexcept
{
    switch (pickup)
    {
        case Pickup::Off:
            pickupLoading.setBypassed();
            break;
        case Pickup::SingleCoil:
            pickupLoading.configure(PickupLoading::setupFor(PickupLoading::singleCoil(), lowInput ? lowJackOhms : highJackOhms));
            break;
        case Pickup::Humbucker:
            pickupLoading.configure(PickupLoading::setupFor(PickupLoading::humbucker(), lowInput ? lowJackOhms : highJackOhms));
            break;
    }
}

// A pedal sets every control on every block: only a real change is applied.
void MarshallPlexi1959Amp::setImpedanceTap(double ohms) noexcept
{
    if (tapOhms == ohms)
        return;
    tapOhms = ohms;
    power.setImpedanceTap(ohms);
    power.setSpeaker(speaker, ohms);
}

void MarshallPlexi1959Amp::setSpeaker(const AmpSpeakerLoad::Speaker& newSpeaker) noexcept
{
    if (AmpSpeakerLoad::sameSpeaker(speaker, newSpeaker))
        return;
    speaker = newSpeaker;
    power.setSpeaker(speaker, tapOhms);
}

void MarshallPlexi1959Amp::reset() noexcept
{
    oversampler.reset();
    preamp.reset();
    power.reset();
    pickupLoading.reset();
}

float MarshallPlexi1959Amp::processOversampledSample(float x) noexcept
{
    // Pickup loading, the preamp up to the tone stack's output node, the power
    // amp (whose phase inverter loads that node), and back to finish the node.
    auto guitar = pickupLoading.process(static_cast<double>(x));
    auto free = preamp.processToStack(guitar);
    auto drawn = power.process(free, preamp.stackOutputOhms());
    preamp.finishStack(drawn);
    return static_cast<float>(power.speakerVolts());
}

void MarshallPlexi1959Amp::processBlock(const float* input, float* output, int numSamples) noexcept
{
    preamp.refreshControls();
    oversampler.processBlock(input, output, numSamples, [this](float x) { return processOversampledSample(x); });
}
