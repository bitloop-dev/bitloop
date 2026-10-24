#pragma once

#include "debug.h"
#include <bitloop/util/hashable.h>

#include <any>
#include <array>
#include <concepts>
#include <cstring>
#include <functional>
#include <mutex>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <unordered_map>

#define VARBUFFER_DEBUG_INFO

template<class U>
concept VarHasHashMethod = requires(const U & u) {
    { u.hash() } -> std::same_as<bl::hash_t>;
};

template<class U>
concept VarHasAnyHashMethod = requires(const U & u) {
    u.hash();
};

template<class T>
concept HasEq = requires(const T & x, const T & y) {
    { x == y } -> std::convertible_to<bool>;
};

template<class T>
concept TriviallyComparable = std::is_trivially_copyable_v<T>;

template<class T>
concept Ostreamable = requires(std::ostream & os, const T & t) {
    { os << t } -> std::same_as<std::ostream&>;
};

// -------------------- type ops --------------------

template<class T, class Enable = void>
struct TypeOps
{
    using Store = std::remove_const_t<T>;

    static void assign(void* dst, const std::any& src)
    {
        *static_cast<Store*>(dst) = std::any_cast<const Store&>(src);
    }

    static bool equals(const void* a, const std::any& b)
    {
        const Store& A = *static_cast<const Store*>(a);
        const Store& B = std::any_cast<const Store&>(b);

        if constexpr (VarHasHashMethod<Store>)
            return A.hash() == B.hash();
        else if constexpr (HasEq<Store>)
            return A == B;
        else if constexpr (TriviallyComparable<Store>)
            return std::memcmp(&A, &B, sizeof(Store)) == 0;
        else
            return false;
    }

    static void store(std::any& dst, const void* src)
    {
        const Store& s = *static_cast<const Store*>(src);

        if (auto* p = std::any_cast<Store>(&dst))
            *p = s;
        else
            dst.template emplace<Store>(s);
    }

    static void copy_any(std::any& dst, const std::any& src)
    {
        const Store& s = std::any_cast<const Store&>(src);

        if (auto* p = std::any_cast<Store>(&dst))
            *p = s;
        else
            dst.template emplace<Store>(s);
    }

    static bool equals_any(const std::any& a, const std::any& b)
    {
        const Store& A = std::any_cast<const Store&>(a);
        const Store& B = std::any_cast<const Store&>(b);

        if constexpr (VarHasHashMethod<Store>)
            return A.hash() == B.hash();
        else if constexpr (HasEq<Store>)
            return A == B;
        else if constexpr (TriviallyComparable<Store>)
            return std::memcmp(&A, &B, sizeof(Store)) == 0;
        else
            return false;
    }

    static void print(std::ostream& os, const std::any& a)
    {
        if constexpr (Ostreamable<Store>)
            os << std::any_cast<const Store&>(a);
        else
            os << "<unprintable>";
    }
};

template<class T, std::size_t N>
struct TypeOps<T[N], void>
{
    using Elem = std::remove_const_t<T>;
    using Store = std::array<Elem, N>;

    static void assign(void* dst, const std::any& src)
    {
        const Store& s = std::any_cast<const Store&>(src);

        if constexpr (std::is_trivially_copyable_v<Elem>)
        {
            std::memcpy(dst, s.data(), sizeof(Elem) * N);
        }
        else
        {
            auto* out = static_cast<Elem*>(dst);
            std::copy(s.begin(), s.end(), out);
        }
    }

    static bool equals(const void* a, const std::any& b)
    {
        const Store& B = std::any_cast<const Store&>(b);

        if constexpr (VarHasHashMethod<Store>)
        {
            Store A{};
            if constexpr (std::is_trivially_copyable_v<Elem>)
                std::memcpy(A.data(), a, sizeof(Elem) * N);
            else
                std::copy(static_cast<const Elem*>(a), static_cast<const Elem*>(a) + N, A.begin());
            return A.hash() == B.hash();
        }
        else if constexpr (TriviallyComparable<Elem>)
        {
            return std::memcmp(a, B.data(), sizeof(Elem) * N) == 0;
        }
        else if constexpr (HasEq<Elem>)
        {
            const Elem* p = static_cast<const Elem*>(a);
            for (std::size_t i = 0; i < N; ++i)
            {
                if (!(p[i] == B[i]))
                    return false;
            }
            return true;
        }
        else
        {
            return false;
        }
    }

