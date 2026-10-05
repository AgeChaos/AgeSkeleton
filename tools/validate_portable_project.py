"""Check portable documents in an isolated Reports directory; leave user documents untouched."""
import argparse
import json
import shutil
import subprocess
import time
from pathlib import Path
from skeleton_ai import SkeletonClient
from project_paths import editor_binary, data_root


def client_for(host, process):
    session = host / f".godot/agechaos-skeleton-ai/{process.pid}/session.json"
    deadline = time.monotonic() + 60
    while not session.is_file():
        if process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(f"Editor failed to start: {session}")
        time.sleep(.1)
    return SkeletonClient(session)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Legacy project.godot directory with an ecs/run/scene fixture")
    args = parser.parse_args()
    report = data_root() / "Reports" / time.strftime("PortableProject-%Y%m%d-%H%M%S")
    report.mkdir(parents=True)
    host = report / "Host"
    host.mkdir()
    shutil.copy2(args.source / "project.godot", host / "project.godot")
    # Existing fixture resources still resolve in their own project; copying assets avoids edits to it.
    shutil.copytree(args.source / "Assets", host / "Assets")
    process = None
    try:
        with (report / "legacy.log").open("wb") as output:
            process = subprocess.Popen([str(editor_binary()), "--headless", "--path", str(host), "--", "--ecs-ai-editor"], stdout=output, stderr=subprocess.STDOUT)
            client = client_for(host, process)
            before = client.call("status")
            tracks = client.call("tracks", entity=0, animation="Walk")
            original = report / "Original" / "角色 测试.ageskeleton"
            client.call("save", path=str(original))
            assert sorted(p.name for p in original.parent.iterdir()) == ["images", original.name]
            count = len(list((original.parent / "images").iterdir()))
            assert count > 0
            client.call("save", path=str(original))
            assert len(list((original.parent / "images").iterdir())) == count
            moved = report / "Moved" / original.name
            original.parent.rename(moved.parent)
            client.call("open", path=str(moved), replace=True)
            assert client.call("status")["entities"] == before["entities"]
            assert client.call("tracks", entity=0, animation="Walk")["tracks"] == tracks["tracks"]
            assert client.call("validate")["ok"]
            copy = report / "SaveAs" / "Copy.ageskeleton"
            (copy.parent / "images").mkdir(parents=True)
            collision_name = next((moved.parent / "images").iterdir()).name
            collision = copy.parent / "images" / collision_name
            collision.write_bytes(b"Unrelated user file must not be overwritten")
            client.call("save", path=str(copy))
            assert collision.read_bytes() == b"Unrelated user file must not be overwritten"
            assert len(list((copy.parent / "images").iterdir())) == count + 1
            client.call("open", path=str(copy), replace=True)
            assert client.call("validate")["ok"]
            client.call("save", path=str(copy))
            assert len(list((copy.parent / "images").iterdir())) == count + 1
            original_bytes = copy.read_bytes()
            blocked = report / "Blocked.ageskeleton"
            blocked.mkdir()
            try:
                client.call("save", path=str(blocked))
                raise AssertionError("Directory accepted as document")
            except RuntimeError:
                pass
            assert copy.read_bytes() == original_bytes
            # Missing images must fail without replacing the live document.
            image = next((moved.parent / "images").iterdir())
            missing = image.with_suffix(".missing")
            image.rename(missing)
            try:
                try:
                    client.call("open", path=str(moved), replace=True)
                    raise AssertionError("Missing image accepted")
                except RuntimeError as error:
                    assert "Invalid skeletal project" in str(error)
                assert client.call("status")["entities"] == before["entities"]
            finally:
                missing.rename(image)
            process.terminate(); process.wait(timeout=15); process = None
        # A fresh default editor must open the relocated file without the old host or import cache.
        default_host = Path.home() / "AppData/Roaming/AgeSkeleton/EditorHost"
        with (report / "standalone.log").open("wb") as output:
            process = subprocess.Popen([str(editor_binary()), "--headless", str(moved), "--", "--ecs-ai-editor"], stdout=output, stderr=subprocess.STDOUT)
            client = client_for(default_host, process)
            assert client.call("status")["entities"] == before["entities"]
            assert client.call("tracks", entity=0, animation="Walk")["tracks"] == tracks["tracks"]
            assert client.call("validate")["ok"]
        (report / "result.json").write_text(json.dumps({"ok": True, "images": count, "project": str(moved)}, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"PORTABLE_PROJECT_PASS {report}")
    finally:
        if process and process.poll() is None:
            process.terminate(); process.wait(timeout=15)


if __name__ == "__main__":
    main()
