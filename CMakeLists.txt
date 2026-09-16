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
        Source/PedalListComponent.cpp
        Source/ReverbPedal.cpp
        Source/NoiseGatePedal.cpp
        Source/TransientShaperPedal.cpp
        Source/CleanAmpPedal.cpp
        Source/Peavey5150Pedal.cpp
        Source/FourByTwelveCabPedal.cpp
        Source/CabImpulseResponse.cpp
        Source/KnobComponent.cpp
        Source/PitchDetector.cpp
        Source/TunerEngine.cpp
        Source/TunerComponent.cpp
        Source/TunerWindow.cpp
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