    static void store(std::any& dst, const void* src)
    {
        const Elem* p = static_cast<const Elem*>(src);

        if (auto* arr = std::any_cast<Store>(&dst))
        {
            if constexpr (std::is_trivially_copyable_v<Elem>)
                std::memcpy(arr->data(), src, sizeof(Elem) * N);
            else
                std::copy(p, p + N, arr->begin());
        }
        else
        {
            Store s;
            if constexpr (std::is_trivially_copyable_v<Elem>)
                std::memcpy(s.data(), src, sizeof(Elem) * N);
            else
                std::copy(p, p + N, s.begin());
            dst.template emplace<Store>(std::move(s));
        }
    }

    static void copy_any(std::any& dst, const std::any& src)
    {
        const Store& s = std::any_cast<const Store&>(src);

        if (auto* p = std::any_cast<Store>(&dst))
            *p = s;
        else
            dst.template emplace<Store>(s);
    }

    static bool equals_any(const std::any& a, const std::any& b)
    {
        const Store& A = std::any_cast<const Store&>(a);
        const Store& B = std::any_cast<const Store&>(b);

        if constexpr (VarHasHashMethod<Store>)
            return A.hash() == B.hash();
        else if constexpr (TriviallyComparable<Store>)
            return std::memcmp(&A, &B, sizeof(Store)) == 0;
        else if constexpr (HasEq<Elem>)
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                if (!(A[i] == B[i]))
                    return false;
            }
            return true;
        }
        else
            return false;
    }

    static void print(std::ostream& os, const std::any& a)
    {
        const Store& s = std::any_cast<const Store&>(a);
        os << "[";
        for (std::size_t i = 0; i < N; ++i) {
            if (i) os << ", ";
            if constexpr (Ostreamable<Elem>)
                os << s[i];
            else
                os << "<unprintable>";
        }
        os << "]";
    }
};

// -------------------- view model --------------------

template<class TargetType, class T>
struct __PushGuard
{
    const TargetType& target;
    const T& target_ref;

    __PushGuard(const TargetType& t, const T& v) : target(t), target_ref(v) {}
    ~__PushGuard() { target._commit(target_ref); }
};

// pull/commit/schedule interface
template<typename TargetType>
class DoubleBufferedAccessor
{
public:
    template<class T, std::enable_if_t<!std::is_array_v<T>, int> = 0>
    std::remove_const_t<T>& _pull(const T& live_member, const char* name = nullptr) const
    {
        return __target._pull(live_member, name);
    }

    template<class T, std::size_t N>
    std::array<std::remove_const_t<T>, N>& _pull(const T(&live_member)[N], const char* name = nullptr) const
    {
        return __target._pull(live_member, name);
    }

    template<class T, std::enable_if_t<!std::is_array_v<T>, int> = 0>
    std::remove_const_t<T> _temp_pull(const T& live_member, const char* name = nullptr) const
    {
        return __target._temp_pull(live_member, name);
    }

    template<class T, std::size_t N>
    auto _temp_pull(const T(&live_member)[N], const char* name = nullptr) const
    {
        return __target._temp_pull(live_member, name);
    }

    template<class T, std::enable_if_t<!std::is_array_v<T>, int> = 0>
    void _commit(const T& live_member) const
    {
        __target._commit(live_member);
    }

    template<class T, std::size_t N>
    void _commit(const T(&live_member)[N]) const
    {
        __target._commit(live_member);
    }

    template<class T>
    void _commit(const T& live_member, const T& staged) const
    {
        __target._commit(live_member, staged);
    }

    template<class T, std::size_t N>
    void _commit(const T(&live_member)[N], const std::array<T, N>& staged) const
    {
        __target._commit(live_member, staged);
    }

    template<class F>
    void _schedule(F&& f) const
    {
        __target._schedule(std::forward<F>(f));
    }

    const TargetType& __target;

