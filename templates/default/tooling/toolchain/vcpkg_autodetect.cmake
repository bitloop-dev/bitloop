# avoid running bitloop discovery/manifest merging inside CMake try_compile scratch projects
get_property(_bl_in_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
if(_bl_in_try_compile)
  return()
endif()

get_property(_bl_toolchain_ready GLOBAL PROPERTY BITLOOP_VCPKG_TOOLCHAIN_READY)
if(_bl_toolchain_ready)
  if(NOT DEFINED VCPKG_ROOT OR
     NOT EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR "Bitloop vcpkg toolchain was initialized without a valid VCPKG_ROOT")
  endif()
  include("${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/bitloop_provider.cmake")

set(_bl_ninja "")
if (CMAKE_GENERATOR MATCHES "^Ninja")
    if (DEFINED CMAKE_MAKE_PROGRAM AND NOT CMAKE_MAKE_PROGRAM STREQUAL "")
        set(_bl_ninja "${CMAKE_MAKE_PROGRAM}")
    else()
        find_program(_bl_ninja NAMES ninja ninja.exe)

        if (NOT _bl_ninja)
            # _ChatGPT_ try VS-bundled Ninja relative to the cmake.exe being used
            get_filename_component(_cmake_bin_dir "${CMAKE_COMMAND}" DIRECTORY)          # .../CMake/bin
            get_filename_component(_cmake_root1   "${_cmake_bin_dir}" DIRECTORY)        # .../CMake
            get_filename_component(_cmake_root2   "${_cmake_root1}" DIRECTORY)          # .../Microsoft/CMake (VS layout)

            set(_cand_vs_ninja "${_cmake_root2}/Ninja/ninja.exe")
            if (EXISTS "${_cand_vs_ninja}")
                set(_bl_ninja "${_cand_vs_ninja}")
            endif()
        endif()

        if (NOT _bl_ninja)
            # _ChatGPT_ try CLion-bundled Ninja near CLion cmake.exe
            get_filename_component(_cmake_bin_dir "${CMAKE_COMMAND}" DIRECTORY)          # .../cmake/win/x64/bin
            get_filename_component(_cmake_root1   "${_cmake_bin_dir}" DIRECTORY)        # .../cmake/win/x64
            get_filename_component(_cmake_root2   "${_cmake_root1}" DIRECTORY)          # .../cmake/win
            get_filename_component(_cmake_root3   "${_cmake_root2}" DIRECTORY)          # .../cmake
            get_filename_component(_cmake_root4   "${_cmake_root3}" DIRECTORY)          # .../bin

            set(_cand_clion_ninja "${_cmake_root4}/ninja/win/x64/ninja.exe")
            if (EXISTS "${_cand_clion_ninja}")
                set(_bl_ninja "${_cand_clion_ninja}")
            endif()
        endif()
    endif()

    if (NOT _bl_ninja)
        message(FATAL_ERROR "Ninja generator selected but ninja.exe not found and CMAKE_MAKE_PROGRAM is empty")
    endif()

    set(CMAKE_MAKE_PROGRAM "${_bl_ninja}" CACHE FILEPATH "" FORCE)
endif()

# vcpkg_autodetect.cmake
#    - Searches for an existing vcpkg in the current directory/workspace
#    - If one isn't found, it clones vcpkg at a pinned <sha>
#    - Also picks a location for shared binary sources/cache to avoid unnecessary rebuilds
#    - then chain-load that vcpkg toolchain

# Helper: Clones vcpkg to 'dest_dir' at the pinned <sha>
function(clone_vcpkg dest_dir pinned_sha)
  get_filename_component(_dst "${dest_dir}" REALPATH)
  get_filename_component(_dst_parent "${_dst}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dst_parent}")

  if(EXISTS "${_dst}/scripts/buildsystems/vcpkg.cmake")
    set(VCPKG_ROOT "${_dst}" PARENT_SCOPE)
    return()
  endif()

  find_package(Git QUIET)
  set(_ok OFF)
  if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" clone https://github.com/microsoft/vcpkg "${_dst}"
                    RESULT_VARIABLE rv)
    if(rv EQUAL 0)
      execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_dst}" checkout ${pinned_sha}
                      RESULT_VARIABLE rv2)
      if(rv2 EQUAL 0)
        set(_ok ON)
      else()
        file(REMOVE_RECURSE "${_dst}")
      endif()
    endif()
  endif()

  if(NOT _ok)
    set(_zip "${_dst_parent}/vcpkg-${pinned_sha}.zip")
    file(DOWNLOAD "https://github.com/microsoft/vcpkg/archive/${pinned_sha}.zip" "${_zip}" TLS_VERIFY ON)
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${_dst_parent}")
    file(RENAME "${_dst_parent}/vcpkg-${pinned_sha}" "${_dst}")
  endif()

  if(NOT EXISTS "${_dst}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR "[vcpkg] Expected toolchain not found under ${_dst}")
  endif()

  set(VCPKG_ROOT "${_dst}" PARENT_SCOPE)
endfunction()



# Resolve workspace and Bitloop provider before composing the vcpkg manifest.
set(_project_dir "${CMAKE_SOURCE_DIR}")
bl_find_bitloop_workspace("${_project_dir}" WORKSPACE_DIR IS_ROOT_WORKSPACE)
bl_resolve_bitloop_provider("${_project_dir}" "${WORKSPACE_DIR}")
bl_report_bitloop_provider()

set(VCPKG_PINNED_SHA "74e6536215718009aae747d86d84b78376bf9e09" CACHE STRING "Pinned vcpkg commit SHA")


# Potential vcpkg directory paths
set(LOCAL_VCPKG_DIR       "${CMAKE_SOURCE_DIR}/.vcpkg")
if(NOT "${WORKSPACE_DIR}" STREQUAL "")
  set(WORKSPACE_VCPKG_DIR "${WORKSPACE_DIR}/.vcpkg")
else()
  set(WORKSPACE_VCPKG_DIR "")
endif()

# Potential vcpkg toolchain paths
set(LOCAL_VCPKG_PATH      "${LOCAL_VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")
set(WORKSPACE_VCPKG_PATH  "${WORKSPACE_VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")

# -------------- Set flags --------------
set(FOUND_WORKSPACE          FALSE)
set(FOUND_LOCAL_VCPKG        FALSE)
set(FOUND_WORKSPACE_VCPKG    FALSE)
set(FOUND_UNUSED_LOCAL_VCPKG FALSE)

# Found workspace?
if (NOT "${WORKSPACE_DIR}" STREQUAL "" AND EXISTS "${WORKSPACE_DIR}")
  set(FOUND_WORKSPACE TRUE)
endif()

# Found local vcpkg?
if (EXISTS "${LOCAL_VCPKG_PATH}")
  set(FOUND_LOCAL_VCPKG TRUE)
endif()

# Found workspace vcpkg?
if (FOUND_WORKSPACE AND EXISTS "${WORKSPACE_VCPKG_PATH}")
  set(FOUND_WORKSPACE_VCPKG TRUE)
endif()

# Both workspace and local vcpkg found?
if (FOUND_WORKSPACE AND FOUND_LOCAL_VCPKG)
    # Are they the same? Treat as workspace, not local
    get_filename_component(_local_vcpkg_real "${LOCAL_VCPKG_DIR}" ABSOLUTE)
    get_filename_component(_workspace_vcpkg_real "${WORKSPACE_VCPKG_DIR}" ABSOLUTE)
    if ("${_local_vcpkg_real}" STREQUAL "${_workspace_vcpkg_real}")
        # They're the same, disable local
        set(FOUND_LOCAL_VCPKG FALSE)
    else()
        # Different - workspace vcpkg takes priority
        set(FOUND_UNUSED_LOCAL_VCPKG TRUE)
    endif()
endif()

# ---------------------------------------


message(STATUS "")
message(STATUS "─────────────────────────────────────── Workspace ───────────────────────────────────────")
message(STATUS "WORKSPACE_DIR:          ${WORKSPACE_DIR}")
message(STATUS "IS_ROOT_WORKSPACE:      ${IS_ROOT_WORKSPACE}")
message(STATUS "FOUND_WORKSPACE:        ${FOUND_WORKSPACE}")

if (FOUND_WORKSPACE_VCPKG)
    message(STATUS "FOUND_WORKSPACE_VCPKG:  TRUE (${WORKSPACE_VCPKG_DIR})")
else()
    message(STATUS "FOUND_WORKSPACE_VCPKG:  FALSE")
endif()

if (FOUND_LOCAL_VCPKG)
    message(STATUS "FOUND_LOCAL_VCPKG:      TRUE (${LOCAL_VCPKG_PATH})")
else()
    message(STATUS "FOUND_LOCAL_VCPKG:      FALSE")
endif()

message(STATUS "─────────────────────────────────────────────────────────────────────────────────────────")
message(STATUS "")

if (FOUND_WORKSPACE)
    if (NOT FOUND_WORKSPACE_VCPKG)
        message(STATUS "Cloning vcpkg in to workspace")
        clone_vcpkg("${WORKSPACE_VCPKG_DIR}" "${VCPKG_PINNED_SHA}")
    endif()

    set(_vcpkg_dir "${WORKSPACE_VCPKG_DIR}")

    if (FOUND_UNUSED_LOCAL_VCPKG)
        message(STATUS "Found local vcpkg which will be ignored as shared workspace vcpkg takes priority.")
        message(STATUS "Consider removing the local vcpkg/cache")
    endif()

elseif (NOT FOUND_LOCAL_VCPKG)
    message(STATUS "No vcpkg found - cloning local vcpkg")
    clone_vcpkg("${LOCAL_VCPKG_DIR}" "${VCPKG_PINNED_SHA}")
    set(_vcpkg_dir "${LOCAL_VCPKG_DIR}")
elseif (FOUND_LOCAL_VCPKG)
    message(STATUS "Local vcpkg found")
    set(_vcpkg_dir "${LOCAL_VCPKG_DIR}")
endif()


# Override VCPKG_ROOT with established vcpkg dir
set(ENV{VCPKG_ROOT} "${_vcpkg_dir}")
set(VCPKG_ROOT "${_vcpkg_dir}" CACHE PATH "" FORCE)

# Pick a root location relative to the determined VCPKG_ROOT
get_filename_component(_root_dir "${_vcpkg_dir}/.." REALPATH)

set(_cache_dir "${_root_dir}/.vcpkg-cache")

file(MAKE_DIRECTORY "${_cache_dir}")

# Shared binary cache
set(ENV{VCPKG_DEFAULT_BINARY_CACHE} "${_cache_dir}")
set(ENV{VCPKG_BINARY_SOURCES} "clear;files,${_cache_dir},readwrite")



# ---- Manifest Compiler (merge child vcpkg.json into one generated manifest) ----

set(BL_MERGED_MANIFEST_DIR "${CMAKE_BINARY_DIR}/_vcpkg_merged")

file(MAKE_DIRECTORY "${BL_MERGED_MANIFEST_DIR}")

# Avoid recursion if this toolchain loader is hit during the discovery configure.
if (DEFINED BITLOOP_INTERNAL_DISCOVERY_RUN AND BITLOOP_INTERNAL_DISCOVERY_RUN)
    set(BL_CHILD_MANIFESTS "")
else()
    message(STATUS "Running bitloop project discovery phase")

    set(_bl_discovery_build_dir "${CMAKE_BINARY_DIR}/_bitloop_discovery")
    set(_bl_discovery_out_file  "${_bl_discovery_build_dir}/discovered_manifests.cmake")

    file(MAKE_DIRECTORY "${_bl_discovery_build_dir}")

    set(_toolchain_dir "${CMAKE_CURRENT_LIST_DIR}")
    set(_bl_discovery_toolchain "${_toolchain_dir}/discovery_toolchain.cmake")

    set(_cmd
        "${CMAKE_COMMAND}"
        -G "${CMAKE_GENERATOR}"
        -S "${_project_dir}"
        -B "${_bl_discovery_build_dir}"
        -DBITLOOP_DISCOVERY=ON
        -DBITLOOP_INTERNAL_DISCOVERY_RUN=1
        "-DBITLOOP_DISCOVERY_OUT:FILEPATH=${_bl_discovery_out_file}"
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${_bl_discovery_toolchain}"
    )

    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND _cmd -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND _cmd -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()

    if (CMAKE_GENERATOR MATCHES "^Ninja")
        list(APPEND _cmd "-DCMAKE_MAKE_PROGRAM:FILEPATH=${_bl_ninja}")
    endif()

    # Preserve triplet
    if (DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
        list(APPEND _cmd "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
    endif()

    execute_process(
            COMMAND ${_cmd}
            RESULT_VARIABLE _r
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE  _err
            COMMAND_ECHO STDOUT
    )

    if (NOT _r EQUAL 0)
        message(SEND_ERROR "Bitloop discovery LOG: ${_out}")
        message(SEND_ERROR "Bitloop discovery ERROR: ${_err}")
        message(FATAL_ERROR "Bitloop discovery configure failed.")
    endif()

    if (NOT EXISTS "${_bl_discovery_out_file}")
        message(FATAL_ERROR "Bitloop: discovery output missing: ${_bl_discovery_out_file}")
    endif()

    include("${_bl_discovery_out_file}") # -> defines BL_CHILD_MANIFESTS

    if(BITLOOP_RESOLVED_PROVIDER STREQUAL "SOURCE")
        list(APPEND BL_CHILD_MANIFESTS "${BITLOOP_RESOLVED_SOURCE_DIR}/vcpkg.json")
    endif()

    if(BL_CHILD_MANIFESTS)
        list(REMOVE_DUPLICATES BL_CHILD_MANIFESTS)
        list(SORT BL_CHILD_MANIFESTS)
    endif()
endif()


# Guard against try-compile / foreign superprojects:
# Only run when the current top-level source dir is your repo root
if(NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${_project_dir}")
  return()
endif()

set(BL_MERGED_MANIFEST_DIR "${CMAKE_BINARY_DIR}/_vcpkg_merged")
file(MAKE_DIRECTORY "${BL_MERGED_MANIFEST_DIR}")

# BL_CHILD_MANIFESTS already prepared by projects

include("${_project_dir}/tooling/toolchain/merge_vcpkg_manifests.cmake")
set(_bl_provided_ports "")
if(BITLOOP_RESOLVED_PROVIDER STREQUAL "SOURCE")
  list(APPEND _bl_provided_ports bitloop)
endif()

bl_merge_vcpkg_manifests(
  ROOT_MANIFEST   "${_project_dir}/vcpkg.json"
  CHILD_MANIFESTS "${BL_CHILD_MANIFESTS}"
  PROVIDED_PORTS  "${_bl_provided_ports}"
  OUT_DIR         "${BL_MERGED_MANIFEST_DIR}"
  MODE            "UNION"
)

# Point vcpkg at the generated manifest (must be set before including vcpkg.cmake)
set(VCPKG_MANIFEST_DIR "${BL_MERGED_MANIFEST_DIR}" CACHE PATH "" FORCE)
set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "" FORCE)


# Finally, load vcpkg toolchain
set_property(GLOBAL PROPERTY BITLOOP_VCPKG_TOOLCHAIN_READY TRUE)
include("${_vcpkg_dir}/scripts/buildsystems/vcpkg.cmake")
