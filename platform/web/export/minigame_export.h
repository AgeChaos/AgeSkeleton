#pragma once

#include "export_plugin.h"
#include "editor/export/portable_export_paths.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/zip_io.h"
#include "core/io/json.h"
#include "editor/file_system/editor_file_system.h"
#include "core/os/os.h"
#include "editor/themes/editor_scale.h"
#include "modules/svg/image_loader_svg.h"
#include "scene/resources/image_texture.h"
#include "wechat_svg.gen.h"
#include "douyin_svg.gen.h"

// Use Web runtime features, but package an official mini-game project instead of HTML.
class EditorExportPlatformMiniGame : public EditorExportPlatformWeb {
	GDCLASS(EditorExportPlatformMiniGame, EditorExportPlatformWeb);
	bool wechat = true;
	Ref<ImageTexture> minigame_logo;
	String absolute(const String &p_path) const { return ProjectSettings::get_singleton()->globalize_path(p_path); }
	static void collect_resource_graph(EditorFileSystemDirectory *p_dir, Dictionary &r_graph) {
		for (int i = 0; i < p_dir->get_file_count(); i++) {
			Array dependencies;
			for (const String &dep : p_dir->get_file_deps(i)) {
				int path_index = dep.find("res://");
				dependencies.push_back(path_index >= 0 ? dep.substr(path_index) : ResourceUID::ensure_path(dep.get_slice("::", 0)));
			}
			r_graph[p_dir->get_file_path(i)] = dependencies;
		}
		for (int i = 0; i < p_dir->get_subdir_count(); i++) collect_resource_graph(p_dir->get_subdir(i), r_graph);
	}
	static Error zip_directory(zipFile p_zip, const String &p_root, const String &p_relative = "") {
		Ref<DirAccess> dir = DirAccess::open(p_root.path_join(p_relative));
		ERR_FAIL_COND_V(dir.is_null(), ERR_CANT_OPEN);
		dir->list_dir_begin();
		for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
			if (name == "." || name == "..") continue;
			String relative = p_relative.path_join(name);
			if (dir->current_is_dir()) {
				Error err = zip_directory(p_zip, p_root, relative);
				if (err != OK) return err;
			} else {
				Ref<FileAccess> file = FileAccess::open(p_root.path_join(relative), FileAccess::READ);
				ERR_FAIL_COND_V(file.is_null(), ERR_CANT_OPEN);
				Vector<uint8_t> bytes = file->get_buffer(file->get_length());
				if (zipOpenNewFileInZip(p_zip, relative.utf8().get_data(), nullptr, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION) != ZIP_OK) return ERR_CANT_CREATE;
				int result = zipWriteInFileInZip(p_zip, bytes.ptr(), bytes.size());
				int closed = zipCloseFileInZip(p_zip);
				if (result != ZIP_OK || closed != ZIP_OK) return ERR_FILE_CANT_WRITE;
			}
		}
		return OK;
	}

protected:
	static void _bind_methods() {}

