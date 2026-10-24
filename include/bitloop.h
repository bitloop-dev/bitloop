#pragma once

#include <fltx.h>

#include <bitloop/platform/platform.h>

// util
#include <bitloop/util/compression.h>
#include <bitloop/util/math_util.h>
#include <bitloop/util/text_util.h>
#include <bitloop/util/json.h>

// core
#include <bitloop/core/scene.h>
#include <bitloop/core/project.h>
#include <bitloop/core/viewport.h>
#include <bitloop/core/main_window.h>

#ifdef BITLOOP_DEV_MODE
    #include <bitloop/core/sim_checkpoint.h>
    #include <bitloop/core/sim_snapshot.h>
    #include <bitloop/core/sim_trace.h>
    #include <bitloop/core/headless_runner.h>
    #include <bitloop/core/input_emulation.h>
#endif

#include <bitloop/core/physics2d.h>

#include <bitloop/nanovgx/nano_canvas.h>
#include <bitloop/nanovgx/nano_bitmap.h>
#include <bitloop/nanovgx/nano_shader_surface.h>

// imgui
#include <bitloop/imguix/imguix.h>



int bitloop_main(int, char* []);
