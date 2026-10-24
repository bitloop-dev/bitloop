# Project-local Bitloop bootstrap. This file is intentionally vendored so a
# generated project remains configurable after it is detached from a workspace.
include_guard(GLOBAL)

get_property(_bl_in_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
if(_bl_in_try_compile)
  return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/toolchain/bitloop_provider.cmake")

macro(_bl_activate_bitloop_provider)
  if(NOT TARGET bitloop::bitloop)
    if(NOT DEFINED BITLOOP_RESOLVED_PROVIDER OR
       "${BITLOOP_RESOLVED_PROVIDER}" STREQUAL "")
      bl_find_bitloop_workspace("${CMAKE_SOURCE_DIR}" _bl_workspace _bl_workspace_is_root)
      bl_resolve_bitloop_provider("${CMAKE_SOURCE_DIR}" "${_bl_workspace}")
    endif()

    if(BITLOOP_RESOLVED_PROVIDER STREQUAL "SOURCE")
      add_subdirectory(
        "${BITLOOP_RESOLVED_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/_bitloop"
        EXCLUDE_FROM_ALL
      )
    elseif(BITLOOP_RESOLVED_PROVIDER STREQUAL "PACKAGE")
      find_package(bitloop CONFIG REQUIRED)
    else()
      message(FATAL_ERROR
        "Bitloop provider was not resolved: '${BITLOOP_RESOLVED_PROVIDER}'")
    endif()
  endif()

  if(NOT TARGET bitloop::bitloop)
    message(FATAL_ERROR "The selected Bitloop provider did not define bitloop::bitloop")
  endif()

  bl_report_bitloop_provider()
endmacro()

macro(bitloop_enable_languages)
  if(NOT BITLOOP_DISCOVERY)
    set(_bl_language_args ${ARGN})
    list(FIND _bl_language_args "LANGUAGES" _bl_languages_index)

    if(_bl_languages_index EQUAL 0)
      list(REMOVE_AT _bl_language_args 0)
    elseif(_bl_languages_index EQUAL 1)
      # Compatibility with bitloop_enable_languages(<project> LANGUAGES ...).
      list(REMOVE_AT _bl_language_args 0 1)
    else()
      message(FATAL_ERROR
        "bitloop_enable_languages expects LANGUAGES followed by one or more languages")
    endif()

    if(NOT _bl_language_args)
      message(FATAL_ERROR "bitloop_enable_languages requires at least one language")
    endif()

    foreach(_bl_language IN LISTS _bl_language_args)
      if(NOT CMAKE_${_bl_language}_COMPILER_LOADED)
        enable_language(${_bl_language})
      endif()
    endforeach()

    if(CMAKE_CXX_COMPILER_LOADED)
      include(CheckCXXCompilerFlag)
    endif()

    _bl_activate_bitloop_provider()
  endif()
endmacro()

if(BITLOOP_DISCOVERY)
  include("${CMAKE_CURRENT_LIST_DIR}/toolchain/discovery_shims.cmake")
endif()