public:
	void initialize() override {
		EditorExportPlatformWeb::initialize();
		Ref<Image> icon;
		icon.instantiate();
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);
		ImageLoaderSVG::create_image_from_string(icon, wechat ? _web_wechat_svg : _web_douyin_svg, EDSCALE, upsample, false);
		minigame_logo = ImageTexture::create_from_image(icon);
	}
	Ref<Texture2D> get_logo() const override { return minigame_logo; }
	void set_wechat(bool p_wechat) { wechat = p_wechat; }
	String get_name() const override { return String::utf8(wechat ? "微信小游戏" : "抖音小游戏"); }
	bool poll_export() override { return false; }
	int get_options_count() const override { return 0; }
	void get_platform_features(List<String> *r_features) const override {
		r_features->push_back("web");
		r_features->push_back(wechat ? "wechat" : "douyin");
	}
	void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override {
		r_features->push_back("wasm32");
		r_features->push_back("nothreads");
		r_features->push_back("web_noextensions");
		r_features->push_back("s3tc");
		r_features->push_back("etc2");
	}
	void get_export_options(List<ExportOption> *r_options) const override {
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "minigame/sdk_directory", PROPERTY_HINT_GLOBAL_DIR), PortableExportPaths::sdk()));
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "template_build/enabled"), false, true));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "template_build/source_directory", PROPERTY_HINT_GLOBAL_DIR), ""));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "template_build/emsdk_directory", PROPERTY_HINT_GLOBAL_DIR), ""));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "template_build/python_executable", PROPERTY_HINT_GLOBAL_FILE), "python"));
		r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "template_build/jobs", PROPERTY_HINT_RANGE, "1,128,1"), 8));
		for (const String &feature : { String("3d"), String("physics_2d"), String("physics_3d"), String("navigation_2d"), String("navigation_3d"), String("advanced_gui") }) {
			r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "template_build/keep_" + feature), true));
		}
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "minigame/node_executable", PROPERTY_HINT_GLOBAL_FILE), "node"));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "minigame/app_id"), wechat ? "touristappid" : "testAppId"));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "minigame/orientation", PROPERTY_HINT_ENUM, "landscape,portrait"), "landscape"));
		r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "minigame/initial_resource_mode", PROPERTY_HINT_ENUM, String::utf8(wechat ? "小游戏包内,微信分包,远程 CDN" : "小游戏包内,抖音分包,远程 CDN")), 0));
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "minigame/compress_resources"), true));
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "minigame/wasm_subpackage"), false));
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "remote_resources/enabled"), false, true));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/cdn_base_url"), ""));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/directories"), "res://Assets/Res"));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/keep_paths"), "res://Assets/Bundles,res://Assets/Config,res://Assets/Resources"));
		for (const String &mode : { String("debug"), String("release") }) {
			String base = PortableExportPaths::web(mode);
			r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "minigame/" + mode + "_javascript", PROPERTY_HINT_GLOBAL_FILE, "*.js"), base + ".js"));
			r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "minigame/" + mode + "_wasm", PROPERTY_HINT_GLOBAL_FILE, "*.wasm"), base + ".wasm"));
		}
	}
	bool get_export_option_visibility(const EditorExportPreset *p_preset, const String &p_option) const override {
		if (p_option.begins_with("template_build/")) return p_option == "template_build/enabled" || bool(p_preset->get("template_build/enabled"));
		return !p_option.begins_with("remote_resources/") || p_option == "remote_resources/enabled" || bool(p_preset->get("remote_resources/enabled"));
	}
	bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override {
		int resource_mode = p_preset->get("minigame/initial_resource_mode");
		if (resource_mode < 0 || resource_mode > 2) {
			r_error += "Invalid mini-game resource mode.\n";
			return false;
		}
		if (bool(p_preset->get("remote_resources/enabled"))) {
			String url = p_preset->get("remote_resources/cdn_base_url");
			if (!url.begins_with("https://") || String(p_preset->get("remote_resources/directories")).strip_edges().is_empty()) {
				r_error += "Remote resources require an HTTPS CDN Base URL and comma-separated resource directories.\n";
				return false;
			}
		}
		if (String(ProjectSettings::get_singleton()->get_setting("leanclr/export/file_manifest", "")).is_empty()) {
			r_error += "Mini-game export requires a built LeanCLR project and leanclr/export/file_manifest.\n";
			return false;
		}
		return true;
	}
	bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override {
		r_missing_templates = false;
		String mode = p_debug ? "debug" : "release";
		if (bool(p_preset->get("template_build/enabled"))) {
			String sdk = absolute(p_preset->get("minigame/sdk_directory"));
			String source = PortableExportPaths::source(p_preset->get("template_build/source_directory"));
			String emsdk = PortableExportPaths::emsdk(p_preset->get("template_build/emsdk_directory"));
			if (!FileAccess::exists(sdk.path_join("shared/build-minigame-template.py"))) {
				r_error += "Missing SDK/shared/build-minigame-template.py. Set MiniGame SDK Directory.\n";
				return false;
			}
			if (!FileAccess::exists(source.path_join("SConstruct"))) {
				r_error += "Missing Godot source: " + source + ". Install matching source in BuildTools/godot beside the editor, or set Source Directory. Disable Template Build to use prebuilt templates.\n";
				return false;
			}
			if (!FileAccess::exists(emsdk.path_join(".emscripten")) || !FileAccess::exists(emsdk.path_join("upstream/emscripten/emcc.py"))) {
				r_error += "Missing or inactive Emscripten: " + emsdk + ". Install and activate the matching SDK in BuildTools/emsdk, or set Emsdk Directory.\n";
				return false;
			}
		}
		for (const String &key : { String("minigame/") + mode + "_javascript", String("minigame/") + mode + "_wasm" }) {
			if (!bool(p_preset->get("template_build/enabled")) && !FileAccess::exists(absolute(p_preset->get(key)))) {
				r_error += "Missing mini-game template: " + key + "\n";
				r_missing_templates = true;
			}
		}
		if (!FileAccess::exists(absolute(p_preset->get("minigame/sdk_directory")).path_join("shared/package-minigame.cjs"))) {
			r_error += "Missing TempSDK/shared/package-minigame.cjs. Set MiniGame SDK Directory.\n";
			return false;
		}
		return !r_missing_templates;
	}
	List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override { List<String> result; result.push_back("zip"); return result; }
	Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<DebugFlags> p_flags = 0, bool p_notify = true) override {
		String configuration_error;
		if (!has_valid_project_configuration(p_preset, configuration_error)) {
			add_message(EXPORT_MESSAGE_ERROR, get_name(), configuration_error);
			return ERR_INVALID_PARAMETER;
		}
		String sdk = absolute(p_preset->get("minigame/sdk_directory"));
		String work = sdk.path_join(".godot-export/" + itos(OS::get_singleton()->get_process_id()) + "-" + itos(OS::get_singleton()->get_ticks_usec()));
		Error err = DirAccess::make_dir_recursive_absolute(work);
		if (err != OK) return err;
		String pack = work.path_join("game.pck");
		err = export_pack(p_preset, p_debug, pack, p_flags);
		if (err != OK) return err;
		String output = work.path_join("project");
		String mode = p_debug ? "debug" : "release";
		String engine_path = absolute(p_preset->get("minigame/" + mode + "_javascript"));
		String wasm_path = absolute(p_preset->get("minigame/" + mode + "_wasm"));
		if (bool(p_preset->get("template_build/enabled"))) {
			List<String> build_args;
			build_args.push_back(sdk.path_join("shared/build-minigame-template.py"));
			build_args.push_back("--source"); build_args.push_back(PortableExportPaths::source(p_preset->get("template_build/source_directory")));
			build_args.push_back("--emsdk"); build_args.push_back(PortableExportPaths::emsdk(p_preset->get("template_build/emsdk_directory")));
			build_args.push_back("--mode"); build_args.push_back(mode);
			build_args.push_back("--jobs"); build_args.push_back(itos(int(p_preset->get("template_build/jobs"))));
			build_args.push_back("--result"); build_args.push_back(work.path_join("template.json"));
			for (const String &feature : { String("3d"), String("physics_2d"), String("physics_3d"), String("navigation_2d"), String("navigation_3d"), String("advanced_gui") }) {
				build_args.push_back("--" + feature); build_args.push_back(bool(p_preset->get("template_build/keep_" + feature)) ? "yes" : "no");
			}
			print_line("Checking cached mini-game template; first build can take several minutes. Logs: " + sdk.path_join("WebGL/template-cache"));
			String build_log;
			int build_exit = -1;
			err = OS::get_singleton()->execute(p_preset->get("template_build/python_executable"), build_args, &build_log, &build_exit, true);
			if (err != OK || build_exit != 0) {
				add_message(EXPORT_MESSAGE_ERROR, get_name(), "Template build failed.\n" + build_log);
				return FAILED;
			}
			print_line(build_log);
			Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(work.path_join("template.json")));
			ERR_FAIL_COND_V(parsed.get_type() != Variant::DICTIONARY, ERR_PARSE_ERROR);
			Dictionary built = parsed;
			engine_path = built.get("engine", ""); wasm_path = built.get("wasm", "");
			ERR_FAIL_COND_V(!FileAccess::exists(engine_path) || !FileAccess::exists(wasm_path), ERR_FILE_NOT_FOUND);
		}
		List<String> args;
		args.push_back(sdk.path_join("shared/package-minigame.cjs"));
		args.push_back("--platform"); args.push_back(wechat ? "wechat" : "douyin");
		args.push_back("--engine"); args.push_back(engine_path);
		args.push_back("--wasm"); args.push_back(wasm_path);
		args.push_back("--pack"); args.push_back(pack);
		args.push_back("--output"); args.push_back(output);
		args.push_back("--appid"); args.push_back(p_preset->get("minigame/app_id"));
		args.push_back("--orientation"); args.push_back(p_preset->get("minigame/orientation"));
		args.push_back("--project-name"); args.push_back(ProjectSettings::get_singleton()->get_setting("application/config/name", "Godot"));
		int resource_mode = p_preset->get("minigame/initial_resource_mode");
		args.push_back("--resource-mode"); args.push_back(resource_mode == 1 ? "subpackage" : "package");
		if (bool(p_preset->get("minigame/compress_resources"))) args.push_back("--compress-pack");
		if (bool(p_preset->get("minigame/wasm_subpackage"))) args.push_back("--wasm-subpackage");
		if (resource_mode == 2) {
			// CDN mode uses the existing dependency-graph split path below.
			if (!bool(p_preset->get("remote_resources/enabled"))) {
				add_message(EXPORT_MESSAGE_ERROR, get_name(), String::utf8("远程 CDN 模式需要开启 remote_resources/enabled 并填写 HTTPS 地址。"));
				return ERR_INVALID_PARAMETER;
			}
		}
		if (resource_mode == 2 && bool(p_preset->get("remote_resources/enabled"))) {
			Dictionary graph, resource_metadata;
			collect_resource_graph(EditorFileSystem::get_singleton()->get_filesystem(), graph);
			Array startup;
			startup.push_back(ResourceUID::ensure_path(ProjectSettings::get_singleton()->get_setting("application/run/main_scene", "")));
			for (const auto &entry : ProjectSettings::get_singleton()->get_autoload_list()) startup.push_back(ResourceUID::ensure_path(entry.value.path));
			resource_metadata["dependencies"] = graph; resource_metadata["startup"] = startup;
			String graph_path = work.path_join("resources.json");
			Ref<FileAccess> graph_file = FileAccess::open(graph_path, FileAccess::WRITE);
			ERR_FAIL_COND_V(graph_file.is_null(), ERR_CANT_CREATE);
			graph_file->store_string(JSON::stringify(resource_metadata)); graph_file.unref();
			args.push_back("--split-resources");
			args.push_back("--resource-graph"); args.push_back(graph_path);
			args.push_back("--cdn-base-url"); args.push_back(p_preset->get("remote_resources/cdn_base_url"));
			args.push_back("--remote-directories"); args.push_back(p_preset->get("remote_resources/directories"));
			args.push_back("--keep-paths"); args.push_back(p_preset->get("remote_resources/keep_paths"));
		}
		String log;
		int exit_code = -1;
		err = OS::get_singleton()->execute(p_preset->get("minigame/node_executable"), args, &log, &exit_code, true);
		if (err != OK || exit_code != 0) {
			add_message(EXPORT_MESSAGE_ERROR, get_name(), "Mini-game packaging failed. Check Node.js, SDK and compatible Wasm templates.\n" + log);
			ERR_PRINT(log);
			return FAILED;
		}
		Ref<FileAccess> zip_access;
		zlib_filefunc_def io = zipio_create_io(&zip_access);
		String archive = work.path_join("export.zip");
		zipFile zip = zipOpen2(archive.utf8().get_data(), APPEND_STATUS_CREATE, nullptr, &io);
		if (!zip) return ERR_CANT_CREATE;
		err = zip_directory(zip, output);
		int closed = zipClose(zip, nullptr);
		if (err != OK || closed != ZIP_OK) return ERR_FILE_CANT_WRITE;
		zip_access.unref();
		err = DirAccess::copy_absolute(archive, p_path);
		if (err != OK) return err;
		print_line("MINIGAME_EXPORT_OK " + get_name() + " " + p_path);
		Ref<DirAccess> cleanup = DirAccess::open(work);
		if (cleanup.is_valid()) cleanup->erase_contents_recursive();
		DirAccess::remove_absolute(work);
		return OK;
	}
};
