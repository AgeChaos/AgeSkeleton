#include "editor/export/portable_export_paths.h"
/**************************************************************************/
/*  export_plugin.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "export_plugin.h"
#include "editor/file_system/editor_file_system.h"

#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/io/zip_io.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_plugin.h"
#include "editor/file_system/editor_paths.h"
#include "editor/import/resource_importer_texture_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/image_texture.h"

#include "modules/modules_enabled.gen.h" // IWYU pragma: keep. For mono.
#include "modules/svg/image_loader_svg.h"

// The build layer only splits files; C# Loader selects the download policy.
static void collect_web_resource_graph(EditorFileSystemDirectory *p_dir, Dictionary &r_graph) {
    for (int i = 0; i < p_dir->get_file_count(); i++) {
        Array deps;
        for (const String &dep : p_dir->get_file_deps(i)) {
            int offset = dep.find("res://");
            deps.push_back(offset >= 0 ? dep.substr(offset) : ResourceUID::ensure_path(dep.get_slice("::", 0)));
        }
        r_graph[p_dir->get_file_path(i)] = deps;
    }
    for (int i = 0; i < p_dir->get_subdir_count(); i++) collect_web_resource_graph(p_dir->get_subdir(i), r_graph);
}

bool EditorExportPlatformWeb::_is_dotnet_project() const {
#ifdef MODULE_MONO_ENABLED
	Ref<EditorExportPlatform> platform(const_cast<EditorExportPlatformWeb *>(this));
	for (const Ref<EditorExportPlugin> &plugin : EditorExport::get_singleton()->get_export_plugins()) {
		if (plugin->get_name() == "C#" && plugin->get_export_features(platform, false).has("dotnet")) {
			return true;
		}
	}
#endif
	return false;
}

bool EditorExportPlatformWeb::_validate_dotnet_options(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	if (!_is_dotnet_project()) {
		return true;
	}
	if ((bool)p_preset->get("variant/thread_support") || (bool)p_preset->get("variant/extensions_support")) {
		r_error += TTR("The experimental .NET Web template does not support threads or GDExtension export.") + "\n";
		return false;
	}
	return true;
}

Error EditorExportPlatformWeb::_extract_template(const String &p_template, const String &p_dir, const String &p_name, bool pwa, bool p_debug, bool p_hash_names) {
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile pkg = unzOpen2(p_template.utf8().get_data(), &io);

	if (!pkg) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Could not open template for export: \"%s\"."), p_template));
		return ERR_FILE_NOT_FOUND;
	}
	if (_is_dotnet_project() && unzLocateFile(pkg, "mono-web-template.json", 1) != UNZ_OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), TTR("This project requires a .NET Web template; a standard Web template cannot run C# assemblies."));
		unzClose(pkg);
		return ERR_FILE_UNRECOGNIZED;
	}
	if (_is_dotnet_project()) {
		unz_file_info info;
		bool valid = unzGetCurrentFileInfo(pkg, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK;
		valid = valid && info.uncompressed_size > 0 && info.uncompressed_size < 16384;
		Dictionary manifest;
		if (valid && unzOpenCurrentFile(pkg) == UNZ_OK) {
			Vector<uint8_t> data;
			data.resize(info.uncompressed_size);
			int read = unzReadCurrentFile(pkg, data.ptrw(), data.size());
			int close_error = unzCloseCurrentFile(pkg);
			if (read == data.size() && close_error == UNZ_OK) {
				Variant parsed = JSON::parse_string(String::utf8((const char *)data.ptr(), data.size()));
				if (parsed.get_type() == Variant::DICTIONARY) {
					manifest = parsed;
				}
			}
		}
		if ((int)manifest.get("format", 0) != 1 || String(manifest.get("godot", "")) != GODOT_VERSION_NUMBER ||
				String(manifest.get("configuration", "")) != (p_debug ? "debug" : "release") ||
				String(manifest.get("runtime_version", "")) != "10.0.11" || String(manifest.get("arch", "")) != "wasm32" || (bool)manifest.get("threads", true)) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), TTR("The .NET Web template version, build configuration, or runtime does not match this editor."));
			unzClose(pkg);
			return ERR_FILE_UNRECOGNIZED;
		}
		if (pwa && !(bool)manifest.get("pwa", false)) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), TTR("This .NET Web template predates PWA support. Install matching PWA-capable templates."));
			unzClose(pkg);
			return ERR_FILE_UNRECOGNIZED;
		}
		if (p_hash_names && !(bool)manifest.get("name_files_as_hashes", false)) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), TTR("This .NET Web template does not support hashed game file names. Install matching templates or disable Name Files As Hashes."));
			unzClose(pkg);
			return ERR_FILE_UNRECOGNIZED;
		}
	}

	if (unzGoToFirstFile(pkg) != UNZ_OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Invalid export template: \"%s\"."), p_template));
		unzClose(pkg);
		return ERR_FILE_CORRUPT;
	}

	do {
		//get filename
		unz_file_info info;
		char fname[16384];
		unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);

		String file = String::utf8(fname);

		// Skip folders.
		if (file.ends_with("/")) {
			continue;
		}
		if (file.is_empty() || file == "." || file == ".." || file.is_absolute_path() || file.contains(":") || file.contains("\\") || file.simplify_path() != file || file.begins_with("../")) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), TTR("Invalid path in export template."));
			unzClose(pkg);
			return ERR_FILE_BAD_PATH;
		}

		// Skip service worker and offline page if not exporting pwa.
		if (!pwa && (file == "godot.service.worker.js" || file == "godot.offline.html")) {
			continue;
		}
		Vector<uint8_t> data;
		data.resize(info.uncompressed_size);

		//read
		unzOpenCurrentFile(pkg);
		unzReadCurrentFile(pkg, data.ptrw(), data.size());
		unzCloseCurrentFile(pkg);

		//write
		if (!file.contains("/") && file.begins_with("godot.")) {
			file = p_name + file.substr(5);
		}
		String dst = p_dir.path_join(file);
		Error mkdir_error = DirAccess::make_dir_recursive_absolute(dst.get_base_dir());
		if (mkdir_error != OK) {
			unzClose(pkg);
			return mkdir_error;
		}
		Ref<FileAccess> f = FileAccess::open(dst, FileAccess::WRITE);
		if (f.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Could not write file: \"%s\"."), dst));
			unzClose(pkg);
			return ERR_FILE_CANT_WRITE;
		}
		f->store_buffer(data.ptr(), data.size());

	} while (unzGoToNextFile(pkg) == UNZ_OK);
	unzClose(pkg);
	return OK;
}

Error EditorExportPlatformWeb::_write_or_error(const uint8_t *p_content, int p_size, String p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), p_path));
		return ERR_FILE_CANT_WRITE;
	}
	f->store_buffer(p_content, p_size);
	return OK;
}

void EditorExportPlatformWeb::_replace_strings(const HashMap<String, String> &p_replaces, Vector<uint8_t> &r_template) {
	String str_template = String::utf8(reinterpret_cast<const char *>(r_template.ptr()), r_template.size());
	String out;
	Vector<String> lines = str_template.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String current_line = lines[i];
		for (const KeyValue<String, String> &E : p_replaces) {
			current_line = current_line.replace(E.key, E.value);
		}
		out += current_line + "\n";
	}
	CharString cs = out.utf8();
	r_template.resize(cs.length());
	for (int i = 0; i < cs.length(); i++) {
		r_template.write[i] = cs[i];
	}
}

Error EditorExportPlatformWeb::_hash_export_files(const String &p_dir, const String &p_name, Vector<SharedObject> &r_shared_objects, Dictionary &r_names) {
	const String pack = p_name + ".pck";
	const String pack_hash = FileAccess::get_sha256(p_dir.path_join(pack));
	ERR_FAIL_COND_V(pack_hash.is_empty(), ERR_FILE_CANT_READ);
	r_names[pack] = p_name + "." + pack_hash + ".pck";
	if (_is_dotnet_project()) {
		const String directory = p_name + ".managed";
		Vector<String> files;
		for (const SharedObject &object : r_shared_objects) {
			const String path = object.target.path_join(object.path.get_file());
			ERR_FAIL_COND_V(!path.begins_with(directory + "/"), ERR_FILE_BAD_PATH);
			files.push_back(path.substr(directory.length() + 1));
		}
		files.sort();
		String fingerprint;
		for (const String &file : files) {
			const String hash = FileAccess::get_sha256(p_dir.path_join(directory).path_join(file));
			ERR_FAIL_COND_V(hash.is_empty(), ERR_FILE_CANT_READ);
			fingerprint += file + ":" + hash + "\n";
		}
		const String hashed_directory = p_name + "." + fingerprint.sha256_text() + ".managed";
		r_names[directory] = hashed_directory;
		for (const String &file : files) {
			r_names[directory.path_join(file)] = hashed_directory.path_join(file);
		}
		for (SharedObject &object : r_shared_objects) {
			object.target = hashed_directory + object.target.substr(directory.length());
		}
	} else {
		// The existing Web loader resolves these files from one executable stem.
		Vector<String> suffixes;
		String fingerprint;
		for (const String &suffix : { ".js", ".wasm", ".audio.worklet.js", ".audio.position.worklet.js", ".side.wasm" }) {
			const String path = p_dir.path_join(p_name + suffix);
			if (FileAccess::exists(path)) {
				const String hash = FileAccess::get_sha256(path);
				ERR_FAIL_COND_V(hash.is_empty(), ERR_FILE_CANT_READ);
				fingerprint += suffix + ":" + hash + "\n";
				suffixes.push_back(suffix);
			}
		}
		const String stem = p_name + "." + fingerprint.sha256_text();
		for (const String &suffix : suffixes) {
			r_names[p_name + suffix] = stem + suffix;
		}
	}
	for (const Variant &key : r_names.keys()) {
		const String source = p_dir.path_join(String(key));
		if (!FileAccess::exists(source)) {
			continue; // Directory entries are configuration mappings, not files.
		}
		const String destination = p_dir.path_join(String(r_names[key]));
		Error err = DirAccess::make_dir_recursive_absolute(destination.get_base_dir());
		ERR_FAIL_COND_V(err != OK, err);
		if (FileAccess::exists(destination)) {
			ERR_FAIL_COND_V(FileAccess::get_sha256(source) != FileAccess::get_sha256(destination), ERR_ALREADY_EXISTS);
			err = DirAccess::remove_absolute(source);
		} else {
			err = DirAccess::rename_absolute(source, destination);
		}
		ERR_FAIL_COND_V(err != OK, err);
	}
	return OK;
}

void EditorExportPlatformWeb::_fix_html(Vector<uint8_t> &p_html, const Ref<EditorExportPreset> &p_preset, const String &p_name, bool p_debug, BitField<EditorExportPlatform::DebugFlags> p_flags, const Vector<SharedObject> p_shared_objects, const Dictionary &p_file_sizes, const Dictionary &p_names) {
	// Engine.js config
	Dictionary config;
	Array libs;
	for (int i = 0; i < p_shared_objects.size(); i++) {
		libs.push_back(p_shared_objects[i].path.get_file());
	}
	Vector<String> flags = gen_export_flags(p_flags & (~DEBUG_FLAG_DUMB_CLIENT));
	Array args;
	for (int i = 0; i < flags.size(); i++) {
		args.push_back(flags[i]);
	}
	config["canvasResizePolicy"] = p_preset->get("html/canvas_resize_policy");
	config["experimentalVK"] = p_preset->get("html/experimental_virtual_keyboard");
	config["focusCanvas"] = p_preset->get("html/focus_canvas_on_start");
	config["gdextensionLibs"] = libs;
	config["executable"] = p_name;
	if (!p_names.is_empty()) {
		config["nameFilesAsHashes"] = true;
		config["mainPack"] = p_names.get(p_name + ".pck", p_name + ".pck");
		if (_is_dotnet_project()) {
			config["managedDirectory"] = p_names[p_name + ".managed"];
		} else {
			config["executable"] = String(p_names.get(p_name + ".wasm", p_name + ".wasm")).get_basename();
		}
	}
	config["args"] = args;
	config["fileSizes"] = p_file_sizes;
	config["ensureCrossOriginIsolationHeaders"] = (bool)p_preset->get("progressive_web_app/ensure_cross_origin_isolation_headers");

	config["godotPoolSize"] = p_preset->get("threads/godot_pool_size");
	config["emscriptenPoolSize"] = p_preset->get("threads/emscripten_pool_size");

	String head_include;
	if (p_preset->get("html/export_icon")) {
		head_include += "<link id=\"-gd-engine-icon\" rel=\"icon\" type=\"image/png\" href=\"" + p_name + ".icon.png\" />\n";
		head_include += "<link rel=\"apple-touch-icon\" href=\"" + p_name + ".apple-touch-icon.png\"/>\n";
	}
	if (p_preset->get("progressive_web_app/enabled")) {
		head_include += "<link rel=\"manifest\" href=\"" + p_name + ".manifest.json\">\n";
		config["serviceWorker"] = p_name + ".service.worker.js";
	}

	// Replaces HTML string
	const String str_config = Variant(config).to_json_string();
	const String custom_head_include = p_preset->get("html/head_include");
	HashMap<String, String> replaces;
	replaces["$GODOT_URL"] = p_names.get(p_name + ".js", p_name + ".js");
	replaces["$GODOT_PROJECT_NAME"] = get_project_setting(p_preset, "application/config/name");
	replaces["$GODOT_HEAD_INCLUDE"] = head_include + custom_head_include;
	replaces["$GODOT_CONFIG"] = str_config;
	replaces["$GODOT_SPLASH_COLOR"] = "#" + Color(get_project_setting(p_preset, "application/boot_splash/bg_color")).to_html(false);

	Vector<String> godot_splash_classes;
	godot_splash_classes.push_back("show-image--" + String(get_project_setting(p_preset, "application/boot_splash/show_image")));
	RSE::SplashStretchMode boot_splash_stretch_mode = get_project_setting(p_preset, "application/boot_splash/stretch_mode");
	godot_splash_classes.push_back("fullsize--" + String(((boot_splash_stretch_mode != RSE::SplashStretchMode::SPLASH_STRETCH_MODE_DISABLED) ? "true" : "false")));
	godot_splash_classes.push_back("use-filter--" + String(get_project_setting(p_preset, "application/boot_splash/use_filter")));
	replaces["$GODOT_SPLASH_CLASSES"] = String(" ").join(godot_splash_classes);
	replaces["$GODOT_SPLASH"] = p_name + ".png";

	if (p_preset->get("variant/thread_support")) {
		replaces["$GODOT_THREADS_ENABLED"] = "true";
	} else {
		replaces["$GODOT_THREADS_ENABLED"] = "false";
	}

	_replace_strings(replaces, p_html);
}

Error EditorExportPlatformWeb::_add_manifest_icon(const Ref<EditorExportPreset> &p_preset, const String &p_path, const String &p_icon, int p_size, Array &r_arr) {
	const String name = p_path.get_file().get_basename();
	const String icon_name = vformat("%s.%dx%d.png", name, p_size, p_size);
	const String icon_dest = p_path.get_base_dir().path_join(icon_name);

	Ref<Image> icon;
	if (!p_icon.is_empty()) {
		Error err = OK;
		icon = _load_icon_or_splash_image(p_icon, &err);
		if (err != OK || icon.is_null() || icon->is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Icon Creation"), vformat(TTR("Could not read file: \"%s\"."), p_icon));
			return err;
		}
		if (icon->get_width() != p_size || icon->get_height() != p_size) {
			icon->resize(p_size, p_size);
		}
	} else {
		icon = _get_project_icon(p_preset);
		icon->resize(p_size, p_size);
	}
	const Error err = icon->save_png(icon_dest);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Icon Creation"), vformat(TTR("Could not write file: \"%s\"."), icon_dest));
		return err;
	}
	Dictionary icon_dict;
	icon_dict["sizes"] = vformat("%dx%d", p_size, p_size);
	icon_dict["type"] = "image/png";
	icon_dict["src"] = icon_name;
	r_arr.push_back(icon_dict);
	return err;
}

Error EditorExportPlatformWeb::_build_pwa(const Ref<EditorExportPreset> &p_preset, const String p_path, const Vector<SharedObject> &p_shared_objects, const Dictionary &p_names) {
	String proj_name = get_project_setting(p_preset, "application/config/name");
	if (proj_name.is_empty()) {
		proj_name = "Godot Game";
	}

	// Service worker
	const String dir = p_path.get_base_dir();
	const String name = p_path.get_file().get_basename();
	bool extensions = (bool)p_preset->get("variant/extensions_support");
	bool ensure_crossorigin_isolation_headers = (bool)p_preset->get("progressive_web_app/ensure_cross_origin_isolation_headers");
	HashMap<String, String> replaces;
	replaces["___GODOT_VERSION___"] = String::num_int64(OS::get_singleton()->get_unix_time()) + "|" + String::num_int64(OS::get_singleton()->get_ticks_usec());
	replaces["___GODOT_NAME___"] = proj_name.substr(0, 16);
	replaces["___GODOT_OFFLINE_PAGE___"] = name + ".offline.html";
	replaces["___GODOT_ENSURE_CROSSORIGIN_ISOLATION_HEADERS___"] = ensure_crossorigin_isolation_headers ? "true" : "false";

	// Files cached during worker install.
	Array cache_files = {
		name + ".html",
		name + ".js",
		name + ".offline.html"
	};
	if (p_preset->get("html/export_icon")) {
		cache_files.push_back(name + ".icon.png");
		cache_files.push_back(name + ".apple-touch-icon.png");
	}

	cache_files.push_back(name + ".audio.worklet.js");
	cache_files.push_back(name + ".audio.position.worklet.js");
	for (int i = 0; i < cache_files.size(); i++) {
		cache_files[i] = p_names.get(cache_files[i], cache_files[i]);
	}
	replaces["___GODOT_CACHE___"] = Variant(cache_files).to_json_string();

	// Heavy files that are cached on demand.
	Array opt_cache_files = {
		name + ".wasm",
		name + ".pck"
	};
	if (extensions) {
		opt_cache_files.push_back(name + ".side.wasm");
		for (int i = 0; i < p_shared_objects.size(); i++) {
			opt_cache_files.push_back(p_shared_objects[i].path.get_file());
		}
	}
	for (int i = 0; i < opt_cache_files.size(); i++) {
		opt_cache_files[i] = p_names.get(opt_cache_files[i], opt_cache_files[i]);
	}
	replaces["___GODOT_OPT_CACHE___"] = Variant(opt_cache_files).to_json_string();

	const String sw_path = dir.path_join(name + ".service.worker.js");
	Vector<uint8_t> sw;
	{
		Ref<FileAccess> f = FileAccess::open(sw_path, FileAccess::READ);
		if (f.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), vformat(TTR("Could not read file: \"%s\"."), sw_path));
			return ERR_FILE_CANT_READ;
		}
		sw.resize(f->get_length());
		f->get_buffer(sw.ptrw(), sw.size());
	}
	_replace_strings(replaces, sw);
	Error err = _write_or_error(sw.ptr(), sw.size(), dir.path_join(name + ".service.worker.js"));
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}

	// Custom offline page
	const String offline_page = p_preset->get("progressive_web_app/offline_page");
	if (!offline_page.is_empty()) {
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		const String offline_dest = dir.path_join(name + ".offline.html");
		err = da->copy(ProjectSettings::get_singleton()->globalize_path(offline_page), offline_dest);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), vformat(TTR("Could not read file: \"%s\"."), offline_dest));
			return err;
		}
	}

	// Manifest
	const char *modes[4] = { "fullscreen", "standalone", "minimal-ui", "browser" };
	const char *orientations[3] = { "any", "landscape", "portrait" };
	const int display = CLAMP(int(p_preset->get("progressive_web_app/display")), 0, 4);
	const int orientation = CLAMP(int(p_preset->get("progressive_web_app/orientation")), 0, 3);

	Dictionary manifest;
	manifest["name"] = proj_name;
	manifest["start_url"] = "./" + name + ".html";
	if (_is_dotnet_project()) {
		manifest["id"] = "./" + name + ".html";
		manifest["scope"] = "./";
	}
	manifest["display"] = String::utf8(modes[display]);
	manifest["orientation"] = String::utf8(orientations[orientation]);
	manifest["background_color"] = "#" + p_preset->get("progressive_web_app/background_color").operator Color().to_html(false);

	Array icons_arr;
	const String icon144_path = p_preset->get("progressive_web_app/icon_144x144");
	err = _add_manifest_icon(p_preset, p_path, icon144_path, 144, icons_arr);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	const String icon180_path = p_preset->get("progressive_web_app/icon_180x180");
	err = _add_manifest_icon(p_preset, p_path, icon180_path, 180, icons_arr);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	const String icon512_path = p_preset->get("progressive_web_app/icon_512x512");
	err = _add_manifest_icon(p_preset, p_path, icon512_path, 512, icons_arr);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	if (_is_dotnet_project()) {
		err = _add_manifest_icon(p_preset, p_path, String(), 192, icons_arr);
		if (err != OK) {
			return err;
		}
	}
	manifest["icons"] = icons_arr;

	CharString cs = Variant(manifest).to_json_string().utf8();
	err = _write_or_error((const uint8_t *)cs.get_data(), cs.length(), dir.path_join(name + ".manifest.json"));
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}

	if (_is_dotnet_project()) {
		// Cache only this export's assets, never stale files left in the destination.
		Dictionary template_metadata = JSON::parse_string(FileAccess::get_file_as_string(dir.path_join("mono-web-template.json")));
		Array paths = template_metadata.get("assets", Array());
		if (paths.is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), TTR("The .NET Web template has no PWA asset inventory."));
			return ERR_FILE_CORRUPT;
		}
		paths.push_back(name + ".html");
		paths.push_back(p_names.get(name + ".pck", name + ".pck"));
		paths.push_back(name + ".manifest.json");
		paths.push_back(name + ".offline.html");
		paths.push_back(name + ".audio.worklet.js");
		paths.push_back(name + ".audio.position.worklet.js");
		if (p_preset->get("html/export_icon")) {
			paths.push_back(name + ".icon.png");
			paths.push_back(name + ".apple-touch-icon.png");
		}
		for (int i = 0; i < icons_arr.size(); i++) {
			Dictionary icon = icons_arr[i];
			paths.push_back(icon["src"]);
		}
		for (int i = 0; i < p_shared_objects.size(); i++) {
			paths.push_back(p_shared_objects[i].target.path_join(p_shared_objects[i].path.get_file()));
		}
		Array entries;
		for (int i = 0; i < paths.size(); i++) {
			String path = paths[i];
			if (path.is_empty() || path.is_absolute_path() || path.contains(":") || path.contains("\\") || path.simplify_path() != path || path.begins_with("../") || path == "..") {
				return ERR_FILE_BAD_PATH;
			}
			String digest = FileAccess::get_sha256(dir.path_join(path));
			if (digest.is_empty()) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), vformat(TTR("Could not read file: \"%s\"."), path));
				return ERR_FILE_CANT_READ;
			}
			Dictionary entry;
			entry["path"] = path;
			entry["sha256"] = digest;
			entries.push_back(entry);
		}
		HashMap<String, String> mono_replaces;
		mono_replaces["___GODOT_MONO_FILES___"] = Variant(entries).to_json_string();
		mono_replaces["___GODOT_MONO_ENTRY___"] = Variant(name + ".html").to_json_string();
		_replace_strings(mono_replaces, sw);
		err = _write_or_error(sw.ptr(), sw.size(), sw_path);
		if (err != OK) {
			return err;
		}
	}

	return OK;
}

void EditorExportPlatformWeb::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	if (p_preset->get("vram_texture_compression/for_desktop")) {
		r_features->push_back("s3tc");
		r_features->push_back("bptc");
	}
	if (p_preset->get("vram_texture_compression/for_mobile")) {
		r_features->push_back("etc2");
		r_features->push_back("astc");
	}
	if (p_preset->get("variant/thread_support").operator bool()) {
		r_features->push_back("threads");
	} else {
		r_features->push_back("nothreads");
	}
	if (p_preset->get("variant/extensions_support").operator bool()) {
		r_features->push_back("web_extensions");
	} else {
		r_features->push_back("web_noextensions");
	}
	r_features->push_back("wasm32");
}

void EditorExportPlatformWeb::get_export_options(List<ExportOption> *r_options) const {
    String sdk = PortableExportPaths::sdk();
    r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/sdk_directory", PROPERTY_HINT_GLOBAL_DIR), sdk));
    r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/node_executable", PROPERTY_HINT_GLOBAL_FILE), "node"));
    r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "remote_resources/enabled"), false, true));
    r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/cdn_base_url"), ""));
    r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/directories"), "res://Assets/Res"));
    r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "remote_resources/keep_paths"), "res://Assets/Bundles,res://Assets/Config,res://Assets/Resources"));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "variant/extensions_support"), false)); // GDExtension support.
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "variant/thread_support"), false, true)); // Thread support (i.e. run with or without COEP/COOP headers).
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "vram_texture_compression/for_desktop"), true)); // S3TC
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "vram_texture_compression/for_mobile"), false)); // ETC or ETC2, depending on renderer

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/export_icon"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/name_files_as_hashes"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "html/custom_html_shell", PROPERTY_HINT_FILE, "*.html"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "html/head_include", PROPERTY_HINT_MULTILINE_TEXT, "monospace,no_wrap"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "html/canvas_resize_policy", PROPERTY_HINT_ENUM, "None,Project,Adaptive"), 2));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/focus_canvas_on_start"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/experimental_virtual_keyboard"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "progressive_web_app/enabled"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "progressive_web_app/ensure_cross_origin_isolation_headers"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/offline_page", PROPERTY_HINT_FILE, "*.html"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "progressive_web_app/display", PROPERTY_HINT_ENUM, "Fullscreen,Standalone,Minimal UI,Browser"), 1));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "progressive_web_app/orientation", PROPERTY_HINT_ENUM, "Any,Landscape,Portrait"), 0));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_144x144", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_180x180", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_512x512", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::COLOR, "progressive_web_app/background_color", PROPERTY_HINT_COLOR_NO_ALPHA), Color()));

	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "threads/emscripten_pool_size"), 8));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "threads/godot_pool_size"), 4));
}

bool EditorExportPlatformWeb::get_export_option_visibility(const EditorExportPreset *p_preset, const String &p_option) const {
	bool advanced_options_enabled = p_preset->are_advanced_options_enabled();
	if (p_option == "custom_template/debug" || p_option == "custom_template/release") {
		return advanced_options_enabled;
	}

	if (p_option == "threads/godot_pool_size" || p_option == "threads/emscripten_pool_size") {
		return p_preset->get("variant/thread_support").operator bool();
	}

	return true;
}

String EditorExportPlatformWeb::get_name() const {
	return "Web";
}

String EditorExportPlatformWeb::get_os_name() const {
	return "Web";
}

Ref<Texture2D> EditorExportPlatformWeb::get_logo() const {
	return logo;
}

bool EditorExportPlatformWeb::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
	r_missing_templates = false;
	if (!_validate_dotnet_options(p_preset, r_error)) {
		return false;
	}

	String err;
	bool valid = false;
	bool extensions = (bool)p_preset->get("variant/extensions_support");
	bool thread_support = (bool)p_preset->get("variant/thread_support");

	// Custom templates take precedence over installed export templates.
	bool dvalid;
	bool rvalid;

	if (p_preset->get("custom_template/debug") != "") {
		dvalid = FileAccess::exists(p_preset->get("custom_template/debug"));
		if (!dvalid) {
			err += TTR("Custom debug template not found.") + "\n";
		}
	} else {
		dvalid = exists_export_template(_get_template_name(extensions, thread_support, true), &err);
	}
	if (p_preset->get("custom_template/release") != "") {
		rvalid = FileAccess::exists(p_preset->get("custom_template/release"));
		if (!rvalid) {
			err += TTR("Custom release template not found.") + "\n";
		}
	} else {
		rvalid = exists_export_template(_get_template_name(extensions, thread_support, false), &err);
	}

	valid = dvalid || rvalid;
	r_missing_templates = !valid;

	if (!err.is_empty()) {
		r_error = err;
	}

	return valid;
}

bool EditorExportPlatformWeb::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String err;
	bool valid = true;

	// Validate the project configuration.

	if (p_preset->get("vram_texture_compression/for_mobile")) {
		if (!ResourceImporterTextureSettings::should_import_etc2_astc()) {
			valid = false;
		}
	}

	if (!err.is_empty()) {
		r_error = err;
	}

	return valid;
}

List<String> EditorExportPlatformWeb::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	list.push_back("html");
	return list;
}

Error EditorExportPlatformWeb::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags, bool p_notify) {

	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags, p_notify);
	String configuration_error;
	if (!_validate_dotnet_options(p_preset, configuration_error)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), configuration_error);
		return ERR_UNAVAILABLE;
	}
	for (int i = 0; i < get_message_count(); i++) {
		if (get_message(i).msg_type == EXPORT_MESSAGE_ERROR) {
			return FAILED;
		}
	}

	const String custom_debug = p_preset->get("custom_template/debug");
	const String custom_release = p_preset->get("custom_template/release");
	const String custom_html = p_preset->get("html/custom_html_shell");
	const bool export_icon = p_preset->get("html/export_icon");
	const bool pwa = p_preset->get("progressive_web_app/enabled");

	const String base_dir = p_path.get_base_dir();
	const String base_path = p_path.get_basename();
	const String base_name = p_path.get_file().get_basename();

	if (!DirAccess::exists(base_dir)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Target folder does not exist or is inaccessible: \"%s\""), base_dir));
		return ERR_FILE_BAD_PATH;
	}

	// Find the correct template
	String template_path = p_debug ? custom_debug : custom_release;
	template_path = template_path.strip_edges();
	if (template_path.is_empty()) {
		bool extensions = (bool)p_preset->get("variant/extensions_support");
		bool thread_support = (bool)p_preset->get("variant/thread_support");
		template_path = find_export_template(_get_template_name(extensions, thread_support, p_debug));
	}

	if (!template_path.is_empty() && !FileAccess::exists(template_path)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Template file not found: \"%s\"."), template_path));
		return ERR_FILE_NOT_FOUND;
	}

	// Export pck and shared objects
	Vector<SharedObject> shared_objects;
	String pck_path = base_path + ".pck";
	Error error = save_pack(p_preset, p_debug, pck_path, &shared_objects);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), pck_path));
		return error;
	}
	for (int i = 0; i < get_message_count(); i++) {
		if (get_message(i).msg_type == EXPORT_MESSAGE_ERROR) {
			return FAILED;
		}
	}

	{
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		for (int i = 0; i < shared_objects.size(); i++) {
			String target = _is_dotnet_project() ? shared_objects[i].target : String();
			if (!target.is_empty() && (target.is_absolute_path() || target.contains(":") || target.contains("\\") || target.simplify_path() != target || target.begins_with("../") || target == "..")) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Invalid .NET payload destination."));
				return ERR_FILE_BAD_PATH;
			}
			String dst = base_dir.path_join(target).path_join(shared_objects[i].path.get_file());
			error = da->make_dir_recursive(dst.get_base_dir());
			if (error != OK) {
				return error;
			}
			error = da->copy(shared_objects[i].path, dst);
			if (error != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), shared_objects[i].path.get_file()));
				return error;
			}
		}
	}

	// Extract templates.
	error = _extract_template(template_path, base_dir, base_name, pwa, p_debug, p_preset->get("html/name_files_as_hashes"));
	if (error) {
		// Message is supplied by the subroutine method.
		return error;
	}

	Dictionary file_names;
	if (p_preset->get("html/name_files_as_hashes")) {
		error = _hash_export_files(base_dir, base_name, shared_objects, file_names);
		if (error != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Could not generate content-hashed Web files."));
			return error;
		}
		pck_path = base_dir.path_join(file_names[base_name + ".pck"]);
	}
	// Parse generated file sizes (pck and wasm, to help show a meaningful loading bar).
	Dictionary file_sizes;
	Ref<FileAccess> f = FileAccess::open(pck_path, FileAccess::READ);
	if (f.is_valid()) {
		file_sizes[pck_path.get_file()] = (uint64_t)f->get_length();
	}
	const String wasm_name = file_names.get(base_name + ".wasm", base_name + ".wasm");
	f = FileAccess::open(base_dir.path_join(wasm_name), FileAccess::READ);
	if (f.is_valid()) {
		file_sizes[wasm_name] = (uint64_t)f->get_length();
	}

	// Read the HTML shell file (custom or from template).
	const String html_path = custom_html.is_empty() ? base_path + ".html" : custom_html;
	Vector<uint8_t> html;
	f = FileAccess::open(html_path, FileAccess::READ);
	if (f.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not read HTML shell: \"%s\"."), html_path));
		return ERR_FILE_CANT_READ;
	}
	html.resize(f->get_length());
	f->get_buffer(html.ptrw(), html.size());
	f.unref(); // close file.

	// Generate HTML file with replaced strings.
	_fix_html(html, p_preset, base_name, p_debug, p_flags, shared_objects, file_sizes, file_names);
	Error err = _write_or_error(html.ptr(), html.size(), p_path);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	html.resize(0);

	// Export splash (why?)
	Ref<Image> splash = _get_project_splash(p_preset);
	const String splash_png_path = base_path + ".png";
	if (splash->save_png(splash_png_path) != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), splash_png_path));
		return ERR_FILE_CANT_WRITE;
	}

	// Save a favicon that can be accessed without waiting for the project to finish loading.
	// This way, the favicon can be displayed immediately when loading the page.
	if (export_icon) {
		Ref<Image> favicon = _get_project_icon(p_preset);
		const String favicon_png_path = base_path + ".icon.png";
		if (favicon->save_png(favicon_png_path) != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), favicon_png_path));
			return ERR_FILE_CANT_WRITE;
		}
		favicon->resize(180, 180);
		const String apple_icon_png_path = base_path + ".apple-touch-icon.png";
		if (favicon->save_png(apple_icon_png_path) != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), apple_icon_png_path));
			return ERR_FILE_CANT_WRITE;
		}
	}

	// Generate the PWA worker and manifest
	if (pwa) {
		err = _build_pwa(p_preset, p_path, shared_objects, file_names);
		if (err != OK) {
			// Message is supplied by the subroutine method.
			return err;
		}
	}

    if (!String(ProjectSettings::get_singleton()->get_setting("leanclr/export/file_manifest", "")).is_empty()) {
        String sdk = ProjectSettings::get_singleton()->globalize_path(p_preset->get("remote_resources/sdk_directory"));
        List<String> args;
        args.push_back(sdk.path_join("shared/package-web-resources.cjs"));
        args.push_back("--html"); args.push_back(p_path);
        args.push_back("--pack"); args.push_back(pck_path);
        args.push_back("--javascript"); args.push_back(base_dir.path_join(file_names.get(base_name + ".js", base_name + ".js")));
        args.push_back("--wasm"); args.push_back(base_dir.path_join(wasm_name));
        String graph_path = base_path + ".resource-graph.json";
        if (bool(p_preset->get("remote_resources/enabled"))) {
            if (pwa || bool(p_preset->get("html/name_files_as_hashes"))) {
                add_message(EXPORT_MESSAGE_ERROR, "Remote resources", "Split resources currently require PWA and Name Files As Hashes disabled.");
                return ERR_UNAVAILABLE;
            }
            Dictionary graph, data;
            collect_web_resource_graph(EditorFileSystem::get_singleton()->get_filesystem(), graph);
            Array startup;
            startup.push_back(ResourceUID::ensure_path(ProjectSettings::get_singleton()->get_setting("application/run/main_scene", "")));
            for (const auto &entry : ProjectSettings::get_singleton()->get_autoload_list()) startup.push_back(ResourceUID::ensure_path(entry.value.path));
            data["dependencies"] = graph; data["startup"] = startup;
            Ref<FileAccess> graph_file = FileAccess::open(graph_path, FileAccess::WRITE);
            ERR_FAIL_COND_V(graph_file.is_null(), ERR_CANT_CREATE);
            graph_file->store_string(JSON::stringify(data)); graph_file.unref();
            args.push_back("--split");
            args.push_back("--graph"); args.push_back(graph_path);
            args.push_back("--cdn"); args.push_back(p_preset->get("remote_resources/cdn_base_url"));
            args.push_back("--directories"); args.push_back(p_preset->get("remote_resources/directories"));
            args.push_back("--keep"); args.push_back(p_preset->get("remote_resources/keep_paths"));
        }
        String log; int exit_code = -1;
        Error result = OS::get_singleton()->execute(p_preset->get("remote_resources/node_executable"), args, &log, &exit_code, true);
        if (FileAccess::exists(graph_path)) DirAccess::remove_absolute(graph_path);
        if (result != OK || exit_code != 0) {
            add_message(EXPORT_MESSAGE_ERROR, "Remote resources", log); ERR_PRINT(log); return FAILED;
        }
        print_line(log);
    }

	return OK;
}

bool EditorExportPlatformWeb::poll_export() {
	Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_runnable_preset_for_platform(this);

	RemoteDebugState prev_remote_debug_state = remote_debug_state;
	remote_debug_state = REMOTE_DEBUG_STATE_UNAVAILABLE;

	if (preset.is_valid()) {
		const bool debug = true;
		// Throwaway variables to pass to validation functions.
		String err;
		bool missing_templates;

		bool valid = has_valid_export_configuration(preset, err, missing_templates, debug) &&
				has_valid_project_configuration(preset, err);

		if (valid) {
			if (server->is_listening()) {
				remote_debug_state = REMOTE_DEBUG_STATE_SERVING;
			} else {
				remote_debug_state = REMOTE_DEBUG_STATE_AVAILABLE;
			}
		}
	}

	if (remote_debug_state != REMOTE_DEBUG_STATE_SERVING && server->is_listening()) {
		server->stop();
	}

	return remote_debug_state != prev_remote_debug_state;
}

Ref<Texture2D> EditorExportPlatformWeb::get_option_icon(int p_index) const {
	Ref<Texture2D> play_icon = EditorExportPlatform::get_option_icon(p_index);

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE: {
			return nullptr;
		} break;

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_index) {
				case 0:
				case 1:
					return play_icon;
				default:
					ERR_FAIL_V(nullptr);
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_index) {
				case 0:
					return play_icon;
				case 1:
					return restart_icon;
				case 2:
					return stop_icon;
				default:
					ERR_FAIL_V(nullptr);
			}
		} break;
	}

	return nullptr;
}

int EditorExportPlatformWeb::get_options_count() const {
	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE: {
			return 0;
		} break;

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			return 2;
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			return 3;
		} break;
	}

	return 0;
}

String EditorExportPlatformWeb::get_option_label(int p_index) const {
	String run_in_browser = TTR("Run in Browser");
	String start_http_server = TTR("Start HTTP Server");
	String reexport_project = TTR("Re-export Project");
	String stop_http_server = TTR("Stop HTTP Server");

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE:
			return "";

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return start_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return reexport_project;
				case 2:
					return stop_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;
	}

	return "";
}

String EditorExportPlatformWeb::get_option_tooltip(int p_index) const {
	String run_in_browser = TTR("Run exported HTML in the system's default browser.");
	String start_http_server = TTR("Start the HTTP server.");
	String reexport_project = TTR("Export project again to account for updates.");
	String stop_http_server = TTR("Stop the HTTP server.");

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE:
			return "";

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return start_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return reexport_project;
				case 2:
					return stop_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;
	}

	return "";
}

Error EditorExportPlatformWeb::run(const Ref<EditorExportPreset> &p_preset, int p_option, BitField<EditorExportPlatform::DebugFlags> p_debug_flags) {
	const uint16_t bind_port = EDITOR_GET("export/web/http_port");
	// Resolve host if needed.
	const String bind_host = EDITOR_GET("export/web/http_host");
	const bool use_tls = EDITOR_GET("export/web/use_tls");

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE: {
			return FAILED;
		} break;

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_option) {
				// Run in Browser.
				case 0: {
					Error err = _export_project(p_preset, p_debug_flags);
					if (err != OK) {
						return err;
					}
					err = _start_server(bind_host, bind_port, use_tls);
					if (err != OK) {
						return err;
					}
					return _launch_browser(bind_host, bind_port, use_tls);
				} break;

				// Start HTTP Server.
				case 1: {
					Error err = _export_project(p_preset, p_debug_flags);
					if (err != OK) {
						return err;
					}
					return _start_server(bind_host, bind_port, use_tls);
				} break;

				default: {
					ERR_FAIL_V_MSG(FAILED, vformat(R"(Invalid option "%s" for the current state.)", p_option));
				}
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_option) {
				// Run in Browser.
				case 0: {
					Error err = _export_project(p_preset, p_debug_flags);
					if (err != OK) {
						return err;
					}
					return _launch_browser(bind_host, bind_port, use_tls);
				} break;

				// Re-export Project.
				case 1: {
					return _export_project(p_preset, p_debug_flags);
				} break;

				// Stop HTTP Server.
				case 2: {
					return _stop_server();
				} break;

				default: {
					ERR_FAIL_V_MSG(FAILED, vformat(R"(Invalid option "%s" for the current state.)", p_option));
				}
			}
		} break;
	}

	return FAILED;
}

Error EditorExportPlatformWeb::_export_project(const Ref<EditorExportPreset> &p_preset, int p_debug_flags) {
	const String dest = EditorPaths::get_singleton()->get_temp_dir().path_join("web");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (!da->dir_exists(dest)) {
		Error err = da->make_dir_recursive(dest);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Could not create HTTP server directory: %s."), dest));
			return err;
		}
	}

	const String basepath = dest.path_join("tmp_js_export");
	Error err = export_project(p_preset, true, basepath + ".html", p_debug_flags);
	if (err != OK) {
		// Export generates several files, clean them up on failure.
		DirAccess::remove_file_or_error(basepath + ".html");
		DirAccess::remove_file_or_error(basepath + ".offline.html");
		DirAccess::remove_file_or_error(basepath + ".js");
		DirAccess::remove_file_or_error(basepath + ".audio.worklet.js");
		DirAccess::remove_file_or_error(basepath + ".audio.position.worklet.js");
		DirAccess::remove_file_or_error(basepath + ".service.worker.js");
		DirAccess::remove_file_or_error(basepath + ".pck");
		DirAccess::remove_file_or_error(basepath + ".png");
		DirAccess::remove_file_or_error(basepath + ".side.wasm");
		DirAccess::remove_file_or_error(basepath + ".wasm");
		DirAccess::remove_file_or_error(basepath + ".icon.png");
		DirAccess::remove_file_or_error(basepath + ".apple-touch-icon.png");
	}
	return err;
}

Error EditorExportPlatformWeb::_launch_browser(const String &p_bind_host, const uint16_t p_bind_port, const bool p_use_tls) {
	OS::get_singleton()->shell_open(String((p_use_tls ? "https://" : "http://") + p_bind_host + ":" + itos(p_bind_port) + "/tmp_js_export.html"));
	// FIXME: Find out how to clean up export files after running the successfully
	// exported game. Might not be trivial.
	return OK;
}

Error EditorExportPlatformWeb::_start_server(const String &p_bind_host, const uint16_t p_bind_port, const bool p_use_tls) {
	IPAddress bind_ip;
	if (p_bind_host.is_valid_ip_address()) {
		bind_ip = p_bind_host;
	} else {
		bind_ip = IP::get_singleton()->resolve_hostname(p_bind_host);
	}
	ERR_FAIL_COND_V_MSG(!bind_ip.is_valid(), ERR_INVALID_PARAMETER, "Invalid editor setting 'export/web/http_host': '" + p_bind_host + "'. Try using '127.0.0.1'.");

	const String tls_key = EDITOR_GET("export/web/tls_key");
	const String tls_cert = EDITOR_GET("export/web/tls_certificate");

	// Restart server.
	server->stop();
	Error err = server->listen(p_bind_port, bind_ip, p_use_tls, tls_key, tls_cert);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Error starting HTTP server: %d."), err));
	}
	return err;
}

Error EditorExportPlatformWeb::_stop_server() {
	server->stop();
	return OK;
}

Ref<Texture2D> EditorExportPlatformWeb::get_run_icon() const {
	return run_icon;
}

void EditorExportPlatformWeb::initialize() {
	if (EditorNode::get_singleton()) {
		server.instantiate();

		Ref<Image> img = memnew(Image);
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

		ImageLoaderSVG::create_image_from_string(img, _web_logo_svg, EDSCALE, upsample, false);
		logo = ImageTexture::create_from_image(img);

		ImageLoaderSVG::create_image_from_string(img, _web_run_icon_svg, EDSCALE, upsample, false);
		run_icon = ImageTexture::create_from_image(img);

		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid()) {
			stop_icon = theme->get_icon(SNAME("Stop"), EditorStringName(EditorIcons));
			restart_icon = theme->get_icon(SNAME("Reload"), EditorStringName(EditorIcons));
		} else {
			stop_icon.instantiate();
			restart_icon.instantiate();
		}
	}
}

EditorExportPlatformWeb::~EditorExportPlatformWeb() {
}
