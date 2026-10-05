import os
import sys

from methods import print_error
from platform_methods import validate_arch


def get_name():
    return "OpenHarmony"


def can_build():
    # SCons must allow an SDK path supplied as a command-line argument.
    return True


def get_tools(env):
    return ["clang", "clang++", "as", "ar", "link"]


def get_opts():
    from SCons.Variables import BoolVariable

    return [
        ("OPENHARMONY_SDK_PATH", "SDK directory containing native/llvm and native/sysroot", get_default_sdk_path()),
        BoolVariable("generate_bundle", "Package the OpenHarmony export template ZIP", True),
    ]


def get_doc_classes():
    return ["EditorExportPlatformOpenHarmony"]


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "arm64",
        "target": "template_debug",
        "builtin_pcre2_with_jit": False,
        "opengl3": True,
        "vulkan": True,
        "module_raycast_enabled": False,
        "supported": [],
    }


def get_default_sdk_path():
    return os.environ.get("OPENHARMONY_SDK_PATH", "")


def configure(env):
    validate_arch(env["arch"], get_name(), ["arm64", "x86_64"])
    if env.editor_build:
        print_error("OpenHarmony supports export templates only. Build the editor for your host platform.")
        sys.exit(255)

    sdk_root = os.path.abspath(env["OPENHARMONY_SDK_PATH"] or get_default_sdk_path())
    compiler_dir = os.path.join(sdk_root, "native", "llvm", "bin")
    sysroot = os.path.join(sdk_root, "native", "sysroot")
    exe = ".exe" if os.name == "nt" else ""
    if not os.path.isfile(os.path.join(compiler_dir, "clang" + exe)) or not os.path.isdir(sysroot):
        print_error(
            "OpenHarmony SDK not found. Set OPENHARMONY_SDK_PATH to the directory containing native/llvm and native/sysroot."
        )
        sys.exit(255)
    if not env["vulkan"]:
        print_error("OpenHarmony currently requires vulkan=yes; GLES3 can be enabled alongside Vulkan.")
        sys.exit(255)
    if env["builtin_pcre2_with_jit"]:
        print_error("OpenHarmony devices require builtin_pcre2_with_jit=no.")
        sys.exit(255)

    env.PrependENVPath("PATH", compiler_dir)
    # Godot's compiler-version probe uses the Python process environment.
    os.environ["PATH"] = compiler_dir + os.pathsep + os.environ.get("PATH", "")
    for variable, executable in {
        "CC": "clang",
        "CXX": "clang++",
        "AR": "llvm-ar",
        "RANLIB": "llvm-ranlib",
        "AS": "clang",
    }.items():
        env[variable] = executable + exe
    env["LINK"] = env["CXX"]
    env["SHLINK"] = env["CXX"]
    env["SHLIBSUFFIX"] = ".so"
    env["SHLINKFLAGS"] = ["-shared"]
    env["OBJSUFFIX"] = ".o"
    env["SHOBJSUFFIX"] = "$OBJSUFFIX"

    triple = "aarch64-linux-ohos" if env["arch"] == "arm64" else "x86_64-linux-ohos"
    target_flags = [f"--target={triple}", f"--sysroot={sysroot}"]
    env.Append(CCFLAGS=target_flags + ["-fPIC", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off"])
    env.Append(ASFLAGS=target_flags + ["-c"])
    env.Append(
        LINKFLAGS=target_flags + ["-fuse-ld=lld", "-Wl,--gc-sections", "-Wl,--no-undefined", "-Wl,-soname,libgodot.so"]
    )
    env.Prepend(CPPPATH=["#platform/openharmony"])
    env.Append(
        CPPDEFINES=[
            "OPENHARMONY_ENABLED",
            "UNIX_ENABLED",
            "VULKAN_ENABLED",
            "RD_ENABLED",
            "MBEDTLS_NO_UDBL_DIVISION",
            "ZSTD_DISABLE_ASM",
        ]
    )
    env.Append(CPPDEFINES=["VK_USE_PLATFORM_OHOS"])
    if env["opengl3"]:
        env.Append(CPPDEFINES=["GLES3_ENABLED", "EGL_ENABLED"])
    env.Append(
        LIBS=[
            "hilog_ndk.z",
            "ace_ndk.z",
            "ace_napi.z",
            "rawfile.z",
            "native_window",
            "native_vsync",
            "ohaudio",
            "ohinputmethod",
            "native_display_manager",
            "native_window_manager",
            "native_drawing",
            "udmf",
            "pasteboard",
            "dl",
            "m",
        ]
    )
    if not env["use_volk"]:
        env.Append(LIBS=["vulkan"])

    if env["lto"] == "auto":
        env["lto"] = "none"
    if env["lto"] != "none":
        flag = "-flto=thin" if env["lto"] == "thin" else "-flto"
        env.Append(CCFLAGS=[flag], LINKFLAGS=[flag])

    if os.name == "nt":
        from SCons.Subst import quote_spaces

        # Let SCons quote response files for long archive lists and SDK paths.
        env["TEMPFILEARGESCFUNC"] = lambda arg: quote_spaces(arg.replace("\\", "/"))
        env["ARCOM"] = '${TEMPFILE("$AR $ARFLAGS $TARGET $SOURCES", "$ARCOMSTR")}'
        env["SHLINKCOM"] = (
            '${TEMPFILE("$SHLINK $SHLINKFLAGS -o $TARGET $SOURCES $LINKFLAGS $_LIBDIRFLAGS $_LIBFLAGS", "$SHLINKCOMSTR")}'
        )
