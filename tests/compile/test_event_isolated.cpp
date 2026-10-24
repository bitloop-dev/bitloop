#include <bitloop/core/event.h>
#include <bitloop/core/input.h>
#include <type_traits>

#if defined(SDL_MAJOR_VERSION) || defined(SDL_stdinc_h_)
#error "Bitloop event and input headers must not include SDL."
#endif

static_assert(bl::EventType::KEY_DOWN == 0x300);
static_assert(std::is_copy_constructible_v<bl::Event>);
static_assert(std::is_copy_assignable_v<bl::Event>);
