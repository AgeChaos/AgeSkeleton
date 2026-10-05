#include "agechaos_hub.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

Error agechaos_open_hub(const String &p_project) {
	String executable;
	String project_path;
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(OS::get_singleton()->get_config_path().path_join("AgeChaos/hub.cfg")) == OK) {
		executable = config->get_value("hub", "executable", "");
		project_path = config->get_value("hub", "project_path", "");
	}
	if (executable.is_empty()) {
		executable = OS::get_singleton()->get_executable_path().get_base_dir().path_join("Hub/AgeChaosHub.exe");
	}
	if (!FileAccess::exists(executable)) {
		OS::get_singleton()->alert(String::utf8("未找到 AgeChaos Hub。请先安装并启动 Hub，再从项目列表打开项目。\n也可以使用 --editor --path <项目目录> 直接打开已有项目。"), "AgeChaos Hub");
		return ERR_FILE_NOT_FOUND;
	}
	List<String> args;
	if (!project_path.is_empty()) { args.push_back("--path"); args.push_back(project_path); }
	if (!p_project.is_empty()) { args.push_back("--"); args.push_back("--import-project=" + p_project); }
	return OS::get_singleton()->create_process(executable, args);
}
