#pragma once

#include "core/io/file_access.h"
#include "core/config/project_settings.h"
#include "core/os/os.h"

class PortableExportPaths {
public:
	static String base() {
		String directory = OS::get_singleton()->get_executable_path().get_base_dir();
#ifdef MACOS_ENABLED
		if (directory.ends_with(".app/Contents/MacOS")) {
			return directory.path_join("../../..").simplify_path();
		}
#endif
		return directory;
	}
	// Empty preset values are resolved at use time, so moving the editor remains safe.
	static String source(const String &p_path) {
		return p_path.is_empty() ? base().path_join("BuildTools/godot") : ProjectSettings::get_singleton()->globalize_path(p_path);
	}
	static String emsdk(const String &p_path) {
		return p_path.is_empty() ? base().path_join("BuildTools/emsdk") : ProjectSettings::get_singleton()->globalize_path(p_path);
	}
	static String sdk() {
		String bundled = base().path_join("SDK");
		return FileAccess::exists(bundled.path_join("shared/package-minigame.cjs")) ? bundled : base().path_join("../SDK").simplify_path();
	}
	static String templates() {
		return FileAccess::exists(base().path_join("../SConstruct")) ? base().path_join("../Template").simplify_path() : base().path_join("Template");
	}
	static String web(const String &p_mode) {
		return templates().path_join("Web/" + p_mode + "/template");
	}
};
