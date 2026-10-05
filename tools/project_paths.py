"""Keep compiler output in bin and authoring projects in the data directory."""
import os
from pathlib import Path

SOURCE_ROOT = Path(__file__).resolve().parents[1]


def data_root():
    override = os.environ.get("AGESKELETON_DATA_ROOT")
    return Path(override).resolve() if override else SOURCE_ROOT.parent / "GodotECSProjects" / "AgeSkeleton"


def editor_binary():
    return SOURCE_ROOT / "bin/AgeSkeleton.exe"


def prepare_build_directory():
    target = SOURCE_ROOT / "bin"
    if target.is_symlink() or (target.exists() and target.resolve() != target.absolute()):
        raise RuntimeError(f"bin must be a real local directory: {target}")
    target.mkdir(parents=True, exist_ok=True)
