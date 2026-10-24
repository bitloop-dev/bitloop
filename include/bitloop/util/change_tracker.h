#pragma once

#include <bitloop/util/hashable.h>

#include <any>
#include <concepts>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

template<class U>
concept HasHashMethod = requires(const U& u) {
    { u.hash() } -> std::same_as<bl::hash_t>;
};

template<class U>
concept HasAnyHashMethod = requires(const U& u) {
    u.hash();
};

template<class U>
concept HasEqualityOperator = requires(const U& a, const U& b) {
    { a == b } -> std::convertible_to<bool>;
};

template<class>
inline constexpr bool dependent_false_v = false;

///--------------------------///
/// Variable Changed Tracker ///
///--------------------------///

class ChangeTracker
{
    template <typename T>
    struct StateMapPair {
        static_assert(!HasAnyHashMethod<T> || HasHashMethod<T>,
            "ChangeTracker hash() must return bl::hash_t (the stable 64-bit hash type).");

        using Baseline = std::conditional_t<HasHashMethod<T>, bl::hash_t, T>;
        std::unordered_map<T*, Baseline> previous;
        std::unordered_map<T*, unsigned char> current;
    };

    struct ClearCommit {
        std::function<void()> clear;
        std::function<void()> commit;
    };

    // one entry per type in this tracker object
    mutable std::unordered_map<std::type_index, std::any> store;
    mutable std::unordered_map<std::type_index, ClearCommit> registry;

    template <typename T>
    StateMapPair<T>& getStateMap() const
    {
        const auto idx = std::type_index(typeid(T));

        auto it = store.find(idx);
        if (it == store.end()) {
            auto& holder = store[idx];
            holder.emplace<StateMapPair<T>>();

            registry[idx] = {
                // clear: wipe both sets
                [this, idx]() {
                    auto it2 = store.find(idx);
                    if (it2 == store.end())
                        return;

                    auto& maps = std::any_cast<StateMapPair<T>&>(it2->second);
                    maps.current.clear();
                    maps.previous.clear();
                },
                // commit: update baseline only when changed; add first-time snapshots
                [this, idx]() {
                    auto it2 = store.find(idx);
                    if (it2 == store.end())
                        return;

                    auto& maps = std::any_cast<StateMapPair<T>&>(it2->second);

                    // 1) existing tracked vars => write only if changed
                    for (auto& kv : maps.previous) {
                        T* p = kv.first;
                        if (!p)
                            continue;

                        if constexpr (HasHashMethod<T>) {
                            const bl::hash_t h = p->hash();
                            if (kv.second != h)
                                kv.second = h;
                        }
                        else if constexpr (HasEqualityOperator<T>) {
                            const T& live = *p;
                            if (!(kv.second == live))
                                kv.second = live; // LHS non-const map value
                        }
                        else if constexpr (std::is_trivially_copyable_v<T>) {
                            const T& live = *p;
                            if (std::memcmp(std::addressof(kv.second), std::addressof(live), sizeof(T)) != 0)
                                std::memcpy(std::addressof(kv.second), std::addressof(live), sizeof(T));
                        }
                        else {
                            static_assert(dependent_false_v<T>,
                                "ChangeTracker: T must be trivially copyable, provide operator==, or provide hash()");
                        }
                    }

                    // 2) newly staged vars => add snapshot if not present
                    for (auto& kv : maps.current) {
                        T* p = kv.first;
                        if (!p)
                            continue;

                        if (maps.previous.find(p) != maps.previous.end())
                            continue;

                        if constexpr (HasHashMethod<T>) {
                            maps.previous.emplace(p, p->hash());
                        }
                        else if constexpr (HasEqualityOperator<T>) {
                            maps.previous.emplace(p, *p);
                        }
                        else if constexpr (std::is_trivially_copyable_v<T>) {
                            typename StateMapPair<T>::Baseline base{};
                            std::memcpy(std::addressof(base), p, sizeof(T));
                            maps.previous.emplace(p, base);
                        }
                        else {
                            static_assert(dependent_false_v<T>,
                                "ChangeTracker: T must be trivially copyable, provide operator==, or provide hash()");
                        }
                    }
                }
            };

            return std::any_cast<StateMapPair<T>&>(holder);
        }

        return std::any_cast<StateMapPair<T>&>(it->second);
    }

    template <typename T>
    [[nodiscard]] bool variableChanged(T& var) const
    {
        using NonConstT = std::remove_const_t<T>;
        auto& maps = getStateMap<NonConstT>();

        auto* key = const_cast<NonConstT*>(std::addressof(var));
        auto it = maps.previous.find(key);

        if (it == maps.previous.end()) {
            // not in baseline yet => stage and report "no change" until snapshot
            maps.current[key] = 0;
            return true;
        }

        if constexpr (HasHashMethod<NonConstT>) {
            return it->second != var.hash();
        }
        else if constexpr (HasEqualityOperator<NonConstT>) {
            // keep non-const object on LHS to allow non-const member operator==
            return !(it->second == var);
        }
        else if constexpr (std::is_trivially_copyable_v<NonConstT>) {
            return std::memcmp(std::addressof(it->second), std::addressof(var), sizeof(NonConstT)) != 0;
        }
        else {
            static_assert(dependent_false_v<NonConstT>,
                "ChangeTracker: T must be trivially copyable, provide operator==, or provide hash()");
        }
    }

public:
    ChangeTracker() = default;

    template <typename... Args>
    [[nodiscard]] bool Changed(Args&&... args) const
    {
        bool any_changed = false;
        ((any_changed = variableChanged(std::forward<Args>(args)) || any_changed), ...);
        return any_changed;
    }

    template <typename T>
    void commitCurrent(T& var) const
    {
        getStateMap<std::remove_const_t<T>>().current[std::addressof(var)] = 0;
    }

    template <typename... Args>
    void commitCurrentMany(Args&... args) const
    {
        (commitCurrent(args), ...);
    }

    void clearCurrent()
    {
        for (auto& [_, cc] : registry)
            cc.clear();
    }

    void updateCurrent()
    {
        for (auto& [_, cc] : registry)
            cc.commit();
    }
};
