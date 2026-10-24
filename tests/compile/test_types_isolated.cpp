#include <bitloop/core/types.h>

using namespace bl;

double no_gcem_test()
{
    DVec2 a(1.0, 2.0);
    DVec2 b(3.0, 4.0);
    return a.angleTo(b);
}

int main()
{
    double test_angle = no_gcem_test();
    return 0;
}