    DoubleBufferedAccessor(const TargetType* t) : __target(*t) {}
    virtual ~DoubleBufferedAccessor() = default;

    // push/pull/scope helpers (single)
    #define _bl_scoped_one(name)     auto& name = _pull(__target.name, #name); __PushGuard<std::remove_reference_t<decltype(__target)>, std::remove_reference_t<decltype(__target.name)>> __push_guard_##name(__target, __target.name)
    #define _bl_pull_one(name)       auto& name = _pull(__target.name, #name)
    #define _bl_view_one(name)       const auto& name = _pull(__target.name, #name)
    #define _bl_pull_temp_one(name)  auto name = _temp_pull(__target.name, #name)
    #define _bl_push_one(name)       _commit(__target.name)

    // push/pull/scope helpers (__VA_ARGS__)
    #define bl_scoped(...)     BL_FOREACH(_bl_scoped_one, __VA_ARGS__)
    #define bl_pull(...)       BL_FOREACH(_bl_pull_one, __VA_ARGS__)
    #define bl_view(...)       BL_FOREACH(_bl_view_one, __VA_ARGS__)
    #define bl_pull_temp(...)  BL_FOREACH(_bl_pull_temp_one, __VA_ARGS__)
    #define bl_push(...)       BL_FOREACH(_bl_push_one, __VA_ARGS__)

    #define bl_schedule _schedule
};

// -------------------- var buffer --------------------

template<class TargetType>
struct VarBuffer
{
    VarBuffer() = default;
    VarBuffer(const VarBuffer&) = delete;
    VarBuffer& operator=(const VarBuffer&) = delete;
    VarBuffer(VarBuffer&&) = delete;
    VarBuffer& operator=(VarBuffer&&) = delete;

    struct Entry
    {
        #ifdef VARBUFFER_DEBUG_INFO
        int id = -1;
        std::string name;
        #endif

        std::any value_shadow;

        // snapshots for change detection (full 64-bit hash if value has .hash())
        std::any mark_live;
        std::any mark_shadow;

        bool hashable = false;
        bl::hash_t(*hash_from_live_fn)(const void*) = nullptr;
        bl::hash_t(*hash_from_any_fn)(const std::any&) = nullptr;

        bool changed = false; // represents "shadow -> live needs applying", "marked" value must not advance while true
        void (*assign_fn)(void*, const std::any&) = nullptr;                 // any -> live
        bool (*equals_fn)(const void*, const std::any&) = nullptr;           // live vs any
        void (*store_from_live_fn)(std::any&, const void*) = nullptr;        // live -> any
        bool (*equals_any_fn)(const std::any&, const std::any&) = nullptr;   // any vs any
        void (*copy_any_fn)(std::any&, const std::any&) = nullptr;
        void (*print_fn)(std::ostream&, const std::any&) = nullptr;

        const void* live_key = nullptr;
        const VarBuffer* owner = nullptr;

        void updateLive()
        {
            if (!owner || !assign_fn || !value_shadow.has_value() || !live_key) return;
            if (!changed) return;

            assign_fn(const_cast<void*>(live_key), value_shadow);

            // clear only after apply, then refresh both baselines to the applied state
            changed = false;
            markLiveValue();
            markShadowValue();
        }

        void updateShadow()
        {
            if (!owner || !live_key) return;

            if (!value_shadow.has_value()) {
                if (store_from_live_fn) store_from_live_fn(value_shadow, live_key);
                // first-time shadow creation establishes baseline
                if (!mark_shadow.has_value()) markShadowValue();
                if (!mark_live.has_value())   markLiveValue();
                return;
            }

            if (changed) return; // do not overwrite UI edits while a shadow -> live apply is pending
            if (!liveChanged()) return;

            if (store_from_live_fn)
                store_from_live_fn(value_shadow, live_key);

            // worker-driven live -> shadow sync should advance the shadow baseline immediately
            markShadowValue();
        }

        void markLiveValue()
        {
            if (!owner || !live_key) return;

            if (hashable && hash_from_live_fn) {
                const bl::hash_t h = hash_from_live_fn(live_key);
                if (auto* p = std::any_cast<bl::hash_t>(&mark_live))
                    *p = h;
                else
                    mark_live.template emplace<bl::hash_t>(h);
                return;
            }

            if (store_from_live_fn)
                store_from_live_fn(mark_live, live_key);
        }

