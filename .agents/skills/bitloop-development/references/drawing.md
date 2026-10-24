# Viewport Drawing

## Contents

- Mental model and coordinate modes
- Styles, paths, shapes, and gradients
- Text, images, and shader surfaces
- Practical rendering techniques

Use this page when drawing with `viewportDraw(Viewport* ctx)`. The authoritative API is `include/bitloop/nanovgx/nano_canvas.h`; search that header when an uncommon overload matters.

This page is about drawing technique and API usage. For visual judgment, screenshot review, readability, glow/trail/overlay scale, or "make it look better" work, also read `visual-qa.md`.

## Mental Model

`Viewport` inherits Bitloop's `Painter` and surface information. The drawing API is similar to NanoVG and HTML canvas. Its default world transform is the identity, so a camera is entirely optional.

Do not mutate simulation state in `viewportDraw`. Update state in `sceneProcess`, `viewportProcess`, or event handlers.

Apply a camera explicitly when a Scene wants one:

```cpp
void MyScene::viewportDraw(Viewport* ctx) const
{
    ctx->transform(camera);
    // Draw world content.
}
```

The Painter derives projection from its own surface at that call, so one Camera can drive differently sized mirrored or offscreen Viewports without retaining either Viewport. Use `setTransform(camera)` when replacing an existing world transform, such as after drawing a stage-space background. Additional `transform(...)` calls compose normally.

## Coordinate Modes

- `worldMode()`: positions, paths, text, and sizes are world-space and scale/rotate with the camera.
- `stageMode()`: positions and sizes are screen/stage-space. Use for HUDs and debug readouts.
- `worldHudMode()`: positions are world-space, but line widths, sizes, and text stay screen-sized. Use for labels or handles attached to world objects.
- `saveCameraTransform()` / `restoreCameraTransform()`: preserve Bitloop camera-mode flags around helper drawing.
- `save()` / `restore()`: NanoVG state stack only; do not use as a substitute for camera transform helpers.

Useful surface helpers include `left()`, `top()`, `right()`, `bottom()`, `width()`, `height()`, `size()`, `surfaceRect()`, `clientRect()`, `outputSize()`, `worldSize<T>()`, and `worldQuad<T>()`.

## Styles

Fill styles:

```cpp
ctx->setFillStyle(Color(255, 255, 255));
ctx->setFillStyle(255, 255, 255, 255);
```

Stroke styles:

```cpp
ctx->setStrokeStyle(Color(255, 255, 255));
ctx->setStrokeStyle(255, 255, 255, 255);
```

Line/composite helpers:

- `setLineWidth(w)`
- `setLineCap(LineCap::CAP_BUTT/CAP_ROUND/CAP_SQUARE)`
- `setLineJoin(LineJoin::JOIN_BEVEL/JOIN_MITER)`
- `setMiterLimit(limit)`
- `setGlobalAlpha(alpha)`
- `setComposite*`, `setBlend*`
- `scopedComposite(...)`, `scopedBlend(...)`, `scopedBlendSeparate(...)`

Keep scoped composite/blend RAII objects alive for the draw span they should affect.

## Paths

Path calls mirror canvas/NanoVG:

```cpp
ctx->beginPath();
ctx->moveTo(x, y);
ctx->lineTo(x, y);
ctx->quadraticTo(cx, cy, x, y);
ctx->bezierTo(x1, y1, x2, y2, x, y);
ctx->arc(cx, cy, r, a0, a1, PathWinding::WINDING_CCW);
ctx->arcTo(x0, y0, x1, y1, r);
ctx->closePath();
ctx->stroke();
```

`moveTo` and `lineTo` also accept vector types.

Important: `circle(...)`, `ellipse(...)`, `drawPath(...)`, and `drawClosedPath(...)` add geometry but do not automatically call `beginPath()`, `stroke()`, or `fill()`. For immediate circles and ellipses, prefer `fillEllipse(...)` or `strokeEllipse(...)`.

Convenience helpers:

- `strokePath(points)` for an open polyline.
- `strokePath(points, i0, i1)` for a subrange.
- `strokeLine(p1, p2)` and `strokeLineSharp(p1, p2)` for one line.
- `moveToSharp`, `lineToSharp`, and `fillTextSharp` for crisp screen-aligned output.

## Shapes

Use immediate helpers when possible:

- Rectangles: `strokeRect(x, y, w, h)`, `fillRect(x, y, w, h)`, and `Rect<T>` overloads.
- Rounded rectangles: `strokeRoundedRect(...)`, `fillRoundedRect(...)`.
- Ellipses/circles: `strokeEllipse(cx, cy, r)`, `fillEllipse(cx, cy, r)`, vector overloads, and `rx/ry` overloads.
- Quads: `strokeQuad(Quad<T>)`, `fillQuad(Quad<T>)`.
- Arrows: `drawArrow(a, b, color, tip_angle, tip_scale, fill_tip)`.

Raw rectangle args are `x, y, width, height`. `Rect<T>` stores corner coordinates and is converted by the overload.

## Gradients

Available fill/stroke gradients:

- Linear: `setFillLinearGradient(p0, p1, inner, outer)`.
- Radial: `setFillRadialGradient(center, r0, r1, inner, outer)`.
- Box: `setFillBoxGradient(pos, size, radius, feather, inner, outer)`.

Stroke equivalents use `setStroke...Gradient`. Use `Color` overloads by default.

## Text

Typical text setup:

```cpp
ctx->setFont(font);
ctx->setFontSize(18);
ctx->setFillStyle(Color(255, 255, 255));
ctx->setTextAlign(TextAlign::ALIGN_CENTER);
ctx->setTextBaseline(TextBaseline::BASELINE_MIDDLE);
ctx->fillText("Label", pos);
```

Use `NanoFont::create("virtual/path")` to load fonts. If no active font exists, `fillText` falls back to the default font.

Use `stageMode()` for fixed screen text and `worldHudMode()` for labels attached to world features.

## Images And Surfaces

- `drawImage(image, Quad<T>)` draws an `Image` into the current coordinate mode.
- `drawImage(WorldImageT<T>)` draws a world image using its stored world quad.
- `drawShaderSurface(surface, Quad<T>, alpha)` draws a shader surface.

Use quads for image placement. The older `drawImage(image, x, y, w, h)` style is not the current reliable path.

## Practical Techniques

- Draw persistent raster or shader content first, then overlays above it.
- Use `drawWorldAxis(...)` only when axes help debug or understand scale.
- Use `worldHudMode()` for selected particles, mouse markers, handles, arrows, and labels that should stay readable while attached to world positions.
- Use `stageMode()` for screen-space counters, debug readouts, and HUDs based on `ctx->width()` / `ctx->height()`.
- Toggle `scalingSizes(false)` for dots, handles, or cursor-like markers that should keep fixed screen size.
- Toggle `scalingLines(false)` when line widths should stay screen-stable.
- For label offsets near world points, compute screen-pixel offsets in world units with `ctx->Offset(dx, dy)`.
- For glow effects, use a radial gradient plus `fillEllipse(...)`, often under `scopedComposite(CompositeOperation::LIGHTER)`.
- For polylines, `beginPath`, `moveTo` first point, `lineTo` subsequent points, then `stroke`.
- For complex vector assets, make path-building lambdas that call `beginPath` and path commands, then invoke the lambda separately before `fill` and before `stroke`.
