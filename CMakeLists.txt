cmake_minimum_required(VERSION 3.22)

project(CSim VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/JUCE)

juce_add_gui_app(CSim
    PRODUCT_NAME "C-Sim"
    COMPANY_NAME "C-Sim"
)

target_sources(CSim
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/SettingsWindow.cpp
        Source/SignalChainComponent.cpp
        Source/PedalInspectorWindow.cpp
        Source/PedalListComponent.cpp
        Source/PedalCatalog.cpp
        Source/PresetManager.cpp
        Source/ModernLookAndFeel.cpp
        Source/ReverbPedal.cpp
        Source/NoiseGatePedal.cpp
        Source/TransientShaperPedal.cpp
        Source/CleanAmpPedal.cpp
        Source/Peavey5150Pedal.cpp
        Source/CabinetPedal.cpp
        Source/CabImpulseResponse.cpp
        Source/KnobComponent.cpp
        Source/PitchDetector.cpp
        Source/TunerEngine.cpp
        Source/TunerComponent.cpp
        Source/TunerWindow.cpp
        Source/TriodeStagePedal.cpp
        Source/ToneStackPedal.cpp
        Source/PowerAmpPedal.cpp
        Source/DiodeClipperPedal.cpp
        Source/SpringReverbPedal.cpp
        Source/BiasTremoloPedal.cpp
        Source/DynamicGainStagePedal.cpp
        Source/GraphicEQPedal.cpp
        Source/DoubleGuitarPedal.cpp
        Source/TransposePedal.cpp
        Source/PrecisionDrivePedal.cpp
        Source/DistortionPedal.cpp
        Source/MesaRectifierPreampPedal.cpp
        Source/FortinMeshuggahPreampPedal.cpp
        Source/PalmMuteTamerPedal.cpp
        Source/DiezelVH4PreampPedal.cpp
        Source/MesaDiezelHybridPreampPedal.cpp
        Source/dsp/KorenTriodeStage.cpp
        Source/dsp/Oversampler4x.cpp
        Source/dsp/FenderToneStack.cpp
        Source/dsp/PowerAmpStage.cpp
        Source/dsp/DiodeClipperStage.cpp
        Source/dsp/NoiseGate.cpp
        Source/dsp/SpringReverb.cpp
        Source/dsp/BiasModulatedTremolo.cpp
        Source/dsp/DynamicGainStage.cpp
        Source/dsp/GraphicEQ.cpp
        Source/dsp/Doubler.cpp
        Source/dsp/PitchShifter.cpp
        Source/dsp/PrecisionDriveStage.cpp
        Source/dsp/MesaRectifierPreamp.cpp
        Source/dsp/SolidStatePowerAmpStage.cpp
        Source/dsp/FortinMeshuggahPreamp.cpp
        Source/dsp/PalmMuteTamer.cpp
        Source/dsp/DiezelVH4Preamp.cpp
        Source/dsp/MesaDiezelHybridPreamp.cpp
)

target_compile_definitions(CSim
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CSim,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CSim,JUCE_VERSION>"
)

