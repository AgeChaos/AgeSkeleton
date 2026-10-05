"""Host-side checks; fixture libraries are not runnable OpenHarmony binaries."""

import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "openharmony_builders", ROOT / "platform/openharmony/platform_openharmony_builders.py"
)
builders = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builders)
version_spec = importlib.util.spec_from_file_location("godot_version", ROOT / "version.py")
version = importlib.util.module_from_spec(version_spec)
version_spec.loader.exec_module(version)
VERSION = f"{version.major}.{version.minor}.{version.patch}"
ENGINE = os.environ.get(
    "GODOT_OPENHARMONY_TEST_EDITOR", str(ROOT / "bin/godot.windows.editor.x86_64.leanclr.leanclr.exe")
)


def package_fixture(directory, arch="arm64-v8a", mono=False, variant="debug", gles3=False):
    template = ROOT / "misc/dist/openharmony_template"
    library = directory / "fixture.so"
    library.write_bytes(b"test fixture, not a native library")
    archive = directory / "template.zip"
    sources = [library, ROOT / "platform/openharmony/bridge_openharmony.h"]
    sources += sorted(path for path in template.rglob("*") if path.is_file())
    builders.generate_bundle(
        [archive],
        sources,
        {
            "openharmony_template_root": str(template),
            "openharmony_template_arch": arch,
            "openharmony_template_variant": variant,
            "openharmony_template_mono": mono,
            "openharmony_template_runtime": "leanclr",
            "openharmony_template_gles3": gles3,
            "openharmony_template_version": VERSION,
        },
    )
    return archive


