#include <bitloop/core/types.h>

#include <type_traits>
#include <utility>

using namespace bl;

namespace
{
    template<class V, class T, int N>
    constexpr bool vectorStorageContract()
    {
        using S = storage_t<T>;
        static_assert(std::is_same_v<decltype(V::x), S>);
        static_assert(std::is_same_v<decltype(std::declval<V&>()[0]), S&>);
        static_assert(std::is_same_v<decltype(std::declval<const V&>()[0]), const S&>);
        static_assert(std::is_same_v<decltype(std::declval<V&>().data()), S*>);
        static_assert(std::is_same_v<decltype(std::declval<const V&>().dot(std::declval<const V&>())), T>);
        static_assert(std::is_standard_layout_v<V> && std::is_trivially_copyable_v<V>);
        static_assert(sizeof(V) == N * sizeof(S));

        V v{};
        v[0] = T{2};
        if (&v[0] != &v.x || v.data() != &v.x) return false;
        const V& cv = v;
        return &cv[0] == &v.x && cv[0] == T{2};
    }

    template<class T, int N>
    constexpr bool matrixStorageContract()
    {
        using M = Matrix<T, N, N>;
        using S = storage_t<T>;
        static_assert(std::is_same_v<typename M::value_type, T>);
        static_assert(std::is_same_v<decltype(M::m), std::array<S, N * N>>);
        static_assert(std::is_same_v<decltype(std::declval<M&>()(0, 0)), S&>);
        static_assert(std::is_same_v<decltype(std::declval<const M&>()(0, 0)), const S&>);
        static_assert(std::is_same_v<decltype(std::declval<M&>()[0]), S&>);
        static_assert(std::is_same_v<decltype(std::declval<const M&>()[0]), const S&>);
        static_assert(std::is_same_v<decltype(std::declval<M&>().data()), S*>);
        static_assert(std::is_same_v<decltype(std::declval<const M&>().data()), const S*>);
        static_assert(std::is_standard_layout_v<M> && std::is_trivially_copyable_v<M>);
        static_assert(sizeof(M) == N * N * sizeof(S));

        M m = M::identity(T{2});
        const M& cm = m;
        if (&m(1, 0) != &m[1] || &cm(1, 0) != &cm[1]) return false;
        if (m.data() != &m[0] || cm.data() != &cm[0]) return false;
        if (!(m * M::identity()).isIdentity(T{2})) return false;
        if (!m.transposed().isIdentity(T{2})) return false;

        if constexpr (N == 2 || N == 3)
        {
            static_assert(std::is_same_v<decltype(m.determinant()), T>);
            if (m.determinant() != T{N == 2 ? 4 : 8}) return false;
            if (!(m * m.inversed()).isIdentity()) return false;
            const Vec2<T> p{T{2}, T{4}};
            const auto q = m * p;
            if (q.x != T{N == 2 ? 4 : 2} || q.y != T{N == 2 ? 8 : 4}) return false;
        }
        M assigned;
        assigned = M::identity();
        assigned *= m;
        return assigned.isIdentity(T{2});
    }

    template<class T>
    constexpr bool scalarStorageContract()
    {
        using S = storage_t<T>;
        static_assert(std::is_same_v<decltype(Range<T>::lo), S> && std::is_same_v<decltype(Range<T>::hi), S>);
        static_assert(std::is_aggregate_v<Range<T>>);
        static_assert(std::is_same_v<decltype(Rect<T>::x1), S> && std::is_same_v<decltype(Rect<T>::y2), S>);
        static_assert(std::is_same_v<decltype(AngledRect<T>::cx), S> && std::is_same_v<decltype(AngledRect<T>::w), S>);
        static_assert(std::is_same_v<decltype(AngledRect<T>::angle), S>);
        static_assert(std::is_same_v<decltype(Segment<T>::a), Vec2<T>>);
        static_assert(std::is_same_v<decltype(Quad<T>::a), Vec2<T>>);
        static_assert(std::is_same_v<decltype(Ray<T>::x), S>);
        static_assert(std::is_same_v<decltype(std::declval<Segment<T>>().lengthSq()), T>);
        static_assert(std::is_same_v<decltype(std::declval<Quad<T>>().minX()), T>);
        static_assert(sizeof(Range<T>) == 2 * sizeof(S));
        static_assert(sizeof(Rect<T>) == 4 * sizeof(S));
        static_assert(sizeof(AngledRect<T>) == 5 * sizeof(S));
        static_assert(sizeof(Segment<T>) == 4 * sizeof(S));
        static_assert(sizeof(Quad<T>) == 8 * sizeof(S));

        const Range<T> range{T{1}, T{4}};
        if (range.lo != T{1} || range.hi != T{4}) return false;
        const Vec3<T> a{T{1}, T{2}, T{3}};
        const Vec4<T> b{T{1}, T{2}, T{3}, T{4}};
        if ((a + a).z != T{6} || (b + b).w != T{8}) return false;
        if (a.dot(a) != T{14} || b.dot(b) != T{30}) return false;
        const Complex<T> z{T{2}, T{1}};
        const auto zz = z * z;
        if (zz.x != T{3} || zz.y != T{4} || z.mag2() != T{5}) return false;
        const Segment<T> segment{T{0}, T{0}, T{3}, T{4}};
        if (segment.lengthSq() != T{25}) return false;

        return vectorStorageContract<Vec3<T>, T, 3>() && vectorStorageContract<Vec4<T>, T, 4>()
            && vectorStorageContract<Complex<T>, T, 2>()
            && matrixStorageContract<T, 2>() && matrixStorageContract<T, 3>() && matrixStorageContract<T, 4>();
    }