target_link_libraries(CSim
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Standalone console diagnostics for the circuit-modeling DSP toolkit
# (Source/dsp/). No JUCE dependency by design - these test one module in
# isolation before it's wired into the app, per the "build order" plan:
# each new dsp/ module should get a matching Tests/*Test.cpp target.
add_executable(KorenTriodeStageTest
    Tests/KorenTriodeStageTest.cpp
    Source/dsp/KorenTriodeStage.cpp
)
target_include_directories(KorenTriodeStageTest PRIVATE Source/dsp)

add_executable(OversamplerAliasingTest
    Tests/OversamplerAliasingTest.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(OversamplerAliasingTest PRIVATE Source/dsp)

add_executable(FenderToneStackTest
    Tests/FenderToneStackTest.cpp
    Source/dsp/FenderToneStack.cpp
)
target_include_directories(FenderToneStackTest PRIVATE Source/dsp)

add_executable(MetalPreampChainTest
    Tests/MetalPreampChainTest.cpp
    Source/dsp/MetalPreampChain.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
    Source/dsp/DiodeClipperStage.cpp
)
target_include_directories(MetalPreampChainTest PRIVATE Source/dsp)

add_executable(PowerAmpStageTest
    Tests/PowerAmpStageTest.cpp
    Source/dsp/PowerAmpStage.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(PowerAmpStageTest PRIVATE Source/dsp)

add_executable(DiodeClipperStageTest
    Tests/DiodeClipperStageTest.cpp
    Source/dsp/DiodeClipperStage.cpp
)
target_include_directories(DiodeClipperStageTest PRIVATE Source/dsp)

add_executable(DynamicGainStageTest
    Tests/DynamicGainStageTest.cpp
    Source/dsp/DynamicGainStage.cpp
    Source/dsp/KorenTriodeStage.cpp
)
target_include_directories(DynamicGainStageTest PRIVATE Source/dsp)

add_executable(SpringReverbTest
    Tests/SpringReverbTest.cpp
    Source/dsp/SpringReverb.cpp
)
target_include_directories(SpringReverbTest PRIVATE Source/dsp)

add_executable(BiasModulatedTremoloTest
    Tests/BiasModulatedTremoloTest.cpp
    Source/dsp/BiasModulatedTremolo.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(BiasModulatedTremoloTest PRIVATE Source/dsp)

add_executable(EmoAmpFullChainTest
    Tests/EmoAmpFullChainTest.cpp
    Source/dsp/DynamicGainStage.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/FenderToneStack.cpp
    Source/dsp/SpringReverb.cpp
    Source/dsp/BiasModulatedTremolo.cpp
    Source/dsp/Oversampler4x.cpp
    Source/dsp/PowerAmpStage.cpp
)
target_include_directories(EmoAmpFullChainTest PRIVATE Source/dsp)

add_executable(HybridAmpTest
    Tests/HybridAmpTest.cpp
    Source/dsp/HybridAmp.cpp
    Source/dsp/DynamicGainStage.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/MetalPreampChain.cpp
    Source/dsp/Oversampler4x.cpp
    Source/dsp/DiodeClipperStage.cpp
    Source/dsp/NoiseGate.cpp
    Source/dsp/FenderToneStack.cpp
    Source/dsp/SpringReverb.cpp
    Source/dsp/BiasModulatedTremolo.cpp
    Source/dsp/PowerAmpStage.cpp
)
target_include_directories(HybridAmpTest PRIVATE Source/dsp)

add_executable(MetalAmpFullChainTest
    Tests/MetalAmpFullChainTest.cpp
    Source/dsp/MetalPreampChain.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
    Source/dsp/DiodeClipperStage.cpp
    Source/dsp/NoiseGate.cpp
    Source/dsp/FenderToneStack.cpp
    Source/dsp/PowerAmpStage.cpp
)
target_include_directories(MetalAmpFullChainTest PRIVATE Source/dsp)

add_executable(GraphicEQTest
    Tests/GraphicEQTest.cpp
    Source/dsp/GraphicEQ.cpp
)
target_include_directories(GraphicEQTest PRIVATE Source/dsp)

add_executable(DoublerTest
    Tests/DoublerTest.cpp
    Source/dsp/Doubler.cpp
)
target_include_directories(DoublerTest PRIVATE Source/dsp)

add_executable(PitchShifterTest
    Tests/PitchShifterTest.cpp
    Source/dsp/PitchShifter.cpp
)
target_include_directories(PitchShifterTest PRIVATE Source/dsp)

add_executable(PrecisionDriveStageTest
    Tests/PrecisionDriveStageTest.cpp
    Source/dsp/PrecisionDriveStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(PrecisionDriveStageTest PRIVATE Source/dsp)

add_executable(MesaRectifierPreampTest
    Tests/MesaRectifierPreampTest.cpp
    Source/dsp/MesaRectifierPreamp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(MesaRectifierPreampTest PRIVATE Source/dsp)

add_executable(SolidStatePowerAmpStageTest
    Tests/SolidStatePowerAmpStageTest.cpp
    Source/dsp/SolidStatePowerAmpStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(SolidStatePowerAmpStageTest PRIVATE Source/dsp)

add_executable(FortinMeshuggahPreampTest
    Tests/FortinMeshuggahPreampTest.cpp
    Source/dsp/FortinMeshuggahPreamp.cpp
    Source/dsp/MesaRectifierPreamp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/DiodeClipperStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(FortinMeshuggahPreampTest PRIVATE Source/dsp)

add_executable(PalmMuteTamerTest
    Tests/PalmMuteTamerTest.cpp
    Source/dsp/PalmMuteTamer.cpp
)
target_include_directories(PalmMuteTamerTest PRIVATE Source/dsp)

add_executable(DiezelVH4PreampTest
    Tests/DiezelVH4PreampTest.cpp
    Source/dsp/DiezelVH4Preamp.cpp
    Source/dsp/MesaRectifierPreamp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(DiezelVH4PreampTest PRIVATE Source/dsp)

add_executable(MesaDiezelHybridPreampTest
    Tests/MesaDiezelHybridPreampTest.cpp
    Source/dsp/MesaDiezelHybridPreamp.cpp
    Source/dsp/DiezelVH4Preamp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(MesaDiezelHybridPreampTest PRIVATE Source/dsp)
