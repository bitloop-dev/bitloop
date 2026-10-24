#pragma once

/// ======== Debug Features ========
//#define BL_DEBUG_FINITE_DOUBLE_CHECKS
//#define BL_FORCE_RELEASE_FINITE_DOUBLE_CHECKS
//#define BL_DEBUG_INCLUDE_LOG_TABS

//#define BL_FORCE_DEBUG
#define BL_DEBUG_OWNERSHIP 0 // assert on cross-thread data ownership violations


/// ======== Platform Simulation ========
//#define BL_ENABLE_SIMULATED_DEVICE

#ifdef BL_ENABLE_SIMULATED_DEVICE
    struct DebugDevicePreset { int w; int h; float dpr; int threads; };

    /// --- flags ---
    #define BL_SIMULATE_MOBILE  // platform()->isMobile() == true + use mouse to emulate touch
    #define BL_SIMULATE_BROWSER // emscripten

    /// --- presets ---
    #define BL_SIMULATED_DEVICE     DebugDevicePreset{1080, 1998, 2.625f, 8}  // Google Pixel 9a (Chrome)
    //#define BL_SIMULATED_DEVICE   DebugDevicePreset{1080, 2424, 2.625f, 8}  // Google Pixel 9a (fullscreen)

    /// --- scale to comfortable size on screen ---
    //#define BL_SIMULATE_DISPLAY_VIEW_SCALE     0.205f // accurate
    #define BL_SIMULATE_DISPLAY_VIEW_SCALE     0.381f // comfortable
#endif

/// ======== Timer filters ========

#define TIMERS_ENABLED

// -- global timer0/timer1 elapse threshold -- 
constexpr double TIMER_ELAPSED_LIMIT = 0.0; // ms (outputs warning if duration exceeds this limit)

