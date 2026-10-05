"""打开独立骨骼编辑器 / Open the standalone skeleton editor."""
import argparse
import subprocess
from pathlib import Path
from project_paths import data_root, editor_binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=None)
    args = parser.parse_args()
    project = args.project.resolve() if args.project else None
    command = []
    if project:
        if project.suffix.lower() == ".ageskeleton" and project.is_file():
            command = [str(project)]
        elif (project / "project.godot").is_file():
            command = ["--editor", "--path", str(project)]
        else:
            parser.error("请选择 .ageskeleton 文件或旧工作区 / Select an .ageskeleton file or legacy workspace")
    binary = editor_binary()
    if not binary.is_file():
        parser.error(f"请先构建 / Build the editor first: {binary}")
    subprocess.Popen([str(binary), *command], cwd=binary.parent)


if __name__ == "__main__":
    main()