        void markShadowValue()
        {
            if (changed) return; // "marked" value must not move forward while an apply is pending

            if (!value_shadow.has_value()) {
                mark_shadow.reset();
                return;
            }

            if (hashable && hash_from_any_fn) {
                const bl::hash_t h = hash_from_any_fn(value_shadow);
                if (auto* p = std::any_cast<bl::hash_t>(&mark_shadow))
                    *p = h;
                else
                    mark_shadow.template emplace<bl::hash_t>(h);
                return;
            }

            if (copy_any_fn)
                copy_any_fn(mark_shadow, value_shadow);
        }

        bool liveChanged() const
        {
            if (!live_key) return false;
            if (!mark_live.has_value()) return false;

            if (hashable && hash_from_live_fn) {
                const bl::hash_t now = hash_from_live_fn(live_key);
                const bl::hash_t was = std::any_cast<const bl::hash_t&>(mark_live);
                return now != was;
            }

            if (!equals_fn) return false;
            return !equals_fn(live_key, mark_live);
        }

        bool shadowChanged() const
        {
            if (!value_shadow.has_value()) return false;
            if (!mark_shadow.has_value())  return false;

            if (hashable && hash_from_any_fn) {
                const bl::hash_t now = hash_from_any_fn(value_shadow);
                const bl::hash_t was = std::any_cast<const bl::hash_t&>(mark_shadow);
                return now != was;
            }

            if (!equals_any_fn) return false;
            return !equals_any_fn(value_shadow, mark_shadow);
        }

        std::string to_string_value() const
        {
            if (!value_shadow.has_value()) return {};
            std::ostringstream oss;
            if (hashable && hash_from_any_fn)
                oss << hash_from_any_fn(value_shadow);
            else if (print_fn)
                print_fn(oss, value_shadow);
            return oss.str();
        }

        std::string to_string_marked_shadow() const
        {
            if (!mark_shadow.has_value()) return {};
            std::ostringstream oss;
            if (hashable)
                oss << std::any_cast<const bl::hash_t&>(mark_shadow);
            else if (print_fn)
                print_fn(oss, mark_shadow);
            return oss.str();
        }

        std::string to_string_marked_live() const
        {
            if (!mark_live.has_value()) return {};
            std::ostringstream oss;
            if (hashable)
                oss << std::any_cast<const bl::hash_t&>(mark_live);
            else if (print_fn)
                print_fn(oss, mark_live);
            return oss.str();
        }
    };

    using Task = std::function<void(TargetType&)>;
    mutable std::mutex m_tasks;
    mutable std::vector<Task> post_commit_tasks;

    template<class F>
    void _schedule(F&& f) const
    {
        static_assert(std::is_invocable_v<F&, TargetType&>, "_schedule expects a callable(TargetType&)");
        std::lock_guard<std::mutex> g(m_tasks);
        post_commit_tasks.emplace_back(std::forward<F>(f));
    }

    void invokeScheduledCalls()
    {
        BL_TAKE_OWNERSHIP("live");
        BL_TAKE_OWNERSHIP("ui");

        std::vector<Task> tasks;
        {
            std::lock_guard<std::mutex> g(m_tasks);
            tasks.swap(post_commit_tasks);
        }

        TargetType& self = static_cast<TargetType&>(*this);
        for (auto& t : tasks) t(self);
    }

