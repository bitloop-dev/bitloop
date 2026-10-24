#include <gcem.hpp>
#include <bitloop/core/types.h>

using namespace bl;

constexpr double with_gcem_test()
{
    DVec2 a(1.0, 2.0);
    DVec2 b(3.0, 4.0);
    return a.angleTo(b);
}

int main()
{
    constexpr double test_angle = with_gcem_test();
    return 0;
}
