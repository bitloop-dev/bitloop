#pragma once
#include <bitloop/core/event_data.h>

union SDL_Event;

BL_BEGIN_NS

[[nodiscard]] EventData copySDLEvent(const SDL_Event& source);

BL_END_NS
