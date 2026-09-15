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
)

target_compile_definitions(CSim
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CSim,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CSim,JUCE_VERSION>"
)

target_link_libraries(CSim
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
