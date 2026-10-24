/*#ifndef GCEM_OVERLOADS_H
#define GCEM_OVERLOADS_H

#include <cmath>

// including gcem before custom float types (for potential constexpr support)
#include <gcem.hpp>

namespace bl
{
    namespace detail
    {
        template<class R, class ExpUnsigned>
        constexpr R ipow_nonneg(R base, ExpUnsigned exp)
        {
            R result = R(1);

            while (exp)
            {
                if (exp & ExpUnsigned(1))
                    result *= base;

                exp >>= 1;

                if (exp)
                    base *= base;
            }

            return result;
        }
    }

    // drop gcem functions in 'bl' namespace where overloads already exist for fdd/... (constexpr used where supported)

    using std::isnan;
    using std::isinf;
    using std::isfinite;
    using std::remainder; // no gcem equivelant

    template<class T> constexpr T abs(T x)   { if consteval { return gcem::abs(x);   } else { return std::abs(x);   } }
    template<class T> constexpr T floor(T x) { if consteval { return gcem::floor(x); } else { return std::floor(x); } }
    template<class T> constexpr T ceil(T x)  { if consteval { return gcem::ceil(x);  } else { return std::ceil(x);  } }
    template<class T> constexpr T round(T x) { if consteval { return gcem::round(x); } else { return std::round(x); } }
    template<class T> constexpr T cos(T x)   { if consteval { return gcem::cos(x);   } else { return std::cos(x);   } }
    template<class T> constexpr T sin(T x)   { if consteval { return gcem::sin(x);   } else { return std::sin(x);   } }
    template<class T> constexpr T tan(T x)   { if consteval { return gcem::tan(x);   } else { return std::tan(x);   } }
    template<class T> constexpr T atan(T x)  { if consteval { return gcem::atan(x);  } else { return std::atan(x);  } }
    template<class T> constexpr T sqrt(T x)  { if consteval { return gcem::sqrt(x);  } else { return std::sqrt(x);  } }
    template<class T> constexpr T exp(T x)   { if consteval { return gcem::exp(x);   } else { return std::exp(x);   } }
    template<class T> constexpr T log(T x)   { if consteval { return gcem::log(x);   } else { return std::log(x);   } }
    template<class T> constexpr T log2(T x)  { if consteval { return gcem::log2(x);  } else { return std::log2(x);  } }
    template<class T> constexpr T log10(T x) { if consteval { return gcem::log10(x); } else { return std::log10(x); } }
    template<class T> constexpr T trunc(T x) { if consteval { return gcem::trunc(x); } else { return std::trunc(x); } }

    template<class T, class U> constexpr T atan2(T y, U x) { if consteval { return gcem::atan2(y, x); } else { return std::atan2(y, x); } }
    template<class T, class U> constexpr T fmod(T x, U y)  { if consteval { return gcem::fmod(x, y);  } else { return std::fmod(x, y);  } }
    template<class T, class U> constexpr std::common_type_t<T, U> pow(T x, U y)
    {
        using R = std::common_type_t<T, U>;

        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>)
        {
            const R base = static_cast<R>(x);

            if constexpr (std::is_signed_v<U>)
            {
                if (y < 0)
                {
                    // integral result cannot represent reciprocals
                    if (base == R(1))  return R(1);
                    if (base == R(-1)) return (y % 2 != 0) ? R(-1) : R(1);
                    return R(0);
                }
            }

            using Uu = std::make_unsigned_t<std::remove_cv_t<U>>;
            const Uu exp = static_cast<Uu>(y);

            return detail::ipow_nonneg<R, Uu>(base, exp);
        }
        else
        {
            const R base = static_cast<R>(x);
            const R exp = static_cast<R>(y);

            if consteval {
                return gcem::pow(base, exp);
            }
            else
            {
                return std::pow(base, exp);
            }
        }
    }
}

#endif
*/