    // ---- bind helpers ----
    template<class T>
    static void bind_ops(Entry& e)
    {
        using Ops = TypeOps<T>;
        using Store = typename Ops::Store;

        static_assert(!VarHasAnyHashMethod<Store> || VarHasHashMethod<Store>,
            "VarBuffer hash() must return bl::hash_t (the stable 64-bit hash type).");

        if (!e.assign_fn)          e.assign_fn = +[](void* dst, const std::any& a) { Ops::assign(dst, a); };
        if (!e.equals_fn)          e.equals_fn = +[](const void* dst, const std::any& a) { return Ops::equals(dst, a); };
        if (!e.store_from_live_fn) e.store_from_live_fn = +[](std::any& dst, const void* src) { Ops::store(dst, src); };
        if (!e.equals_any_fn)      e.equals_any_fn = +[](const std::any& a, const std::any& b) { return Ops::equals_any(a, b); };
        if (!e.copy_any_fn)        e.copy_any_fn = +[](std::any& dst, const std::any& src) { Ops::copy_any(dst, src); };
        if (!e.print_fn)           e.print_fn = +[](std::ostream& os, const std::any& a) { Ops::print(os, a); };

        if constexpr (VarHasHashMethod<Store>) {
            e.hashable = true;
            e.hash_from_live_fn = +[](const void* src) -> bl::hash_t {
                const Store& s = *static_cast<const Store*>(src);
                return s.hash();
            };
            e.hash_from_any_fn = +[](const std::any& a) -> bl::hash_t {
                return std::any_cast<const Store&>(a).hash();
            };
        }
    }

    // ---- storage ----
    mutable std::unordered_map<const void*, Entry> ui_stage;

    // scalar pull
    template<class T, std::enable_if_t<!std::is_array_v<T>, int> = 0>
    std::remove_const_t<T>& _pull(const T& live_member, const char* name = nullptr) const
    {
        using Store = std::remove_const_t<T>;
        const void* live_key = static_cast<const void*>(&live_member);

        Entry& e = ui_stage[live_key];
        if (!e.owner) {
            e.owner = this;
            e.live_key = live_key;
            #ifdef VARBUFFER_DEBUG_INFO
            e.id = (int)ui_stage.size() - 1;
            e.name = name ? name : "";
            #else
            (void)name;
            #endif
        }

        if (!e.assign_fn) bind_ops<T>(e);

        if (!e.value_shadow.has_value()) {
            e.value_shadow.template emplace<Store>(static_cast<const Store&>(live_member));
            // establish initial baselines so first-frame UI edits compare correctly
            e.markShadowValue();
            e.markLiveValue();
        }

        return *std::any_cast<Store>(&e.value_shadow);
    }

    // array pull (T[N])
    template<class T, std::size_t N>
    std::array<std::remove_const_t<T>, N>& _pull(const T(&live_member)[N], const char* name = nullptr) const
    {
        using Ops = TypeOps<T[N]>;
        using Store = typename Ops::Store;

        const void* live_key = static_cast<const void*>(live_member);

        Entry& e = ui_stage[live_key];
        if (!e.owner) {
            e.owner = this;
            e.live_key = live_key;
            #ifdef VARBUFFER_DEBUG_INFO
            e.id = (int)ui_stage.size() - 1;
            e.name = name ? name : "";
            #else
            (void)name;
            #endif
        }

        if (!e.assign_fn) bind_ops<T[N]>(e);

        if (!e.value_shadow.has_value()) {
            Store s{};
            std::memcpy(s.data(), live_member, sizeof(std::remove_const_t<T>) * N);
            e.value_shadow.template emplace<Store>(std::move(s));
            // establish initial baselines so first-frame UI edits compare correctly
            e.markShadowValue();
            e.markLiveValue();
        }

        return *std::any_cast<Store>(&e.value_shadow);
    }

    // scalar commit (from shadow ref)
    template<class T, std::enable_if_t<!std::is_array_v<T>, int> = 0>
    void _commit(const T& live_member) const
    {
        const void* live_key = static_cast<const void*>(&live_member);
        auto it = ui_stage.find(live_key);
        if (it == ui_stage.end()) return;

        Entry& e = it->second;
        if (!e.equals_any_fn) bind_ops<T>(e);

        e.changed = e.shadowChanged();
    }

    // array commit (from shadow ref)
    template<class T, std::size_t N>
    void _commit(const T(&live_member)[N]) const
    {
        const void* live_key = static_cast<const void*>(live_member);
        auto it = ui_stage.find(live_key);
        if (it == ui_stage.end()) return;

        Entry& e = it->second;
        if (!e.equals_any_fn) bind_ops<T[N]>(e);

        e.changed = e.shadowChanged();
    }

