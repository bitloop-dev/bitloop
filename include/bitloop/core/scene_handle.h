#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <bitloop/platform/platform_macros.h>
#include <bitloop/util/json.h>

BL_BEGIN_NS

class ProjectBase;
class SceneBase;

struct SceneHandleState
{
    ProjectBase* project = nullptr;
    std::uint64_t generation = 0;
};

class SceneHandleBase
{
    std::weak_ptr<SceneHandleState> state;
    std::uint64_t token = 0;
    std::uint64_t generation = 0;

    friend class ProjectBase;
    friend class Layout;
    friend class Viewport;

protected:

    SceneHandleBase(std::shared_ptr<SceneHandleState> state, std::uint64_t token, std::uint64_t generation)
        : state(std::move(state)), token(token), generation(generation)
    {
    }

    void setLaunchConfigParserImpl(std::function<void(const JSON::json&)> parser) const;

public:

    SceneHandleBase() = default;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] SceneBase* get() const noexcept;
    [[nodiscard]] std::string id() const;

    [[nodiscard]] explicit operator bool() const noexcept;

    friend bool operator==(const SceneHandleBase&, const SceneHandleBase&) = default;
};

template<typename SceneType>
class SceneHandle : public SceneHandleBase
{
    using Config = typename SceneType::Config;

    std::weak_ptr<Config> config;

    friend class ProjectBase;

    SceneHandle(SceneHandleBase handle, const std::shared_ptr<Config>& config)
        : SceneHandleBase(std::move(handle)), config(config)
    {
    }

public:

    SceneHandle() = default;

    [[nodiscard]] SceneType* get() const noexcept
    {
        return static_cast<SceneType*>(SceneHandleBase::get());
    }

    [[nodiscard]] SceneType& operator*() const
    {
        SceneType* scene = get();
        if (!scene)
            throw std::logic_error("SceneHandle cannot be dereferenced before construction or after destruction");
        return *scene;
    }

    [[nodiscard]] SceneType* operator->() const
    {
        return &operator*();
    }

    template<typename Parser>
    void setLaunchConfigParser(Parser parser) const
    {
        static_assert(
            std::is_invocable_v<Parser&, Config&, const JSON::json&>,
            "A Scene launch-config parser must accept Config& and const JSON::json&");

        const std::weak_ptr<Config> config_state = config;
        setLaunchConfigParserImpl(
            [config_state, parser = std::move(parser)](const JSON::json& values) mutable
            {
                const std::shared_ptr<Config> locked = config_state.lock();
                if (!locked)
                    throw std::logic_error("Scene launch configuration is no longer available");
                std::invoke(parser, *locked, values);
            });
    }
};

BL_END_NS
