# Types And Precision

Use this page when choosing Bitloop math types, precision, and geometry helpers.

## Scalar Choices

Bitloop uses familiar aliases such as `f32` and `f64`, and extended precision types from the `fltx` package. The umbrella include imports `fltx.h` and `bl::literals`.

Default to `f64`/`DVec2` for simulation coordinates unless performance, memory, or shader interop clearly favors `f32`.

Use extended precision only when the domain needs it:

- `fdd` for higher precision than double.
- `fqd` for deep zoom or very large/small coordinate ranges.
- `DDVec2` and `QDVec2` aliases for extended precision vectors.

Extended precision is useful for fractal deep-zoom or very large/small coordinate domains, but it is more expensive and can complicate conversions to drawing or shaders.

Before adding an external precision dependency, check whether `fdd`/`fqd`, `DDVec2`/`QDVec2`, `Complex<T>`, and the existing geometry helpers are sufficient for the requested accuracy and scale. A project-local dependency is justified only when the domain needs capabilities Bitloop does not reasonably provide, such as specialized exact arithmetic, validated numerics, or algorithms whose correctness would be risky to recreate.

## Extended Precision Storage Vs Scalar Math

Use canonical `fdd`/`fqd` as the template argument of vectors, `Complex`, matrices, ranges, and geometry types. Their scalar members use `storage_t<T>` internally (`fdd_s`/`fqd_s` for extended precision), while scalar calculations returning `T` return the canonical value type. DD/QD aliases follow this convention, for example `QDVec3 = Vec3<fqd>` and `QDMat3 = Mat3<fqd>`.

References and pointers to scalar members, including indexing and `data()`, expose the actual `storage_t<T>` objects, not `T&` or `T*`. Geometry accessors returning vectors still expose `Vec2<T>`. Use raw `_s` forms directly only when explicit storage representation is needed.

Storage-component arithmetic remains eager. To opt into expression-template fusion, load canonical values before composing the expression:

```cpp
fqd dx = p.x - cx;
fqd dy = p.y - cy;
fqd dist = bl::sqrt(dx * dx + dy * dy);
```

Use `value_t<T>` to normalize a scalar storage or expression type to its canonical value type, and `storage_t<T>` for the raw member representation. These are type mappings, not evaluations or reference casts.

## Vector Types

`Vec2<T>`, `Vec3<T>`, and `Vec4<T>` provide typed coordinates and arithmetic. Common aliases include:

- `SVec2`, `IVec2`, `FVec2`, `DVec2`, `DDVec2`, `QDVec2`
- matching `Vec3` and `Vec4` aliases

`Vec2<T>` supports indexing, conversions, arithmetic, dot products, magnitudes, angles, normalization, flooring/rounding/snapping, interpolation, and min/max helpers.

Use `value.isFinite()` to check both vector components instead of repeating component-wise finite checks in project code.

Use concrete vector aliases when the type is obvious. Use templates when helper code should support multiple precisions.

## Complex Numbers

`Complex<T>` is available for real/imaginary simulation math. It supports arithmetic, magnitude, argument, conjugation, and squaring. Use it for complex-plane domains instead of encoding complex values as unrelated `Vec2` math.

## Matrices And Transforms

Matrix aliases include `Mat2<T>`, `Mat3<T>`, and `Mat4<T>` plus concrete precision aliases. Matrices use column-major storage and `operator()(row, col)` access.

Apply a camera while drawing with `ctx->transform(camera)`. When a calculation needs the projection value, use `camera.transformFor(surface)` or `ctx->cameraTransform(camera)` instead of recreating the transform math.

## Geometry Types

Use Bitloop geometry types instead of ad hoc structs:

- `Range<T>` for scalar intervals.
- `Segment<T>` for line segments.
- `Rect<T>` for axis-aligned rectangles.
- `Quad<T>` for four-corner drawing and image placement.
- `Ray<T>` for origins and directions.
- `AngledRect<T>` for oriented rectangles.

`Rect<T>` is commonly used as `x1, y1, x2, y2` and exposes helpers such as size/width/height/center in existing code. Drawing overloads that accept `Rect<T>` convert it to the API's expected rectangle form internally.

## Color

Use `Color` for RGBA colors in drawing, traces, and UI-oriented state. Drawing APIs usually accept `Color` directly or `(r, g, b, a)` byte components.

## Practical Defaults

- Simulation positions and velocities: `DVec2`.
- Pixel sizes and counts: integer vectors such as `IVec2`.
- Shader uniforms and GPU-oriented values: `f32`/`FVec2` unless higher precision is explicitly supported.
- Deep zoom camera/world coordinates: `fqd`/`QDVec2` when preserving precision matters; their raw storage representation is `fqd_s`.
- Large particle arrays: keep per-particle structs compact; avoid extended precision unless necessary.
