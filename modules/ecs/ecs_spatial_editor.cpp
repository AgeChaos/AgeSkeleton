#include "ecs_tool_adapters.h"
#ifdef TOOLS_ENABLED
#include "ecs_spatial_editor.h"

#include "core/input/input_event.h"
#include "core/math/geometry_2d.h"
#include "core/object/callable_mp.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/material.h"
#include "scene/resources/sky.h"
#include "scene/theme/theme_db.h"
#include "servers/rendering/rendering_server.h"

void ECSSpatialEditor::_bind_methods() {
	ADD_SIGNAL(MethodInfo("rect_tool_requested"));
	ADD_SIGNAL(MethodInfo("entity_selected", PropertyInfo(Variant::INT, "index")));
	ADD_SIGNAL(MethodInfo("runtime_transform_changed",PropertyInfo(Variant::INT,"index"),PropertyInfo(Variant::TRANSFORM3D,"transform")));
}
ECSSpatialEditor::ECSSpatialEditor() {
	set_custom_minimum_size(Vector2(320, 240));
	set_v_size_flags(SIZE_EXPAND_FILL);
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_focus_mode(FOCUS_ALL);
	set_clip_contents(true);
	auto *toolbar = memnew(PanelContainer);
	add_child(toolbar);
	toolbar->set_position(Vector2(12, 42));
	auto *buttons = memnew(VBoxContainer);
	toolbar->add_child(buttons);
	buttons->add_theme_constant_override("separation", 1);
	const char *tips[] = { "Pan (Q)", "Move (W)", "Rotate (E)", "Scale (R)", "Rect / UI (T)", "Transform (Y)" };
	for (int i = 0; i < 6; i++) {
		tool_buttons[i] = memnew(Button);
		tool_buttons[i]->set_custom_minimum_size(Size2(28, 28));
		tool_buttons[i]->set_toggle_mode(true);
		tool_buttons[i]->set_focus_mode(FOCUS_NONE);
		tool_buttons[i]->set_tooltip_text(tips[i]);
		tool_buttons[i]->connect("pressed", callable_mp(this, &ECSSpatialEditor::set_tool).bind(i));
		buttons->add_child(tool_buttons[i]);
	}
	tool_buttons[1]->set_pressed_no_signal(true);
	auto *rs = RenderingServer::get_singleton();
	scenario = rs->scenario_create();
	viewport = rs->viewport_create();
	camera = rs->camera_create();
	rs->viewport_set_scenario(viewport, scenario);
	rs->viewport_attach_camera(viewport, camera);
	rs->viewport_set_update_mode(viewport, RSE::VIEWPORT_UPDATE_ALWAYS);
	rs->viewport_set_clear_mode(viewport, RSE::VIEWPORT_CLEAR_ALWAYS);
	light = rs->directional_light_create();
	// The editor key light illuminates meshes, not the procedural preview sky.
	// Keep the horizon neutral when orbiting toward the light direction.
	rs->light_directional_set_sky_mode(light, RSE::LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_ONLY);
	light_instance = rs->instance_create();
	rs->instance_set_base(light_instance, light);
	rs->instance_set_scenario(light_instance, scenario);
	rs->instance_set_transform(light_instance, Transform3D(Basis::from_euler(Vector3(-0.8, -0.5, 0)), Vector3()));
	Ref<ProceduralSkyMaterial> sky_material;
	sky_material.instantiate();
	// Use the native editor sky: muted blue sky, warm ground and a shared horizon.
	Ref<Sky> sky;
	sky.instantiate();
	sky->set_material(sky_material);
	default_environment.instantiate();
	default_environment->set_background(Environment::BG_SKY);
	default_environment->set_sky(sky);
	default_environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
	default_environment->set_ambient_light_energy(0.65);
	rs->scenario_set_environment(scenario, default_environment->get_rid());
	grid_mesh.instantiate();
	grid_material.instantiate();
	Ref<Shader> grid_shader;
	grid_shader.instantiate();
	grid_shader->set_code(R"(shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, shadows_disabled;
varying vec3 world_position;
uniform float grid_step = 1.0;
void vertex() { world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
float grid_lines(vec2 position, float spacing) {
    vec2 coord = position / spacing;
    vec2 width = max(fwidth(coord), vec2(0.00001));
    vec2 line = abs(fract(coord - 0.5) - 0.5) / width;
    float coverage = 1.0 - min(min(line.x, line.y), 1.0);
    return coverage * (1.0 - smoothstep(0.4, 1.5, max(width.x, width.y)));
}
void fragment() {
    vec2 p = world_position.xz;
    float minor = grid_lines(p, grid_step);
    float major = grid_lines(p, grid_step * 5.0);
    vec2 footprint = max(fwidth(p), vec2(0.00001));
    float x_axis = 1.0 - smoothstep(0.0, footprint.y * 1.4, abs(p.y));
    float z_axis = 1.0 - smoothstep(0.0, footprint.x * 1.4, abs(p.x));
    float fade = 1.0 - smoothstep(grid_step * 35.0, grid_step * 350.0, distance(world_position, CAMERA_POSITION_WORLD));
    ALBEDO = mix(vec3(0.32, 0.37, 0.43), vec3(0.48, 0.54, 0.61), major);
    ALBEDO = mix(ALBEDO, vec3(0.9, 0.22, 0.18), x_axis);
    ALBEDO = mix(ALBEDO, vec3(0.20, 0.40, 0.95), z_axis);
    ALPHA = max(max(minor * 0.28, major * 0.52), max(x_axis, z_axis) * 0.78) * fade;
})");
	grid_material->set_shader(grid_shader);
	grid_mesh->set_material(grid_material);
	grid_instance = rs->instance_create();
	rs->instance_set_base(grid_instance, grid_mesh->get_rid());
	rs->instance_set_scenario(grid_instance, scenario);
	set_process(true);
}
void ECSSpatialEditor::clear_preview() {
	preview_definitions.clear();
	animation_preview = false;
	auto *rs = RenderingServer::get_singleton();
	for (RID rid : instances) {
		if (rid.is_valid()) {
			rs->free_rid(rid);
		}
	}
	instances.clear();
	preview.unref();
	ids.clear();
}
ECSSpatialEditor::~ECSSpatialEditor() {
	clear_preview();
	auto *rs = RenderingServer::get_singleton();
	for (RID rid : { grid_instance, light_instance, light, camera, viewport, scenario }) {
		rs->free_rid(rid);
	}
}
void ECSSpatialEditor::edit_scene(const Ref<ECSScene> &value, int selection) {
	if(scene!=value) { runtime_preview=false; runtime_entities.clear(); runtime_frame=-1; }
	if (dragging) {
		finish_drag(false);
	}
	scene = value;
	selected = selection;
	clear_preview();
	if (scene.is_null()) {
		RenderingServer::get_singleton()->scenario_set_environment(scenario, default_environment->get_rid());
		queue_redraw();
		return;
	}
	preview.instantiate();
	Array entities = scene->get_entities();
	String adapter_error; if (!ECSToolAdapters::compile(entities, adapter_error)) { preview.unref(); queue_redraw(); return; }
	ids = preview->create_entities(entities.size());
	preview_definitions=entities;
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		for (const String &field : { String("position"), String("rotation"), String("scale") }) {
			if (entity.has(field) && entity[field].get_type() == Variant::VECTOR3) {
				preview->set_vector(ids[i], field, entity[field]);
			}
		}
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		int parent = entity.get("parent", -1);
		if (parent >= 0 && parent < ids.size() && parent != i) {
			preview->set_parent(ids[i], ids[parent]);
		}
	}
	for (int i = 0; i < entities.size(); i++) { preview->set_active(ids[i], Dictionary(entities[i]).get("active", true)); }
	auto *rs = RenderingServer::get_singleton();
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (entity.has("particles")) { preview->set_particles(ids[i], entity["particles"]); }
		Ref<Mesh> mesh = entity.get("mesh", Variant());
		RID instance;
		if (mesh.is_valid()) {
			preview->set_mesh(ids[i], mesh, entity.get("material", Variant()));
			instance = rs->instance_create();
			rs->instance_set_base(instance, mesh->get_rid());
			rs->instance_set_scenario(instance, scenario);
			rs->instance_set_transform(instance, preview->get_global_transform(ids[i]));
			rs->instance_set_visible(instance,preview->is_active_in_hierarchy(ids[i]));
			Ref<Material> mesh_material = entity.get("material", Variant());
			if (mesh_material.is_valid()) {
				rs->instance_geometry_set_material_override(instance, mesh_material->get_rid());
			}
		}
		instances.push_back(instance);
	}
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (!entity.has("skeleton")) continue;
		Dictionary rig = entity["skeleton"];
		PackedInt64Array bones = rig.get("bones", PackedInt64Array());
		bool valid = true;
		for (int b = 0; b < bones.size(); b++) { if (bones[b] < 0 || bones[b] >= ids.size()) { valid = false; break; } bones.set(b, ids[bones[b]]); }
		if (valid) preview->set_skeleton(ids[i], rig.get("skin", Variant()), bones);
	}
	update_model_skeletons();
	rs->scenario_set_environment(scenario, scene->get_environment().is_valid() ? scene->get_environment()->get_rid() : default_environment->get_rid());
	if(runtime_preview) { apply_runtime_entities(); }
	queue_redraw();
}
void ECSSpatialEditor::update_runtime_preview(const Array &entities,int64_t frame) {
	runtime_preview=true;
	runtime_entities=entities;
	runtime_frame=frame;
	apply_runtime_entities();
	update_model_skeletons();
	queue_redraw();
}
void ECSSpatialEditor::update_runtime_definition(int index,const Dictionary &definition) {
	if(preview.is_null() || index<0 || index>=ids.size()) { return; }
	if(definition.has("particles") && preview->get_particles(ids[index])!=Dictionary(definition["particles"])) { preview->set_particles(ids[index],definition["particles"]); }
	if(definition.has("mesh") && instances[index].is_valid()) {
		Ref<Mesh> mesh=definition["mesh"]; Ref<Material> material=definition.get("material",Variant());
		if(mesh.is_valid()) { RenderingServer::get_singleton()->instance_set_base(instances[index],mesh->get_rid()); }
		RenderingServer::get_singleton()->instance_geometry_set_material_override(instances[index],material.is_valid()?material->get_rid():RID());
	}
}
void ECSSpatialEditor::apply_runtime_entities() {
	if(preview.is_null()) { return; }
	auto *rs=RenderingServer::get_singleton();
	for(int i=0;i<ids.size();i++) {
		if(dragging && i==selected) { continue; }
		Dictionary data=i<runtime_entities.size()?Dictionary(runtime_entities[i]):Dictionary();
		bool active=data.get("active",false);
		Transform3D transform=data.get("transform",Transform3D());
		// Mirror global poses. Never write simulation state into the ECSScene resource.
		preview->set_parent(ids[i],0);
		preview->set_vector(ids[i],"position",transform.origin);
		preview->set_vector(ids[i],"rotation",Math::is_zero_approx(transform.basis.determinant())?Vector3():transform.basis.get_euler_normalized());
		preview->set_vector(ids[i],"scale",transform.basis.get_scale());
		preview->set_active(ids[i],active);
		if(instances[i].is_valid()) {
			rs->instance_set_visible(instances[i],active);
			rs->instance_set_transform(instances[i],transform);
		}
		preview->apply_particle_preview(ids[i],data.get("particles",Dictionary()),scenario,transform,active);
	}
}
void ECSSpatialEditor::stop_runtime_preview() {
	if(!runtime_preview) { return; }
	runtime_preview=false;
	runtime_entities.clear();
	runtime_frame=-1;
	edit_scene(scene,selected);
}
Dictionary ECSSpatialEditor::runtime_preview_status() const {
	Dictionary data;
	Array entities;
	for(const Variant &item:runtime_entities) {
		Dictionary entity=item;
		Dictionary particles=entity.get("particles",Dictionary());
		entity=entity.duplicate();
		entity.erase("particles");
		entity["particle_count"]=particles.get("visible",0);
		entities.push_back(entity);
	}
	data["active"]=runtime_preview; data["frame"]=runtime_frame; data["entities"]=entities;
	return data;
}
void ECSSpatialEditor::update_camera() {
	if (!viewport.is_valid()) {
		return;
	}
	Vector3 direction(Math::cos(pitch) * Math::sin(yaw), Math::sin(pitch), Math::cos(pitch) * Math::cos(yaw));
	camera_transform = Transform3D(Basis(), focus + direction * distance).looking_at(focus, Vector3(0, 1, 0));
	auto *rs = RenderingServer::get_singleton();
	rs->camera_set_transform(camera, camera_transform);
	if (orthographic) {
		rs->camera_set_orthogonal(camera, distance, .05, 10000);
	} else {
		rs->camera_set_perspective(camera, 60, .05, 10000);
	}
	Vector2i new_size(MAX(1, int(get_size().x)), MAX(1, int(get_size().y)));
	if (new_size != viewport_size) {
		viewport_size = new_size;
		rs->viewport_set_size(viewport, viewport_size.x, viewport_size.y);
	}
	update_grid();
}

