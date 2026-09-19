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
        Source/CabinetPedal.cpp
        Source/CabImpulseResponse.cpp
        Source/CabinetVisualEditor.cpp
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
        Source/CentaurDrivePedal.cpp
        Source/PalmMuteTamerPedal.cpp
        Source/FenderStyleAmpPedal.cpp
        Source/MesaTripleRectifierPedal.cpp
        Source/SuperSonic22Pedal.cpp
        Source/MorningGloryPedal.cpp
        Source/DynaCompPedal.cpp
        Source/WardenPedal.cpp
        Source/FuzzChorusPedal.cpp
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
        Source/dsp/CentaurDriveStage.cpp
        Source/dsp/SolidStatePowerAmpStage.cpp
        Source/dsp/PalmMuteTamer.cpp
        Source/dsp/FenderStyleAmp.cpp
        Source/dsp/MesaTripleRectifierAmp.cpp
        Source/dsp/OrangeDualTerrorPreamp.cpp
        Source/dsp/SuperSonic22Preamp.cpp
        Source/dsp/BassmanToneStack.cpp
        Source/dsp/MorningGloryStage.cpp
        Source/dsp/DynaCompStage.cpp
        Source/dsp/WardenStage.cpp
        Source/dsp/FuzzFaceStage.cpp
        Source/dsp/PillowChorusStage.cpp
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

