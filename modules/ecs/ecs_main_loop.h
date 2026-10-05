// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "ecs_scene.h"
#include "ecs_ui_system.h"
#include "ecs_world.h"
#ifdef TOOLS_ENABLED
#include "ecs_ai_runtime.h"
#endif

#include "core/os/main_loop.h"

#include "modules/modules_enabled.gen.h"
#ifdef MODULE_LEANCLR_ENABLED
#include "modules/leanclr/leanclr_host.h"

#include <memory>
#endif
#include "scene/resources/3d/primitive_meshes.h"
class ECSMainLoop : public MainLoop {
	GDCLASS(ECSMainLoop, MainLoop);
	Ref<ECSWorld> world;
#ifdef TOOLS_ENABLED
	Ref<ECSAIRuntime> ai_runtime;
	PackedInt64Array editor_scene_entities;
	bool scene_preview_requested = false;
	void send_scene_preview();
	int editor_inspect_entity = -1;
	Dictionary live_definition(int p_index) const;
	bool live_set(int p_index, const String &p_property, const Variant &p_value, bool p_global);
#endif
	Ref<ECSScene> scene_resource;
	Transform3D active_camera;
	ECSUISystem ui_system;
#ifdef MODULE_LEANCLR_ENABLED
	std::unique_ptr<LeanCLRHost> game_host;
#endif
	bool game_initialized = false;
	bool game_reload_pending = false;
	void reload_game();
	void start_game();
	bool game_callback(const String &p_method, const Array &p_arguments = Array(), bool p_cleanup = false);
	void input_event(const Ref<InputEvent> &p_event);
	void input_text(const String &p_text);
	Size2i get_ui_viewport_size() const;
	Size2i preview_resolution;
	Ref<BoxMesh> mesh;
	RID viewport, scenario, camera;
	struct RenderGroup {
		Ref<Mesh> mesh;
		Ref<Material> material;
		RID instance, multimesh;
		uint64_t entity = 0;
		int count = -1;
	};
	Vector<RenderGroup> render_groups;
	void update_rendering();
	bool quitting = false;
	bool editor_paused = false;
	bool benchmark = false;
	uint64_t previous_frame_ticks = 0, benchmark_started = 0;
	Vector<double> frame_samples, physics_samples, extraction_samples, gpu_samples;
	void report_benchmark();
	double elapsed = 0, debug_elapsed = 0;
	int frames = 0, count = 0, inspect_offset = 0;
	void window_event(int p_event);
	static Error debug_capture(void *p_user, const String &p_message, const Array &p_args, bool &r_captured);
	void send_snapshot();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	Ref<ECSWorld> get_world() const { return world; }
	void quit(int p_exit_code = 0);
	bool request_game_reload();
	void set_camera_transform(const Transform3D &p_transform);
	Error save_scene(const String &p_path);
	void initialize() override;
	bool physics_process(double p_delta) override;
	bool process(double p_delta) override;
	void finalize() override;
};
