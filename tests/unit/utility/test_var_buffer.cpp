#include <bitloop/core/var_buffer.h>
#include <bitloop/util/change_tracker.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    struct BufferedTarget : VarBuffer<BufferedTarget>
    {
        std::vector<int> values{ 1, 2, 3 };
        std::string labels[2]{ "left", "right" };
    };

    struct WideHashValue
    {
        bl::hash_t value{};

        bl::hash_t hash() const noexcept { return value; }
    };

    struct NarrowHashValue
    {
        std::uint32_t hash() const noexcept { return 0; }
    };

    static_assert(sizeof(bl::hash_t) == 8);
    static_assert(VarHasHashMethod<WideHashValue>);
    static_assert(HasHashMethod<WideHashValue>);
    static_assert(!VarHasHashMethod<NarrowHashValue>);
    static_assert(!HasHashMethod<NarrowHashValue>);

    struct WideHashTarget : VarBuffer<WideHashTarget>
    {
        WideHashValue value{ 0x0000000100000000ull };
    };

    struct WideHashUI : DoubleBufferedAccessor<WideHashTarget>
    {
        using DoubleBufferedAccessor::DoubleBufferedAccessor;

        void trackValue()
        {
            bl_view(value);
            (void)value;
        }

        void stageValue(bl::hash_t hash)
        {
            bl_scoped(value);
            value.value = hash;
        }
    };

    struct FixedHashable : bl::Hashable
    {
        bl::hash_t value{};

        explicit FixedHashable(bl::hash_t value) : value(value) {}

    protected:
        hash_t compute_hash() const noexcept override { return value; }
    };

    static_assert(std::is_same_v<decltype(std::declval<const FixedHashable&>().hash()), bl::hash_t>);

    struct BufferedUI : DoubleBufferedAccessor<BufferedTarget>
    {
        using DoubleBufferedAccessor::DoubleBufferedAccessor;

        std::vector<int> temporaryValues()
        {
            bl_pull_temp(values);
            values.push_back(4);
            return values;
        }

        std::array<std::string, 2> temporaryLabels()
        {
            bl_pull_temp(labels);
            labels[0] = "changed locally";
            return labels;
        }

        void stagePersistentValues()
        {
            bl_pull(values);
            values.push_back(9);
        }
    };
}

TEST_CASE("bl_pull_temp creates an untracked local snapshot", "[var-buffer]")
{
    BufferedTarget target;
    BufferedUI ui(&target);

    const std::vector<int> temporary = ui.temporaryValues();

    REQUIRE(temporary == std::vector<int>{ 1, 2, 3, 4 });
    REQUIRE(target.values == std::vector<int>{ 1, 2, 3 });
    REQUIRE(target.ui_stage.empty());
}

TEST_CASE("bl_pull_temp copies non-trivial arrays without tracking", "[var-buffer]")
{
    BufferedTarget target;
    BufferedUI ui(&target);

    const std::array<std::string, 2> temporary = ui.temporaryLabels();

    REQUIRE(temporary == std::array<std::string, 2>{ "changed locally", "right" });
    REQUIRE(target.labels[0] == "left");
    REQUIRE(target.ui_stage.empty());
}

TEST_CASE("bl_pull_temp snapshots an existing persistent shadow", "[var-buffer]")
{
    BufferedTarget target;
    BufferedUI ui(&target);

    ui.stagePersistentValues();
    const std::vector<int> temporary = ui.temporaryValues();

    REQUIRE(temporary == std::vector<int>{ 1, 2, 3, 9, 4 });
    REQUIRE(target.values == std::vector<int>{ 1, 2, 3 });
    REQUIRE(target.ui_stage.size() == 1);
}

TEST_CASE("Hashable exposes its complete 64-bit hash", "[hashing]")
{
    constexpr bl::hash_t expected = 0xfedcba9876543210ull;
    FixedHashable value(expected);

    REQUIRE(value.hash() == expected);
    if constexpr (sizeof(std::size_t) >= sizeof(bl::hash_t))
        REQUIRE(value.stl_hash() == static_cast<std::size_t>(expected));
    else
        REQUIRE(value.stl_hash() == static_cast<std::size_t>(expected ^ (expected >> 32)));
}

TEST_CASE("VarBuffer distinguishes hashes that collide after 32-bit folding", "[var-buffer][hashing]")
{
    constexpr bl::hash_t original = 0x0000000100000000ull;
    constexpr bl::hash_t changed = 0x0000000000000001ull;
    static_assert(static_cast<std::uint32_t>(original ^ (original >> 32)) ==
                  static_cast<std::uint32_t>(changed ^ (changed >> 32)));

    WideHashTarget target;
    WideHashUI ui(&target);
    ui.trackValue();

    target.value.value = changed;
    REQUIRE(target.liveChanged());

    target.value.value = original;
    target.markLiveValue();
    ui.stageValue(changed);
    REQUIRE(target.shadowChanged());
}

TEST_CASE("ChangeTracker retains complete 64-bit hash baselines", "[hashing]")
{
    constexpr bl::hash_t original = 0x0000000100000000ull;
    constexpr bl::hash_t changed = 0x0000000000000001ull;

    ChangeTracker tracker;
    WideHashValue value{ original };

    REQUIRE(tracker.Changed(value));
    tracker.commitCurrent(value);
    tracker.updateCurrent();

    value.value = changed;
    REQUIRE(tracker.Changed(value));
}