add_executable(CentaurDriveStageTest
    Tests/CentaurDriveStageTest.cpp
    Source/dsp/CentaurDriveStage.cpp
    Source/dsp/DiodeClipperStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(CentaurDriveStageTest PRIVATE Source/dsp)

add_executable(SolidStatePowerAmpStageTest
    Tests/SolidStatePowerAmpStageTest.cpp
    Source/dsp/SolidStatePowerAmpStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(SolidStatePowerAmpStageTest PRIVATE Source/dsp)

add_executable(PalmMuteTamerTest
    Tests/PalmMuteTamerTest.cpp
    Source/dsp/PalmMuteTamer.cpp
)
target_include_directories(PalmMuteTamerTest PRIVATE Source/dsp)

add_executable(MorningGloryStageTest
    Tests/MorningGloryStageTest.cpp
    Source/dsp/MorningGloryStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(MorningGloryStageTest PRIVATE Source/dsp)

add_executable(DynaCompStageTest
    Tests/DynaCompStageTest.cpp
    Source/dsp/DynaCompStage.cpp
)
target_include_directories(DynaCompStageTest PRIVATE Source/dsp)

add_executable(WardenStageTest
    Tests/WardenStageTest.cpp
    Source/dsp/WardenStage.cpp
)
target_include_directories(WardenStageTest PRIVATE Source/dsp)

add_executable(FenderStyleAmpTest
    Tests/FenderStyleAmpTest.cpp
    Source/dsp/FenderStyleAmp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(FenderStyleAmpTest PRIVATE Source/dsp)

add_executable(MesaTripleRectifierAmpTest
    Tests/MesaTripleRectifierAmpTest.cpp
    Source/dsp/MesaTripleRectifierAmp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
    Source/dsp/DiodeClipperStage.cpp
)
target_include_directories(MesaTripleRectifierAmpTest PRIVATE Source/dsp)

add_executable(BassmanToneStackTest
    Tests/BassmanToneStackTest.cpp
    Source/dsp/BassmanToneStack.cpp
)
target_include_directories(BassmanToneStackTest PRIVATE Source/dsp)

add_executable(FuzzFaceStageTest
    Tests/FuzzFaceStageTest.cpp
    Source/dsp/FuzzFaceStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(FuzzFaceStageTest PRIVATE Source/dsp)

add_executable(PillowChorusStageTest
    Tests/PillowChorusStageTest.cpp
    Source/dsp/PillowChorusStage.cpp
)
target_include_directories(PillowChorusStageTest PRIVATE Source/dsp)

add_executable(OrangeDualTerrorPreampTest
    Tests/OrangeDualTerrorPreampTest.cpp
    Source/dsp/OrangeDualTerrorPreamp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(OrangeDualTerrorPreampTest PRIVATE Source/dsp)

add_executable(NodalNetworkTest
    Tests/NodalNetworkTest.cpp
)
target_include_directories(NodalNetworkTest PRIVATE Source/dsp)

add_executable(SuperSonic22PreampTest
    Tests/SuperSonic22PreampTest.cpp
    Source/dsp/SuperSonic22Preamp.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_include_directories(SuperSonic22PreampTest PRIVATE Source/dsp)

add_executable(DualCabMixTest
    Tests/DualCabMixTest.cpp
)
target_include_directories(DualCabMixTest PRIVATE Source/dsp)

# Links the real (JUCE-based) IR generator, but only juce_audio_basics -
# no audio device, no GUI - so it still runs as a plain console test.
add_executable(CabDualTest
    Tests/CabDualTest.cpp
    Source/CabImpulseResponse.cpp
)
target_link_libraries(CabDualTest
    PRIVATE
        juce::juce_audio_basics
    PUBLIC
        juce::juce_recommended_config_flags
)

# Drives the REAL CabinetPedal (real Convolution, real WAV files) - JUCE-
# coupled, so it links the same JUCE modules as the app and the two source
# files the pedal itself pulls in for its Inspector editor.
add_executable(CabPedalIRTest
    Tests/CabPedalIRTest.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
)
target_compile_definitions(CabPedalIRTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(CabPedalIRTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

add_executable(MicPlacementTest
    Tests/MicPlacementTest.cpp
)
target_include_directories(MicPlacementTest PRIVATE Source/dsp)

add_executable(CabMicPlacementTest
    Tests/CabMicPlacementTest.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
)
target_compile_definitions(CabMicPlacementTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(CabMicPlacementTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

add_executable(CabStereoTest
    Tests/CabStereoTest.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
)
target_compile_definitions(CabStereoTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(CabStereoTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

add_executable(CabMixedSpeakerTest
    Tests/CabMixedSpeakerTest.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
)
target_compile_definitions(CabMixedSpeakerTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(CabMixedSpeakerTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

add_executable(SpeakerCompressionTest
    Tests/SpeakerCompressionTest.cpp
)
target_include_directories(SpeakerCompressionTest PRIVATE Source/dsp)

add_executable(CabSpeakerPushTest
    Tests/CabSpeakerPushTest.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
)
target_compile_definitions(CabSpeakerPushTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(CabSpeakerPushTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

add_executable(AmpSpeakerLoadTest
    Tests/AmpSpeakerLoadTest.cpp
)
target_include_directories(AmpSpeakerLoadTest PRIVATE Source/dsp)

# Drives the REAL Mesa pedal (JUCE-coupled): checks the OR60 Presence/Resonance
# wiring against the amp/speaker load model.
add_executable(MesaLoadTest
    Tests/MesaLoadTest.cpp
    Source/MesaTripleRectifierPedal.cpp
    Source/dsp/MesaTripleRectifierAmp.cpp
    Source/dsp/OrangeDualTerrorPreamp.cpp
    Source/dsp/PowerAmpStage.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
    Source/dsp/DiodeClipperStage.cpp
)
target_compile_definitions(MesaLoadTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(MesaLoadTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

# Drives the REAL Super-Sonic 22 pedal (JUCE-coupled): the wiring of preamp ->
# reverb -> power stage, the knob mapping, and levels.
add_executable(SuperSonic22PedalTest
    Tests/SuperSonic22PedalTest.cpp
    Source/SuperSonic22Pedal.cpp
    Source/dsp/SuperSonic22Preamp.cpp
    Source/dsp/PowerAmpStage.cpp
    Source/dsp/SpringReverb.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
)
target_compile_definitions(SuperSonic22PedalTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(SuperSonic22PedalTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

add_executable(SpeakerLoadLinkTest
    Tests/SpeakerLoadLinkTest.cpp
)
target_include_directories(SpeakerLoadLinkTest PRIVATE Source/dsp)


# A real Cabinet and a real Mesa run as a chain: the amp's OR60 Resonance must
# act on the speaker the Cabinet is modeling (Source/SpeakerLoadLink.h).
add_executable(CabAmpLinkTest
    Tests/CabAmpLinkTest.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
    Source/MesaTripleRectifierPedal.cpp
    Source/dsp/MesaTripleRectifierAmp.cpp
    Source/dsp/OrangeDualTerrorPreamp.cpp
    Source/dsp/PowerAmpStage.cpp
    Source/dsp/KorenTriodeStage.cpp
    Source/dsp/Oversampler4x.cpp
    Source/dsp/DiodeClipperStage.cpp
)
target_compile_definitions(CabAmpLinkTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(CabAmpLinkTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

# Developer tool (not a test): renders the Cabinet's Inspector window offscreen
# to PNGs - `EditorSnapshot <output dir>` - for checking layouts without the app.
add_executable(EditorSnapshot
    Tests/EditorSnapshot.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
    Source/PedalInspectorWindow.cpp
    Source/KnobComponent.cpp
)
target_compile_definitions(EditorSnapshot
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)
target_link_libraries(EditorSnapshot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

# Functional check of the Cabinet's visual editor: drives the real controls and
# mouse through JUCE's API (no window) and reads the parameters back.
add_executable(CabEditorTest
    Tests/CabEditorTest.cpp
    Source/CabinetPedal.cpp
    Source/CabImpulseResponse.cpp
    Source/CabinetVisualEditor.cpp
    Source/ModernLookAndFeel.cpp
    Source/KnobComponent.cpp
)
target_compile_definitions(CabEditorTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1   # lets the test run the message loop so the editor's timers fire
)
target_link_libraries(CabEditorTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)
