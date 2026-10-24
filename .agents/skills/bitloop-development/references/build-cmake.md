# Build And CMake

Use this page when changing project hierarchy, CMake wiring, project-local dependencies, targets, or reusable command-line tooling. Use [configure-build-run.md](configure-build-run.md) for operational configure/build/run setup.

## Project Macros

Bitloop project groups use:

```cmake
bitloop_new_project(Name)
bitloop_add_dependency(Name "./ChildProject")
bitloop_finalize()
```

The root `bitloop_new_project` creates the executable. Nested projects become linkable project libraries. Preserve the generated local pattern unless a requested feature requires a change.

## Project-Local Dependencies

Every project must remain buildable as a standalone root. Add a third-party dependency to the `vcpkg.json` owned by the project that uses it, then add its documented `find_package(...)` and `target_link_libraries(...)` calls to that project's `CMakeLists.txt`.

A parent may merge discovered child manifests for its composite build, but that generated union is not the dependency source of truth. Do not add a package globally merely because one child needs it.

## Framework Provider Selection

Project tooling selects Bitloop once for the opened root:

- A project inside a gallery workspace uses `<workspace>/bitloop`.
- An example inside a Bitloop checkout uses that enclosing source tree.
- A detached project uses its checked-in `bitloop` vcpkg dependency.

`BITLOOP_PROVIDER=AUTO` is the default. `SOURCE` and `PACKAGE`, plus an optional `BITLOOP_SOURCE_DIR`, are top-level expert overrides. Child projects reuse the resolved `bitloop::bitloop` target and must not choose a different provider.

Workspace markers select the shared vcpkg/cache and candidate source location. Manifest discovery is separate: it starts at the opened root and follows only enabled `bitloop_add_dependency(...)` edges. Source mode removes only the `bitloop` port from the generated union and adds the framework source manifest; checked-in project manifests remain unchanged.

Prefer Bitloop and standard C++ capabilities when they fit. Ask before adding a dependency that broadens architecture, ownership, or long-term maintenance for speculative benefit.

Typical project-local wiring after `bitloop_finalize()` is:

```cmake
find_package(<Package> CONFIG REQUIRED)
target_link_libraries(<ProjectName> PRIVATE <ImportedTarget>)
```

Verify the package and imported target names from the installed CMake configuration or authoritative package documentation.

## Root Target And Headless Mode

Derive the root target from the active root `bitloop_new_project(...)` call; do not assume a gallery-specific target name. The resulting application supports GUI and headless flags such as `--headless`, `--project`, `--list-scenes`, repeatable `--scene`, `--scene-config`, `--frames`, `--dt`, `--surface`, and `--trace`.

Build the smallest target that contains the changed project. When parent registration changed, build the parent root target.

## Reusable Tooling

Extend the framework CLI for reusable Bitloop operations rather than copying ad hoc tools into every generated project. Keep project-specific scripts inside their owning project.

Framework-owned project CMake tooling is a managed snapshot:

```powershell
bitloop tooling sync --check --project <ProjectRoot>
bitloop tooling sync --project <ProjectRoot>
```

Synchronization leaves project CMake logic, presets, manifests, and scripts untouched. It refuses to replace modified managed files unless `--force` is supplied.

Compact trace reconstruction uses:

```powershell
bitloop trace reconstruct --file .\.codex\runs\<run>\frames.jsonl --frame 54 --pretty
```

## Hygiene

- Keep project and directory names exact.
- Check target-name collisions before changing hierarchy.
- Preserve user presets and build directories.
- Do not edit generated merged manifests as sources of truth.
