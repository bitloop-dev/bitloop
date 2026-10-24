include_guard(GLOBAL)

if(NOT DEFINED BITLOOP_PROVIDER OR "${BITLOOP_PROVIDER}" STREQUAL "")
  set(BITLOOP_PROVIDER "AUTO" CACHE STRING "Bitloop provider: AUTO, SOURCE, or PACKAGE")
else()
  set(BITLOOP_PROVIDER "${BITLOOP_PROVIDER}" CACHE STRING "Bitloop provider: AUTO, SOURCE, or PACKAGE")
endif()
set_property(CACHE BITLOOP_PROVIDER PROPERTY STRINGS AUTO SOURCE PACKAGE)

if(NOT DEFINED BITLOOP_SOURCE_DIR)
  set(BITLOOP_SOURCE_DIR "" CACHE PATH "Optional Bitloop source tree override")
else()
  set(BITLOOP_SOURCE_DIR "${BITLOOP_SOURCE_DIR}" CACHE PATH "Optional Bitloop source tree override")
endif()

function(bl_find_bitloop_workspace start_dir out_dir out_is_root)
  get_filename_component(_dir "${start_dir}" REALPATH)
  set(_fallback "")

  while(TRUE)
    set(_marker "${_dir}/.bitloop-workspace")
    if(EXISTS "${_marker}")
      file(READ "${_marker}" _contents)
      string(REPLACE "\r\n" "\n" _contents "${_contents}")
      string(REPLACE "\r" "\n" _contents "${_contents}")
      string(TOLOWER "${_contents}" _contents)
      string(REGEX MATCH "[ \t]*root[ \t]*=[ \t]*([^ \t\r\n]+)" _match "${_contents}")

      set(_is_root OFF)
      if(_match)
        set(_value "${CMAKE_MATCH_1}")
        if(_value STREQUAL "true" OR _value STREQUAL "1" OR
           _value STREQUAL "on" OR _value STREQUAL "yes")
          set(_is_root ON)
        endif()
      endif()

      if(_is_root)
        set(${out_dir} "${_dir}" PARENT_SCOPE)
        set(${out_is_root} ON PARENT_SCOPE)
        return()
      endif()

      if("${_fallback}" STREQUAL "")
        set(_fallback "${_dir}")
      endif()
    endif()

    get_filename_component(_parent "${_dir}" DIRECTORY)
    if("${_parent}" STREQUAL "${_dir}")
      break()
    endif()
    set(_dir "${_parent}")
  endwhile()

  set(${out_dir} "${_fallback}" PARENT_SCOPE)
  set(${out_is_root} OFF PARENT_SCOPE)
endfunction()