Dictionary ECSSpatialEditor::effect_preview(int entity, const String &action, double time) {
 Dictionary result; bool ok=preview.is_valid();
 if(runtime_preview) { result["ok"]=false; return result; }
 if(ok && action!="statistics") { ok=entity>=0 && entity<ids.size() && preview->control_particles(ids[entity],action,time); }
 result["ok"]=ok;
 if(ok) { preview->sync_particles(scenario); result["statistics"]=preview->get_particles_statistics(); queue_redraw(); }
 return result;
}
void ECSSpatialEditor::set_grid_visible(bool visible) {
	grid_visible = visible;
	RenderingServer::get_singleton()->instance_set_visible(grid_instance, visible);
}
void ECSSpatialEditor::update_grid() {
	float desired_step = MAX(distance / 12.0f, 0.01f);
	float decade = Math::pow(10.0f, Math::floor(Math::log(desired_step) / Math::log(10.0f)));
	float normalized = desired_step / decade;
	float step = decade * (normalized <= 1.001f ? 1.0f : normalized <= 2.0f ? 2.0f
										  : normalized <= 5.0f				? 5.0f
																			: 10.0f);
	Vector3 center(Math::snapped(focus.x, step * 5), 0, Math::snapped(focus.z, step * 5));
	if (center == grid_center && step == grid_step) {
		return;
	}
	grid_center = center;
	grid_step = step;
	grid_material->set_shader_parameter("grid_step", step);
	grid_mesh->set_size(Vector2(step * 1000, step * 1000));
	RenderingServer::get_singleton()->instance_set_transform(grid_instance, Transform3D(Basis(), center));
}

