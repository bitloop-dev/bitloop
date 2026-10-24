#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "../../../.agents/skills/bitloop-development/assets/health_probe_support.hpp"

#include <limits>
#include <string>

TEST_CASE("temporary health probe support selects explicit frames")
{
    bitloop_health::ExplicitFrameSchedule schedule{ 100, 2, 5, 2, 3 };

    REQUIRE(schedule.contains(2));
    REQUIRE(schedule.contains(100));
    REQUIRE_FALSE(schedule.contains(4));
    REQUIRE(schedule.selectedFrames() == std::vector<bitloop_health::FrameIndex>{ 2, 3, 5, 100 });
}

TEST_CASE("temporary health probe summaries merge stable chunk reductions")
{
    bitloop_health::ScalarSummary first;
    first.add(1.0);
    first.add(2.0);

    bitloop_health::ScalarSummary second;
    second.add(3.0);
    second.add(std::numeric_limits<double>::infinity());
    first.merge(second);

    REQUIRE(first.totalCount() == 4);
    REQUIRE(first.finiteCount() == 3);
    REQUIRE(first.nonFiniteCount() == 1);
    REQUIRE(*first.min() == 1.0);
    REQUIRE(*first.max() == 3.0);
    REQUIRE(*first.mean() == 2.0);
    REQUIRE(*first.populationVariance() == Catch::Approx(2.0 / 3.0));
    REQUIRE(*first.sampleVariance() == Catch::Approx(1.0));
}

TEST_CASE("temporary health probe findings preserve totals while bounding examples")
{
    bitloop_health::BoundedFindings<std::string> findings(2);
    findings.record("first");
    findings.record("second");
    findings.record("third");

    REQUIRE(findings.totalCount() == 3);
    REQUIRE(findings.droppedCount() == 1);
    REQUIRE(findings.examples() == std::vector<std::string>{ "first", "second" });
}