    static_assert(scalarStorageContract<f32>());
    static_assert(scalarStorageContract<f64>());
    static_assert(scalarStorageContract<fdd>());
    static_assert(scalarStorageContract<fqd>());

    static_assert(std::is_same_v<DDVec3, Vec3<fdd>> && std::is_same_v<QDVec3, Vec3<fqd>>);
    static_assert(std::is_same_v<DDVec4, Vec4<fdd>> && std::is_same_v<QDVec4, Vec4<fqd>>);
    static_assert(std::is_same_v<DDMat2, Mat2<fdd>> && std::is_same_v<QDMat2, Mat2<fqd>>);
    static_assert(std::is_same_v<DDMat3, Mat3<fdd>> && std::is_same_v<QDMat3, Mat3<fqd>>);
    static_assert(std::is_same_v<DDMat4, Mat4<fdd>> && std::is_same_v<QDMat4, Mat4<fqd>>);
    static_assert(std::is_same_v<DDRange, Range<fdd>> && std::is_same_v<QDRange, Range<fqd>>);
    static_assert(std::is_same_v<DDSegment, Segment<fdd>> && std::is_same_v<QDSegment, Segment<fqd>>);
    static_assert(std::is_same_v<DDRect, Rect<fdd>> && std::is_same_v<QDRect, Rect<fqd>>);
    static_assert(std::is_same_v<DDQuad, Quad<fdd>> && std::is_same_v<QDQuad, Quad<fqd>>);
    static_assert(std::is_same_v<DDRay, Ray<fdd>> && std::is_same_v<QDRay, Ray<fqd>>);
    static_assert(std::is_same_v<DDAngledRect, AngledRect<fdd>> && std::is_same_v<QDAngledRect, AngledRect<fqd>>);
    static_assert(std::is_same_v<upgrade_vec3_float_t<f64>, DDVec3> && std::is_same_v<upgrade_vec4_float_t<fdd>, QDVec4>);
    static_assert(std::is_same_v<downgrade_vec3_float_t<fqd>, DDVec3> && std::is_same_v<downgrade_vec4_float_t<fdd>, DVec4>);
    static_assert(float_index_v<fdd> == float_index_v<fdd_s> && float_index_v<fqd> == float_index_v<fqd_s>);
    static_assert(std::is_same_v<mat_mul_t<fqd, fqd>, fqd> && std::is_same_v<scalar_mul_t<fdd, double>, fdd>);
    static_assert(std::is_same_v<scalar_div_t<fqd, double>, fqd>);

    template<class T>
    void geometryStorageCompile()
    {
        const std::vector<Range<T>> ranges{{T{1}, T{4}}, {T{2}, T{8}}};
        (void)Range<T>::mergeShrink(ranges);
        (void)Range<T>::mergeExpand(ranges);
        Rect<T> r{T{1}, T{2}, T{3}, T{4}};
        (void)r.center();
        (void)r.scaled(T{2});
        Quad<T> q{r};
        (void)q.boundingRect();
        (void)q.scaled(T{2});
        AngledRect<T> a{T{1}, T{2}, T{3}, T{4}, T{0}};
        if constexpr (!std::is_same_v<T, storage_t<T>>) a.fitTo(a, a, T{1});
        (void)a.toQuad();
        Matrix<T, 3, 3> m{T{1}, T{0}, T{0}, T{0}, T{1}, T{0}, T{0}, T{0}, T{1}};
        Matrix<double, 3, 3> native{m};
        m = native;
    }

    template void geometryStorageCompile<f32>();
    template void geometryStorageCompile<f64>();
    template void geometryStorageCompile<fdd>();
    template void geometryStorageCompile<fqd>();
}
