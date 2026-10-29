# Managed Dawn overlay

Bitloop project tooling carries the pinned Dawn 20250922.223923 port, including its matching Emdawn bindings. Registry baselines and upstream source/archive versions are unchanged. Original vcpkg port scripts are covered by LICENSE.vcpkg.txt.

`007-stable-d3d12-shader-cache.patch` sorts an unordered binding list before Dawn serializes the native shader-cache key.

`008-windows-dxc-package.patch` enables the Windows `dxc` feature with the baseline `directx-dxc` package. Microsoft provides the compiler binaries; Dawn is rebuilt with DXC support, without building LLVM. The feature includes D3D12. The package exports the compiler runtime locations for Bitloop's executable deployment helper. Other platforms retain their original build paths.

This is a managed snapshot of Bitloop's `templates/default/tooling/ports`; edit that canonical source, then use `bitloop tooling sync`. The toolchain adds the overlay before vcpkg installs dependencies. Preserve project-specific ports alongside it. Retire individual patches only after checking the chosen upstream release includes their behavior.
