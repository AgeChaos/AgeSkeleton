"""Check packaged ELF libraries against the SDK; this is not a device test.

Requires pyelftools. Run with --help for arguments.
"""

import argparse
import io
import json
import zipfile
from pathlib import Path

from elftools.elf.elffile import ELFFile


def inspect_library(data, machine):
    elf = ELFFile(io.BytesIO(data))
    if elf.elfclass != 64 or elf.header.e_machine != machine or elf.header.e_type != "ET_DYN":
        raise ValueError("Expected a 64-bit shared library for " + machine)
    symbols = elf.get_section_by_name(".dynsym")
    exports, imports = set(), set()
    for symbol in symbols.iter_symbols():
        if symbol.entry.st_shndx == "SHN_UNDEF":
            if symbol.entry.st_info.bind == "STB_GLOBAL":
                imports.add(symbol.name)
        elif symbol.entry.st_other.visibility in ("STV_DEFAULT", "STV_PROTECTED"):
            exports.add(symbol.name)
    needed = [tag.needed for tag in elf.get_section_by_name(".dynamic").iter_tags() if tag.entry.d_tag == "DT_NEEDED"]
    return exports, imports, needed


def verify(hap, sdk, arch, assembly):
    abi, triple, machine = {
        "arm64": ("arm64-v8a", "aarch64-linux-ohos", "EM_AARCH64"),
        "x86_64": ("x86_64", "x86_64-linux-ohos", "EM_X86_64"),
    }[arch]
    sdk_libraries = sdk / "native/sysroot/usr/lib" / triple
    with zipfile.ZipFile(hap) as archive:
        if archive.testzip() is not None:
            raise ValueError("HAP ZIP checksum failure")
        if archive.read("resources/rawfile/template.pck")[:4] != b"GDPC":
            raise ValueError("Missing or invalid Godot PCK")
        libraries = {
            Path(name).name: inspect_library(archive.read(name), machine)
            for name in archive.namelist()
            if name.startswith(f"libs/{abi}/") and not name.endswith("/")
        }
    for name in ("libgodot.so", "libentry.so", "libc++_shared.so", f"lib{assembly}.so"):
        if name not in libraries:
            raise ValueError("Missing packaged library: " + name)
    if "godotsharp_game_main_init" not in libraries[f"lib{assembly}.so"][0]:
        raise ValueError("C# library does not export godotsharp_game_main_init")

    packaged_names = set(libraries)

    def load(name):
        if name not in libraries:
            if Path(name).name != name or not (sdk_libraries / name).is_file():
                raise ValueError("Dependency missing from HAP and SDK: " + name)
            libraries[name] = inspect_library((sdk_libraries / name).read_bytes(), machine)
        return libraries[name]

    def exports_for(name, visited):
        if name in visited:
            return set()
        visited.add(name)
        exports, _, needed = load(name)
        available = set(exports)
        for dependency in needed:
            available.update(exports_for(dependency, visited))
        return available

    errors = []
    for name in sorted(packaged_names):
        missing = libraries[name][1] - exports_for(name, set())
        if missing:
            errors.append(f"{name}: unresolved symbols: {', '.join(sorted(missing))}")
    if errors:
        raise ValueError("\n".join(errors))
    return {
        "hap": str(hap),
        "architecture": arch,
        "libraries": sorted(packaged_names),
        "sdk_symbol_check": "passed",
        "device_test": "not performed",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hap", type=Path)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--assembly", required=True)
    parser.add_argument("--arch", choices=("arm64", "x86_64"), default="arm64")
    args = parser.parse_args()
    print(json.dumps(verify(args.hap, args.sdk, args.arch, args.assembly), indent=2))
