juce_add_console_app(InlayProductUnlockingTests
    PRODUCT_NAME "Inlay Product Unlocking Tests")

juce_generate_juce_header(InlayProductUnlockingTests)

target_sources(InlayProductUnlockingTests
    PRIVATE
        UnitTestsRunner/main.cpp)

target_compile_definitions(InlayProductUnlockingTests
    PRIVATE
        JUCE_UNIT_TESTS=1)

target_link_libraries(InlayProductUnlockingTests
    PRIVATE
        inlay::inlay_product_unlocking
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME InlayProductUnlockingTests COMMAND InlayProductUnlockingTests)
