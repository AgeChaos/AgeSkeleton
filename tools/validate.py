"""在临时工程中运行制作工具回归，不写入用户 Workspace。"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from project_paths import editor_binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=editor_binary())
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="agechaos-skeleton-regression-") as directory:
        project = Path(directory)
        (project / "project.godot").write_text(
            'config_version=5\n[application]\nconfig/name="Skeleton regression"\n'
            'run/main_loop_type="ECSMainLoop"\n[rendering]\n'
            'renderer/rendering_method="gl_compatibility"\n', encoding="utf-8")
        with args.log.open("wb") as output:
            result = subprocess.run([str(args.binary.resolve()), "--headless", "--path", str(project),
                                     "--", "--skeleton-self-test"], stdout=output,
                                    stderr=subprocess.STDOUT, timeout=180)
        log = args.log.read_text(encoding="utf-8", errors="replace")
        if result.returncode or "AGE_SKELETON_STANDALONE_PASS" not in log:
            print(log)
            return 1
        print("PASS: 独立制作工具回归；日志：", args.log)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