function(bl_validate_bitloop_source candidate out_valid out_path)
  if("${candidate}" STREQUAL "")
    set(${out_valid} OFF PARENT_SCOPE)
    set(${out_path} "" PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_candidate "${candidate}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(EXISTS "${_candidate}")
    get_filename_component(_candidate "${_candidate}" REALPATH)
  endif()

  set(_valid ON)
  foreach(_required IN ITEMS
      CMakeLists.txt
      cmake/bitloopSimulation.cmake
      include/bitloop.h
      vcpkg.json)
    if(NOT EXISTS "${_candidate}/${_required}")
      set(_valid OFF)
    endif()
  endforeach()

  set(${out_valid} "${_valid}" PARENT_SCOPE)
  set(${out_path} "${_candidate}" PARENT_SCOPE)
endfunction()

function(bl_find_enclosing_bitloop_source start_dir out_path)
  get_filename_component(_dir "${start_dir}" REALPATH)

  while(TRUE)
    bl_validate_bitloop_source("${_dir}" _valid _candidate)
    if(_valid)
      set(${out_path} "${_candidate}" PARENT_SCOPE)
      return()
    endif()

    get_filename_component(_parent "${_dir}" DIRECTORY)
    if("${_parent}" STREQUAL "${_dir}")
      break()
    endif()
    set(_dir "${_parent}")
  endwhile()

  set(${out_path} "" PARENT_SCOPE)
endfunction()

function(bl_resolve_bitloop_provider source_dir workspace_dir)
  string(TOUPPER "${BITLOOP_PROVIDER}" _requested)
  if(NOT _requested STREQUAL "AUTO" AND
     NOT _requested STREQUAL "SOURCE" AND
     NOT _requested STREQUAL "PACKAGE")
    message(FATAL_ERROR
      "Invalid BITLOOP_PROVIDER='${BITLOOP_PROVIDER}'. Expected AUTO, SOURCE, or PACKAGE.")
  endif()

  if(_requested STREQUAL "PACKAGE" AND NOT "${BITLOOP_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR
      "BITLOOP_SOURCE_DIR cannot be combined with BITLOOP_PROVIDER=PACKAGE.")
  endif()

  set(_resolved_provider "")
  set(_resolved_source "")
  set(_reason "")

  if(_requested STREQUAL "PACKAGE")
    set(_resolved_provider "PACKAGE")
    set(_reason "forced")
  else()
    if(NOT "${BITLOOP_SOURCE_DIR}" STREQUAL "")
      bl_validate_bitloop_source("${BITLOOP_SOURCE_DIR}" _valid _candidate)
      if(NOT _valid)
        message(FATAL_ERROR
          "BITLOOP_SOURCE_DIR is not a Bitloop source tree: ${_candidate}")
      endif()
      set(_resolved_source "${_candidate}")
      set(_reason "explicit source override")
    endif()

    if("${_resolved_source}" STREQUAL "" AND NOT "${workspace_dir}" STREQUAL "")
      bl_validate_bitloop_source("${workspace_dir}/bitloop" _valid _candidate)
      if(_valid)
        set(_resolved_source "${_candidate}")
        set(_reason "workspace source")
      endif()
    endif()

    if("${_resolved_source}" STREQUAL "")
      bl_find_enclosing_bitloop_source("${source_dir}" _candidate)
      if(NOT "${_candidate}" STREQUAL "")
        set(_resolved_source "${_candidate}")
        set(_reason "enclosing source")
      endif()
    endif()

    if(NOT "${_resolved_source}" STREQUAL "")
      set(_resolved_provider "SOURCE")
    elseif(_requested STREQUAL "SOURCE")
      message(FATAL_ERROR
        "BITLOOP_PROVIDER=SOURCE was requested, but no valid Bitloop source tree was found.")
    else()
      set(_resolved_provider "PACKAGE")
      set(_reason "no source tree found")
    endif()
  endif()

  set(BITLOOP_PROVIDER "${_requested}" CACHE STRING "Bitloop provider: AUTO, SOURCE, or PACKAGE" FORCE)
  set(BITLOOP_RESOLVED_PROVIDER "${_resolved_provider}" CACHE INTERNAL "Resolved Bitloop provider" FORCE)
  set(BITLOOP_RESOLVED_SOURCE_DIR "${_resolved_source}" CACHE INTERNAL "Resolved Bitloop source tree" FORCE)
  set(BITLOOP_RESOLVED_REASON "${_reason}" CACHE INTERNAL "Bitloop provider selection reason" FORCE)
  set(BITLOOP_WORKSPACE_DIR "${workspace_dir}" CACHE INTERNAL "Selected Bitloop workspace" FORCE)
endfunction()

function(bl_report_bitloop_provider)
  get_property(_reported GLOBAL PROPERTY BITLOOP_PROVIDER_REPORTED)
  if(_reported)
    return()
  endif()

  if(BITLOOP_RESOLVED_PROVIDER STREQUAL "SOURCE")
    set(_revision "")
    set(_dirty "")
    find_program(_bl_git_executable NAMES git git.exe)
    if(_bl_git_executable)
      execute_process(
        COMMAND "${_bl_git_executable}" -C "${BITLOOP_RESOLVED_SOURCE_DIR}" rev-parse --short HEAD
        RESULT_VARIABLE _revision_result
        OUTPUT_VARIABLE _revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(_revision_result EQUAL 0)
        execute_process(
          COMMAND "${_bl_git_executable}" -C "${BITLOOP_RESOLVED_SOURCE_DIR}" status --porcelain --untracked-files=normal
          RESULT_VARIABLE _status_result
          OUTPUT_VARIABLE _status
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET
        )
        if(_status_result EQUAL 0 AND NOT "${_status}" STREQUAL "")
          set(_dirty ", dirty")
        endif()
        set(_revision "; git ${_revision}${_dirty}")
      else()
        set(_revision "")
      endif()
    endif()

    message(STATUS
      "Bitloop provider: SOURCE (${BITLOOP_RESOLVED_REASON}; ${BITLOOP_RESOLVED_SOURCE_DIR}${_revision})")
  else()
    message(STATUS "Bitloop provider: PACKAGE (${BITLOOP_RESOLVED_REASON}; vcpkg)")
  endif()

  set_property(GLOBAL PROPERTY BITLOOP_PROVIDER_REPORTED TRUE)
endfunction()
