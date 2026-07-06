juce_add_plugin(ArpeggiatorTutorial
    COMPANY_NAME "JUCE"
    BUNDLE_ID "cloud.inlay.examples.arpeggiatortutorial"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Arp1
    FORMATS Standalone VST3
    PRODUCT_NAME "ArpeggiatorTutorial")

juce_generate_juce_header(ArpeggiatorTutorial)

target_sources(ArpeggiatorTutorial
    PRIVATE
        Source/Main.cpp
        Source/Arpeggiator.cpp
        Source/Arpeggiator.h
        Source/ArpeggiatorEditor.cpp
        Source/ArpeggiatorEditor.h)

target_compile_definitions(ArpeggiatorTutorial
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ArpeggiatorTutorial
    PRIVATE
        inlay::inlay_product_unlocking
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
