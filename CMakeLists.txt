cmake_minimum_required(VERSION 3.24)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/BootstrapDependencies.cmake")

project(PilotMonitoring LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

find_package(unofficial-x-plane CONFIG REQUIRED)
find_package(unofficial-lua CONFIG REQUIRED)
find_package(whisper CONFIG REQUIRED)

if(WIN32)
  set(PILOTMONITORING_XPLANE_LIBRARY
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/XPLM_64.lib")
  if(NOT EXISTS "${PILOTMONITORING_XPLANE_LIBRARY}")
    message(FATAL_ERROR
      "The vcpkg X-Plane SDK package is missing XPLM_64.lib at "
      "${PILOTMONITORING_XPLANE_LIBRARY}")
  endif()
  set_property(TARGET unofficial::x-plane::xplm
    PROPERTY IMPORTED_IMPLIB "${PILOTMONITORING_XPLANE_LIBRARY}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_library(pilotmonitoring_core STATIC EXCLUDE_FROM_ALL
  src/execution_engine.cpp
  src/grammar-parser.cpp
)

target_include_directories(pilotmonitoring_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(pilotmonitoring_core
  PUBLIC
    unofficial::lua::lua
    whisper
)

add_library(PilotMonitoring MODULE EXCLUDE_FROM_ALL
  src/control_panel.cpp
  src/plugin_main.cpp
  src/voice_service.cpp
)

target_link_libraries(PilotMonitoring
  PRIVATE
    pilotmonitoring_core
    unofficial::x-plane::xplm
    whisper
)

if(WIN32)
  target_link_libraries(PilotMonitoring PRIVATE opengl32 ole32 uuid)
endif()

set_target_properties(PilotMonitoring PROPERTIES
  PREFIX ""
  SUFFIX ".xpl"
  OUTPUT_NAME "PilotMonitoring"
)

target_compile_definitions(PilotMonitoring PRIVATE
  IBM=1
  APL=0
  LIN=0
  XPLM200=1
  XPLM210=1
  XPLM300=1
  XPLM301=1
  XPLM302=1
  XPLM303=1
  XPLM400=1
  XPLM410=1
)

if(BUILD_TESTING)
  add_executable(lua_engine_tests EXCLUDE_FROM_ALL
    tests/lua_engine_tests.cpp
  )
  add_executable(c172_tests EXCLUDE_FROM_ALL
    tests/c172_tests.cpp
  )
  add_executable(b738_tests EXCLUDE_FROM_ALL
    tests/b738_tests.cpp
  )

  target_link_libraries(lua_engine_tests PRIVATE pilotmonitoring_core)
  target_link_libraries(c172_tests PRIVATE pilotmonitoring_core)
  target_link_libraries(b738_tests PRIVATE pilotmonitoring_core)

  add_test(NAME lua_engine_tests COMMAND lua_engine_tests)
  add_test(NAME aircraft.C172.tune_radio COMMAND c172_tests tune_radio)
  add_test(NAME aircraft.C172.lineup_checklist COMMAND c172_tests lineup_checklist)
  add_test(NAME aircraft.B738.radio COMMAND b738_tests radio)
  add_test(NAME aircraft.B738.flap COMMAND b738_tests flap)
  add_test(NAME aircraft.B738.references COMMAND b738_tests references)
  add_test(NAME aircraft.B738.automation COMMAND b738_tests automation)
  add_test(NAME aircraft.B738.runway_vacated COMMAND b738_tests runway_vacated)

  set_tests_properties(
    lua_engine_tests
    aircraft.C172.tune_radio
    aircraft.C172.lineup_checklist
    aircraft.B738.radio
    aircraft.B738.flap
    aircraft.B738.references
    aircraft.B738.automation
    aircraft.B738.runway_vacated
    PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

install(TARGETS PilotMonitoring
  RUNTIME DESTINATION "PilotMonitoring/win_x64"
  LIBRARY DESTINATION "PilotMonitoring/win_x64"
)

install(DIRECTORY distribution/resources/
  DESTINATION "PilotMonitoring/resources"
)

install(FILES distribution/README.txt
  DESTINATION "PilotMonitoring"
)

set(PILOTMONITORING_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")
set(PILOTMONITORING_PACKAGE_DEPENDS PilotMonitoring)
if(BUILD_TESTING)
  list(APPEND PILOTMONITORING_PACKAGE_DEPENDS
    lua_engine_tests
    c172_tests
    b738_tests
  )
endif()

add_custom_target(package ALL
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PILOTMONITORING_PACKAGE_DIR}"
  COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
          --config "$<CONFIG>"
          --prefix "${PILOTMONITORING_PACKAGE_DIR}"
  DEPENDS ${PILOTMONITORING_PACKAGE_DEPENDS}
  USES_TERMINAL
  VERBATIM
)
