cmake_minimum_required(VERSION 3.22)

project(
    inlay_juce_module
    VERSION 1.0.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)

option(
    INLAY_BUILD_EXAMPLES
    "Build Inlay examples"
    ${PROJECT_IS_TOP_LEVEL}
)

option(
    INLAY_BUILD_TESTS
    "Build Inlay tests"
    ${PROJECT_IS_TOP_LEVEL}
)

option(
    INLAY_FETCH_JUCE
    "Fetch JUCE when building Inlay as a standalone project"
    ${PROJECT_IS_TOP_LEVEL}
)

if(NOT TARGET juce::juce_core)
    find_package(JUCE CONFIG QUIET)
endif()

if(NOT TARGET juce::juce_core)
    if(INLAY_FETCH_JUCE)
        include(FetchContent)

        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        501c07674e1ad693085a7e7c398f205c2677f5da
        )

        FetchContent_MakeAvailable(JUCE)
    else()
        message(FATAL_ERROR
            "JUCE must be provided before adding inlay-juce-module. "
            "Add JUCE with add_subdirectory(), FetchContent, or find_package()."
        )
    endif()
endif()

juce_add_module(
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/inlay_product_unlocking"
    ALIAS_NAMESPACE inlay
)

if(INLAY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(INLAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
