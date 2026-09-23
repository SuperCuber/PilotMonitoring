# This file runs before project() when the production preset is used.
# It keeps the downloaded vcpkg toolchain out of the source tree's tracked files.

set(PILOTMONITORING_VENDOR_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../third_party/_bootstrap")
file(MAKE_DIRECTORY "${PILOTMONITORING_VENDOR_DIR}")

set(PILOTMONITORING_VCPKG_ROOT
  "${PILOTMONITORING_VENDOR_DIR}/vcpkg")
set(PILOTMONITORING_VCPKG_EXE
  "${PILOTMONITORING_VCPKG_ROOT}/vcpkg.exe")

if(NOT EXISTS "${PILOTMONITORING_VCPKG_EXE}")
  set(_vcpkg_archive
    "${PILOTMONITORING_VENDOR_DIR}/vcpkg-2026.07.29.zip")
  if(NOT EXISTS "${_vcpkg_archive}")
    message(STATUS "Downloading pinned vcpkg source archive")
    file(DOWNLOAD
      "https://github.com/microsoft/vcpkg/archive/refs/tags/2026.07.29.zip"
      "${_vcpkg_archive}"
      SHOW_PROGRESS
      STATUS _vcpkg_download_status
    )
    list(GET _vcpkg_download_status 0 _vcpkg_download_code)
    if(NOT _vcpkg_download_code EQUAL 0)
      list(GET _vcpkg_download_status 1 _vcpkg_download_message)
      message(FATAL_ERROR "vcpkg download failed: ${_vcpkg_download_message}")
    endif()
  endif()

  set(_vcpkg_extract_dir
    "${PILOTMONITORING_VENDOR_DIR}/vcpkg-extract")
  file(REMOVE_RECURSE "${_vcpkg_extract_dir}")
  file(MAKE_DIRECTORY "${_vcpkg_extract_dir}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${_vcpkg_archive}"
    WORKING_DIRECTORY "${_vcpkg_extract_dir}"
    RESULT_VARIABLE _vcpkg_extract_result
  )
  if(NOT _vcpkg_extract_result EQUAL 0)
    message(FATAL_ERROR "Could not extract the vcpkg archive")
  endif()

  file(GLOB _vcpkg_source_dirs LIST_DIRECTORIES true
    "${_vcpkg_extract_dir}/vcpkg-*")
  list(LENGTH _vcpkg_source_dirs _vcpkg_source_count)
  if(NOT _vcpkg_source_count EQUAL 1)
    message(FATAL_ERROR "Unexpected vcpkg archive layout")
  endif()
  list(GET _vcpkg_source_dirs 0 _vcpkg_source_dir)
  file(REMOVE_RECURSE "${PILOTMONITORING_VCPKG_ROOT}")
  file(RENAME "${_vcpkg_source_dir}" "${PILOTMONITORING_VCPKG_ROOT}")
endif()

if(NOT EXISTS "${PILOTMONITORING_VCPKG_EXE}")
  message(STATUS "Bootstrapping vcpkg")
  execute_process(
    COMMAND "${PILOTMONITORING_VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics
    WORKING_DIRECTORY "${PILOTMONITORING_VCPKG_ROOT}"
    RESULT_VARIABLE _vcpkg_bootstrap_result
  )
  if(NOT _vcpkg_bootstrap_result EQUAL 0)
    message(FATAL_ERROR "vcpkg bootstrap failed")
  endif()
endif()

set(CMAKE_TOOLCHAIN_FILE
  "${PILOTMONITORING_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
  CACHE FILEPATH "vcpkg toolchain file" FORCE)
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md"
  CACHE STRING "vcpkg target triplet" FORCE)
