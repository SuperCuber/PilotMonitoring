cmake_minimum_required(VERSION 3.22)

project(PilotMonitoring VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(XP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/XPSDK430/SDK")

if (NOT EXISTS "${XP_SDK_DIR}/CHeaders/XPLM/XPLMPlugin.h")
    message(FATAL_ERROR "X-Plane SDK headers were not found under ${XP_SDK_DIR}")
endif()

# Build whisper.cpp into the plugin so the finished .xpl has no whisper DLL
# beside it.  Recognition is not wired up yet; this verifies the final plugin
# links against the actual library target from the checked-out source.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/whisper.cpp EXCLUDE_FROM_ALL)

add_library(pilotmonitoring_core STATIC
    src/command_processor.cpp
    libs/whisper.cpp/examples/grammar-parser.cpp
)

target_include_directories(pilotmonitoring_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/whisper.cpp/examples"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/whisper.cpp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/whisper.cpp/ggml/include"
)

add_library(PilotMonitoring SHARED
    src/plugin_main.cpp
    src/voice_service.cpp
)

target_include_directories(PilotMonitoring PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/whisper.cpp/examples"
    "${XP_SDK_DIR}/CHeaders/XPLM"
    "${XP_SDK_DIR}/CHeaders/Widgets"
)

# IBM selects Windows types in the SDK; XPLM makes the five entry points
# exports rather than imports.  XPLM430 enables the current SDK API surface.
target_compile_definitions(PilotMonitoring PRIVATE
    IBM=1
    XPLM=1
    XPLM200=1
    XPLM300=1
    XPLM400=1
    XPLM420=1
    XPLM430=1
)

target_link_libraries(PilotMonitoring PRIVATE
    pilotmonitoring_core
    whisper
    "${XP_SDK_DIR}/Libraries/Win/XPLM_64.lib"
    ole32
    uuid
)

# X-Plane's current SDK layout is <plugin name>/win_x64/<plugin name>.xpl.
# CMake produces a DLL, but the .xpl suffix identifies it as an X-Plane plugin.
set_target_properties(PilotMonitoring PROPERTIES
    PREFIX ""
    OUTPUT_NAME "PilotMonitoring"
    SUFFIX ".xpl"
    RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/package/PilotMonitoring/win_x64"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE
        "${CMAKE_BINARY_DIR}/package/PilotMonitoring/win_x64"
)

add_custom_command(TARGET PilotMonitoring POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/package/PilotMonitoring/resources/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/README.txt"
        "${CMAKE_BINARY_DIR}/package/PilotMonitoring/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/commands.json"
        "${CMAKE_BINARY_DIR}/package/PilotMonitoring/resources/commands.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/whisper.cpp/models/ggml-base.en.bin"
        "${CMAKE_BINARY_DIR}/package/PilotMonitoring/resources/models/ggml-base.en.bin"
    COMMENT "Adding plugin documentation to the package"
)

if (BUILD_TESTING)
    add_executable(text_command_tests
        tests/text_command_tests.cpp
    )

    target_link_libraries(text_command_tests PRIVATE
        pilotmonitoring_core
    )

    add_test(NAME text_command_tests COMMAND text_command_tests)
endif()
