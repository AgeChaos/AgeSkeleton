"""打开独立骨骼编辑器 / Open the standalone skeleton editor."""
import argparse
import subprocess
from pathlib import Path
from project_paths import data_root, editor_binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=data_root() / "Project/Workspace")
    args = parser.parse_args()
    project = args.project.resolve()
    if not (project / "project.godot").is_file():
        parser.error(f"缺少工程配置 / Missing project.godot: {project}")
    binary = editor_binary()
    if not binary.is_file():
        parser.error(f"请先构建 / Build the editor first: {binary}")
    subprocess.Popen([str(binary), "--editor", "--path", str(project)], cwd=binary.parent)


if __name__ == "__main__":
    main()
