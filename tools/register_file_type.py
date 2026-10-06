"""Register .ageskeleton files for the current Windows user (no administrator required)."""
import argparse
import ctypes
import sys
import shutil
from pathlib import Path


def register(binary):
    import winreg
    binary = Path(binary).resolve()
    if not binary.is_file():
        raise FileNotFoundError(binary)
    # Keep the document icon independent of executable replacement during updates.
    icon = binary.with_name("AgeSkeleton.ico")
    source_icon = Path(__file__).resolve().parents[1] / "platform/windows/godot.ico"
    if source_icon.is_file():
        shutil.copy2(source_icon, icon)
    if not icon.is_file():
        raise FileNotFoundError(icon)
    values = {
        r"Software\Classes\.ageskeleton": "AgeSkeleton.Project",
        r"Software\Classes\AgeSkeleton.Project": "AgeSkeleton Project",
        r"Software\Classes\AgeSkeleton.Project\DefaultIcon": f'"{icon}",0',
        r"Software\Classes\AgeSkeleton.Project\shell\open\command": f'"{binary}" "%1"',
    }
    for key, value in values.items():
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as handle:
            winreg.SetValueEx(handle, "", 0, winreg.REG_SZ, value)
    ctypes.windll.shell32.SHChangeNotify(0x08000000, 0, None, None)
    print(f"Registered .ageskeleton: {binary}; icon: {icon}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path(__file__).resolve().parents[1] / "bin/AgeSkeleton.exe")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Windows file association only")
    register(args.binary)
