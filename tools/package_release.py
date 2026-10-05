"""Package the editor and original Moonwing sample using an explicit file list.

Never scan local workspaces, historical exports or neighboring test projects.
The ZIP contains third-party notices and a SHA-256 inventory for review.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "bin/AgeSkeleton.exe")
    parser.add_argument("--console", type=Path, default=ROOT / "bin/AgeSkeleton.console.exe")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".zip":
        parser.error("Output must be a .zip file")
    if args.output.exists():
        parser.error("Output already exists; choose a new filename")
    sample = ROOT / "samples/moonwing"
    provenance = json.loads((sample / "provenance.json").read_text(encoding="utf-8"))
    if provenance.get("name") != "Moonwing" or provenance.get("license") != "MIT":
        parser.error("Unexpected sample provenance")
    files = {"AgeSkeleton.exe": args.binary, "AgeSkeleton.console.exe": args.console}
    for name in ("LICENSE.txt", "LICENSE.AgeChaos.txt", "COPYRIGHT.txt", "AUTHORS.md", "THIRD_PARTY_NOTICES.md", "README.md"):
        files[name] = ROOT / name
    files["licenses/Godot-MIT.txt"] = ROOT / "misc/agechaos/Godot-MIT.txt"
    for name in ("project.godot", "provenance.json", "LICENSE.txt", "Assets/Moonwing.ecsrig.tres",
                 "Assets/body.svg", "Assets/head.svg", "Assets/tail.svg", "Assets/tail_tip.svg", "Assets/wing.svg", "Assets/leg.svg"):
        files["samples/moonwing/" + name] = sample / name
    missing = [str(path) for path in files.values() if not path.is_file()]
    if missing:
        parser.error("Missing release inputs: " + ", ".join(missing))
    payload = {name: path.read_bytes() for name, path in files.items()}
    payload["workspace.path"] = b"samples/moonwing\n"
    manifest = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in payload.items()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in payload.items():
            archive.writestr("AgeSkeleton/" + name, data)
        archive.writestr("AgeSkeleton/manifest.json", json.dumps(manifest, indent=2)+"\n")
    # Re-read the finished artifact; verification covers the actual package, not just inputs.
    with zipfile.ZipFile(args.output) as archive:
        assert archive.testzip() is None
        assert len(archive.namelist()) == len(manifest)+1
        for name, record in manifest.items():
            assert hashlib.sha256(archive.read("AgeSkeleton/"+name)).hexdigest() == record["sha256"]
    print(f"Verified {args.output}: {len(payload)} files; only the original Moonwing sample included")


if __name__ == "__main__":
    main()
