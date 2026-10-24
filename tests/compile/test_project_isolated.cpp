#include <bitloop/core/project.h>

#if defined(SDL_MAJOR_VERSION) || defined(SDL_stdinc_h_)
#error "Bitloop project headers must not include SDL."
#endif

#if defined(IMGUI_VERSION) || defined(IMPLOT_VERSION)
#error "Bitloop project headers must not include ImGui or ImPlot."
#endif
