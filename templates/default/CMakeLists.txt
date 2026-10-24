cmake_minimum_required(VERSION 3.21)
project({SIM_NAME} NONE)

include("${CMAKE_CURRENT_LIST_DIR}/tooling/findBitloop.cmake")

bitloop_enable_languages(LANGUAGES C CXX)

# BITLOOP_TEMPLATE_GUIDE_BEGIN
# ---------------------------------  flags  ----------------------------------

# BITLOOP_TEMPLATE_AI_BEGIN
# If this CMake project is opened as root, BITLOOP_DEV_MODE overrides child projects.
# set(BITLOOP_DEV_MODE TRUE)
# BITLOOP_TEMPLATE_AI_END

# ------------------  configure project hierarchy/sources  -------------------
# BITLOOP_TEMPLATE_GUIDE_END

set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/{SIM_NAME}")
file(GLOB_RECURSE SIM_SOURCES CONFIGURE_DEPENDS
    "${SRC_DIR}/*.cpp"
    "${SRC_DIR}/*.h"
    "${SRC_DIR}/*.hpp"
)

# BITLOOP_TEMPLATE_GUIDE_BEGIN
# Create the new project.
# BITLOOP_TEMPLATE_GUIDE_END
bitloop_new_project({SIM_NAME} ${SIM_SOURCES})

# BITLOOP_TEMPLATE_GUIDE_BEGIN
# Add dependency projects.
# bitloop_add_dependency({SIM_NAME} "./path/to/project")

# Finalize the project after declaring all dependencies.
# BITLOOP_TEMPLATE_GUIDE_END
bitloop_finalize()

# BITLOOP_TEMPLATE_GUIDE_BEGIN
# ---------  configure your target (compile options, packages, etc.)  ---------

# Fast math.
# if (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
#     if (MSVC)
#         target_compile_options({SIM_NAME} PRIVATE /arch:AVX2 /fp:fast)
#     else() # GCC/Clang
#         target_compile_options({SIM_NAME} PRIVATE -mavx2 -mfma -ffp-contract=fast -ffast-math -fno-math-errno)
#     endif()
# else()
#     target_compile_options({SIM_NAME} PRIVATE -O3 -flto -msimd128 -mrelaxed-simd -ffast-math -ffp-contract=fast -fno-math-errno)
# endif()
# BITLOOP_TEMPLATE_GUIDE_END