class StartupSourceTests(unittest.TestCase):
    def test_startup_gles_preference_is_not_failure_fallback(self):
        source = (ROOT / "platform/openharmony/bridge_openharmony.cpp").read_text(encoding="utf-8")
        probe = source.split("static bool prefer_gles_on_startup()", 1)[1].split("#endif", 1)[0]
        self.assertIn("VkApplicationInfo application_info{}", probe)
        self.assertIn("VkInstanceCreateInfo instance_info{}", probe)
        self.assertIn("bool prefer_gles = false", probe)
        self.assertIn("result == VK_SUCCESS && instance != VK_NULL_HANDLE", probe)
        self.assertLess(probe.index("destroy_instance(instance, nullptr)"), probe.index("dlclose(library)"))
        self.assertIn('dlsym(library, "vkCreateSurfaceOHOS")', probe)
        selection = source.split("bool has_explicit_driver = false", 1)[1].split("const char **cmdline", 1)[0]
        self.assertIn('args[i] == "--" || args[i] == "++"', selection)
        self.assertIn('args[i] == "--rendering-driver" || args[i] == "-rd"', selection)
        self.assertIn("if (!has_explicit_driver && prefer_gles_on_startup())", selection)
        self.assertIn('args.insert(engine_args_end + 1, "opengl3")', selection)
        self.assertLess(source.index("if (!has_explicit_driver"), source.index("Main::setup(OS_OpenHarmony"))
        display = (ROOT / "platform/openharmony/display_server_openharmony.cpp").read_text(encoding="utf-8")
        self.assertIn("No automatic GLES retry was performed", display)

    def test_vulkan_preflight_precedes_renderer_resources(self):
        # Source-order check; rejection/cleanup also needs an emulator test.
        source = (ROOT / "platform/openharmony/display_server_openharmony.cpp").read_text(encoding="utf-8")
        check = source.index("// Check the selected device before the compositor")
        self.assertLess(source.index("rendering_device->initialize("), check)
        self.assertLess(check, source.index("rendering_device->screen_create("))
        self.assertLess(check, source.index("RendererCompositorRD::make_current()"))
        preflight = source[check : source.index("rendering_device->screen_create(")]
        for texture_format in ("R8G8B8A8_UNORM", "R8G8B8A8_UINT", "D16_UNORM"):
            self.assertIn(f"RenderingDevice::DATA_FORMAT_{texture_format}", preflight)
        self.assertIn(
            "texture_is_format_supported_for_usage(required.format, RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT)",
            preflight,
        )
        self.assertIn("if (!required_formats_supported)", preflight)
        self.assertIn("r_error = ERR_UNAVAILABLE;\n\t\treturn;", preflight)
        self.assertNotIn("RasterizerGLES3", preflight)

    def test_vulkan_surface_entry_point_is_checked(self):
        source = (ROOT / "platform/openharmony/rendering_context_driver_vulkan_openharmony.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('vkGetInstanceProcAddr(instance_get(), "vkCreateSurfaceOHOS")', source)
        self.assertLess(
            source.index("ERR_FAIL_NULL_V_MSG(create_surface"), source.index("VkResult err = create_surface(")
        )
        self.assertIn("ERR_FAIL_COND_V_MSG(err != VK_SUCCESS", source)

    def test_gles_uses_context_resolved_entry_points(self):
        platform = ROOT / "platform/openharmony"
        gl = (platform / "platform_gl.h").read_text(encoding="utf-8")
        self.assertIn("#define GLES_API_ENABLED", gl)
        self.assertIn("#define GLAD_GLES2", gl)
        self.assertIn("thirdparty/glad/glad/gl.h", gl)
        self.assertNotIn("<GLES3/gl3.h>", gl)
        self.assertIn("thirdparty/glad/glad/egl.h", (platform / "platform_egl.h").read_text(encoding="utf-8"))
        source = (platform / "display_server_openharmony.cpp").read_text(encoding="utf-8")
        init = source.split("Error DisplayServerOpenHarmony::initialize_egl()", 1)[1].split("return OK;", 1)[0]
        self.assertLess(init.index("gladLoaderLoadEGL(EGL_NO_DISPLAY)"), init.index("eglGetDisplay("))
        self.assertLess(init.index("eglInitialize("), init.index("gladLoaderLoadEGL(egl_display)"))
        self.assertLess(init.index("gladLoaderLoadEGL(egl_display)"), init.index("eglCreateContext("))
        detect = (platform / "detect.py").read_text(encoding="utf-8")
        self.assertIn('["GLES3_ENABLED", "EGL_ENABLED"]', detect)
        self.assertNotIn('LIBS=["EGL", "GLESv3"]', detect)
        build = (ROOT / "drivers/gl_context/SCsub").read_text(encoding="utf-8")
        ohos = build.split('if env["platform"] == "openharmony"', 1)[1]
        self.assertIn('env_thirdparty.Append(CPPDEFINES=["GLAD_GLES2"])', ohos)
        self.assertIn('thirdparty_dir + "gl.c", thirdparty_dir + "egl.c"', ohos)

    def test_rendering_probes_are_debug_only_and_opt_in(self):
        source = (ROOT / "drivers/gles3/rasterizer_gles3.cpp").read_text(encoding="utf-8")
        for setting in ("presentation_probe", "direct_blit_probe", "simple_copy_probe"):
            self.assertIn(f'get_setting("debug/openharmony/{setting}", false)', source)
        self.assertEqual(source.count("#if defined(OPENHARMONY_ENABLED) && defined(DEBUG_ENABLED)"), 4)
        effects = (ROOT / "drivers/gles3/effects/copy_effects.cpp").read_text(encoding="utf-8")
        self.assertEqual(effects.count("#if defined(OPENHARMONY_ENABLED) && defined(DEBUG_ENABLED)"), 2)
        self.assertEqual(effects.count('get_setting("debug/openharmony/copy_state_probe", false)'), 2)

    def test_presentation_probe_restores_bindings_and_uses_integer_mask_query(self):
        source = (ROOT / "platform/openharmony/display_server_openharmony.cpp").read_text(encoding="utf-8")
        swap = source.split("void DisplayServerOpenHarmony::swap_buffers()", 1)[1].split(
            "void DisplayServerOpenHarmony::release_rendering_thread()", 1
        )[0]
        self.assertIn(
            '#ifdef DEBUG_ENABLED\n\t\tif (ProjectSettings::get_singleton()->get_setting("debug/openharmony/presentation_probe", false))',
            swap,
        )
        self.assertIn("glGetIntegerv(GL_COLOR_WRITEMASK, color_mask)", swap)
        self.assertNotIn("glGetBooleanv", swap)
        for statement in (
            "glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer)",
            "glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo)",
            "glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo)",
            "glScissor(scissor[0], scissor[1], scissor[2], scissor[3])",
        ):
            self.assertIn(statement, swap)

    def test_canvas_uploads_avoid_emulator_buffer_mapping(self):
        source = (ROOT / "drivers/gles3/rasterizer_canvas_gles3.cpp").read_text(encoding="utf-8")
        guard = "#if defined(WEB_ENABLED) || defined(OPENHARMONY_ENABLED)"
        blocks = source.split(guard)[1:]
        self.assertEqual(len(blocks), 3)
        for block in blocks:
            upload, mapped = block.split("#endif", 1)[0].split("#else", 1)
            self.assertIn("_upload_canvas_buffer(", upload)
            self.assertNotIn("glMapBufferRange(", upload)
            self.assertIn("glMapBufferRange(", mapped)

    def test_canvas_allocation_zero_data_is_openharmony_only(self):
        source = (ROOT / "drivers/gles3/rasterizer_canvas_gles3.cpp").read_text(encoding="utf-8")
        allocate = source.split("static void _allocate_canvas_buffer(", 1)[1].split(
            "static void _upload_canvas_buffer(", 1
        )[0]
        self.assertIn("const void *initial_data = nullptr;\n#ifdef OPENHARMONY_ENABLED", allocate)
        self.assertIn("zero_data.resize(p_size);\n\tzero_data.fill(0);\n\tinitial_data = zero_data.ptr();", allocate)
        self.assertIn("buffer_allocate_data(p_target, p_id, p_size, initial_data, GL_STREAM_DRAW, p_name)", allocate)
        self.assertEqual(source.count("_allocate_canvas_buffer("), 5)
        allocations = source.split("void RasterizerCanvasGLES3::_allocate_instance_data_buffer()", 1)[1].split(
            "void RasterizerCanvasGLES3::set_time(", 1
        )[0]
        self.assertEqual(allocations.count("data.max_instance_buffer_size,"), 2)
        self.assertIn("sizeof(LightUniform) * data.max_lights_per_render,", allocations)
        self.assertIn("sizeof(StateBuffer),", allocations)
        self.assertNotIn("nullptr", allocations)

    def test_canvas_upload_diagnostics_preserve_offsets_and_limit_logs(self):
        source = (ROOT / "drivers/gles3/rasterizer_canvas_gles3.cpp").read_text(encoding="utf-8")
        upload = source.split("static void _upload_canvas_buffer(", 1)[1].split(
            "void RasterizerCanvasGLES3::_update_transform_2d_to_mat4(", 1
        )[0]
        self.assertIn("glBufferSubData(p_target, p_offset, p_size, p_data);", upload)
        self.assertLess(upload.index('"before upload"'), upload.index("glBufferSubData("))
        self.assertLess(upload.index("glBufferSubData("), upload.index('"after upload"'))
        self.assertEqual(upload.count("#if defined(OPENHARMONY_ENABLED) && defined(DEBUG_ENABLED)"), 2)
        self.assertIn("if (reports < 16)", source)
        self.assertIn("GLenum error = glGetError();", source)

    def test_scene_output_is_unconditional_for_emulator_rewriting(self):
        source = (ROOT / "drivers/gles3/shaders/scene.glsl").read_text(encoding="utf-8")
        self.assertEqual(source.count("layout(location = 0) out vec4"), 1)
        self.assertIn(
            "layout(location = 0) out vec4 frag_color;\n#if defined(RENDER_MATERIAL)\n"
            "#define albedo_output_buffer frag_color",
            source,
        )
        self.assertIn("#elif defined(RENDER_MOTION_VECTORS)\n#define motion_vectors frag_color", source)

    def test_input_callbacks_only_enqueue_native_data(self):
        source = (ROOT / "platform/openharmony/bridge_openharmony.cpp").read_text(encoding="utf-8")
        callbacks = source.split("void godot_touch(", 1)[1].split("static void process_touch(", 1)[0]
        self.assertNotIn("Input::", callbacks)
        self.assertNotIn("instantiate()", callbacks)
        self.assertEqual(callbacks.count("MutexLock lock(godot_step_mutex);"), 3)
        self.assertEqual(callbacks.count("if (!input_ready || !p_event"), 3)
        self.assertEqual(callbacks.count("pending_input_events.push_back(event);"), 3)

    def test_input_gate_tracks_game_loop_lifetime(self):
        source = (ROOT / "platform/openharmony/bridge_openharmony.cpp").read_text(encoding="utf-8")
        finalize = source.split("void godot_finalize()", 1)[1].split("void godot_step(", 1)[0]
        self.assertLess(finalize.index("input_ready = false;"), finalize.index("Main::cleanup();"))
        self.assertLess(finalize.index("pending_input_events.clear();"), finalize.index("Main::cleanup();"))
        loop = source.split("void godot_step(", 1)[1].split("int64_t godot_init(", 1)[0]
        self.assertLess(
            loop.index("main_loop_begin();"), loop.index("input_ready = Input::get_singleton() != nullptr;")
        )
        self.assertLess(loop.index("godot_step_mutex.unlock();"), loop.index("process_touch(&event.touch, 1);"))
        self.assertLess(loop.index("process_key(&event.key);"), loop.index("main_loop_iterate()"))

    def test_ui_thread_releases_ownership_before_requesting_vsync(self):
        # Source-order guard only; actual callback threading requires an emulator/device test.
        source = (ROOT / "platform/openharmony/bridge_openharmony.cpp").read_text(encoding="utf-8")
        init = source.split("int64_t godot_init(", 1)[1].split("void godot_touch(", 1)[0]
        setup = init.index("Error err = Main::setup(")
        failure = init.index("if (err != OK) {", setup)
        failure_end = init.index("}", failure)
        release = init.index("Thread::release_main_thread();", failure_end)
        clear_access = init.index("set_current_thread_safe_for_nodes(false);", release)
        request = init.index("OH_NativeVSync_RequestFrame(")
        self.assertLess(clear_access, request)


class TemplateTests(unittest.TestCase):
    def test_architectures_and_mono_metadata(self):
        for arch in ("arm64-v8a", "x86_64"):
            for mono in (False, True):
                with self.subTest(arch=arch, mono=mono), tempfile.TemporaryDirectory() as temp:
                    archive = package_fixture(Path(temp), arch, mono)
                    with zipfile.ZipFile(archive) as bundle:
                        metadata = json.loads(bundle.read("godot_openharmony.json"))
                        self.assertEqual(
                            metadata,
                            {
                                "architecture": arch,
                                "variant": "debug",
                                "dotnet": mono,
                                "runtime": "leanclr",
                                "gles3": False,
                                "version": VERSION,
                            },
                        )
                        self.assertIn(f"entry/src/main/cpp/libs/{arch}/libgodot.so", bundle.namelist())
                        self.assertIn("entry/src/main/cpp/include/bridge_openharmony.h", bundle.namelist())
                        self.assertIn("AppScope/app.json5", bundle.namelist())
                        self.assertEqual(bundle.testzip(), None)


@unittest.skipUnless(Path(ENGINE).is_file(), "Set GODOT_OPENHARMONY_TEST_EDITOR to a built editor")
class ExportTests(unittest.TestCase):
    def export_fixture(
        self,
        *,
        template_arch="arm64-v8a",
        preset_arch="arm64",
        unsafe_path=False,
        template_variant="debug",
        release=False,
        gles3=False,
        renderer="mobile",
    ):
        temporary = tempfile.TemporaryDirectory(prefix="godot-ohos-test-")
        self.addCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        archive = package_fixture(folder, template_arch, variant=template_variant, gles3=gles3)
        if unsafe_path:
            with zipfile.ZipFile(archive, "a") as bundle:
                bundle.writestr("../escape.txt", "must not be extracted")
        (folder / "project.godot").write_text(
            f'config_version=5\n[application]\nconfig/name="OHOSTest"\n[rendering]\nrenderer/rendering_method="{renderer}"\nrenderer/rendering_method.mobile="{renderer}"\ntextures/vram_compression/import_etc2_astc=true\n',
            encoding="utf-8",
        )
        (folder / "Main.tscn").write_text('[gd_scene format=3]\n[node name="Main" type="Node"]\n', encoding="utf-8")
        (folder / "export_presets.cfg").write_text(
            '[preset.0]\nname="OpenHarmony"\nplatform="OpenHarmony"\nrunnable=false\nexport_filter="all_resources"\ninclude_filter=""\nexclude_filter=""\n'
            "[preset.0.options]\n"
            f'custom_template/debug="{archive.as_posix()}"\n'
            f"architectures/arm64={str(preset_arch == 'arm64').lower()}\n"
            f"architectures/x86_64={str(preset_arch == 'x86_64').lower()}\n"
            "build/export_project_only=true\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                ENGINE,
                "--headless",
                "--editor",
                "--path",
                str(folder),
                "--export-release" if release else "--export-debug",
                "OpenHarmony",
                str(folder / "result.hap"),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=90,
        )
        return result, folder

    def test_project_export_without_sdk(self):
        result, folder = self.export_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((folder / "result/entry/src/main/resources/rawfile/template.pck").is_file())
        self.assertTrue((folder / "result/entry/src/main/cpp/libs/arm64-v8a/libgodot.so").is_file())
        self.assertFalse((folder / "result.hap").exists(), "Project-only export must not claim to build a HAP")

    def test_compatibility_template_exports(self):
        result, folder = self.export_fixture(gles3=True, renderer="gl_compatibility")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((folder / "result/entry/src/main/resources/rawfile/template.pck").is_file())

    def test_compatibility_rejects_vulkan_only_template(self):
        result, folder = self.export_fixture(renderer="gl_compatibility")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not include GLES3", result.stdout + result.stderr)
        self.assertFalse((folder / "result/entry/src/main/resources/rawfile/template.pck").exists())

    def test_wrong_architecture_fails(self):
        result, folder = self.export_fixture(template_arch="x86_64")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("does not match", result.stdout + result.stderr)
        self.assertFalse((folder / "result/entry/src/main/resources/rawfile/template.pck").exists())

    def test_archive_path_traversal_fails(self):
        result, folder = self.export_fixture(unsafe_path=True)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Invalid path", result.stdout + result.stderr)
        self.assertFalse((folder / "escape.txt").exists())

    def test_wrong_build_mode_fails(self):
        result, folder = self.export_fixture(template_variant="release")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stdout + result.stderr)
        self.assertFalse((folder / "result/entry/src/main/resources/rawfile/template.pck").exists())


if __name__ == "__main__":
    unittest.main()
