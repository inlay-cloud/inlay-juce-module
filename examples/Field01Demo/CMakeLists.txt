set(FIELD01_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

juce_add_binary_data(Field01Assets
    NAMESPACE Field01Assets
    HEADER_NAME Field01Assets.h
    SOURCES
        "${FIELD01_ASSET_DIR}/field-01-runtime-base.svg"
        "${FIELD01_ASSET_DIR}/range-audio.svg"
        "${FIELD01_ASSET_DIR}/range-low.svg"
        "${FIELD01_ASSET_DIR}/wave-selector-pulse.svg"
        "${FIELD01_ASSET_DIR}/wave-selector-sine.svg"
        "${FIELD01_ASSET_DIR}/wave-selector-triangle.svg")

juce_add_plugin(Field01Demo
    COMPANY_NAME "Inlay"
    BUNDLE_ID "cloud.inlay.examples.field01"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Inly
    PLUGIN_CODE Fld1
    FORMATS Standalone VST3
    PRODUCT_NAME "Field 01")

juce_generate_juce_header(Field01Demo)

target_sources(Field01Demo
    PRIVATE
        Source/Main.cpp
        Source/Field01Processor.cpp
        Source/Field01Processor.h
        Source/Field01Editor.cpp
        Source/Field01Editor.h)

target_compile_definitions(Field01Demo
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Field01Demo
    PRIVATE
        Field01Assets
        inlay::inlay_product_unlocking
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
