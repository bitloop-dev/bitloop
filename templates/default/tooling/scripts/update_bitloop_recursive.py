#!/usr/bin/env python3
import subprocess
from pathlib import Path

def update_project_vcpkg(project_dir: Path, display_name: str | None = None) -> None:
    project_dir = project_dir.resolve()
    if not project_dir.is_dir():
        raise NotADirectoryError(project_dir)

    manifest = project_dir / "vcpkg.json"
    if not manifest.exists():
        return

    if display_name is not None:
        print(f"\n== {display_name} ==")
    else:
        print(f"\n== {project_dir.name} ==")

    subprocess.run(["vcpkg", "x-update-baseline"], cwd=project_dir, check=True)

def update_projects_vcpkg_recursive(root: Path) -> None:
    root = root.resolve()
    if not root.is_dir():
        raise NotADirectoryError(root)

    for project_dir in sorted(p for p in root.rglob("*") if p.is_dir()):
        manifest = project_dir / "vcpkg.json"
        if not manifest.exists():
            continue

        update_project_vcpkg(project_dir, display_name=str(project_dir.relative_to(root)))

def main() -> None:
    script_root = Path(__file__).resolve().parent.parent.parent

    # Update the root vcpkg baseline (if it has a manifest)
    update_project_vcpkg(script_root, display_name="root")

    update_projects_vcpkg_recursive(script_root / "projects")
    update_projects_vcpkg_recursive(script_root / "bitloop" / "examples")

if __name__ == "__main__":
    main()