Vector2 ECSSpatialEditor::project(const Vector3 &position) const {
	Vector3 local = camera_transform.affine_inverse().xform(position);
	if (local.z >= -.05) {
		return Vector2(-100000, -100000);
	}
	if (orthographic) {
		return get_size() * .5 + Vector2(local.x, -local.y) * (get_size().y / distance);
	}
	float f = get_size().y / (2 * Math::tan(Math::deg_to_rad(30.0)));
	return get_size() * .5 + Vector2(local.x, -local.y) * (f / -local.z);
}
void ECSSpatialEditor::_notification(int what) {
	if (what == NOTIFICATION_ENTER_TREE) {
		auto *rs = RenderingServer::get_singleton();
		// Render this offscreen target before the window that samples its texture.
		// Without this dependency a resize can expose an unrendered allocation.
		// Re-establish it on entry because docks can move into another Window.
		rs->viewport_set_parent_viewport(viewport, get_viewport()->get_viewport_rid());
		update_camera();
		rs->viewport_set_active(viewport, true);
	}
	if (what == NOTIFICATION_EXIT_TREE) {
		auto *rs = RenderingServer::get_singleton();
		rs->viewport_set_active(viewport, false);
		rs->viewport_set_parent_viewport(viewport, RID());
	}
	if (what == NOTIFICATION_THEME_CHANGED) {
		const char *icons[] = { "ToolPan", "ToolMove", "ToolRotate", "ToolScale", "ECSToolRect", "ToolTransform" };
		for (int i = 0; i < 6; i++) {
			if (tool_buttons[i]) {
				tool_buttons[i]->set_button_icon(get_editor_theme_icon(StringName(icons[i])));
			}
		}
	}

	if (what == NOTIFICATION_WM_WINDOW_FOCUS_OUT || what == NOTIFICATION_FOCUS_EXIT || (what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree())) {
		panning = false;
		hand_panning = false;
		orbit = false;
		finish_drag(false);
	}
	if (what == NOTIFICATION_PROCESS && is_visible_in_tree()) {
		if (preview.is_valid() && !runtime_preview) {
			preview->step(get_process_delta_time()); preview->sync_particles(scenario);
			for (int i = 0; animation_preview && i < instances.size() && i < ids.size(); i++) {
				if (instances[i].is_valid()) { RenderingServer::get_singleton()->instance_set_transform(instances[i], preview->get_global_transform(ids[i])); }
			}
		}
		update_model_skeletons();
		update_camera();
		queue_redraw();
	}
	if (what == NOTIFICATION_RESIZED) {
		// Do the size update immediately as well as from PROCESS. Native window
		// dragging can deliver several resize notifications between process ticks.
		update_camera();
		queue_redraw();
	}
	if (what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree()) {
		finish_drag(false);
	}
	if (what != NOTIFICATION_DRAW) {
		return;
	}
	auto *rs = RenderingServer::get_singleton();
	// Also cover the area when no render target is available yet.
	draw_rect(Rect2(Vector2(), get_size()), Color(0.055, 0.065, 0.08), true);
	RID viewport_texture = rs->viewport_get_texture(viewport);
	if (viewport_texture.is_valid() && viewport_size.x > 0 && viewport_size.y > 0) {
		rs->canvas_item_add_texture_rect(get_canvas_item(), Rect2(Vector2(), get_size()), viewport_texture);
	}
	if (preview.is_valid()) {
		for (int i = 0; i < ids.size(); i++) {
			Vector2 p = project(preview->get_global_transform(ids[i]).origin);
			if (!Rect2(Vector2(), get_size()).has_point(p)) {
				continue;
			}
			draw_circle(p, i == selected ? 6 : 3, i == selected ? Color(1, .75, .2) : Color(.7, .8, .9));
		}
	}
	draw_gizmo();
	draw_navigation();
	if(runtime_preview) {
		draw_string(get_theme_font("font","Label"),Vector2(12,25),String(U"运行场景"),HORIZONTAL_ALIGNMENT_LEFT,-1,14,Color(.45,.75,1));
	}
}
void ECSSpatialEditor::set_tool(int p_tool) {
	finish_drag(false);
	panning = false;
	hand_panning = false;
	if (p_tool == 4) {
		tool_buttons[4]->set_pressed_no_signal(false);
		emit_signal("rect_tool_requested");
		return;
	}
	tool_mode = p_tool == 0 ? -1 : p_tool == 5 ? 3
											   : p_tool - 1;
	if (tool_mode >= 0 && tool_mode < 3) {
		mode = tool_mode;
	}
	for (int i = 0; i < 6; i++) {
		tool_buttons[i]->set_pressed_no_signal(i == p_tool);
	}
	grab_focus();
	queue_redraw();
}
void ECSSpatialEditor::draw_gizmo() {
	gizmo_handles.clear();
	gizmo_planes.clear();
	if (tool_mode < 0 || preview.is_null() || selected < 0 || selected >= ids.size()) {
		return;
	}
	Vector3 origin = preview->get_global_transform(ids[selected]).origin;
	float size = MAX(.1f, camera_transform.origin.distance_to(origin) * .13f);
	Vector2 center = project(origin);
	const Transform3D view = camera_transform.affine_inverse();
	if (!origin.is_finite() || view.xform(origin).z >= -.05 || !center.is_finite()) { return; }
	if (tool_mode == 0 || tool_mode == 3) {
		for (int normal = 0; normal < 3; normal++) {
			Vector3 first, second;
			first[(normal + 1) % 3] = size * .3;
			second[(normal + 2) % 3] = size * .3;
			const Vector3 corners[] = { origin + (first + second) * .3, origin + first + second * .3, origin + first + second, origin + first * .3 + second };
			bool clipped = false;
			for (const Vector3 &corner : corners) { clipped |= !corner.is_finite() || view.xform(corner).z >= -.05; }
			if (clipped) { continue; }
			PackedVector2Array points;
			points.push_back(project(origin + (first + second) * .3));
			points.push_back(project(origin + first + second * .3));
			points.push_back(project(origin + first + second));
			points.push_back(project(origin + first * .3 + second));
			Color color = normal == 0 ? Color(.95, .3, .3, .28) : normal == 1 ? Color(.3, .9, .3, .28)
																			  : Color(.3, .5, 1, .28);
			float area = 0;
			for (int i = 0; i < points.size(); i++) {
				area += (points[i] - points[0]).cross(points[(i + 1) % points.size()] - points[0]);
			}
			if (!Math::is_finite(area) || Math::abs(area) < 1) {
				continue;
			}
			PackedColorArray colors; colors.push_back(color);
			draw_primitive(points, colors, PackedVector2Array());
			gizmo_planes.push_back({ points, normal });
		}
	}
	for (int a = 0; a < 3; a++) {
		Color color = a == 0 ? Color(.95, .3, .3) : a == 1 ? Color(.35, .85, .35)
														   : Color(.35, .55, 1);
		Vector3 axis_vector;
		axis_vector[a] = size;
		if (tool_mode == 0 || tool_mode == 3) {
			if (view.xform(origin + axis_vector).z >= -.05) { continue; }
			Vector2 end = project(origin + axis_vector);
			if (end.distance_to(center) < 2) {
				continue;
			}
			draw_line(center, end, color, 2, true);
			Vector2 direction = (end - center).normalized();
			Vector2 cross(-direction.y, direction.x);
			PackedVector2Array triangle;
			triangle.push_back(end);
			triangle.push_back(end - direction * 10 + cross * 4);
			triangle.push_back(end - direction * 10 - cross * 4);
			draw_primitive(triangle, PackedColorArray({ color }), PackedVector2Array());
			gizmo_handles.push_back({ center + direction * 14, end, 0, a });
		}
		if (tool_mode == 1 || tool_mode == 3) {
			for (int i = 0; i < 64; i++) {
				Vector3 v1, v2;
				float t1 = i * Math::TAU / 64, t2 = (i + 1) * Math::TAU / 64;
				v1[(a + 1) % 3] = Math::cos(t1) * size * .75;
				v1[(a + 2) % 3] = Math::sin(t1) * size * .75;
				v2[(a + 1) % 3] = Math::cos(t2) * size * .75;
				v2[(a + 2) % 3] = Math::sin(t2) * size * .75;
				Vector2 start = project(origin + v1), end = project(origin + v2);
				draw_line(start, end, color, 1.5, true);
				gizmo_handles.push_back({ start, end, 1, a });
			}
		}
		if (tool_mode == 2 || tool_mode == 3) {
			Vector2 end = project(origin + axis_vector * .5f);
			draw_line(center, end, color, 2, true);
			draw_rect(Rect2(end - Vector2(4, 4), Vector2(8, 8)), color);
			gizmo_handles.push_back({ end, end, 2, a });
		}
	}
}
bool ECSSpatialEditor::pick_gizmo(const Vector2 &p_point) {
	float best = 9;
	bool found = false;
	drag_plane = -1;
	for (const GizmoHandle &handle : gizmo_handles) {
		Vector2 segment[2] = { handle.start, handle.end };
		Vector2 point = handle.start.is_equal_approx(handle.end) ? handle.start : Geometry2D::get_closest_point_to_segment(p_point, segment);
		float d = point.distance_to(p_point);
		if (d <= best) {
			best = d;
			mode = handle.operation;
			axis = handle.axis;
			found = true;
		}
	}
	if (!found) {
		for (const GizmoPlane &plane : gizmo_planes) {
			if (Geometry2D::is_point_in_polygon(p_point, plane.points)) {
				mode = 0;
				drag_plane = plane.normal;
				return true;
			}
		}
	}
	return found;
}
void ECSSpatialEditor::draw_navigation() {
	Vector2 center(get_size().x - 65, 65);
	Ref<Font> font = ThemeDB::get_singleton()->get_fallback_font();
	for (int i = 0; i < 6; i++) {
		Vector3 axis_vector;
		axis_vector[i % 3] = i < 3 ? 1 : -1;
		Vector3 local = camera_transform.basis.inverse().xform(axis_vector);
		Vector2 direction(local.x, -local.y);
		Vector2 end = center + direction * 38;
		navigation_ends[i] = end;
		Color color = i >= 3 ? Color(.58, .58, .58) : i == 0 ? Color(.95, .3, .3)
				: i == 1									 ? Color(.4, .9, .3)
															 : Color(.3, .55, 1);
		draw_line(center, end, color, 3, true);
		if (direction.length_squared() < .001) {
			continue;
		}
		Vector2 d = direction.normalized(), cross(-d.y, d.x);
		PackedVector2Array triangle;
		triangle.push_back(end);
		triangle.push_back(end - d * 12 + cross * 6);
		triangle.push_back(end - d * 12 - cross * 6);
		draw_primitive(triangle, PackedColorArray({ color }), PackedVector2Array());
		if (i < 3) {
			draw_string(font, end + Vector2(-3, -7), i == 0 ? "x" : i == 1 ? "y"
																		   : "z",
					HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(1, 1, 1));
		}
	}
	PackedVector2Array cube;
	cube.push_back(center + Vector2(0, -9));
	cube.push_back(center + Vector2(9, -4));
	cube.push_back(center + Vector2(9, 6));
	cube.push_back(center + Vector2(0, 11));
	cube.push_back(center + Vector2(-9, 6));
	cube.push_back(center + Vector2(-9, -4));
	draw_colored_polygon(cube, Color(.7, .7, .7));
	draw_line(center, center + Vector2(0, 11), Color(.3, .3, .3), 1);
	draw_line(center, center + Vector2(9, -4), Color(.3, .3, .3), 1);
	draw_line(center, center + Vector2(-9, -4), Color(.3, .3, .3), 1);
	draw_string(font, center + Vector2(-20, 62), orthographic ? "Ortho" : "Persp", HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(.85, .85, .85));
	Vector2 lock(get_size().x - 18, 16);
	Color lock_color = view_rotation_locked ? Color(1, .75, .25) : Color(.55, .55, .55);
	draw_rect(Rect2(lock, Vector2(8, 7)), lock_color);
	draw_arc(lock + Vector2(4, 0), 3, Math::PI, Math::TAU, 12, lock_color, 1.5, true);
}
void ECSSpatialEditor::snap_view(int p_axis) {
	finish_drag(false);
	if (p_axis == 0) {
		yaw = Math::PI * .5;
		pitch = 0;
	} else if (p_axis == 1) {
		yaw = 0;
		pitch = Math::PI * .5 - .001;
	} else if (p_axis == 2) {
		yaw = 0;
		pitch = 0;
	} else if (p_axis == 3) {
		yaw = -Math::PI * .5;
		pitch = 0;
	} else if (p_axis == 4) {
		yaw = 0;
		pitch = -Math::PI * .5 + .001;
	} else {
		yaw = Math::PI;
		pitch = 0;
	}
	orthographic = true;
	update_camera();
	queue_redraw();
}
bool ECSSpatialEditor::navigation_click(const Vector2 &p_point) {
	Vector2 center(get_size().x - 65, 65);
	if (Rect2(get_size().x - 25, 5, 22, 27).has_point(p_point)) {
		view_rotation_locked = !view_rotation_locked;
		queue_redraw();
		return true;
	}
	if (Rect2(center + Vector2(-30, 44), Vector2(70, 26)).has_point(p_point) || p_point.distance_to(center) < 12) {
		orthographic = !orthographic;
		update_camera();
		queue_redraw();
		return true;
	}
	int chosen = -1;
	float best = 13;
	for (int i = 0; i < 6; i++) {
		float d = p_point.distance_to(navigation_ends[i]);
		if (d < best) {
			best = d;
			chosen = i;
		}
	}
	if (chosen >= 0) {
		snap_view(chosen);
		return true;
	}
	return false;
}
void ECSSpatialEditor::finish_drag(bool commit) {
	if (!dragging) {
		return;
	}
	dragging = false;
	if (scene.is_null() || selected < 0 || selected >= ids.size()) {
		return;
	}
	String field = mode == 0 ? "position" : mode == 1 ? "rotation"
													  : "scale";
	Vector3 value = preview->get_vector(ids[selected], field);
	if(runtime_preview) {
		if(!commit) { preview->set_vector(ids[selected],field,original); }
		emit_signal("runtime_transform_changed",selected,preview->get_global_transform(ids[selected]));
		return;
	}
	if (commit && value != original) {
		Array after = before.duplicate(true);
		Dictionary entity = after[selected];
		entity[field] = value;
		auto *undo = EditorUndoRedoManager::get_singleton();
		undo->create_action(String(U"编辑 ECS 空间变换"), UndoRedo::MERGE_DISABLE, scene.ptr());
		undo->add_do_method(scene.ptr(), "set_entities", after);
		undo->add_undo_method(scene.ptr(), "set_entities", before);
		undo->commit_action();
	} else {
		preview->set_vector(ids[selected], field, original);
		for (int i = 0; i < ids.size(); i++) {
			if (instances[i].is_valid()) {
				RenderingServer::get_singleton()->instance_set_transform(instances[i], preview->get_global_transform(ids[i]));
			}
		}
	}
	before = Array();
	queue_redraw();
}
void ECSSpatialEditor::gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->is_command_or_control_pressed() || key->is_alt_pressed()) {
			return;
		}
		Key k = key->get_keycode();
		if (k == Key::ESCAPE) {
			finish_drag(false);
			accept_event();
			return;
		}
		if (dragging) {
			return;
		}
		if (k == Key::Q) {
			set_tool(0);
		} else if (k == Key::T) {
			set_tool(4);
		} else if (k == Key::W) {
			set_tool(1);
		} else if (k == Key::E) {
			set_tool(2);
		} else if (k == Key::R) {
			set_tool(3);
		} else if (k == Key::X) {
			axis = 0;
		} else if (k == Key::Y) {
			set_tool(5);
		} else if (k == Key::Z) {
			axis = 2;
		} else if (k == Key::F && preview.is_valid() && selected >= 0 && selected < ids.size()) {
			focus = preview->get_global_transform(ids[selected]).origin;
		} else {
			return;
		}
		accept_event();
		queue_redraw();
		return;
	}
	Ref<InputEventMouseButton> mouse = event;
	if (mouse.is_valid()) {
		if (mouse->get_button_index() == MouseButton::LEFT && mouse->is_pressed() && navigation_click(mouse->get_position())) {
			accept_event();
			return;
		}
		if (mouse->get_button_index() == MouseButton::MIDDLE) {
			if (mouse->is_pressed()) {
				finish_drag(false);
				grab_focus();
			}
			panning = mouse->is_pressed();
			orbit = false;
			accept_event();
			return;
		}
		if (mouse->get_button_index() == MouseButton::RIGHT) {
			orbit = mouse->is_pressed() && !view_rotation_locked;
			grab_focus();
			accept_event();
			return;
		}
		if (mouse->is_pressed() && (mouse->get_button_index() == MouseButton::WHEEL_UP || mouse->get_button_index() == MouseButton::WHEEL_DOWN)) {
			distance = CLAMP(distance * (mouse->get_button_index() == MouseButton::WHEEL_UP ? .85 : 1.18), .2, 2000.0);
			accept_event();
			return;
		}
		if (mouse->get_button_index() == MouseButton::LEFT) {
			grab_focus();
			if (tool_mode == -1) {
				panning = mouse->is_pressed();
				hand_panning = panning;
				accept_event();
				return;
			}
			if (!mouse->is_pressed()) {
				finish_drag(true);
				accept_event();
				return;
			}
			if (preview.is_null()) {
				return;
			}
			if (selected >= 0 && selected < ids.size() && pick_gizmo(mouse->get_position())) {
				dragging = true;
				drag_start = mouse->get_position();
				drag_world_origin = preview->get_global_transform(ids[selected]).origin;
				before = scene->get_entities();
				original = preview->get_vector(ids[selected], mode == 0 ? "position" : mode == 1 ? "rotation"
																								 : "scale");
				accept_event();
				return;
			}
			int closest = -1;
			float best = 14;
			for (int i = 0; i < ids.size(); i++) {
				float d = project(preview->get_global_transform(ids[i]).origin).distance_to(mouse->get_position());
				if (d < best) {
					best = d;
					closest = i;
				}
			}
			// Mesh picking uses the nearest transformed mesh bounds, not only the entity origin.
			if (closest < 0) {
				Vector2 screen = mouse->get_position() - get_size() * .5;
				float focal = MAX(1.0f, get_size().y) / (2 * Math::tan(Math::deg_to_rad(30.0)));
				Vector3 direction = camera_transform.basis.xform(Vector3(screen.x / focal, -screen.y / focal, -1)).normalized();
				Vector3 ray_origin = camera_transform.origin;
				if (orthographic) {
					direction = -camera_transform.basis.get_column(2);
					ray_origin += camera_transform.basis.xform(Vector3(screen.x, -screen.y, 0)) * (distance / MAX(1.0f, get_size().y));
				}
				float nearest = INFINITY;
				Array definitions = preview_definitions;
				for (int i = 0; i < ids.size(); i++) {
					Ref<Mesh> mesh = Dictionary(definitions[i]).get("mesh", Variant());
					if (mesh.is_null()) {
						continue;
					}
					Transform3D transform = preview->get_global_transform(ids[i]);
					if (Math::is_zero_approx(transform.basis.determinant())) {
						continue;
					}
					Transform3D inverse = transform.affine_inverse();
					Vector3 hit;
					bool inside = false;
					if (mesh->get_aabb().find_intersects_ray(inverse.xform(ray_origin), inverse.basis.xform(direction), inside, &hit)) {
						float depth = (transform.xform(hit) - ray_origin).dot(direction);
						if (depth >= 0 && depth < nearest) {
							nearest = depth;
							closest = i;
						}
					}
				}
			}
			if (closest < 0) {
				return;
			}
			if (closest >= scene->get_entities().size()) { closest = Dictionary(preview_definitions[closest]).get("parent", -1); }
			if (closest < 0 || closest >= scene->get_entities().size()) { return; }
			if (closest != selected) {
				selected = closest;
				emit_signal("entity_selected", selected);
			} else {
				dragging = true;
				drag_start = mouse->get_position();
				drag_world_origin = preview->get_global_transform(ids[selected]).origin;
				before = scene->get_entities();
				original = preview->get_vector(ids[selected], mode == 0 ? "position" : mode == 1 ? "rotation"
																								 : "scale");
			}
			accept_event();
			queue_redraw();
		}
	}
	Ref<InputEventMouseMotion> motion = event;
	if (motion.is_valid()) {
		if (panning) {
			if (!(motion->get_button_mask().has_flag(hand_panning ? MouseButtonMask::LEFT : MouseButtonMask::MIDDLE))) {
				panning = false;
				return;
			}
			float units_per_pixel = (orthographic ? distance : 2.0f * distance * Math::tan(Math::deg_to_rad(30.0))) / MAX(1.0f, get_size().y);
			Vector2 delta = motion->get_relative();
			focus += (-camera_transform.basis.get_column(0) * delta.x + camera_transform.basis.get_column(1) * delta.y) * units_per_pixel;
			update_camera();
			queue_redraw();
			accept_event();
			return;
		}
		if (orbit) {
			yaw -= motion->get_relative().x * .01;
			pitch = CLAMP(pitch + motion->get_relative().y * .01, -1.5, 1.5);
			accept_event();
		}
		if (dragging) {
			Vector2 delta = motion->get_position() - drag_start;
			Vector3 value = original;
			if (mode == 0) {
				Vector3 world_delta;
				Vector2 origin_pixel = project(drag_world_origin);
				if (drag_plane >= 0) {
					int a = (drag_plane + 1) % 3, b = (drag_plane + 2) % 3;
					Vector3 av, bv;
					av[a] = 1;
					bv[b] = 1;
					Vector2 u = project(drag_world_origin + av) - origin_pixel, v = project(drag_world_origin + bv) - origin_pixel;
					float determinant = u.cross(v);
					if (Math::abs(determinant) > .001) {
						world_delta[a] = delta.cross(v) / determinant;
						world_delta[b] = u.cross(delta) / determinant;
					}
				} else {
					Vector3 axis_vector;
					axis_vector[axis] = 1;
					Vector2 projected_axis = project(drag_world_origin + axis_vector) - origin_pixel;
					if (projected_axis.length_squared() > .001) {
						world_delta[axis] = delta.dot(projected_axis) / projected_axis.length_squared();
					}
				}
				uint64_t parent = preview->get_parent(ids[selected]);
				Basis basis = parent ? preview->get_global_transform(parent).basis : Basis();
				if (!Math::is_zero_approx(basis.determinant())) {
					value += basis.inverse().xform(world_delta);
				}
			} else {
				value[axis] += (delta.x - delta.y) * (mode == 1 ? .01 : .005);
			}
			if (mode == 2) {
				value[axis] = MAX(.01f, value[axis]);
			}
			preview->set_vector(ids[selected], mode == 0 ? "position" : mode == 1 ? "rotation"
																				  : "scale",
					value);
			if(runtime_preview) { emit_signal("runtime_transform_changed",selected,preview->get_global_transform(ids[selected])); }
			for (int i = 0; i < ids.size(); i++) {
				if (instances[i].is_valid()) {
					RenderingServer::get_singleton()->instance_set_transform(instances[i], preview->get_global_transform(ids[i]));
				}
			}
			accept_event();
			queue_redraw();
		}
	}
}
bool ECSSpatialEditor::run_self_test() {
	Ref<ECSScene> test;
	test.instantiate();
	Dictionary entity;
	entity["position"] = Vector3();
	Array entities;
	entities.push_back(entity);
	test->set_entities(entities);
	edit_scene(test, 0);
	before = test->get_entities();
	original = Vector3();
	dragging = true;
	mode = 0;
	preview->set_vector(ids[0], "position", Vector3(2, 3, 4));
	finish_drag(true);
	bool ok = Vector3(Dictionary(test->get_entities()[0])["position"]) == Vector3(2, 3, 4);
	auto *undo = EditorUndoRedoManager::get_singleton();
	ok = ok && undo->undo() && Vector3(Dictionary(test->get_entities()[0])["position"]) == Vector3();
	ok = ok && undo->redo();
	for (int operation = 1; operation <= 2; operation++) {
		edit_scene(test, 0);
		mode = operation;
		String field = operation == 1 ? "rotation" : "scale";
		before = test->get_entities();
		original = preview->get_vector(ids[0], field);
		dragging = true;
		Vector3 edited = operation == 1 ? Vector3(.1, .2, .3) : Vector3(2, 3, 4);
		preview->set_vector(ids[0], field, edited);
		finish_drag(true);
		ok = ok && Vector3(Dictionary(test->get_entities()[0])[field]) == edited;
		ok = ok && undo->undo() && undo->redo();
		before = test->get_entities();
		original = edited;
		dragging = true;
		preview->set_vector(ids[0], field, Vector3(5, 5, 5));
		finish_drag(false);
		ok = ok && Vector3(Dictionary(test->get_entities()[0])[field]) == edited && preview->get_vector(ids[0], field) == edited;
	}
	update_camera();
	Vector3 old_focus = focus;
	Array original_entities = test->get_entities().duplicate(true);
	Ref<InputEventMouseButton> middle;
	middle.instantiate();
	middle->set_button_index(MouseButton::MIDDLE);
	middle->set_pressed(true);
	gui_input(middle);
	Ref<InputEventMouseMotion> motion;
	motion.instantiate();
	motion->set_relative(Vector2(40, 20));
	motion->set_button_mask(MouseButtonMask::MIDDLE);
	gui_input(motion);
	ok &= !focus.is_equal_approx(old_focus) && test->get_entities() == original_entities;
	middle->set_pressed(false);
	gui_input(middle);
	Vector3 stopped_focus = focus;
	gui_input(motion);
	ok &= focus.is_equal_approx(stopped_focus) && !panning;
	middle->set_pressed(true);
	gui_input(middle);
	_notification(NOTIFICATION_WM_WINDOW_FOCUS_OUT);
	gui_input(motion);
	ok &= focus.is_equal_approx(stopped_focus) && !panning;
	set_tool(0);
	ok &= tool_mode == -1 && tool_buttons[0]->is_pressed();
	set_tool(1);
	ok &= mode == 0 && tool_buttons[1]->is_pressed() && !tool_buttons[0]->is_pressed();
	set_tool(2);
	ok &= mode == 1;
	set_tool(3);
	ok &= mode == 2;
	set_tool(5);
	ok &= tool_mode == 3;
	float old_yaw = yaw, old_pitch = pitch;
	bool old_projection = orthographic;
	snap_view(2);
	ok &= orthographic && Math::is_zero_approx(yaw) && Math::is_zero_approx(pitch);
	Vector3 near_point = focus + Vector3(1, 0, 1), far_point = focus + Vector3(1, 0, -1);
	ok &= project(near_point).is_equal_approx(project(far_point));
	orthographic = false;
	update_camera();
	ok &= !project(near_point).is_equal_approx(project(far_point));
	for (int a = 0; a < 6; a++) {
		snap_view(a);
		ok &= camera_transform.is_finite();
	}
	yaw = old_yaw;
	pitch = old_pitch;
	orthographic = old_projection;
	set_tool(1);
	focus = old_focus;
	mode = 0;
	edit_scene(Ref<ECSScene>(), -1);
	return ok;
}
bool ECSSpatialEditor::test_runtime_drag() {
	if(!runtime_preview || selected<0 || gizmo_handles.is_empty()) { return false; }
	GizmoHandle handle=gizmo_handles[0];
	Vector2 from=handle.start.lerp(handle.end,.65),to=from+Vector2(35,0);
	Ref<InputEventMouseButton> press; press.instantiate(); press->set_button_index(MouseButton::LEFT); press->set_pressed(true); press->set_position(from); gui_input(press);
	if(!dragging) { return false; }
	Ref<InputEventMouseMotion> motion; motion.instantiate(); motion->set_button_mask(MouseButtonMask::LEFT); motion->set_position(to); motion->set_relative(to-from); gui_input(motion);
	Ref<InputEventMouseButton> release; release.instantiate(); release->set_button_index(MouseButton::LEFT); release->set_position(to); gui_input(release);
	return !dragging;
}
void ECSSpatialEditor::start_visual_test() {
	Ref<ECSScene> test;
	test.instantiate();
	Array entities;
	Ref<BoxMesh> mesh;
	mesh.instantiate();
	Ref<StandardMaterial3D> test_material;
	test_material.instantiate();
	test_material->set_albedo(Color(.2, .6, 1));
	for (int i = 0; i < 3; i++) {
		Dictionary entity;
		entity["position"] = Vector3((i - 1) * 2, 0, 0);
		entity["mesh"] = mesh;
		entity["material"] = test_material;
		entities.push_back(entity);
	}
	test->set_entities(entities);
	edit_scene(test, 1);
	if (auto *tabs = Object::cast_to<TabContainer>(get_parent())) {
		tabs->set_current_tab(0);
	}
	get_tree()->create_timer(2.0)->connect("timeout", callable_mp(this, &ECSSpatialEditor::capture_visual));
}
void ECSSpatialEditor::capture_visual() {
	Ref<Image> image = RenderingServer::get_singleton()->texture_2d_get(RenderingServer::get_singleton()->viewport_get_texture(viewport));
	bool ok = image.is_valid() && image->get_width() > 300 && image->get_height() > 200;
	int colored = 0;
	if (ok) {
		for (int y = 0; y < image->get_height(); y += 4) {
			for (int x = 0; x < image->get_width(); x += 4) {
				Color c = image->get_pixel(x, y);
				if (c.b > c.r * 1.4 && c.b > .1) {
					colored++;
				}
			}
		}
		ok = colored > 50;
		image->save_png("D:/GODOTS/TempSDK/ecs-3d-preview.png");
	}
	get_viewport()->get_texture()->get_image()->save_png("D:/GODOTS/TempSDK/ecs-3d-editor.png");
	if (ok) {
		print_line("ECS_SPATIAL_VISUAL_PASS native mesh preview");
	} else {
		ERR_PRINT("ECS_SPATIAL_VISUAL_FAILED");
	}
	get_tree()->quit(ok ? 0 : 1);
}
bool ECSSpatialEditor::preview_animation(int p_owner, double p_time, bool p_playing) {
	if (preview.is_null() || scene.is_null() || p_owner < 0 || p_owner >= ids.size() || p_owner >= scene->get_entities().size()) { return false; }
	Dictionary definition = scene->get_entities()[p_owner];
	if (!definition.has("animation")) { return false; }
	Dictionary data = Dictionary(definition["animation"]).duplicate(true);
	PackedInt64Array targets = data.get("targets", PackedInt64Array());
	for (int i = 0; i < targets.size(); i++) { if (targets[i] < 0 || targets[i] >= ids.size()) { return false; } targets.set(i, ids[targets[i]]); }
	if (data.has("targets")) { data["targets"] = targets; }
	data["time"] = p_time; data["playing"] = true;
	if (!preview->set_animation(ids[p_owner], data)) { return false; }
	preview->advance_animation_preview(0);
	Dictionary patch; patch["playing"] = p_playing; preview->set_animation(ids[p_owner], patch);
	animation_preview = true;
	update_model_skeletons();
	for (int i = 0; i < instances.size(); i++) { if (instances[i].is_valid()) { RenderingServer::get_singleton()->instance_set_transform(instances[i], preview->get_global_transform(ids[i])); } }
	queue_redraw(); return true;
}
void ECSSpatialEditor::update_model_skeletons() {
	if (preview.is_null() || preview->query(PackedStringArray({"skeleton"}), true).is_empty()) return;
	for (const ECSWorld::RenderBatch &batch : preview->get_render_batches(Ref<Mesh>())) {
		if (!batch.skeleton.is_valid()) continue;
		int index = ids.find(batch.entity);
		if (index >= 0 && index < instances.size() && instances[index].is_valid()) RenderingServer::get_singleton()->instance_attach_skeleton(instances[index], batch.skeleton);
	}
}
#endif
