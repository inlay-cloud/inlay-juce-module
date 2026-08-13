function(add_inlay_console_application target)
    set(one_value_args PRODUCT_NAME VERSION)
    cmake_parse_arguments(APP "" "${one_value_args}" "" ${ARGN})

    if(NOT APP_PRODUCT_NAME)
        set(APP_PRODUCT_NAME ${target})
    endif()

    set(version_args)
    if(APP_VERSION)
        list(APPEND version_args VERSION ${APP_VERSION})
    endif()

    juce_add_console_app(${target}
        PRODUCT_NAME "${APP_PRODUCT_NAME}"
        ${version_args})

    juce_generate_juce_header(${target})

    target_link_libraries(${target}
        PRIVATE
            inlay::inlay_product_unlocking
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

function(add_inlay_test_application target)
    add_inlay_console_application(${target})

    target_compile_definitions(${target}
        PRIVATE
            JUCE_UNIT_TESTS=1)

    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_inlay_test_application(UnitTestsRunner)
target_sources(UnitTestsRunner
    PRIVATE
        UnitTestsRunner/main.cpp)

add_inlay_test_application(EmulatorTests)
target_compile_definitions(EmulatorTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_sources(EmulatorTests
    PRIVATE
        UnitTestsRunner/main.cpp
        Emulator/Protocol.cpp
        Emulator/ProtocolTests.cpp
        Emulator/EventWriter.cpp
        Emulator/EventWriterTests.cpp
        Emulator/CommandProcessor.cpp
        Emulator/CommandProcessorTests.cpp
        Emulator/Emulator.cpp
        Emulator/EmulatorTests.cpp
        Emulator/ProcessTests.cpp)

add_inlay_console_application(HeadlessEmulator
    PRODUCT_NAME "Inlay JUCE Emulator"
    VERSION ${PROJECT_VERSION})
set_target_properties(HeadlessEmulator PROPERTIES OUTPUT_NAME "inlay-juce-emulator")
target_sources(HeadlessEmulator
    PRIVATE
        Emulator/main.cpp
        Emulator/Protocol.cpp
        Emulator/EventWriter.cpp
        Emulator/CommandProcessor.cpp
        Emulator/Emulator.cpp)

target_compile_definitions(EmulatorTests PRIVATE
    INLAY_EMULATOR_PATH="$<TARGET_FILE:HeadlessEmulator>")
add_dependencies(EmulatorTests HeadlessEmulator)

add_custom_target(AllTests
    DEPENDS
        UnitTestsRunner
        EmulatorTests)
