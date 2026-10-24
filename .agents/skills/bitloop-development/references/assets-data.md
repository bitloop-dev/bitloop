# Assets, Images, And Data

Use this page when loading fonts/images, writing project data, using CPU rasters, or drawing shader surfaces.

## Project Paths

Use project root helpers for files that belong to a project:

```cpp
auto path = ProjectBase::activeProject()->rootPath("data/file.json");
```

Avoid hardcoded absolute paths inside simulation code.

## Fonts

Load NanoVG fonts with:

```cpp
NanoFont font = NanoFont::create("/data/fonts/Roboto-Regular.ttf");
ctx->setFont(font);
```

Set font size, alignment, baseline, and fill style before `fillText`.

## CPU Images

`Image` stores RGBA8 pixels and supports creation, clearing, pixel writes, and reads.

`WorldImageT<T>` combines an image with world-space raster grid metadata. Use it when CPU-generated pixels need to align with world coordinates.

Typical flow:

```cpp
world_image.setRasterSize(size);
world_image.setWorldStageTransform(ctx->cameraTransform(camera));
world_image.setStageRect(ctx->surfaceRect());
```

The transform is needed only for stage-space placement or conversion. A world image positioned with `setWorldRect(...)` does not retain or require a camera.

Only regenerate expensive rasters when inputs change.

## Image Loading

Use `image_loader.h` helpers for loading image data and GL textures. Supported formats include common stb formats, WebP, and SVG depending on build options.

Use existing `Image`/texture helpers rather than building custom loaders for ordinary assets.

## Shader Surfaces

`ShaderSurface` renders GLSL into a GL texture that can be drawn through NanoVG. Use it for GPU-heavy visuals, progressive rendering, or image effects.

Create/defer surfaces through the existing helpers and owner thread queue where applicable:

```cpp
auto surface = makeShaderSurface(main_window()->threadQueue());
```

Destroy GL resources on the GUI/render owner thread. Existing projects post cleanup through `main_window()->threadQueue().post(...)`.

## Drawing Order

For raster/shader simulations:

1. Update CPU image or shader state in `viewportProcess`.
2. Draw image/surface first in `viewportDraw`.
3. Draw axes, selections, markers, and text overlays afterward.

## Persistence

Use JSON helpers for structured state. Use checkpoint chunks for large arrays. Use compact semantic traces for diagnostics rather than writing ad hoc logs unless the log is truly project-specific.

