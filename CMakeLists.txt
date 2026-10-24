# bitloop/CMakeLists.txt
cmake_minimum_required(VERSION 3.21)
project(bitloop VERSION 0.9 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# ────────── Default Options ──────────
option(BITLOOP_WITH_FFMPEG      "Build with FFmpeg"             OFF)
option(BITLOOP_WITH_FFMPEG_X265 "FFmpeg built with x265 (GPL)"  OFF)
option(BITLOOP_BUILD_TESTS      "Build tests"                   ${PROJECT_IS_TOP_LEVEL})

# ────────── Bootstrap package helpers for both in‑tree & installed modes ──────────
set(BL_SIM_CMAKE "${CMAKE_CURRENT_LIST_DIR}/cmake/bitloopSimulation.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BL_SIM_CMAKE}")
include(${BL_SIM_CMAKE})

# ----------------------------
# Discovery mode: provide bitloop::bitloop and stop here
# ----------------------------
if (BITLOOP_DISCOVERY)
  if (NOT TARGET bitloop::bitloop)
    add_library(bitloop INTERFACE)
    add_library(bitloop::bitloop ALIAS bitloop)

    # Match normal public include root ($<BUILD_INTERFACE:.../include>)
    target_include_directories(bitloop INTERFACE
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
    )
  endif()

  include("${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain/discovery_shims.cmake")

  # discovery only; add_subdirectory(examples) called at end of file (once bitloop::bitloop is guaranteed to exist)
  if(PROJECT_IS_TOP_LEVEL)
    bitloop_new_project(BitloopExamples)
    bitloop_add_dependency(BitloopExamples examples)
    bitloop_finalize()
  endif()

  return()
endif()

# ──────────────────────────────────────────────────────────────────
# ──────────────────────  Framework Sources  ───────────────────────
# ──────────────────────────────────────────────────────────────────

set(BL_INC      "${CMAKE_CURRENT_SOURCE_DIR}/include/bitloop")
set(BL_SRC      "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(HINT_FILE   "${BL_INC}/cpp.hint")
#set(NATVIS_FILE "${BL_INC}/bl.natvis")

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/bitloop.h"
    "${BL_SRC}/bitloop.cpp"
    "${BL_INC}/core/*.h"
    "${BL_SRC}/core/*.cpp"
)

file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${BL_INC}/platform/*.h"
    "${BL_SRC}/platform/*.cpp"
)

file(GLOB UTILITY_SOURCES CONFIGURE_DEPENDS
    "${BL_SRC}/util/*.cpp"
    "${BL_INC}/util/*.h"
)

file(GLOB IMGUIX_SOURCES CONFIGURE_DEPENDS
    "${BL_SRC}/imguix/*.cpp"
    "${BL_INC}/imguix/*.h"
)

file(GLOB NANOVGX_SOURCES CONFIGURE_DEPENDS
    "${BL_SRC}/nanovgx/*.cpp"
    "${BL_INC}/nanovgx/*.h"
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
source_group("Core"                  FILES ${CORE_SOURCES})
source_group("Platform"              FILES ${PLATFORM_SOURCES})
source_group("Utility"               FILES ${UTILITY_SOURCES})
source_group("ImGui (ext)"           FILES ${IMGUIX_SOURCES})
source_group("NanoVG (ext)"          FILES ${NANOVGX_SOURCES})
source_group("Simulations"           FILES ${SIMULATION_HEADERS} ${SIMULATION_SOURCES})

# ──────── Mark all .c files as c++ ────────
foreach(_cfile IN LISTS EXTERNAL_SOURCES)
  if(_cfile MATCHES "\\.c$")
    set_source_files_properties(${_cfile} PROPERTIES LANGUAGE CXX)
  endif()
endforeach()


set(ALL_SRC_FILES
    ${HINT_FILE}
    #${NATVIS_FILE}

    ${CORE_SOURCES}
    ${PLATFORM_SOURCES}
    ${UTILITY_SOURCES}
    ${IMGUIX_SOURCES}
    ${NANOVGX_SOURCES}
)

add_library(bitloop STATIC ${ALL_SRC_FILES})
_apply_common_settings(bitloop)

if (MSVC)
    target_compile_options(bitloop PRIVATE /Bt+)
    target_link_options(bitloop PRIVATE /TIME)
endif()


# todo: To embed icon, you will need to place icon/rc in "static" dir and add a step to copy
#       somewhere into the build folder, then call in _apply_main_settings so all child sims
#       get the same icon.
#
#if(WIN32)
#	message(STATUS "${CMAKE_CURRENT_BINARY_DIR}/../app/data/icon/appicon.rc")
#	target_sources(bitloop PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/../app/data/icon/appicon.rc")
#endif()


# Precompiled headers (build performance)
# Limit PCH to stable standard headers to maximize reuse and minimize invalidation.
#target_precompile_headers(bitloop PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/bitloop/pch.h>")

if (EMSCRIPTEN)
    # Fix missing __EMSCRIPTEN__ define (possibly due to custom toolchain loader)
	target_compile_definitions(bitloop PUBLIC __EMSCRIPTEN__=1)
endif()

target_compile_definitions(bitloop PUBLIC
  $<$<CONFIG:Debug>:BL_DEBUG=1>
  $<$<CONFIG:RelWithDebInfo>:BL_RELWITHDEBINFO=1>
  $<$<CONFIG:Release>:BL_RELEASE=1>
  $<$<CONFIG:MinSizeRel>:BL_MINSIZEREL=1>
)

#target_precompile_headers(bitloop PUBLIC <bitloop.h>)

target_include_directories(bitloop
    PUBLIC
        # Make: #include<bitloop.h> possible from child projects
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

)

# ──────────────────────────────────────────────────────────────────
# ─────────────────────────  Dependencies  ─────────────────────────
# ──────────────────────────────────────────────────────────────────

find_package(Freetype CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(fltx CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(nanovg CONFIG REQUIRED)
find_package(NanoSVG CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)

find_path(TSL_ORDERED_MAP_INCLUDE_DIRS NAMES "tsl/ordered_hash.h" REQUIRED)
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
#find_path(GCEM_INCLUDE_DIRS "gcem.hpp")


target_link_libraries(bitloop
  PUBLIC
    Freetype::Freetype
    imgui::imgui
    implot::implot
    SDL3::SDL3
    glad::glad
    fltx::fltx
    concurrentqueue::concurrentqueue
    nanovg::nanovg
    NanoSVG::nanosvg
    NanoSVG::nanosvgrast
    WebP::webp WebP::libwebpmux WebP::webpdemux
    nlohmann_json::nlohmann_json
    unofficial::brotli::brotlidec
    unofficial::brotli::brotlienc
)
target_include_directories(bitloop PRIVATE ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS})
target_include_directories(bitloop PRIVATE ${TSL_ORDERED_MAP_INCLUDE_DIRS})
#target_include_directories(bitloop PRIVATE ${GCEM_INCLUDE_DIRS})

if (NOT EMSCRIPTEN)
    #message(STATUS "Non-web build - permitting FFmpeg")
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)
    find_package(FFMPEG QUIET COMPONENTS avcodec avformat avutil swscale)

    # BITLOOP_WITH_FFMPEG
    if (FFMPEG_FOUND)
       set(BITLOOP_WITH_FFMPEG TRUE)
    else()
       set(BITLOOP_WITH_FFMPEG FALSE)
    endif()

    # BITLOOP_WITH_FFMPEG_X265
    find_library(X265_LIB NAMES x265 x265-static libx265)
    find_path(X265_INCLUDE_DIR NAMES x265.h)
     if (X265_LIB AND X265_INCLUDE_DIR)
       set(BITLOOP_WITH_FFMPEG_X265 TRUE)
    else()
       set(BITLOOP_WITH_FFMPEG_X265 FALSE)
     endif()

    #message(STATUS "BITLOOP_WITH_FFMPEG:        ${BITLOOP_WITH_FFMPEG}")
    #message(STATUS "BITLOOP_WITH_FFMPEG_X265:   ${BITLOOP_WITH_FFMPEG_X265}")

    if(FFMPEG_FOUND)
        target_link_libraries(bitloop PUBLIC ${FFMPEG_LIBRARIES})
        set(BITLOOP_WITH_FFMPEG ON)
    endif()
else()
    message(STATUS "Web build - not permitting FFmpeg")
    set(BITLOOP_WITH_FFMPEG FALSE)
    set(BITLOOP_WITH_FFMPEG_X265 FALSE)
endif()

target_compile_definitions(bitloop PUBLIC
  BITLOOP_FFMPEG_ENABLED=$<BOOL:${BITLOOP_WITH_FFMPEG}>
  BITLOOP_FFMPEG_X265_ENABLED=$<BOOL:${BITLOOP_WITH_FFMPEG_X265}>
)

add_library(bitloop::bitloop ALIAS bitloop)

# Experimental source-build target. Keep this list explicit while separating
# backend calls from simulation code; an archive build does not prove isolation.
set(ALL_CORE_SRC_FILES
    "${BL_SRC}/core/camera.cpp"
    "${BL_SRC}/core/event.cpp"
    "${BL_SRC}/core/layout.cpp"
    "${BL_SRC}/core/layout_solver.cpp"
    "${BL_SRC}/core/project.cpp"
    "${BL_SRC}/core/project_worker.cpp"
    "${BL_SRC}/core/scene.cpp"
    "${BL_SRC}/core/snapshot_presets.cpp"
    "${BL_SRC}/core/viewport.cpp"
    "${BL_SRC}/core/sim_checkpoint.cpp"
    "${BL_SRC}/core/sim_snapshot.cpp"
    "${BL_SRC}/core/sim_trace.cpp"
    "${BL_SRC}/util/json.cpp"
    "${BL_SRC}/util/text_util.cpp"
    "${BL_SRC}/imguix/imguix.cpp"
    "${BL_SRC}/imguix/imgui_gradient_edit.cpp"
)

add_library(bitloop-core STATIC EXCLUDE_FROM_ALL ${ALL_CORE_SRC_FILES})
add_library(bitloop::core ALIAS bitloop-core)

target_compile_features(bitloop-core PUBLIC cxx_std_23)
set_target_properties(bitloop-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER "Bitloop/Experimental"
)

target_include_directories(bitloop-core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}
        ${TSL_ORDERED_MAP_INCLUDE_DIRS}
)

# Mirror shared-type configuration, including dev-mode definitions added by the
# consuming project later. This does not link the full library into core.
target_compile_definitions(bitloop-core
    PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    PUBLIC "$<TARGET_PROPERTY:bitloop,INTERFACE_COMPILE_DEFINITIONS>"
)

target_link_libraries(bitloop-core
    PUBLIC
        imgui::imgui
        implot::implot
        fltx::fltx
        concurrentqueue::concurrentqueue
        nlohmann_json::nlohmann_json
)

# _apply_common_settings also sets up the graphical Web application.
if(MSVC)
    target_compile_options(bitloop-core PRIVATE
        /MP /permissive- /W3 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS /Bt+
    )
elseif(EMSCRIPTEN)
    target_compile_options(bitloop-core PUBLIC -pthread -msimd128)
    target_link_options(bitloop-core INTERFACE -pthread)
endif()

if (NOT EMSCRIPTEN)
    add_executable(reconstruct_full_frame EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/trace/reconstruct_full_frame.cpp"
    )
    target_link_libraries(reconstruct_full_frame PRIVATE nlohmann_json::nlohmann_json)
    target_compile_features(reconstruct_full_frame PRIVATE cxx_std_20)
    set_target_properties(reconstruct_full_frame PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/_bitloop/tools"
        FOLDER "Bitloop/Tools"
    )

    install(TARGETS reconstruct_full_frame
        RUNTIME DESTINATION bin
    )
endif()


# ──────── Install static libs ────────
install(TARGETS bitloop
    EXPORT bitloopTargets
    ARCHIVE  DESTINATION lib
    LIBRARY  DESTINATION lib
    RUNTIME  DESTINATION bin
)

# ──────── Install public headers ────────
install(DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/include/" # trailing slash important to avoid nesting another "include/"
    DESTINATION include
    FILES_MATCHING
      PATTERN "*.h"
      PATTERN "*.hpp"
)

# ──────── Export CMake config file for find_package() ────────
install(EXPORT bitloopTargets
    NAMESPACE bitloop::
    DESTINATION share/bitloop
)

# ──────── Install framework main entry‐point template ────────
#message(STATUS "Creating: ${CMAKE_CURRENT_SOURCE_DIR}/src/bitloop_main.cpp")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bitloop_main.cpp"
    DESTINATION share/bitloop/src
    RENAME bitloop_main.cpp
)

# ──────── Install common ────────
if (EMSCRIPTEN)
    install(DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/common"
        DESTINATION share/bitloop
        PATTERN "README.md" EXCLUDE
    )
else()
    # Non-emscripten
    install(DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/common"
        DESTINATION share/bitloop
        PATTERN "README.md" EXCLUDE
        PATTERN "static/" EXCLUDE
        PATTERN "static/*" EXCLUDE
    )
endif()

# ──────── Install simulation cmake helpers ────────
install(FILES
   "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bitloopSimulation.cmake"
   DESTINATION share/bitloop/cmake
 )

# ──────── Config & Version file ────────
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bitloopConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/bitloopConfig.cmake"
    INSTALL_DESTINATION share/bitloop/cmake)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/bitloopConfigVersion.cmake"
    VERSION  ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/bitloopConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/bitloopConfigVersion.cmake"
    DESTINATION share/bitloop/cmake)


if(PROJECT_IS_TOP_LEVEL)
    message(STATUS "Loaded bitloop framework as root project - including example projects")
    add_subdirectory(examples)
endif()


if (BITLOOP_BUILD_TESTS)

  enable_testing()
  add_subdirectory(tests)
endif()
