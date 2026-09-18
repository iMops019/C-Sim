#include "PedalCatalog.h"

#include "ReverbPedal.h"
#include "NoiseGatePedal.h"
#include "TransientShaperPedal.h"
#include "CleanAmpPedal.h"
#include "Peavey5150Pedal.h"
#include "CabinetPedal.h"
#include "TriodeStagePedal.h"
#include "ToneStackPedal.h"
#include "PowerAmpPedal.h"
#include "DiodeClipperPedal.h"
#include "SpringReverbPedal.h"
#include "BiasTremoloPedal.h"
#include "DynamicGainStagePedal.h"
#include "GraphicEQPedal.h"
#include "DoubleGuitarPedal.h"
#include "TransposePedal.h"
#include "PrecisionDrivePedal.h"
#include "DistortionPedal.h"
#include "PalmMuteTamerPedal.h"
#include "MorningGloryPedal.h"
#include "DynaCompPedal.h"
#include "WardenPedal.h"
#include "FuzzChorusPedal.h"
#include "MesaRectifierPreampPedal.h"
#include "FortinMeshuggahPreampPedal.h"
#include "DiezelVH4PreampPedal.h"
#include "MesaDiezelHybridPreampPedal.h"
#include "FenderTwinReverbPreampPedal.h"
#include "FenderPrincetonReverbPreampPedal.h"
#include "FenderDeluxeReverbPreampPedal.h"
#include "FenderHybridReverbPreampPedal.h"
#include "FenderStyleAmpPedal.h"

namespace PedalCatalog
{
    std::vector<PedalListComponent::CatalogItem> pedals()
    {
        return { { "Reverb", [] { return std::make_unique<ReverbPedal>(); } },
                 { "Noise Gate", [] { return std::make_unique<NoiseGatePedal>(); } },
                 { "Transient Shaper", [] { return std::make_unique<TransientShaperPedal>(); } },
                 { "Graphic EQ", [] { return std::make_unique<GraphicEQPedal>(); } },
                 { "Double Guitar", [] { return std::make_unique<DoubleGuitarPedal>(); } },
                 { "Transpose", [] { return std::make_unique<TransposePedal>(); } },
                 { "Precision Drive", [] { return std::make_unique<PrecisionDrivePedal>(); } },
                 { "Distortion", [] { return std::make_unique<DistortionPedal>(); } },
                 { "Palm Mute Tamer", [] { return std::make_unique<PalmMuteTamerPedal>(); } },
                 { "Morning Glory", [] { return std::make_unique<MorningGloryPedal>(); } },
                 { "Dyna Comp", [] { return std::make_unique<DynaCompPedal>(); } },
                 { "The Warden", [] { return std::make_unique<WardenPedal>(); } },
                 { "Fuzz Chorus", [] { return std::make_unique<FuzzChorusPedal>(); } } };
    }

    std::vector<PedalListComponent::CatalogItem> amps()
    {
        return { { "Drop Tuned Clean 1", [] { return std::make_unique<CleanAmpPedal>(); } },
                 { "5150 Lead", [] { return std::make_unique<Peavey5150Pedal>(); } },
                 { "Mesa Rectifier Preamp", [] { return std::make_unique<MesaRectifierPreampPedal>(); } },
                 { "Fortin Meshuggah Preamp", [] { return std::make_unique<FortinMeshuggahPreampPedal>(); } },
                 { "Diezel VH4 Preamp", [] { return std::make_unique<DiezelVH4PreampPedal>(); } },
                 { "Mesa/Diezel Hybrid Preamp", [] { return std::make_unique<MesaDiezelHybridPreampPedal>(); } },
                 { "Fender Twin Reverb Preamp", [] { return std::make_unique<FenderTwinReverbPreampPedal>(); } },
                 { "Fender Princeton Reverb Preamp", [] { return std::make_unique<FenderPrincetonReverbPreampPedal>(); } },
                 { "Fender Deluxe Reverb Preamp", [] { return std::make_unique<FenderDeluxeReverbPreampPedal>(); } },
                 { "Fender Twin/Princeton Hybrid Preamp", [] { return std::make_unique<FenderHybridReverbPreampPedal>(); } },
                 { "Fender Style Amp (WIP)", [] { return std::make_unique<FenderStyleAmpPedal>(); } } };
    }

    std::vector<PedalListComponent::CatalogItem> cabs()
    {
        return { { "Cabinet", [] { return std::make_unique<CabinetPedal>(); } } };
    }

    // Raw circuit-level building blocks - stack these yourself (Triode
    // Stage, Tone Stack, Diode Clipper, Power Amp, in whatever order and
    // however many you like) to build your own amp from scratch, rather
    // than only using the fixed preset amps above.
    std::vector<PedalListComponent::CatalogItem> lab()
    {
        return { { "Triode Stage", [] { return std::make_unique<TriodeStagePedal>(); } },
                 { "Tone Stack", [] { return std::make_unique<ToneStackPedal>(); } },
                 { "Diode Clipper", [] { return std::make_unique<DiodeClipperPedal>(); } },
                 { "Power Amp", [] { return std::make_unique<PowerAmpPedal>(); } },
                 { "Dynamic Gain Stage", [] { return std::make_unique<DynamicGainStagePedal>(); } },
                 { "Bias Tremolo", [] { return std::make_unique<BiasTremoloPedal>(); } },
                 { "Spring Reverb", [] { return std::make_unique<SpringReverbPedal>(); } } };
    }

    std::unique_ptr<Pedal> createByName(const juce::String& name)
    {
        for (auto* catalogFn : { &pedals, &amps, &cabs, &lab })
            for (auto& item : (*catalogFn)())
                if (item.name == name)
                    return item.create();

        return nullptr;
    }
}
