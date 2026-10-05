"""构建独立骨骼制作工具；不修改安装版和用户工程。"""
import argparse
import os
import subprocess
import sys
import shutil
from project_paths import SOURCE_ROOT, data_root, prepare_build_directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    source = SOURCE_ROOT
    if not (source / "SConstruct").is_file():
        parser.error("项目根目录缺少 SConstruct / Missing SConstruct in source root")
    prepare_build_directory()
    command = [sys.executable, "-m", "SCons", "platform=windows", "arch=x86_64",
               "target=editor", "extra_suffix=skeleton",
               "disable_xr=yes", "tests=no",
               "vulkan=yes", "d3d12=no", "angle=no", "accesskit=no",
               "-j" + str(args.jobs)]
    if args.dry_run:
        command.append("--dry-run")
    result = subprocess.call(command, cwd=source)
    if result == 0 and not args.dry_run:
        root = source / "bin"
        root.mkdir(parents=True, exist_ok=True)
        binary = source / "bin/godot.windows.editor.x86_64.skeleton.exe"
        shutil.copy2(binary, root / "AgeSkeleton.exe")
        console = source / "bin/godot.windows.editor.x86_64.skeleton.console.exe"
        if console.exists():
            shutil.copy2(console, root / "AgeSkeleton.console.exe")
        workspace = data_root() / "Project/Workspace"
        workspace.mkdir(parents=True, exist_ok=True)
        project = workspace / "project.godot"
        if not project.exists():
            project.write_text('config_version=5\n[application]\nconfig/name="AgeSkeleton"\n[rendering]\nrenderer/rendering_method="gl_compatibility"\n', encoding="utf-8")
        try:
            workspace_path = os.path.relpath(workspace, root)
        except ValueError:  # Different Windows drives cannot use relative paths.
            workspace_path = str(workspace)
        (root / "workspace.path").write_text(workspace_path + "\n", encoding="utf-8")
        print("Ready:", root / "AgeSkeleton.exe")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
