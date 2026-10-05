"""Package an export template without modifying the checked-in ArkTS project."""

import json
import zipfile
from pathlib import Path


def generate_bundle(target, source, env):
    template_root = Path(env["openharmony_template_root"]).resolve()
    arch = env["openharmony_template_arch"]
    with zipfile.ZipFile(str(target[0]), "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for path in source[2:]:
            path = Path(str(path)).resolve()
            bundle.write(path, path.relative_to(template_root).as_posix())
        bundle.write(str(source[0]), f"entry/src/main/cpp/libs/{arch}/libgodot.so")
        bundle.write(str(source[1]), "entry/src/main/cpp/include/bridge_openharmony.h")
        bundle.writestr(
            "godot_openharmony.json",
            json.dumps({
                "architecture": arch,
                "variant": env["openharmony_template_variant"],
                "dotnet": env["openharmony_template_mono"],
                "runtime": env.get("openharmony_template_runtime", "none"),
                "gles3": env.get("openharmony_template_gles3", False),
                "version": env["openharmony_template_version"],
            }),
        )
