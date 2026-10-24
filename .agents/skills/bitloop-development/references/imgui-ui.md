# ImGui And Buffered State

Use this page when adding sidebar controls, buttons, live settings, or UI-triggered scene actions.

## Core Model

Bitloop integrates ImGui through a double-buffered model so UI edits do not directly mutate live simulation state. UI code edits a shadow copy. The worker syncs shadow values to live simulation state at safe points between processing calls.

`Project<ProjectType>` and `Scene<SceneType>` inherit `VarBuffer<T>`. Their UI classes usually inherit a `BufferedInterfaceModel`, which provides the `bl_*` macros.

## Common Macros

Use `bl_scoped(name)` for the common case:

```cpp
bl_scoped(camera);
camera.populateUI();
```

`bl_scoped` pulls a temporary editable copy into the UI scope and pushes it back automatically when the scope exits.

Use `bl_pull(name)` and `bl_push(name)` when a control flow shape needs manual push:

```cpp
bl_pull(gravity);
if (ImGui::SliderDouble("Gravity", &gravity, 0.0, 10.0))
{
    bl_push(gravity);
}
```

Use `bl_view(a, b, c)` for read-only access in UI code.

Use `bl_pull_temp(name)` for an untracked UI-local copy. It never commits and
does not create a persistent buffer entry, so its memory and comparison cost end
with the surrounding C++ scope. If the member is already tracked, the temporary
copy is taken from its current shadow value.

Use `bl_schedule([](SceneType& scene) { ... });` to queue a live scene call for the next safe sync point:

```cpp
if (ImGui::Button("Reset"))
{
    bl_schedule([](MyScene& scene) {
        scene.resetSimulation();
    });
}
```

Because scheduled lambdas execute later, prefer capturing explicit values by value when the lambda depends on current UI input.

## Custom Types

Custom synced types should be copyable and assignable. For efficient change detection, provide one of:

- `operator==(const T&) const`
- `bl::hash_t hash() const`
- trivial copyable layout when byte comparison is acceptable

Without meaningful equality/hash behavior, Bitloop may not be able to avoid unnecessary syncing.

`bl::hash_t` is always the stable 64-bit hash type, including on wasm32. Do not
return `size_t`: Bitloop deliberately rejects narrower `hash()` return types so
change detection cannot silently discard the upper 32 bits. `bl::StableHasher`
is the standard helper. Hash logical fields explicitly; pointer addresses and
raw object representations are not portable across processes or ABIs.

Classes derived from `bl::Hashable` also expose `stl_hash()` for native-width
STL hash adapters. Do not use `stl_hash()` for buffered change detection or
persistent identifiers because it folds to 32 bits where `size_t` is 32-bit.

## UI-Only State

UI-only values, such as popup open flags or transient filter text, do not need `bl_scoped`. Keep them in the UI class when they do not affect live simulation.

## Camera Controls

Camera UI usually looks like:

```cpp
if (ImGui::CollapsingHeaderBox("View", true))
{
    bl_scoped(camera);
    camera.populateUI();
    ImGui::EndCollapsingHeaderBox();
}
```

This lets ImGui edit a shadow camera and lets Bitloop safely sync it into the live scene.

## BasicProject Caveat

`BasicProject` is for simpler projects and custom UI cases. It is not the default buffered project pattern. Use the generated `Project<ProjectType>`/`Scene<SceneType>` style for new simulations unless an existing project clearly uses `BasicProject` for a reason.