    // scalar staged commit (copy staged into shadow then commit)
    template<class T, std::enable_if_t<!std::is_array_v<T>, int> = 0>
    void _commit(const T& live_member, const T& staged) const
    {
        using Store = std::remove_const_t<T>;
        const void* live_key = static_cast<const void*>(&live_member);

        Entry& e = ui_stage[live_key];
        if (!e.owner) {
            e.owner = this;
            e.live_key = live_key;
        }
        if (!e.assign_fn) bind_ops<T>(e);

        if (auto* p = std::any_cast<Store>(&e.value_shadow))
            *p = static_cast<const Store&>(staged);
        else
            e.value_shadow.template emplace<Store>(static_cast<const Store&>(staged));

        if (!e.mark_shadow.has_value()) e.markShadowValue();
        if (!e.mark_live.has_value())   e.markLiveValue();

        e.changed = e.shadowChanged();
    }

    // array staged commit
    template<class T, std::size_t N>
    void _commit(const T(&live_member)[N], const std::array<T, N>& staged) const
    {
        using Ops = TypeOps<T[N]>;
        using Store = typename Ops::Store;

        const void* live_key = static_cast<const void*>(live_member);

        Entry& e = ui_stage[live_key];
        if (!e.owner) {
            e.owner = this;
            e.live_key = live_key;
        }
        if (!e.assign_fn) bind_ops<T[N]>(e);

        if (auto* p = std::any_cast<Store>(&e.value_shadow))
            *p = staged;
        else
            e.value_shadow.template emplace<Store>(staged);

        if (!e.mark_shadow.has_value()) e.markShadowValue();
        if (!e.mark_live.has_value())   e.markLiveValue();

        e.changed = e.shadowChanged();
    }

    // Return an untracked UI-local snapshot. If the member is already tracked,
    // copy its current shadow so temporary reads remain coherent with the UI.
    // Otherwise copy the live member without registering a persistent entry.
    template<class T, std::enable_if_t<!std::is_array_v<T>, int> = 0>
    std::remove_const_t<T> _temp_pull(const T& live_member, const char* name = nullptr) const
    {
        using Store = std::remove_const_t<T>;
        (void)name;

        const void* live_key = static_cast<const void*>(&live_member);
        auto it = ui_stage.find(live_key);
        if (it != ui_stage.end() && it->second.value_shadow.has_value())
            return std::any_cast<const Store&>(it->second.value_shadow);

        return static_cast<const Store&>(live_member);
    }

    template<class T, std::size_t N>
    std::array<std::remove_const_t<T>, N> _temp_pull(
        const T(&live_member)[N],
        const char* name = nullptr) const
    {
        using Elem = std::remove_const_t<T>;
        using Store = std::array<Elem, N>;
        (void)name;

        const void* live_key = static_cast<const void*>(live_member);
        auto it = ui_stage.find(live_key);
        if (it != ui_stage.end() && it->second.value_shadow.has_value())
            return std::any_cast<const Store&>(it->second.value_shadow);

        Store snapshot{};
        if constexpr (std::is_trivially_copyable_v<Elem>)
            std::memcpy(snapshot.data(), live_member, sizeof(Elem) * N);
        else
            std::copy(live_member, live_member + N, snapshot.begin());
        return snapshot;
    }

    // -------- apply / mark / query --------
    void updateLive() { for (auto& kv : ui_stage) kv.second.updateLive(); }
    void updateShadow() { for (auto& kv : ui_stage) kv.second.updateShadow(); }
    void markLiveValue() { for (auto& kv : ui_stage) kv.second.markLiveValue(); }
    void markShadowValue() { for (auto& kv : ui_stage) kv.second.markShadowValue(); }

    bool liveChanged() const
    {
        for (auto const& kv : ui_stage)
            if (kv.second.liveChanged()) return true;
        return false;
    }

    bool shadowChanged() const
    {
        for (auto const& kv : ui_stage)
            if (kv.second.shadowChanged()) return true;
        return false;
    }

    // only updates shadow vars if the UI hasn't already altered that variable
    void updateUnchangedShadowVars()
    {
        for (auto& kv : ui_stage) {
            if (!kv.second.shadowChanged())
                kv.second.updateShadow();
        }
    }
};
