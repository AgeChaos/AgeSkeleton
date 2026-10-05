#pragma once

#include "editor_export_platform.h"
#include "portable_export_paths.h"
#include "editor_export_preset.h"
#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "editor/settings/editor_settings.h"

class NativeTemplateBuild {
public:
	static void options(List<EditorExportPlatform::ExportOption> *r_options) {
		r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, "template_build/enabled"), false, true));
		r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::STRING, "template_build/sdk_directory", PROPERTY_HINT_GLOBAL_DIR), PortableExportPaths::sdk()));
		r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::STRING, "template_build/source_directory", PROPERTY_HINT_GLOBAL_DIR), ""));
		r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::STRING, "template_build/python_executable", PROPERTY_HINT_GLOBAL_FILE), "python"));
		r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::INT, "template_build/jobs", PROPERTY_HINT_RANGE, "1,128,1"), 8));
		for (const String &feature : { String("3d"), String("physics_2d"), String("physics_3d"), String("navigation_2d"), String("navigation_3d"), String("advanced_gui") }) {
			r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, "template_build/keep_" + feature), true));
		}
	}
	static bool enabled(const Ref<EditorExportPreset> &p_preset) { return p_preset->has("template_build/enabled") && bool(p_preset->get("template_build/enabled")); }
	static Error prepare(EditorExportPlatform *p_platform, const Ref<EditorExportPreset> &p_preset, const String &p_target, bool p_debug, Ref<EditorExportPreset> &r_preset) {
		Dictionary request;
		String source = PortableExportPaths::source(p_preset->get("template_build/source_directory"));
		if (!FileAccess::exists(source.path_join("SConstruct"))) {
			p_platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Template Build", "Missing Godot source: " + source + ". Install matching source in BuildTools/godot beside the editor, or set Source Directory. Disable Template Build to use prebuilt templates.");
			return ERR_FILE_NOT_FOUND;
		}
		request["platform"] = p_target;
		request["mode"] = p_debug ? "debug" : "release";
		Dictionary options;
		for (const KeyValue<StringName, Variant> &entry : p_preset->get_values()) {
			String key = entry.key;
			if (key.begins_with("template_build/") || key.begins_with("architectures/") || key == "binary_format/architecture" || key == "gradle_build/use_gradle_build") options[key] = p_preset->get(entry.key);
		}
		String key = p_debug ? "custom_template/debug" : "custom_template/release";
		if (p_target == "android" && bool(p_preset->get("gradle_build/use_gradle_build"))) key = "gradle_build/android_source_template";
		String base_template = p_preset->get(key);
		request["base_template"] = base_template.is_empty() ? String() : ProjectSettings::get_singleton()->globalize_path(base_template);
		request["options"] = options;
		options["template_build/source_directory"] = source;
		if (p_target == "android") request["android_sdk_path"] = EditorSettings::get_singleton()->get("export/android/android_sdk_path");
		Error temporary_error = OK;
		Ref<FileAccess> input = FileAccess::create_temp(FileAccess::WRITE_READ, "native-template", "json", true, &temporary_error);
		ERR_FAIL_COND_V(input.is_null(), ERR_CANT_CREATE);
		input->store_string(JSON::stringify(request)); input->flush();
		String input_path = input->get_path_absolute();
		input.unref();
		List<String> args;
		String sdk = ProjectSettings::get_singleton()->globalize_path(p_preset->get("template_build/sdk_directory"));
		args.push_back(sdk.path_join("shared/build-native-template.py")); args.push_back("--request"); args.push_back(input_path);
		String log;
		int exit_code = -1;
		print_line("Building/checking native template cache: " + p_target);
		Error err = OS::get_singleton()->execute(p_preset->get("template_build/python_executable"), args, &log, &exit_code, true);
		if (err != OK || exit_code != 0) {
			DirAccess::remove_absolute(input_path);
			p_platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "Template Build", log);
			return FAILED;
		}
		Variant result = JSON::parse_string(FileAccess::get_file_as_string(input_path));
		DirAccess::remove_absolute(input_path);
		ERR_FAIL_COND_V(result.get_type() != Variant::DICTIONARY, ERR_PARSE_ERROR);
		Dictionary built = result;
		String path = built.get("template", "");
		ERR_FAIL_COND_V(!FileAccess::exists(path), ERR_FILE_NOT_FOUND);
		print_line(log);
		r_preset = p_preset->copy_for_template(key, path);
		if (built.has("gradle_directory")) r_preset = r_preset->copy_for_template("gradle_build/gradle_build_directory", built["gradle_directory"]);
		return OK;
	}
};
