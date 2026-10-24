#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

// Scratch-only mechanics for temporary Bitloop health probes. This file supplies
// no correctness policy; project adapters must define state, scales, and oracles.
namespace bitloop_health
{
    using FrameIndex = std::uint64_t;

    class ExplicitFrameSchedule
    {
    public:
        ExplicitFrameSchedule() = default;

        explicit ExplicitFrameSchedule(std::vector<FrameIndex> frames) : frames(std::move(frames))
        {
            normalize();
        }

        ExplicitFrameSchedule(std::initializer_list<FrameIndex> frames) : frames(frames)
        {
            normalize();
        }

        [[nodiscard]] bool contains(FrameIndex frame) const
        {
            return std::binary_search(frames.begin(), frames.end(), frame);
        }

        [[nodiscard]] const std::vector<FrameIndex>& selectedFrames() const
        {
            return frames;
        }

    private:
        void normalize()
        {
            std::sort(frames.begin(), frames.end());
            frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        }

        std::vector<FrameIndex> frames;
    };

    class ScalarSummary
    {
    public:
        void add(double value)
        {
            ++total;
            if (!std::isfinite(value))
            {
                ++nonFinite;
                return;
            }

            ++finite;
            minimum = (std::min)(minimum, value);
            maximum = (std::max)(maximum, value);

            // Welford's recurrence avoids the cancellation of sum(x^2) - sum(x)^2.
            const double delta = value - average;
            average += delta / static_cast<double>(finite);
            secondMoment += delta * (value - average);
        }

        void merge(const ScalarSummary& other)
        {
            total += other.total;
            nonFinite += other.nonFinite;
            if (other.finite == 0)
                return;

            if (finite == 0)
            {
                finite = other.finite;
                minimum = other.minimum;
                maximum = other.maximum;
                average = other.average;
                secondMoment = other.secondMoment;
                return;
            }

            // Chan's parallel merge is the numerically stable counterpart of Welford's update.
            const auto combined = finite + other.finite;
            const double delta = other.average - average;
            secondMoment += other.secondMoment + delta * delta * static_cast<double>(finite) * static_cast<double>(other.finite) / static_cast<double>(combined);
            average += delta * static_cast<double>(other.finite) / static_cast<double>(combined);
            finite = combined;
            minimum = (std::min)(minimum, other.minimum);
            maximum = (std::max)(maximum, other.maximum);
        }

        [[nodiscard]] std::uint64_t totalCount() const { return total; }
        [[nodiscard]] std::uint64_t finiteCount() const { return finite; }
        [[nodiscard]] std::uint64_t nonFiniteCount() const { return nonFinite; }

        [[nodiscard]] std::optional<double> min() const
        {
            return finite == 0 ? std::nullopt : std::optional<double>(minimum);
        }

        [[nodiscard]] std::optional<double> max() const
        {
            return finite == 0 ? std::nullopt : std::optional<double>(maximum);
        }

        [[nodiscard]] std::optional<double> mean() const
        {
            return finite == 0 ? std::nullopt : std::optional<double>(average);
        }

        [[nodiscard]] std::optional<double> populationVariance() const
        {
            return finite == 0 ? std::nullopt : std::optional<double>((std::max)(0.0, secondMoment / static_cast<double>(finite)));
        }

        [[nodiscard]] std::optional<double> sampleVariance() const
        {
            return finite < 2 ? std::nullopt : std::optional<double>((std::max)(0.0, secondMoment / static_cast<double>(finite - 1)));
        }

    private:
        std::uint64_t total = 0;
        std::uint64_t finite = 0;
        std::uint64_t nonFinite = 0;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        double average = 0.0;
        double secondMoment = 0.0;
    };

    template <typename Finding>
    class BoundedFindings
    {
    public:
        explicit BoundedFindings(std::size_t sampleLimit) : sampleLimit(sampleLimit)
        {
            samples.reserve(sampleLimit);
        }

        template <typename Value>
        void record(Value&& finding)
        {
            ++total;
            if (samples.size() < sampleLimit)
                samples.emplace_back(std::forward<Value>(finding));
        }

        void merge(const BoundedFindings& other)
        {
            total += other.total;
            for (const auto& sample : other.samples)
            {
                if (samples.size() == sampleLimit)
                    break;
                samples.push_back(sample);
            }
        }

        [[nodiscard]] std::uint64_t totalCount() const { return total; }
        [[nodiscard]] std::uint64_t droppedCount() const { return total - static_cast<std::uint64_t>(samples.size()); }
        [[nodiscard]] const std::vector<Finding>& examples() const { return samples; }

    private:
        std::size_t sampleLimit;
        std::uint64_t total = 0;
        std::vector<Finding> samples;
    };
}
