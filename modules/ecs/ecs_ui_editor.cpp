#include "ecs_ui_components.h"
// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#ifdef TOOLS_ENABLED
#include "ecs_ui_editor.h"

#include "core/config/project_settings.h"
#include "core/math/geometry_2d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_zoom_widget.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/font.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/panel_container.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/theme/theme_db.h"

void ECSUICanvasEditor::_bind_methods() {
	ADD_SIGNAL(MethodInfo("tilemap_edited",PropertyInfo(Variant::INT,"index"),PropertyInfo(Variant::DICTIONARY,"definition")));
	ADD_SIGNAL(MethodInfo("images_dropped",PropertyInfo(Variant::PACKED_STRING_ARRAY,"paths"),PropertyInfo(Variant::VECTOR2,"position")));
	ADD_SIGNAL(MethodInfo("mesh_edited",PropertyInfo(Variant::INT,"entity"),PropertyInfo(Variant::DICTIONARY,"polygon")));
	ADD_SIGNAL(MethodInfo("bone_create_requested",PropertyInfo(Variant::VECTOR2,"start"),PropertyInfo(Variant::VECTOR2,"end")));
	ADD_SIGNAL(MethodInfo("bone_pose_edited",PropertyInfo(Variant::INT,"entity"),PropertyInfo(Variant::STRING,"field"),PropertyInfo(Variant::VECTOR3,"value")));
	ADD_SIGNAL(MethodInfo("runtime_transform_changed",PropertyInfo(Variant::INT,"index"),PropertyInfo(Variant::TRANSFORM3D,"transform")));
	ADD_SIGNAL(MethodInfo("entity_selected", PropertyInfo(Variant::INT, "index")));
}
void ECSUICanvasEditor::set_skeleton_authoring(bool enabled) {
    skeleton_authoring=enabled;
    if(zoom_bar_control) {
        if(enabled) {
            zoom_bar_control->set_anchors_and_offsets_preset(PRESET_BOTTOM_LEFT);
            zoom_bar_control->set_offset(SIDE_LEFT,24*EDSCALE);
            zoom_bar_control->set_offset(SIDE_RIGHT,212*EDSCALE);
            zoom_bar_control->set_offset(SIDE_TOP,-54*EDSCALE);
            zoom_bar_control->set_offset(SIDE_BOTTOM,-24*EDSCALE);
        } else {
            zoom_bar_control->set_anchors_and_offsets_preset(PRESET_TOP_LEFT);
            zoom_bar_control->set_position(Vector2(24,24)*EDSCALE);
        }
    }
    if(legacy_toolbar) { legacy_toolbar->set_visible(!enabled); }
    queue_redraw();
}
float ECSUICanvasEditor::display_scale() const {
	if(skeleton_authoring) { return zoom; }
	return MAX(.01f, MIN(MAX(1.0f, get_size().x - 64 * EDSCALE) / canvas_size.x, MAX(1.0f, get_size().y - 64 * EDSCALE) / canvas_size.y) * zoom);
}
ECSUICanvasEditor::ECSUICanvasEditor() {
	set_custom_minimum_size(Size2(320, 240));
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_v_size_flags(SIZE_EXPAND_FILL);
	set_focus_mode(FOCUS_ALL);
	set_clip_contents(true);
	set_tooltip_text(String(U"单击选择；拖动移动；四角调整大小；滚轮缩放；中键平移；Home 适应；Esc 取消。自动布局容器的子项仅允许调整大小。"));
	viewport = memnew(SubViewport);
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(true);
	viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	add_child(viewport);
	pan = skeleton_authoring?get_size()*.5f:Vector2(32, 32) * EDSCALE;
	auto *zoom_bar = memnew(HBoxContainer); zoom_bar_control=zoom_bar;
	add_child(zoom_bar);
	zoom_bar->set_position(Vector2(24, 24) * EDSCALE);
	fit_button = memnew(Button);
	fit_button->set_flat(true);
	fit_button->set_tooltip_text(TTR("Fit Canvas"));
	fit_button->connect("pressed", callable_mp(this, &ECSUICanvasEditor::fit_canvas));
	zoom_bar->add_child(fit_button);
	zoom_widget = memnew(EditorZoomWidget);
	zoom_widget->set_shortcut_context(this);
	zoom_widget->connect("zoom_changed", callable_mp(this, &ECSUICanvasEditor::set_preview_zoom));
	zoom_bar->add_child(zoom_widget);
	auto *toolbar = memnew(PanelContainer); legacy_toolbar=toolbar;
	add_child(toolbar);
	toolbar->set_position(Vector2(24, 62) * EDSCALE);
	auto *buttons = memnew(VBoxContainer);
	toolbar->add_child(buttons);
	buttons->add_theme_constant_override("separation", 1);
	const String tips[] = { TTR("Pan (Q)"), TTR("Move (W)"), String(U"旋转 2D 骨骼（E）；UI 布局不旋转"), TTR("Scale (R)"), TTR("Rect / UI (T)"), TTR("Transform (Y)") };
	for (int i = 0; i < 6; i++) {
		tool_buttons[i] = memnew(Button);
		tool_buttons[i]->set_custom_minimum_size(Size2(28, 28) * EDSCALE);
		tool_buttons[i]->set_toggle_mode(true);
		tool_buttons[i]->set_focus_mode(FOCUS_NONE);
		tool_buttons[i]->set_tooltip_text(tips[i]);
		tool_buttons[i]->connect("pressed", callable_mp(this, &ECSUICanvasEditor::set_tool).bind(i));
		buttons->add_child(tool_buttons[i]);
	}
	tool_buttons[2]->set_disabled(false);
	tool_buttons[4]->set_pressed_no_signal(true);
	set_process(true);
}
void ECSUICanvasEditor::sync_authoring_preview() {
    if(preview.is_null()) { return; }
    preview->sync_skeletal_2d(viewport->get_viewport_rid());
    for(int index:authoring_hidden) { if(index>=0 && index<ids.size()) { preview->hide_authoring_polygon(ids[index]); } }
}
void ECSUICanvasEditor::set_authoring_item_state(int index,bool hidden,bool locked) {
    finish_drag(false); finish_mesh(false); finish_paint(false);
    if(hidden) { authoring_hidden.insert(index); } else { authoring_hidden.erase(index); }
    if(locked) { authoring_locked.insert(index); } else { authoring_locked.erase(index); }
    sync_authoring_preview(); queue_redraw();
}
void ECSUICanvasEditor::set_authoring_option(bool enabled,int option) {
	if(option==0) { show_bones=enabled; } else if(option==1) { show_images=enabled; } else if(option==2) { pick_bones=enabled; } else if(option==3) { pick_images=enabled; } queue_redraw();
}
void ECSUICanvasEditor::set_tool(int p_tool) {
	if (p_tool < 0 || p_tool > 7 || (p_tool>=6 && !skeleton_authoring)) { return; }
	creating_bone=false;
	finish_drag(false);
	panning = false;
	tool_mode = p_tool;
	for (int i = 0; i < 6; i++) { tool_buttons[i]->set_pressed_no_signal(i == p_tool); }
	set_default_cursor_shape(p_tool == 6 ? CURSOR_CROSS : p_tool == 0 ? CURSOR_DRAG : CURSOR_ARROW);
	if(is_inside_tree()) { grab_focus(); }
	queue_redraw();
}
ECSUICanvasEditor::~ECSUICanvasEditor() {
	renderer.clear();
}
void ECSUICanvasEditor::edit_scene(const Ref<ECSScene> &value, int selection) {
	finish_tilemap(false);
	finish_mesh(false);
	finish_paint(false);
	runtime_preview=false; bone_drag=false;
	dragging = false;
	if (scene != value) {
        authoring_hidden.clear(); authoring_locked.clear();
		center_origin_pending=skeleton_authoring;
		zoom = 1;
		pan = skeleton_authoring?get_size()*.5f:Vector2(32, 32) * EDSCALE;
	}
	scene = value;
	selected = selection;
	renderer.clear();
	preview.unref();
	ids.clear();
	if (scene.is_null()) {
		queue_redraw();
		return;
	}
	canvas_size = Size2(MAX(1, int(GLOBAL_GET("display/window/size/viewport_width"))), MAX(1, int(GLOBAL_GET("display/window/size/viewport_height"))));
	viewport->set_size(skeleton_authoring?Size2(MAX(1.0f,get_size().x),MAX(1.0f,get_size().y)):canvas_size);
	// Build a visual-only scene: no physics, audio, scripts or game systems.
	Ref<ECSScene> visual; visual.instantiate();
	Array entities = scene->get_entities().duplicate(true);
	for (int i = 0; i < entities.size(); i++) {
		Dictionary source = entities[i], filtered;
		for (const Variant &key : source.keys()) {
			String kind = key;
			if (kind == "tilemap_2d") {
				Dictionary tilemap = Dictionary(source[key]).duplicate(true);
				tilemap["collision_enabled"] = false;
				tilemap["navigation_enabled"] = false;
				filtered[key] = tilemap;
			}
			if (kind == "sprite_frames" || kind == "theme" || kind == "position" || kind == "rotation" || kind == "scale" || kind == "shear" || kind == "parent" || kind == "active" || kind == "bone_2d" || kind == "skeleton_2d" || kind == "polygon_2d" || kind == "ui" || ECSUIComponents::is_component(kind)) { filtered[key] = source[key]; }
		}
		entities[i] = filtered;
	}
	visual->set_entities(entities);
	preview = visual->instantiate();
	if (preview.is_null()) { queue_redraw(); return; }
	ids = preview->query(PackedStringArray(),true);
	sync_authoring_preview();
	renderer.attach(preview, viewport->get_viewport_rid());
	queue_redraw();
}

void ECSUICanvasEditor::preview_setup_pose(const Array &entities) {
	if(preview.is_null() || entities.size()!=ids.size()) { return; }
	for(int i=0;i<ids.size();i++) {
		Dictionary entity=entities[i];
		preview->set_vector(ids[i],"position",entity.get("position",Vector3()));
		preview->set_vector(ids[i],"rotation",entity.get("rotation",Vector3()));
		preview->set_vector(ids[i],"scale",entity.get("scale",Vector3(1,1,1)));
		preview->set_vector(ids[i],"shear",entity.get("shear",Vector3()));
	}
	for(int i=0;i<ids.size();i++) {
		Dictionary entity=entities[i];
		if(entity.has("skeleton_2d")) {
			Dictionary rig=Dictionary(entity["skeleton_2d"]).duplicate(true);
			PackedInt64Array bones=rig.get("bones",PackedInt64Array());
			for(int j=0;j<bones.size();j++) { if(bones[j]<0 || bones[j]>=ids.size()) { return; } bones.set(j,ids[bones[j]]); }
			rig["bones"]=bones;
			HashMap<uint64_t,uint64_t> map; for(int j=0;j<ids.size();j++) { map[j]=ids[j]; } if(!ECSWorld::remap_skeleton_slots(rig,map)) { return; }
			if(!rig.has("bind_poses")) { rig["bind_poses"]=Array(); }
			preview->set_skeleton_2d(ids[i],rig);
		}
		update_runtime_definition(i,entity);
	}
	sync_authoring_preview(); queue_redraw();
}
void ECSUICanvasEditor::update_runtime_preview(const Array &entities) {
	if(preview.is_null() || entities.size()!=ids.size()) { return; }
	runtime_preview=true;
	for(int i=0;i<ids.size();i++) {
		if(bone_drag && i==selected) { continue; }
		Dictionary data=entities[i]; Transform3D t=data.get("transform",Transform3D());
		preview->remove_animation(ids[i]); preview->set_parent(ids[i],0);
		preview->set_vector(ids[i],"position",t.origin); preview->set_vector(ids[i],"rotation",t.basis.get_euler_normalized()); preview->set_vector(ids[i],"scale",t.basis.get_scale());
		preview->set_active(ids[i],data.get("active",false));
	}
	sync_authoring_preview(); queue_redraw();
}
void ECSUICanvasEditor::update_runtime_definition(int index,const Dictionary &entity) {
	if(preview.is_null() || index<0 || index>=ids.size()) { return; }
	if(entity.has("bone_2d")) { preview->set_bone_2d(ids[index],entity["bone_2d"]); }
	if(entity.has("polygon_2d")) {
		Dictionary polygon=Dictionary(entity["polygon_2d"]).duplicate(true); int rig=polygon.get("skeleton",-1);
		if(rig < -1 || rig>=ids.size()) { return; } polygon["skeleton"]=rig<0?int64_t(0):ids[rig]; preview->set_polygon_2d(ids[index],polygon);
	}
}
bool ECSUICanvasEditor::preview_animation(int owner, double time, bool playing, const String &state) {
	if (preview.is_null() || scene.is_null() || owner < 0 || owner >= ids.size()) { return false; }
	Dictionary entity = scene->get_entities()[owner];
	if (!entity.has("animation")) { return false; }
	Dictionary animation = Dictionary(entity["animation"]).duplicate(true);
	PackedInt64Array targets = animation.get("targets", PackedInt64Array());
	for (int i=0;i<targets.size();i++) { if(targets[i]<0 || targets[i]>=ids.size()) { return false; } targets.set(i,ids[targets[i]]); }
	if(animation.has("targets")) { animation["targets"]=targets; }
	if(!state.is_empty()) {
		Dictionary states=animation.get("states",Dictionary()); if(!states.has(state)) { return false; }
		animation["clip"]=states[state]; animation["state"]=state;
	}
	animation["time"]=time; animation["playing"]=true;
	if(!preview->set_animation(ids[owner],animation)) { return false; }
	preview->advance_animation_preview(0);
	Dictionary patch; patch["playing"]=playing; preview->set_animation(ids[owner],patch);
	sync_authoring_preview(); queue_redraw(); return true;
}
bool ECSUICanvasEditor::transition_animation_preview(int owner,const String &state,double duration) {
	return preview.is_valid() && owner>=0 && owner<ids.size() && preview->travel_animation(ids[owner],state,duration);
}
void ECSUICanvasEditor::stop_animation_preview() { edit_scene(scene,selected); }
Rect2 ECSUICanvasEditor::get_selected_rect() {
	return preview.is_valid() && selected >= 0 && selected < ids.size() ? renderer.editor_rect(ids[selected], canvas_size) : Rect2();
}
Vector2 ECSUICanvasEditor::selected_axis(int axis) const {
	Vector2 fallback=axis==0?Vector2(1,0):Vector2(0,-1);
	if(preview.is_null() || selected<0 || selected>=ids.size() || !preview->get_ui(ids[selected]).is_empty()) { return fallback; }
	if(skeleton_authoring && axis_space==2) { return fallback; }
	uint64_t source=skeleton_authoring && axis_space==1?preview->get_parent(ids[selected]):ids[selected]; if(!source) { return fallback; }
	const Basis basis=preview->get_global_transform(source).basis;
	Vector3 column=basis.get_column(axis)*(axis==0?1.0:-1.0);
	Vector2 direction(column.x,column.y);
	return direction.length_squared()>0.000001?direction.normalized():fallback;
}
Vector2 ECSUICanvasEditor::selected_pivot() {
	if (preview.is_valid() && selected >= 0 && selected < ids.size() && preview->get_ui(ids[selected]).is_empty()) {
		Vector3 origin = preview->get_global_transform(ids[selected]).origin;
		return Vector2(origin.x, origin.y);
	}
	Vector2 pivot(.5, .5);
	if (scene.is_valid() && selected >= 0 && selected < scene->get_entities().size()) {
		Dictionary entity = scene->get_entities()[selected];
		Dictionary layout = entity.get("ui_layout", Dictionary());
		Variant value = layout.get("pivot", pivot);
		if (value.get_type() == Variant::VECTOR2 && Vector2(value).is_finite()) { pivot = value; }
	}
	Rect2 rect = get_selected_rect();
	return rect.position + rect.size * pivot;
}
bool ECSUICanvasEditor::frame_selected_attachment() {
    if(!skeleton_authoring || preview.is_null() || selected<0 || selected>=ids.size()) { return false; }
    PackedVector2Array points=preview->get_deformed_polygon_2d(ids[selected]);
    if(points.is_empty()) { return false; }
    Rect2 bounds(points[0],Vector2());
    for(const Vector2 &point:points) { if(!point.is_finite()) { return false; } bounds.expand_to(point); }
    finish_drag(false);
    Vector2 available(MAX(1.0f,get_size().x-100*EDSCALE),MAX(1.0f,get_size().y-140*EDSCALE));
    zoom=CLAMP(MIN(available.x/MAX(1.0f,bounds.size.x),available.y/MAX(1.0f,bounds.size.y)),.25f,16.0f);
    pan=Vector2(50,35)*EDSCALE+available*.5f-bounds.get_center()*zoom;
    center_origin_pending=false;
    queue_redraw();
    return true;
}
void ECSUICanvasEditor::fit_canvas() {
	finish_drag(false);
	if(skeleton_authoring && preview.is_valid()) {
		Rect2 bounds; bool found=false;
		auto expand=[&](Vector2 point) { if(!found) { bounds=Rect2(point,Vector2()); found=true; } else { bounds.expand_to(point); } };
		for(uint64_t id:ids) { if(preview->is_skeleton_attachment_visible(id)) { for(const Vector2 &point:preview->get_deformed_polygon_2d(id)) { expand(point); } } Dictionary bone=preview->get_bone_2d(id); if(!bone.is_empty()) { Transform3D transform=preview->get_global_transform(id); Vector3 end=transform.xform(Vector3(double(bone["length"]),0,0)); expand(Vector2(transform.origin.x,transform.origin.y)); expand(Vector2(end.x,end.y)); } }
		if(found) { Vector2 available=Vector2(MAX(100.0f,get_size().x-80*EDSCALE),MAX(100.0f,get_size().y-240*EDSCALE)); zoom=CLAMP(MIN(available.x/MAX(1.0f,bounds.size.x),available.y/MAX(1.0f,bounds.size.y)),.25f,4.0f); pan=Vector2(40,40)*EDSCALE+available*.5f-bounds.get_center()*zoom; queue_redraw(); return; }
	}
	zoom = 1;
	pan = skeleton_authoring?get_size()*.5f:Vector2(32, 32) * EDSCALE;
	queue_redraw();
}
void ECSUICanvasEditor::set_preview_zoom(float p_scale) {
	finish_drag(false);
	const Vector2 center = get_size() * .5f;
	const Vector2 at = (center - pan) / display_scale();
	const float base_scale = display_scale() / zoom;
	zoom = CLAMP(p_scale / base_scale, .25f, 16.0f);
	pan = center - at * display_scale();
	queue_redraw();
}
void ECSUICanvasEditor::_notification(int what) {
	if (what == NOTIFICATION_THEME_CHANGED && fit_button) {
		fit_button->set_button_icon(get_editor_theme_icon(SNAME("CenterView")));
		const char *icons[] = { "ToolPan", "ToolMove", "ToolRotate", "ToolScale", "ECSToolRect", "ToolTransform" };
		for (int i = 0; i < 6; i++) {
			if (tool_buttons[i]) { tool_buttons[i]->set_button_icon(get_editor_theme_icon(StringName(icons[i]))); }
		}
	}
	if (what == NOTIFICATION_PROCESS && is_visible_in_tree()) {
		if(skeleton_authoring) {
			if(center_origin_pending) { pan=get_size()*.5f; center_origin_pending=false; fit_canvas(); }
			else if(authoring_view_size!=Size2() && authoring_view_size!=get_size()) { pan+=(get_size()-authoring_view_size)*.5f; }
			authoring_view_size=get_size();
			Size2i size(MAX(1,int(get_size().x)),MAX(1,int(get_size().y))); if(viewport->get_size()!=size) { viewport->set_size(size); }
			viewport->set_global_canvas_transform(Transform2D(0,Vector2(zoom,zoom),0,pan));
		}
		if (preview.is_valid()) { preview->advance_animation_preview(get_process_delta_time()); sync_authoring_preview(); }
		renderer.draw(canvas_size);
		const float base_scale = display_scale() / zoom;
		zoom_widget->set_block_signals(true);
		zoom_widget->setup_zoom_limits(base_scale * .25f, base_scale * 16.0f);
		zoom_widget->set_zoom(display_scale());
		zoom_widget->set_block_signals(false);
		queue_redraw();
	}
	if (what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree()) {
		finish_mesh(false);
		creating_bone=false;
		finish_paint(false);
		finish_drag(false);
		panning = false;
	}
	if (what == NOTIFICATION_DRAW) {
		draw_rect(Rect2(Vector2(), get_size()), get_theme_color("base_color", "Editor").lerp(get_theme_color("mono_color", "Editor"), .18));
		if (preview.is_null()) {

			draw_origin_guides();
			draw_rulers();
			return;
		}
		float scale = display_scale();
		const Rect2 canvas_rect(pan, canvas_size * scale);
		draw_rect(canvas_rect, get_theme_color("dark_color_1", "Editor"));
		if (skeleton_authoring) {
			const float cell = MAX(1.0f, 96.0f * scale);
			const Vector2 offset(Math::fposmod(pan.x, cell * 2), Math::fposmod(pan.y, cell * 2));
			for (int y = -2; y * cell + offset.y < get_size().y; y++) {
				for (int x = -2; x * cell + offset.x < get_size().x; x++) {
					draw_rect(Rect2(Vector2(x * cell, y * cell) + offset, Vector2(cell, cell)), (x + y) % 2 ? Color(.32,.32,.33) : Color(.35,.35,.36));
				}
			}
		}
		draw_origin_guides();
		if(!skeleton_authoring || show_images) { draw_texture_rect(viewport->get_texture(), skeleton_authoring?Rect2(Vector2(),get_size()):canvas_rect, false); }
		if (grid_visible && !skeleton_authoring) {
			float step = 50;
			while (step * scale < 24 * EDSCALE) { step *= 2; }
			while (step * scale > 96 * EDSCALE) { step *= .5f; }
			const float spacing = step * scale;
			Color color = get_theme_color("font_color", "Editor"); color.a = .09;
			for (float x = Math::fposmod(pan.x, spacing); x < get_size().x; x += spacing) {
				draw_line(Vector2(x, 18 * EDSCALE), Vector2(x, get_size().y), color);
			}
			for (float y = Math::fposmod(pan.y, spacing); y < get_size().y; y += spacing) {
				draw_line(Vector2(18 * EDSCALE, y), Vector2(get_size().x, y), color);
			}
		}
		for(int i=0;i<ids.size();i++) {
			Dictionary bone=preview->get_bone_2d(ids[i]); if(bone.is_empty() || authoring_hidden.has(i) || (skeleton_authoring && !show_bones) || !preview->is_active_in_hierarchy(ids[i])) { continue; }
			Transform3D transform=preview->get_global_transform(ids[i]);
			Vector3 end=transform.xform(Vector3(double(bone["length"]),0,0));
			Vector2 a=Vector2(transform.origin.x,transform.origin.y)*scale+pan,b=Vector2(end.x,end.y)*scale+pan;
			// Keep the root ring and tapered body readable at every canvas zoom.
			const Color color=i==selected?Color(.36,.70,1):Color(.70,.72,.74);
			const float radius=7*EDSCALE;
			const Vector2 delta=b-a;
			if(delta.length()>radius*1.5f) {
				const Vector2 direction=delta.normalized(),side(-direction.y,direction.x);
				const float width=MIN(7*EDSCALE,delta.length()*.16f);
				const Vector2 base=a+direction*(radius+2*EDSCALE);
				PackedVector2Array body({base+side*width,b,base-side*width});
				draw_colored_polygon(body,color);
				body.push_back(body[0]);
				draw_polyline(body,Color(.12,.13,.15,.8),1*EDSCALE,true);
			}
			draw_arc(a,radius,0,Math::TAU,40,Color(.12,.13,.15,.8),4*EDSCALE,true);
			draw_arc(a,radius,0,Math::TAU,40,color,2*EDSCALE,true);
		}
		if(!skeleton_authoring) {
		Color canvas_border = get_theme_color("font_color", "Editor");
		canvas_border.a = .65;
		draw_rect(canvas_rect, canvas_border, false, 1);
		Ref<Font> canvas_font = get_theme_font("font", "Label");
		const int canvas_font_size = MAX(10, int(12 * EDSCALE));
		const Vector2 caption(MAX(22.0f * EDSCALE, pan.x + 5 * EDSCALE), MIN(get_size().y - 5 * EDSCALE, MAX(60.0f * EDSCALE, canvas_rect.get_end().y + 18 * EDSCALE)));
		draw_string(canvas_font, caption, vformat(String(U"%d × %d px"), int(canvas_size.x), int(canvas_size.y)), HORIZONTAL_ALIGNMENT_LEFT, -1, canvas_font_size, get_theme_color("font_color", "Editor"));
		}
        // Show the selected mesh in its evaluated pose, independently of the transform tool.
        // Boundary edges omit interior triangulation; hidden/inactive variants stay hidden.
        if(skeleton_authoring && show_images && selected>=0 && selected<ids.size() && !authoring_hidden.has(selected) && preview->is_active_in_hierarchy(ids[selected]) && preview->is_skeleton_attachment_visible(ids[selected])) {
            Dictionary mesh=preview->get_polygon_2d(ids[selected]);
            PackedVector2Array points=preview->get_deformed_polygon_2d(ids[selected]);
            PackedInt32Array triangles=mesh.get("triangles",PackedInt32Array());
            if(triangles.is_empty()) { triangles=Geometry2D::triangulate_polygon(PackedVector2Array(mesh.get("polygon",PackedVector2Array()))); }
            HashMap<uint64_t,int> edges;
            for(int t=0;t+2<triangles.size();t+=3) { for(int j=0;j<3;j++) {
                int a=triangles[t+j],b=triangles[t+(j+1)%3];
                if(a<0 || b<0 || a>=points.size() || b>=points.size()) { continue; }
                uint64_t key=(uint64_t(MIN(a,b))<<32)|uint32_t(MAX(a,b));
                int *count=edges.getptr(key); if(count) { ++*count; } else { edges.insert(key,1); }
            } }
            for(const KeyValue<uint64_t,int> &edge:edges) { if(edge.value==1) {
                draw_line(points[int(edge.key>>32)]*scale+pan,points[int(uint32_t(edge.key))]*scale+pan,get_theme_color("accent_color","Editor"),2*EDSCALE,true);
            } }
            // Selection keeps a fixed-size origin marker even outside transform tools.
            if(!points.is_empty() && tool_mode!=1 && tool_mode!=2 && tool_mode!=3 && tool_mode!=5) {
                Vector2 origin=selected_pivot()*scale+pan;
                for(Vector2 axis:{Vector2(1,0),Vector2(0,1)}) {
                    draw_line(origin-axis*6*EDSCALE,origin+axis*6*EDSCALE,Color(0,0,0,.8),3*EDSCALE,true);
                    draw_line(origin-axis*6*EDSCALE,origin+axis*6*EDSCALE,Color(.95,.95,.95),EDSCALE,true);
                }
            }
        }
		if (selected >= 0 && !authoring_locked.has(selected) && !authoring_hidden.has(selected) && selected < ids.size() && (!preview->get_ui(ids[selected]).is_empty() || !preview->get_bone_2d(ids[selected]).is_empty() || !preview->get_polygon_2d(ids[selected]).is_empty() || !preview->get_skeleton_2d(ids[selected]).is_empty())) {
			const bool is_ui = !preview->get_ui(ids[selected]).is_empty();
			Rect2 rect = get_selected_rect();
			rect.position = rect.position * scale + pan;
			rect.size *= scale;
			if (is_ui) { draw_rect(rect, get_theme_color("accent_color", "Editor"), false, 2); }
			if (tool_mode == 1 || tool_mode == 5) {
				const Vector2 origin = selected_pivot() * scale + pan;
				const float length = 64 * EDSCALE;
				const Color axis_colors[] = { Color(.95, .25, .2), Color(.45, .95, .2) };
				const Vector2 directions[] = { selected_axis(0), selected_axis(1) };
				for (int axis = 0; axis < 2; axis++) {
					const Vector2 direction = directions[axis], side(-direction.y, direction.x);
					const Vector2 end = origin + direction * length;
					draw_line(origin, end, axis_colors[axis], 2 * EDSCALE, true);
					PackedVector2Array arrow;
					arrow.push_back(end);
					arrow.push_back(end - direction * 11 * EDSCALE + side * 5 * EDSCALE);
					arrow.push_back(end - direction * 11 * EDSCALE - side * 5 * EDSCALE);
					draw_primitive(arrow, PackedColorArray({ axis_colors[axis] }), PackedVector2Array());
				}
				draw_rect(Rect2(origin - Vector2(5, 5) * EDSCALE, Vector2(10, 10) * EDSCALE), get_theme_color("accent_color", "Editor"));
				draw_rect(Rect2(origin - Vector2(5, 5) * EDSCALE, Vector2(10, 10) * EDSCALE), get_theme_color("font_color", "Editor"), false, 1);
			}
			if (skeleton_authoring && !is_ui && (tool_mode == 2 || tool_mode == 3)) {
				const Vector2 origin = selected_pivot() * scale + pan;
				const Color accent = get_theme_color("accent_color", "Editor");
				if (tool_mode == 2) {
					draw_arc(origin, 52 * EDSCALE, 0, Math::TAU, 96, accent, 2 * EDSCALE, true);
					draw_circle(origin, 3 * EDSCALE, accent);
				} else {
					for (int axis = 0; axis < 2; axis++) {
						Vector2 end = origin + selected_axis(axis) * 64 * EDSCALE;
						Color color = axis == 0 ? Color(.95,.3,.25) : Color(.45,.85,.3);
						draw_line(origin, end, color, 2 * EDSCALE, true);
						draw_rect(Rect2(end - Vector2(5,5)*EDSCALE, Vector2(10,10)*EDSCALE), color);
					}
					draw_rect(Rect2(origin - Vector2(6,6)*EDSCALE, Vector2(12,12)*EDSCALE), accent);
				}
			}
			if (is_ui && tool_mode >= 3) {
				const Vector2 corners[] = { rect.position, Vector2(rect.get_end().x, rect.position.y), rect.get_end(), Vector2(rect.position.x, rect.get_end().y) };
				for (const Vector2 &corner : corners) {
					const Rect2 handle(corner - Vector2(4, 4) * EDSCALE, Vector2(8, 8) * EDSCALE);
					draw_rect(handle, get_theme_color("accent_color", "Editor"));
					draw_rect(handle, get_theme_color("font_color", "Editor"), false, 1);
				}
			}
		}
		if(skeleton_authoring && !keyframe_edit_mode && weight_bone>=0 && selected>=0 && selected<ids.size()) {
			draw_arc(brush_cursor,weight_radius,0,Math::TAU,48,Color(1,.65,.15),1.5,true);
			Dictionary mesh=preview->get_polygon_2d(ids[selected]); if(!mesh.is_empty()) {
				Dictionary rig=preview->get_skeleton_2d(int64_t(mesh["skeleton"])); PackedInt64Array bones=rig.get("bones",PackedInt64Array()); int bone=weight_bone<ids.size()?bones.find(ids[weight_bone]):-1;
				PackedVector2Array points=preview->get_deformed_polygon_2d(ids[selected]); PackedInt32Array indices=mesh["bones"]; PackedFloat32Array weights=mesh["weights"];
				for(int i=0;i<points.size() && indices.size()==points.size()*4;i++) { float value=0; for(int j=0;j<4;j++) { if(indices[i*4+j]==bone) { value+=weights[i*4+j]; } } Vector3 p(points[i].x,points[i].y,0); draw_circle(Vector2(p.x,p.y)*scale+pan,4,Color(.1,.3,1).lerp(Color(1,.15,.05),value)); }
			}
		}
			if(mesh_edit_mode && skeleton_authoring && !keyframe_edit_mode && !authoring_locked.has(selected) && !authoring_hidden.has(selected) && selected>=0 && selected<ids.size()) {
			Dictionary mesh=preview->get_polygon_2d(ids[selected]); PackedVector2Array points=preview->get_deformed_polygon_2d(ids[selected]);
			PackedInt32Array triangles=mesh.get("triangles",PackedInt32Array()); if(triangles.is_empty()) { triangles=Geometry2D::triangulate_polygon(PackedVector2Array(mesh.get("polygon",PackedVector2Array()))); }
			for(int t=0;t+2<triangles.size();t+=3) { for(int j=0;j<3;j++) { int a=triangles[t+j],b=triangles[t+(j+1)%3]; if(a<points.size() && b<points.size()) { draw_line(points[a]*scale+pan,points[b]*scale+pan,Color(.2,.85,1,.8),1,true); } } }
			for(int i=0;i<points.size();i++) { draw_circle(points[i]*scale+pan,4*EDSCALE,i==mesh_vertex?Color(1,.7,.1):Color(.2,.85,1)); }
		}
		if(creating_bone) {
			Vector2 a=create_bone_start*scale+pan,b=create_bone_end*scale+pan;
			draw_line(a,b,Color(.15,.85,.95),3*EDSCALE,true); draw_circle(a,4*EDSCALE,Color(.15,.85,.95)); draw_circle(b,4*EDSCALE,Color(.8,.95,1),false,EDSCALE,true);
		}
		draw_rulers();
	}
}
void ECSUICanvasEditor::draw_origin_guides() {
	if (!grid_visible) { return; }
	const float band = Math::round(18 * EDSCALE);
	const Vector2 extent = get_size();
	// The axes and ruler labels share the same canvas-to-screen transform as picking.
	if (pan.y >= band && pan.y < extent.y) {
		draw_line(Vector2(band, pan.y), Vector2(extent.x, pan.y), Color(.96, .25, .35), 1);
	}
	if (pan.x >= band && pan.x < extent.x) {
		draw_line(Vector2(pan.x, band), Vector2(pan.x, extent.y), Color(.55, .75, .15), 1);
	}
}
void ECSUICanvasEditor::draw_rulers() {
	const float band = Math::round(18 * EDSCALE);
	const float scale = display_scale();
	const Vector2 extent = get_size();
	const Color background = get_theme_color("dark_color_1", "Editor");
	const Color text = get_theme_color("font_color", "Editor");
	Color tick = text;
	tick.a = .45;
	draw_rect(Rect2(0, 0, extent.x, band), background);
	draw_rect(Rect2(0, 0, band, extent.y), background);
	if (skeleton_authoring) {
		draw_rect(Rect2(0, extent.y - band, extent.x, band), background);
		draw_rect(Rect2(extent.x - band, 0, band, extent.y), background);
	}
	Ref<Font> font = get_theme_font("font", "Label");
	const int font_size = MAX(8, int(10 * EDSCALE));
	const double desired = 100.0 * EDSCALE / scale;
	const double decade = Math::pow(10.0, Math::floor(Math::log(desired) / Math::log(10.0)));
	const double fraction = desired / decade;
	const double major = decade * (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10);
	const double minor = major / 5.0;
	for (int axis = 0; axis < 2; axis++) {
		const double origin = pan[axis];
		const int64_t first = int64_t(Math::ceil((band - origin) / (minor * scale)));
		const int64_t last = int64_t(Math::floor((extent[axis] - origin) / (minor * scale)));
		for (int64_t index = first; index <= last; index++) {
			const float screen = Math::round(origin + index * minor * scale);
			const bool primary = index % 5 == 0;
			const float length = (primary ? 8 : 4) * EDSCALE;
			if (axis == 0) {
				draw_line(Vector2(screen, band - length), Vector2(screen, band), tick, 1);
				if (skeleton_authoring) { draw_line(Vector2(screen, extent.y - band), Vector2(screen, extent.y - band + length), tick, 1); }
			} else {
				draw_line(Vector2(band - length, screen), Vector2(band, screen), tick, 1);
				if (skeleton_authoring) { draw_line(Vector2(extent.x - band, screen), Vector2(extent.x - band + length, screen), tick, 1); }
			}
			if (primary) {
				const String label = String::num(index * minor, major < 1 ? 2 : 0);
				if (axis == 0) {
					draw_string(font, Vector2(screen + 3 * EDSCALE, font->get_ascent(font_size)), label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text);
					if (skeleton_authoring) { draw_string(font, Vector2(screen + 3 * EDSCALE, extent.y - band + font->get_ascent(font_size)), label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text); }
				} else {
					draw_set_transform(Vector2(font->get_ascent(font_size), screen - 3 * EDSCALE), -Math::PI / 2);
					draw_string(font, Vector2(), label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text);
					draw_set_transform(Vector2(), 0);
					if (skeleton_authoring) {
						draw_set_transform(Vector2(extent.x - band + font->get_ascent(font_size), screen - 3 * EDSCALE), -Math::PI / 2);
						draw_string(font, Vector2(), label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text);
						draw_set_transform(Vector2(), 0);
					}
				}
			}
		}
	}
	draw_rect(Rect2(0, 0, band, band), background);
}
void ECSUICanvasEditor::finish_drag(bool commit) {
	if (!dragging) {
		return;
	}
	dragging = false;
	if (scene.is_null() || preview.is_null() || selected < 0 || selected >= ids.size()) {
		return;
	}
	if (bone_drag) {
		bone_drag=false; String field=tool_mode==2?"rotation":tool_mode==3?"scale":"position";
		Vector3 value=preview->get_vector(ids[selected],field);
		if(runtime_preview) { if(!commit) { preview->set_vector(ids[selected],field,bone_drag_value); } emit_signal("runtime_transform_changed",selected,preview->get_global_transform(ids[selected])); return; }
		if(keyframe_edit_mode) { if(commit) { emit_signal("bone_pose_edited",selected,field,value); } else { preview->set_vector(ids[selected],field,bone_drag_value); } return; }
		if(skeleton_authoring && setup_pose_callback.is_valid()) {
			Array after=setup_pose_after; setup_pose_after=Array();
			if(!commit || after.is_empty() || value.is_equal_approx(bone_drag_value)) { preview_setup_pose(scene->get_entities()); return; }
			auto *undo=EditorUndoRedoManager::get_singleton();
			undo->create_action(String(U"调整骨骼姿态与补偿"),UndoRedo::MERGE_DISABLE,scene.ptr());
			undo->add_do_method(scene.ptr(),"set_entities",after); undo->add_undo_method(scene.ptr(),"set_entities",scene->get_entities()); undo->commit_action(); return;
		}
		if(!commit || value.is_equal_approx(bone_drag_value)) { preview->set_vector(ids[selected],field,bone_drag_value); queue_redraw(); return; }
		Array before=scene->get_entities(),after=before.duplicate(true); Dictionary entity=after[selected]; entity[field]=value;
		auto *undo=EditorUndoRedoManager::get_singleton(); undo->create_action(String(U"调整 2D 骨骼姿态"),UndoRedo::MERGE_DISABLE,scene.ptr()); undo->add_do_method(scene.ptr(),"set_entities",after); undo->add_undo_method(scene.ptr(),"set_entities",before); undo->commit_action(); return;
	}
	Dictionary ui = preview->get_ui(ids[selected]);
	Rect2 rect = ui.get("rect", drag_rect);
	if (!commit || rect == drag_rect) {
		Dictionary restore;
		restore["rect"] = drag_rect;
		preview->set_ui(ids[selected], restore);
		queue_redraw();
		return;
	}
	Array before = scene->get_entities();
	if (selected >= before.size() || before[selected].get_type() != Variant::DICTIONARY) {
		return;
	}
	Array after = before.duplicate(true);
	Dictionary entity = after[selected];
	String layout_key = entity.has("ui_layout") ? "ui_layout" : "ui";
	if (!entity.has(layout_key) || entity[layout_key].get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary definition = entity[layout_key];
	if (layout_key == "ui_layout") {
		Vector4 margins = definition.get("margins", Vector4());
		rect.position -= Vector2(margins.x, margins.y);
		rect.size += Vector2(margins.x + margins.z, margins.y + margins.w);
	}
	definition["rect"] = rect;
	auto *undo = EditorUndoRedoManager::get_singleton();
	undo->create_action(resizing ? String(U"调整 ECS UI 大小") : String(U"移动 ECS UI"), UndoRedo::MERGE_DISABLE, scene.ptr());
	undo->add_do_method(scene.ptr(), "set_entities", after);
	undo->add_undo_method(scene.ptr(), "set_entities", before);
	undo->commit_action();
	queue_redraw();
}
bool ECSUICanvasEditor::run_compensated_drag_self_test(const Ref<ECSScene> &resource) {
	edit_scene(resource,1); set_tool(1); set_axis_space(2);
	Array before=resource->get_entities().duplicate(true);
	Transform3D child_before=preview->get_global_transform(ids[2]);
	PackedVector2Array points_before=preview->get_deformed_polygon_2d(ids[3]);
	auto mouse=[&](Vector2 at,bool pressed) { Ref<InputEventMouseButton> e; e.instantiate(); e->set_button_index(MouseButton::LEFT); e->set_pressed(pressed); e->set_position(at); gui_input(e); };
	auto move=[&](Vector2 at) { Ref<InputEventMouseMotion> e; e.instantiate(); e->set_position(at); e->set_button_mask(MouseButtonMask::LEFT); gui_input(e); };
	Vector2 at=selected_pivot()*display_scale()+pan+Vector2(30,0)*EDSCALE;
	// Selecting and dragging with the dedicated selection tool must not edit a pose.
	set_tool(7); mouse(at,true); move(at+Vector2(20,0)*display_scale()); mouse(at+Vector2(20,0)*display_scale(),false);
	if(selected<0 || bone_drag || dragging || !preview->get_global_transform(ids[2]).is_equal_approx(child_before) || resource->get_entities()!=before) { return false; }
	print_line("SKELETON_SELECT_ONLY_PASS selection_drag_preserves_pose");
	// Gizmo hits must retain the selected bone even when the handle is over empty space.
	for(int handle=0;handle<4;handle++) {
		edit_scene(resource,1); set_tool(handle==0?2:3);
		Vector2 origin=selected_pivot()*display_scale()+pan;
		Vector2 direction=handle==2?selected_axis(1):selected_axis(0);
		Vector2 start=handle==0?origin+Vector2(52,0)*EDSCALE:handle==3?origin:origin+direction*64*EDSCALE;
		Vector2 end=handle==0?origin+Vector2(0,52)*EDSCALE:handle==3?origin+Vector2(32,0)*EDSCALE:start+direction*32*EDSCALE;
		const String field=handle==0?"rotation":"scale";
		Vector3 old=preview->get_vector(ids[1],field);
		mouse(start,true); if(!bone_drag || selected!=1 || transform_handle!=(handle==0||handle==3?2:handle-1)) { return false; }
		move(end); mouse(end,false);
		Vector3 result=Dictionary(resource->get_entities()[1]).get(field,field=="scale"?Vector3(1,1,1):Vector3());
		Vector3 expected=old;
		if(handle==0) { expected.z+=Math::PI/2; } else { if(handle!=2) { expected.x*=1.5; } if(handle!=1) { expected.y*=1.5; } }
		if(!result.is_equal_approx(expected) || !EditorUndoRedoManager::get_singleton()->undo()) { return false; }
		edit_scene(resource,1); mouse(start,true); move(end);
		Ref<InputEventKey> cancel; cancel.instantiate(); cancel->set_keycode(Key::ESCAPE); cancel->set_pressed(true); gui_input(cancel);
		if(resource->get_entities()!=before) { return false; }
	}
	print_line("SKELETON_TRANSFORM_HANDLES_PASS rotate x_scale y_scale uniform_scale selection undo escape");
	edit_scene(resource,1); set_tool(1); at=selected_pivot()*display_scale()+pan+Vector2(30,0)*EDSCALE;
	mouse(at,true); if(!bone_drag) { return false; }
	move(at+Vector2(20,0)*display_scale());
	if(!preview->get_global_transform(ids[2]).is_equal_approx(child_before)) { return false; }
	PackedVector2Array points=preview->get_deformed_polygon_2d(ids[3]);
	if(points.size()!=points_before.size()) { return false; }
	for(int i=0;i<points.size();i++) { if(!points[i].is_equal_approx(points_before[i])) { return false; } }
	mouse(at+Vector2(20,0)*display_scale(),false);
	Vector3 old_position=Dictionary(before[1]).get("position",Vector3());
	if(!Vector3(Dictionary(resource->get_entities()[1]).get("position",Vector3())).is_equal_approx(old_position+Vector3(20,0,0))) { return false; }
	auto *undo=EditorUndoRedoManager::get_singleton(); if(!undo->undo()) { return false; }
	if(Vector3(Dictionary(resource->get_entities()[1]).get("position",Vector3()))!=old_position) { return false; }
	if(!undo->redo() || !undo->undo()) { return false; }
	edit_scene(resource,1); at=selected_pivot()*display_scale()+pan+Vector2(30,0)*EDSCALE;
	mouse(at,true); move(at+Vector2(20,0)*display_scale());
	Ref<InputEventKey> escape; escape.instantiate(); escape->set_keycode(Key::ESCAPE); escape->set_pressed(true); gui_input(escape);
	if(!preview->get_global_transform(ids[2]).is_equal_approx(child_before) || Vector3(Dictionary(resource->get_entities()[1]).get("position",Vector3()))!=old_position) { return false; }
	set_axis_space(0);
	print_line("SKELETON_COMPENSATION_DRAG_PASS live_preview commit undo redo escape"); return true;
}
bool ECSUICanvasEditor::run_weight_brush_self_test(const Ref<ECSScene> &value) {
	Ref<ECSScene> previous=scene; int old_selection=selected; bool old_mode=keyframe_edit_mode;
	edit_scene(value,3); keyframe_edit_mode=false; configure_weight_brush(2,100,.5);
	Dictionary before=Dictionary(value->get_entities()[3])["polygon_2d"]; PackedVector2Array points=preview->get_deformed_polygon_2d(ids[3]); if(points.is_empty()) { return false; }
	auto click=[&](bool pressed) { Ref<InputEventMouseButton> event; event.instantiate(); event->set_button_index(MouseButton::LEFT); event->set_pressed(pressed); event->set_position(points[0]*display_scale()+pan); gui_input(event); };
	click(true); click(false); Dictionary after=Dictionary(value->get_entities()[3])["polygon_2d"]; bool ok=PackedFloat32Array(before["weights"])!=PackedFloat32Array(after["weights"]);
	ok &= EditorUndoRedoManager::get_singleton()->undo(); Dictionary restored=Dictionary(value->get_entities()[3])["polygon_2d"]; ok &= PackedFloat32Array(restored["weights"])==PackedFloat32Array(before["weights"]);
	edit_scene(value,3); click(true); Ref<InputEventKey> escape; escape.instantiate(); escape->set_keycode(Key::ESCAPE); escape->set_pressed(true); gui_input(escape); click(false); restored=Dictionary(value->get_entities()[3])["polygon_2d"]; ok &= PackedFloat32Array(restored["weights"])==PackedFloat32Array(before["weights"]);
	configure_weight_brush(-1,60,.15); keyframe_edit_mode=old_mode; edit_scene(previous,old_selection); return ok;
}
Dictionary ECSUICanvasEditor::paint_weights(const Dictionary &polygon,const Transform3D &transform,const Vector2 &center,float radius,int bone,float strength) {
	Dictionary result=polygon.duplicate(true); PackedVector2Array points=result.get("polygon",PackedVector2Array()); PackedInt32Array indices=result.get("bones",PackedInt32Array()); PackedFloat32Array weights=result.get("weights",PackedFloat32Array());
	if(radius<=0 || !Math::is_finite(radius) || !Math::is_finite(strength) || bone<0 || indices.size()!=points.size()*4 || weights.size()!=indices.size()) { return result; }
	for(int i=0;i<points.size();i++) {
		Vector3 world=transform.xform(Vector3(points[i].x,points[i].y,0)); double falloff=1.0-Vector2(world.x,world.y).distance_to(center)/radius; if(falloff<=0) { continue; }
		int slot=-1,weakest=i*4; for(int j=0;j<4;j++) { int n=i*4+j; if(indices[n]==bone && weights[n]>0) { slot=n; } if(weights[n]<weights[weakest]) { weakest=n; } }
		if(slot<0) { if(strength<0) { continue; } slot=weakest; indices.set(slot,bone); weights.set(slot,0); }
		double desired=CLAMP(double(weights[slot])+strength*falloff,0.0,1.0),other=0;
		for(int j=0;j<4;j++) { if(i*4+j!=slot) { other+=weights[i*4+j]; } }
		if(other<=1e-8) { desired=1; }
		for(int j=0;j<4;j++) { int n=i*4+j; weights.set(n,n==slot?desired:(other>1e-8?weights[n]*(1-desired)/other:0)); }
	}
	result["bones"]=indices; result["weights"]=weights; return result;
}
void ECSUICanvasEditor::paint_at(const Vector2 &position,bool subtract) {
	if(!painting || preview.is_null()) { return; }
	Dictionary entity=scene->get_entities()[selected],polygon=entity.get("polygon_2d",Dictionary()); int rig=polygon.get("skeleton",-1); if(rig<0 || rig>=ids.size()) { return; }
	Dictionary skeleton=Dictionary(scene->get_entities()[rig]).get("skeleton_2d",Dictionary()); PackedInt64Array bones=skeleton.get("bones",PackedInt64Array()); int bone=bones.find(weight_bone); if(bone<0) { return; }
	Dictionary geometry=stroke_data.duplicate(true); geometry["polygon"]=preview->get_deformed_polygon_2d(ids[selected]);
	Dictionary painted=paint_weights(geometry,Transform3D(),(position-pan)/display_scale(),weight_radius/display_scale(),bone,subtract?-weight_strength:weight_strength); stroke_data["bones"]=painted["bones"]; stroke_data["weights"]=painted["weights"];
	Dictionary update; update["bones"]=stroke_data["bones"]; update["weights"]=stroke_data["weights"]; preview->set_polygon_2d(ids[selected],update); queue_redraw();
}
void ECSUICanvasEditor::finish_paint(bool commit) {
	if(!painting) { return; } painting=false;
	if(!commit) { if(preview.is_valid() && selected>=0 && selected<ids.size()) { Dictionary restore; restore["bones"]=stroke_before["bones"]; restore["weights"]=stroke_before["weights"]; preview->set_polygon_2d(ids[selected],restore); } return; }
	Array before=scene->get_entities(),after=before.duplicate(true); Dictionary entity=after[selected]; entity["polygon_2d"]=stroke_data;
	auto *undo=EditorUndoRedoManager::get_singleton(); undo->create_action(String(U"绘制骨骼蒙皮权重"),UndoRedo::MERGE_DISABLE,scene.ptr()); undo->add_do_method(scene.ptr(),"set_entities",after); undo->add_undo_method(scene.ptr(),"set_entities",before); undo->commit_action();
}

void ECSUICanvasEditor::edit_mesh_topology(const Vector2 &position,bool remove) {
	Dictionary mesh=Dictionary(Dictionary(scene->get_entities()[selected])["polygon_2d"]).duplicate(true);
	PackedVector2Array points=mesh["polygon"],uv=mesh.get("uv",PackedVector2Array()),displayed=preview->get_deformed_polygon_2d(ids[selected]);
	PackedInt32Array triangles=mesh.get("triangles",PackedInt32Array()),bones=mesh.get("bones",PackedInt32Array()); PackedFloat32Array weights=mesh.get("weights",PackedFloat32Array());
	if(triangles.is_empty()) { triangles=Geometry2D::triangulate_polygon(points); }
	if(remove) {
		int vertex=-1; float best=10*EDSCALE; for(int i=0;i<displayed.size();i++) { float distance=(displayed[i]*display_scale()+pan).distance_to(position); if(distance<best) { best=distance; vertex=i; } }
		if(vertex<0 || points.size()<=3) { return; }
		HashMap<int,Vector<int>> adjacent; PackedInt32Array remaining;
		for(int t=0;t<triangles.size();t+=3) { int found=-1; for(int j=0;j<3;j++) { if(triangles[t+j]==vertex) { found=j; } } if(found<0) { for(int j=0;j<3;j++) { remaining.push_back(triangles[t+j]); } continue; } int a=triangles[t+(found+1)%3],b=triangles[t+(found+2)%3]; if(!adjacent.has(a)) { adjacent[a]=Vector<int>(); } if(!adjacent.has(b)) { adjacent[b]=Vector<int>(); } adjacent[a].push_back(b); adjacent[b].push_back(a); }
		if(adjacent.is_empty()) { return; }
		int start=adjacent.begin()->key; for(const KeyValue<int,Vector<int>> &entry:adjacent) { if(entry.value.size()>2) { return; } if(entry.value.size()==1) { start=entry.key; } }
		PackedInt32Array ring; int previous=-1,current=start;
		for(int step=0;step<int(adjacent.size());step++) { if(ring.has(current)) { return; } ring.push_back(current); int next=-1; for(int candidate:adjacent[current]) { if(candidate!=previous) { next=candidate; break; } } if(next<0 || next==start) { break; } previous=current; current=next; }
		if(ring.size()!=int(adjacent.size())) { return; }
		if(ring.size()>=3) { PackedVector2Array boundary; for(int index:ring) { boundary.push_back(points[index]); } PackedInt32Array fill=Geometry2D::triangulate_polygon(boundary); if(fill.is_empty()) { return; } for(int index:fill) { remaining.push_back(ring[index]); } }
		if(remaining.is_empty()) { return; }
		for(int i=0;i<remaining.size();i++) { if(remaining[i]>vertex) { remaining.set(i,remaining[i]-1); } }
		points.remove_at(vertex); if(!uv.is_empty()) { uv.remove_at(vertex); } if(!weights.is_empty()) { for(int j=0;j<4;j++) { bones.remove_at(vertex*4); weights.remove_at(vertex*4); } } triangles=remaining;
	} else {
		if(points.size()>=16384) { return; } Vector2 at=(position-pan)/display_scale(); int triangle=-1; Vector3 bary;
		for(int t=0;t<triangles.size();t+=3) { Vector2 a=displayed[triangles[t]],b=displayed[triangles[t+1]],c=displayed[triangles[t+2]]; float determinant=(b-a).cross(c-a); if(Math::abs(determinant)<.000001) { continue; } float y=(at-a).cross(c-a)/determinant,z=(b-a).cross(at-a)/determinant; if(y>.0001 && z>.0001 && y+z<.9999) { triangle=t; bary=Vector3(1-y-z,y,z); break; } }
		if(triangle<0) { return; } int n=points.size(),a=triangles[triangle],b=triangles[triangle+1],c=triangles[triangle+2]; points.push_back(points[a]*bary.x+points[b]*bary.y+points[c]*bary.z);
		if(!uv.is_empty()) { uv.push_back(uv[a]*bary.x+uv[b]*bary.y+uv[c]*bary.z); }
		if(!weights.is_empty()) { HashMap<int,float> influence; int vertices[3]={a,b,c}; for(int k=0;k<3;k++) { for(int j=0;j<4;j++) { int bone=bones[vertices[k]*4+j]; if(!influence.has(bone)) { influence[bone]=0; } influence[bone]+=weights[vertices[k]*4+j]*bary[k]; } } int chosen[4]={0,0,0,0}; float values[4]={0,0,0,0}; for(const KeyValue<int,float> &entry:influence) { for(int j=0;j<4;j++) { if(entry.value>values[j]) { for(int k=3;k>j;k--) { chosen[k]=chosen[k-1]; values[k]=values[k-1]; } chosen[j]=entry.key; values[j]=entry.value; break; } } } float sum=values[0]+values[1]+values[2]+values[3]; for(int j=0;j<4;j++) { bones.push_back(chosen[j]); weights.push_back(sum>0?values[j]/sum:(j==0?1:0)); } }
		triangles.set(triangle+2,n); for(int index:{b,c,n,c,a,n}) { triangles.push_back(index); }
	}
	mesh["polygon"]=points; mesh["uv"]=uv; mesh["triangles"]=triangles; if(!weights.is_empty()) { mesh["bones"]=bones; mesh["weights"]=weights; }
	emit_signal("mesh_edited",selected,mesh);
}
Dictionary ECSUICanvasEditor::remap_mesh_vertex(const Dictionary &mesh,const PackedVector2Array &displayed,int vertex,const Vector2 &position) {
    PackedVector2Array points=mesh.get("polygon",PackedVector2Array()),uv=mesh.get("uv",PackedVector2Array());
    if(vertex<0 || vertex>=points.size() || displayed.size()!=points.size() || !position.is_finite()) { return Dictionary(); }
    PackedInt32Array triangles=mesh.get("triangles",PackedInt32Array());
    if(triangles.is_empty()) { triangles=Geometry2D::triangulate_polygon(points); }
    // Use the mouse-down snapshot for every motion: no cumulative UV or weight drift.
    int chosen=-1; real_t best=1e30; Vector3 bary;
    for(int t=0;t+2<triangles.size();t+=3) {
        int a=triangles[t],b=triangles[t+1],c=triangles[t+2];
        if(a<0 || b<0 || c<0 || a>=points.size() || b>=points.size() || c>=points.size()) { return Dictionary(); }
        const Vector2 pa=displayed[a],pb=displayed[b],pc=displayed[c];
        real_t determinant=(pb-pa).cross(pc-pa);
        if(Math::abs(determinant)<.000001) { continue; }
        // Do not allow adjusting the topology to fold a triangle over its neighbour.
        if(a==vertex || b==vertex || c==vertex) {
            Vector2 na=a==vertex?position:pa,nb=b==vertex?position:pb,nc=c==vertex?position:pc;
            if((nb-na).cross(nc-na)/determinant<.0001) { return Dictionary(); }
        }
        real_t y=(position-pa).cross(pc-pa)/determinant,z=(pb-pa).cross(position-pa)/determinant;
        Vector3 candidate(1-y-z,y,z);
        real_t distance=0;
        if(candidate.x<0 || candidate.y<0 || candidate.z<0) {
            distance=1e30;
            Vector2 corners[3]={pa,pb,pc};
            for(int edge=0;edge<3;edge++) { Vector2 segment[2]={corners[edge],corners[(edge+1)%3]}; distance=MIN(distance,Geometry2D::get_closest_point_to_segment(position,segment).distance_squared_to(position)); }
        }
        if(distance<best) { best=distance; chosen=t; bary=candidate; }
    }
    if(chosen<0) { return Dictionary(); }
    int vertices[3]={triangles[chosen],triangles[chosen+1],triangles[chosen+2]};
    Dictionary result=mesh.duplicate(true);
    if(!uv.is_empty()) {
        if(uv.size()!=points.size()) { return Dictionary(); }
        Vector2 coordinate=uv[vertices[0]]*bary.x+uv[vertices[1]]*bary.y+uv[vertices[2]]*bary.z;
        if(!coordinate.is_finite()) { return Dictionary(); }
        bool normalized=true; for(const Vector2 &point:uv) { normalized &= point.x>=0 && point.x<=1 && point.y>=0 && point.y<=1; }
        if(normalized && (coordinate.x<-.00001 || coordinate.x>1.00001 || coordinate.y<-.00001 || coordinate.y>1.00001)) { return Dictionary(); }
        if(normalized) { coordinate=coordinate.clamp(Vector2(),Vector2(1,1)); }
        uv.set(vertex,coordinate); result["uv"]=uv;
    }
    PackedInt32Array bones=mesh.get("bones",PackedInt32Array()); PackedFloat32Array weights=mesh.get("weights",PackedFloat32Array());
    if(!weights.is_empty()) {
        if(weights.size()!=points.size()*4 || bones.size()!=weights.size()) { return Dictionary(); }
        // Outside the old contour, extend the nearest edge's influences without negative weights.
        Vector3 blend(MAX(real_t(0),bary.x),MAX(real_t(0),bary.y),MAX(real_t(0),bary.z));
        blend/=blend.x+blend.y+blend.z;
        HashMap<int,real_t> influences;
        for(int k=0;k<3;k++) { for(int j=0;j<4;j++) { int offset=vertices[k]*4+j; influences[bones[offset]]+=weights[offset]*blend[k]; } }
        int chosen_bones[4]={0,0,0,0}; real_t chosen_weights[4]={0,0,0,0};
        for(const KeyValue<int,real_t> &entry:influences) { for(int j=0;j<4;j++) { if(entry.value>chosen_weights[j]) { for(int k=3;k>j;k--) { chosen_weights[k]=chosen_weights[k-1]; chosen_bones[k]=chosen_bones[k-1]; } chosen_weights[j]=entry.value; chosen_bones[j]=entry.key; break; } } }
        real_t sum=chosen_weights[0]+chosen_weights[1]+chosen_weights[2]+chosen_weights[3];
        if(sum<=0) { return Dictionary(); }
        for(int j=0;j<4;j++) { bones.set(vertex*4+j,chosen_bones[j]); weights.set(vertex*4+j,chosen_weights[j]/sum); }
        result["bones"]=bones; result["weights"]=weights;
    }
    return result;
}
bool ECSUICanvasEditor::run_mesh_edit_self_test(const Ref<ECSScene> &value) {
    // An affine texture mapping must remain fixed when an interior point moves.
    Dictionary fixture; PackedVector2Array plane({Vector2(0,0),Vector2(10,0),Vector2(10,10),Vector2(0,10),Vector2(5,5)});
    PackedVector2Array texcoords; for(const Vector2 &point:plane) { texcoords.push_back(point/10); }
    fixture["polygon"]=plane; fixture["uv"]=texcoords; fixture["triangles"]=PackedInt32Array({0,1,4,1,2,4,2,3,4,3,0,4});
    PackedInt32Array bone_ids; PackedFloat32Array influences;
    for(const Vector2 &point:plane) { for(int j=0;j<4;j++) { bone_ids.push_back(j==1?1:0); influences.push_back(j==0?1-point.x/10:j==1?point.x/10:0); } }
    fixture["bones"]=bone_ids; fixture["weights"]=influences;
    Transform2D screen(Vector2(-2,.4),Vector2(.3,1.5),Vector2(30,-40));
    PackedVector2Array displayed; for(const Vector2 &point:plane) { displayed.push_back(screen.xform(point)); }
    Dictionary mapped=remap_mesh_vertex(fixture,displayed,4,screen.xform(Vector2(6,4)));
    if(mapped.is_empty() || !PackedVector2Array(mapped["uv"])[4].is_equal_approx(Vector2(.6,.4))) { ERR_PRINT("Mesh texture remap failed"); return false; }
    PackedInt32Array mapped_bones=mapped["bones"]; PackedFloat32Array mapped_weights=mapped["weights"]; real_t right_weight=0,sum=0;
    for(int j=0;j<4;j++) { sum+=mapped_weights[16+j]; if(mapped_bones[16+j]==1) { right_weight+=mapped_weights[16+j]; } }
    if(!Math::is_equal_approx(right_weight,real_t(.6)) || !Math::is_equal_approx(sum,real_t(1)) || PackedVector2Array(fixture["uv"])!=texcoords) { ERR_PRINT("Mesh influence interpolation failed"); return false; }
    if(!remap_mesh_vertex(fixture,displayed,4,screen.xform(Vector2(20,20))).is_empty()) { ERR_PRINT("Mesh fold was accepted"); return false; }
    // Keep the original event workflow regression for the explicit deformation mode.
    set_mesh_preserve_texture(false);
	edit_scene(value,3); set_mesh_edit_mode(true); set_keyframe_edit_mode(false); configure_weight_brush(-1,60,.15);
	Dictionary before=Dictionary(value->get_entities()[3])["polygon_2d"]; PackedVector2Array original=before["polygon"]; Vector2 at=preview->get_deformed_polygon_2d(ids[3])[0]*display_scale()+pan;
	auto click=[&](bool pressed) { Ref<InputEventMouseButton> event; event.instantiate(); event->set_button_index(MouseButton::LEFT); event->set_pressed(pressed); event->set_position(at); gui_input(event); };
	click(true); Ref<InputEventMouseMotion> motion; motion.instantiate(); motion->set_position(at+Vector2(12,7)); gui_input(motion); click(false);
	PackedVector2Array after=Dictionary(Dictionary(value->get_entities()[3])["polygon_2d"])["polygon"]; bool ok=after[0]!=original[0] && value->instantiate().is_valid();
	ok &= EditorUndoRedoManager::get_singleton()->undo(); edit_scene(value,3); ok &= PackedVector2Array(Dictionary(Dictionary(value->get_entities()[3])["polygon_2d"])["polygon"])==original;
	click(true); gui_input(motion); Ref<InputEventKey> cancel; cancel.instantiate(); cancel->set_keycode(Key::ESCAPE); cancel->set_pressed(true); gui_input(cancel); click(false);
	ok &= PackedVector2Array(Dictionary(Dictionary(value->get_entities()[3])["polygon_2d"])["polygon"])==original;
	PackedInt32Array faces=before.get("triangles",PackedInt32Array()); if(faces.is_empty()) { faces=Geometry2D::triangulate_polygon(original); }
	PackedVector2Array display=preview->get_deformed_polygon_2d(ids[3]); Vector2 center=(display[faces[0]]+display[faces[1]]+display[faces[2]])/3*display_scale()+pan;
	edit_mesh_topology(center,false); ok &= PackedVector2Array(Dictionary(Dictionary(value->get_entities()[3])["polygon_2d"])["polygon"]).size()==original.size()+1 && value->instantiate().is_valid();
	edit_mesh_topology(center,true); ok &= PackedVector2Array(Dictionary(Dictionary(value->get_entities()[3])["polygon_2d"])["polygon"]).size()==original.size() && value->instantiate().is_valid();
	print_line(String("MESH_DEFORM_CHECK ")+(ok?"pass":"fail"));
    set_mesh_preserve_texture(true);
    // Exercise UV/weight updates and cancellation through real canvas input on a bound mesh.
    edit_scene(value,3);
    Dictionary adjusted_before=Dictionary(value->get_entities()[3])["polygon_2d"],preview_before=preview->get_polygon_2d(ids[3]);
    PackedVector2Array old_display=preview->get_deformed_polygon_2d(ids[3]);
    PackedInt32Array adjacent=adjusted_before.get("triangles",PackedInt32Array());
    if(adjacent.is_empty()) { adjacent=Geometry2D::triangulate_polygon(PackedVector2Array(adjusted_before["polygon"])); }
    int picked=adjacent[0]; Vector2 start=old_display[picked];
    Vector2 destination=start.lerp((old_display[adjacent[0]]+old_display[adjacent[1]]+old_display[adjacent[2]])/3,.05);
    auto press=[&](bool down) { Ref<InputEventMouseButton> e; e.instantiate(); e->set_button_index(MouseButton::LEFT); e->set_pressed(down); e->set_position(start*display_scale()+pan); gui_input(e); };
    press(true); motion->set_position(destination*display_scale()+pan); gui_input(motion);
    print_line("MESH_DRAG_CHECK actual="+String(preview->get_deformed_polygon_2d(ids[3])[picked])+" desired="+String(destination));
    ok &= preview->get_deformed_polygon_2d(ids[3])[picked].is_equal_approx(destination);
    gui_input(cancel); press(false);
    Dictionary restored=preview->get_polygon_2d(ids[3]);
    for(const char *field:{"polygon","uv","bones","weights"}) { ok &= restored.get(field,Variant())==preview_before.get(field,Variant()); }
    press(true); gui_input(motion); press(false);
    Dictionary committed=Dictionary(value->get_entities()[3])["polygon_2d"];
    print_line(String("MESH_COMMIT_CHECK ")+(committed!=adjusted_before?"changed":"unchanged"));
    ok &= committed!=adjusted_before && value->instantiate().is_valid();
    ok &= EditorUndoRedoManager::get_singleton()->undo();
    ok &= Dictionary(Dictionary(value->get_entities()[3])["polygon_2d"])==adjusted_before;
    ok &= EditorUndoRedoManager::get_singleton()->redo();
    ok &= Dictionary(Dictionary(value->get_entities()[3])["polygon_2d"])==committed;
    ok &= EditorUndoRedoManager::get_singleton()->undo();
    set_mesh_edit_mode(false);
    print_line(ok?"ECS_MESH_ADJUST_PASS uv weights reflected_transform drag cancel undo redo deform":"ECS_MESH_ADJUST_FAIL");
    return ok;
}
Transform2D ECSUICanvasEditor::mesh_vertex_transform(int vertex,const Dictionary &definition) const {
	Transform3D global=preview->get_global_transform(ids[selected]);
	Transform2D base(Vector2(global.basis[0][0],global.basis[1][0]),Vector2(global.basis[0][1],global.basis[1][1]),Vector2(global.origin.x,global.origin.y));
	Dictionary mesh=preview->get_polygon_2d(ids[selected]); int64_t rig_id=mesh.get("skeleton",int64_t(0)); if(!rig_id) { return base; }
	Dictionary rig=preview->get_skeleton_2d(rig_id); PackedInt64Array bones=rig["bones"]; Array poses=rig["bind_poses"]; PackedInt32Array indices=definition.is_empty()?PackedInt32Array(mesh["bones"]):PackedInt32Array(definition["bones"]); PackedFloat32Array weights=definition.is_empty()?PackedFloat32Array(mesh["weights"]):PackedFloat32Array(definition["weights"]);
	Transform2D blended{Vector2(),Vector2(),Vector2()};
	for(int j=0;j<4;j++) { int index=indices[vertex*4+j]; Transform3D bone=preview->get_global_transform(bones[index]); Transform2D current(Vector2(bone.basis[0][0],bone.basis[1][0]),Vector2(bone.basis[0][1],bone.basis[1][1]),Vector2(bone.origin.x,bone.origin.y)); Transform2D skin=current*Transform2D(poses[index]).affine_inverse(); for(int axis=0;axis<3;axis++) { blended[axis]+=skin[axis]*weights[vertex*4+j]; } }
	Transform3D root=preview->get_global_transform(rig_id); Transform2D root2d(Vector2(root.basis[0][0],root.basis[1][0]),Vector2(root.basis[0][1],root.basis[1][1]),Vector2(root.origin.x,root.origin.y));
	return blended*root2d.affine_inverse()*base;
}
void ECSUICanvasEditor::finish_mesh(bool commit) {
	if(mesh_vertex<0) { return; } mesh_vertex=-1;
	if(commit && mesh_work!=mesh_before) { emit_signal("mesh_edited",selected,mesh_work); }
	else if(preview.is_valid() && selected>=0 && selected<ids.size()) { Dictionary restore; for(const char *field:{"polygon","uv","bones","weights"}) { if(mesh_before.has(field)) { restore[field]=mesh_before[field]; } } preview->set_polygon_2d(ids[selected],restore); }
	queue_redraw();
}
bool ECSUICanvasEditor::can_drop_data(const Point2 &,const Variant &value) const {
	if(!skeleton_authoring || keyframe_edit_mode || value.get_type()!=Variant::DICTIONARY) { return false; }
	Dictionary payload=value; if(String(payload.get("type",String()))!="files") { return false; }
	PackedStringArray files=payload.get("files",PackedStringArray()); if(files.is_empty()) { return false; }
	for(const String &file:files) { String extension=file.get_extension().to_lower(); if(extension!="png" && extension!="jpg" && extension!="jpeg" && extension!="webp") { return false; } }
	return true;
}
void ECSUICanvasEditor::drop_data(const Point2 &point,const Variant &value) {
	if(!can_drop_data(point,value)) { return; } Dictionary payload=value;
	emit_signal("images_dropped",payload["files"],(point-pan)/display_scale());
}
void ECSUICanvasEditor::gui_input(const Ref<InputEvent> &event) {
	if(tilemap_input(event)) { accept_event(); return; }
	if (preview.is_null()) {
		return;
	}
	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed() && key->get_keycode() == Key::ESCAPE) {
		finish_mesh(false); finish_paint(false); finish_drag(false); creating_bone=false; queue_redraw();
		accept_event();
		return;
	}
	if (key.is_valid() && key->is_pressed() && !key->is_echo() && !key->is_ctrl_pressed() && !key->is_alt_pressed() && !key->is_meta_pressed()) {
		int tool = -1;
		switch (key->get_keycode()) {
			case Key::Q: tool = 0; break;
			case Key::W: tool = 1; break;
			case Key::E: tool = 2; break;
			case Key::R: tool = 3; break;
			case Key::T: tool = 4; break;
			case Key::Y: tool = 5; break;
			default: break;
		}
		if (tool >= 0) { set_tool(tool); accept_event(); return; }
	}
	Ref<InputEventMouseButton> button = event;
	Ref<InputEventMouseMotion> motion = event;
	if(tool_mode==6 && skeleton_authoring && !keyframe_edit_mode) {
		if(button.is_valid() && button->get_button_index()==MouseButton::RIGHT) { creating_bone=false; queue_redraw(); accept_event(); return; }
		if(button.is_valid() && button->get_button_index()==MouseButton::LEFT) {
			Vector2 at=(button->get_position()-pan)/display_scale();
			if(button->is_pressed()) { creating_bone=true; create_bone_start=at; create_bone_end=at; grab_focus(); }
			else if(creating_bone) { creating_bone=false; create_bone_end=at; if(create_bone_start.distance_to(at)*display_scale()>=4*EDSCALE) { emit_signal("bone_create_requested",create_bone_start,at); } }
			queue_redraw(); accept_event(); return;
		}
		if(motion.is_valid() && creating_bone) { create_bone_end=(motion->get_position()-pan)/display_scale(); queue_redraw(); accept_event(); return; }
	}
	if(mesh_edit_mode && skeleton_authoring && !keyframe_edit_mode && !authoring_locked.has(selected) && !authoring_hidden.has(selected) && selected>=0 && selected<ids.size()) {
		if(button.is_valid() && button->is_pressed() && (button->get_button_index()==MouseButton::RIGHT || (button->get_button_index()==MouseButton::LEFT && button->is_double_click()))) { finish_mesh(false); edit_mesh_topology(button->get_position(),button->get_button_index()==MouseButton::RIGHT); accept_event(); return; }

		if(button.is_valid() && button->get_button_index()==MouseButton::LEFT) {
			if(!button->is_pressed()) { finish_mesh(true); accept_event(); return; }
			PackedVector2Array points=preview->get_deformed_polygon_2d(ids[selected]); float nearest=10*EDSCALE;
			for(int i=0;i<points.size();i++) { float distance=(points[i]*display_scale()+pan).distance_to(button->get_position()); if(distance<nearest) { nearest=distance; mesh_vertex=i; } }
			if(mesh_vertex>=0) { mesh_before=Dictionary(scene->get_entities()[selected])["polygon_2d"]; mesh_work=mesh_before.duplicate(true); mesh_display_before=points; mesh_grab_offset=points[mesh_vertex]-(button->get_position()-pan)/display_scale(); grab_focus(); }
			accept_event(); return;
		}
		if(motion.is_valid() && mesh_vertex>=0) {
            const Vector2 destination=(motion->get_position()-pan)/display_scale()+mesh_grab_offset;
            bool original_position=destination.is_equal_approx(mesh_display_before[mesh_vertex]);
            Dictionary next=mesh_preserve_texture && !original_position?remap_mesh_vertex(mesh_before,mesh_display_before,mesh_vertex,destination):mesh_before.duplicate(true);
            if(next.is_empty()) { accept_event(); return; }
            Transform2D transform=mesh_vertex_transform(mesh_vertex,next); if(Math::abs(transform.determinant())<.000001) { accept_event(); return; }
            PackedVector2Array points=next["polygon"]; if(!original_position) { points.set(mesh_vertex,transform.affine_inverse().xform(destination)); } next["polygon"]=points;
            Dictionary update; for(const char *field:{"polygon","uv","bones","weights"}) { if(next.has(field)) { update[field]=next[field]; } }
            if(preview->set_polygon_2d(ids[selected],update)) { mesh_work=next; }
            queue_redraw(); accept_event(); return;
		}
	}
	if(skeleton_authoring && !keyframe_edit_mode && weight_bone>=0) {
		if(key.is_valid() && key->is_pressed() && key->get_keycode()==Key::ESCAPE) { finish_paint(false); accept_event(); return; }
		if(motion.is_valid()) { brush_cursor=motion->get_position(); if(painting) { paint_at(brush_cursor,motion->is_shift_pressed()); accept_event(); return; } queue_redraw(); }
		if(button.is_valid() && button->get_button_index()==MouseButton::LEFT) {
			if(!button->is_pressed()) { finish_paint(true); accept_event(); return; }
			if(scene.is_valid() && !authoring_locked.has(selected) && !authoring_hidden.has(selected) && selected>=0 && selected<scene->get_entities().size()) { Dictionary entity=scene->get_entities()[selected]; if(entity.has("polygon_2d")) { stroke_before=entity["polygon_2d"]; if(int(stroke_before.get("skeleton",-1))<0 || stroke_before.get("weights",Variant()).get_type()!=Variant::PACKED_FLOAT32_ARRAY) { accept_event(); return; } stroke_data=stroke_before.duplicate(true); painting=true; paint_at(button->get_position(),button->is_shift_pressed()); accept_event(); return; } }
		}
	}

	if (key.is_valid() && key->is_pressed() && key->get_keycode() == Key::HOME) {
		finish_drag(false);
		zoom = 1;
		pan = skeleton_authoring?get_size()*.5f:Vector2(32, 32) * EDSCALE;
		queue_redraw();
		accept_event();
		return;
	}
	if (button.is_valid() && (button->get_button_index() == MouseButton::MIDDLE || (tool_mode == 0 && button->get_button_index() == MouseButton::LEFT))) {
		finish_drag(false);
		panning = button->is_pressed();
		if (panning) { grab_focus(); }
		accept_event();
		return;
	}
	if (motion.is_valid() && panning) {
		pan += motion->get_relative();
		queue_redraw();
		accept_event();
		return;
	}
	if (button.is_valid() && button->is_pressed() && (button->get_button_index() == MouseButton::WHEEL_UP || button->get_button_index() == MouseButton::WHEEL_DOWN)) {
		finish_drag(false);
		Vector2 at = (button->get_position() - pan) / display_scale();
		zoom = CLAMP(zoom * (button->get_button_index() == MouseButton::WHEEL_UP ? 1.25f : .8f), .25f, 16.0f);
		pan = button->get_position() - at * display_scale();
		queue_redraw();
		accept_event();
		return;
	}
	if (button.is_valid() && button->get_button_index() == MouseButton::LEFT) {
		if (!button->is_pressed()) {
			finish_drag(true);
			accept_event();
			return;
		}
		if (button->get_position().x < 18 * EDSCALE || button->get_position().y < 18 * EDSCALE) {
			return;
		}
		Vector2 position = (button->get_position() - pan) / display_scale();
		Rect2 selected_rect = get_selected_rect();
		move_axis = -1;
		transform_handle = -1;
		if (skeleton_authoring && (tool_mode == 2 || tool_mode == 3) && selected >= 0 && selected < ids.size() && !authoring_locked.has(selected) && !authoring_hidden.has(selected)) {
			const Vector2 local = (position-selected_pivot())*display_scale()/EDSCALE;
			if (tool_mode == 2 && Math::abs(local.length()-52) <= 8) { transform_handle = 2; }
			if (tool_mode == 3) {
				if (Rect2(-9,-9,18,18).has_point(local)) { transform_handle = 2; }
				else { for (int axis=0; axis<2; axis++) {
					if (local.distance_to(selected_axis(axis)*64) <= 10) { transform_handle=axis; drag_axis_direction=selected_axis(axis); break; }
				} }
			}
		}
		if ((tool_mode == 1 || tool_mode == 5) && selected >= 0 && !authoring_locked.has(selected) && !authoring_hidden.has(selected) && selected < ids.size() && (!preview->get_ui(ids[selected]).is_empty() || !preview->get_bone_2d(ids[selected]).is_empty() || !preview->get_polygon_2d(ids[selected]).is_empty() || !preview->get_skeleton_2d(ids[selected]).is_empty())) {
			const Vector2 local = (position - selected_pivot()) * display_scale() / EDSCALE;
			if (Rect2(-7, -7, 14, 14).has_point(local)) { move_axis = 2; }
			else {
				for(int axis=0;axis<2;axis++) {
					Vector2 direction=selected_axis(axis), perpendicular(-direction.y,direction.x);
					if(Rect2(7,-7,62,14).has_point(Vector2(local.dot(direction),local.dot(perpendicular)))) { move_axis=axis; drag_axis_direction=direction; break; }
				}
			}
		}
		resizing = false;
		if (move_axis < 0 && tool_mode >= 3 && selected >= 0 && !preview->get_ui(ids[selected]).is_empty()) {
			const Vector2 corners[] = { selected_rect.position, Vector2(selected_rect.get_end().x, selected_rect.position.y), selected_rect.get_end(), Vector2(selected_rect.position.x, selected_rect.get_end().y) };
			float nearest = 1e20f;
			for (int i = 0; i < 4; i++) {
				const float distance = (position - corners[i]).length_squared();
				if (Rect2(corners[i] - Vector2(8, 8) * EDSCALE / display_scale(), Vector2(16, 16) * EDSCALE / display_scale()).has_point(position) && distance < nearest) {
					resizing = true;
					resize_corner = i;
					nearest = distance;
				}
			}
		}
		if (!resizing && move_axis < 0 && transform_handle < 0) {
			uint64_t entity = renderer.editor_pick(position, canvas_size);
			selected = ids.find(entity);
			if(selected<0) {
				for(int i=ids.size()-1;i>=0;i--) {
					Dictionary bone=preview->get_bone_2d(ids[i]); if(bone.is_empty() || authoring_hidden.has(i) || authoring_locked.has(i) || (skeleton_authoring && (!pick_bones || !show_bones)) || !preview->is_active_in_hierarchy(ids[i])) { continue; }
					Transform3D t=preview->get_global_transform(ids[i]); Vector3 end=t.xform(Vector3(double(bone["length"]),0,0));
					Vector2 a(t.origin.x,t.origin.y), b(end.x,end.y), segment[2]={a,b};
					if(Geometry2D::get_closest_point_to_segment(position,segment).distance_to(position)<9*EDSCALE/display_scale()) { selected=i; break; }
				}
			}
			if(selected<0) { for(int i=ids.size()-1;i>=0;i--) { PackedVector2Array polygon=preview->get_deformed_polygon_2d(ids[i]); if(!authoring_hidden.has(i) && !authoring_locked.has(i) && (!skeleton_authoring || (pick_images && show_images)) && polygon.size()>=3 && preview->is_active_in_hierarchy(ids[i]) && preview->is_skeleton_attachment_visible(ids[i]) && Geometry2D::is_point_in_polygon(position,polygon)) { selected=i; break; } } }
			emit_signal("entity_selected", selected);
		}
		if (selected < 0 || selected >= ids.size() || authoring_locked.has(selected) || authoring_hidden.has(selected)) {
			return;
		}
		if (skeleton_authoring && tool_mode == 7) { queue_redraw(); accept_event(); return; }
		if (tool_mode == 3 && !resizing) { resizing = true; resize_corner = 2; }
		Dictionary ui = preview->get_ui(ids[selected]);
		if (ui.is_empty()) {
			if(preview->get_bone_2d(ids[selected]).is_empty() && preview->get_polygon_2d(ids[selected]).is_empty() && preview->get_skeleton_2d(ids[selected]).is_empty()) { return; }
			setup_pose_after=Array(); bone_drag=true; dragging=true; resizing=false; drag_start=position;
			bone_drag_value=preview->get_vector(ids[selected],tool_mode==2?"rotation":tool_mode==3?"scale":"position");
			Vector3 origin=preview->get_global_transform(ids[selected]).origin; bone_drag_origin=Vector2(origin.x,origin.y);
			grab_focus(); accept_event(); return;
		}
		if (runtime_preview) { return; }
		if (!resizing) {
			uint64_t parent = preview->get_parent(ids[selected]);
			while (parent) {
				Dictionary parent_ui = preview->get_ui(parent);
				if (!parent_ui.is_empty()) {
					String kind = parent_ui.get("kind", "");
					if (kind == "hbox" || kind == "vbox" || kind == "grid") {
						return;
					}
					break;
				}
				parent = preview->get_parent(parent);
			}
		}
		drag_start = position;
		drag_rect = ui["rect"];
		dragging = true;
		grab_focus();
		accept_event();
		queue_redraw();
	} else if (motion.is_valid() && dragging) {
		Vector2 delta = (motion->get_position() - pan) / display_scale() - drag_start;
		if(bone_drag) {
			Vector3 value=bone_drag_value;
			if(tool_mode==2) { Vector2 current=(motion->get_position()-pan)/display_scale()-bone_drag_origin,initial=drag_start-bone_drag_origin; value.z+=initial.angle_to(current); }
			else if(tool_mode==3) {
				if(transform_handle>=0) {
					const float travel=transform_handle==2 ? delta.x-delta.y : delta.dot(drag_axis_direction);
					const float factor=MAX(.001f,1+travel*display_scale()/(64*EDSCALE));
					if(transform_handle!=1) { value.x*=factor; } if(transform_handle!=0) { value.y*=factor; }
				} else { float initial=(drag_start-bone_drag_origin).length(); float current=((motion->get_position()-pan)/display_scale()-bone_drag_origin).length(); float factor=initial>1?MAX(.001f,current/initial):MAX(.001f,1+delta.x*.01f); value.x*=factor; value.y*=factor; }
			}
			else { if(move_axis==0 || move_axis==1) { delta=drag_axis_direction*delta.dot(drag_axis_direction); } uint64_t parent=preview->get_parent(ids[selected]); Vector3 movement(delta.x,delta.y,0); if(parent) { Basis basis=preview->get_global_transform(parent).basis; if(Math::is_zero_approx(basis.determinant())) { return; } movement=basis.inverse().xform(movement); } value+=movement; }
			if(skeleton_authoring && !keyframe_edit_mode && !runtime_preview && setup_pose_callback.is_valid()) {
				Array after=setup_pose_callback.call(selected,tool_mode==2?String("rotation"):tool_mode==3?String("scale"):String("position"),value);
				if(!after.is_empty()) { setup_pose_after=after; preview_setup_pose(after); } else { setup_pose_after=Array(); preview_setup_pose(scene->get_entities()); }
				accept_event(); return;
			}
			preview->set_vector(ids[selected],tool_mode==2?"rotation":tool_mode==3?"scale":"position",value); if(runtime_preview) { emit_signal("runtime_transform_changed",selected,preview->get_global_transform(ids[selected])); } sync_authoring_preview(); queue_redraw(); accept_event(); return;
		}
		Rect2 rect = drag_rect;
		if (resizing) {
			const Vector2 end = drag_rect.get_end();
			if (resize_corner == 0 || resize_corner == 3) {
				rect.position.x = MIN(drag_rect.position.x + delta.x, end.x - 1.0f);
				rect.size.x = end.x - rect.position.x;
			} else { rect.size.x = MAX(1.0f, drag_rect.size.x + delta.x); }
			if (resize_corner == 0 || resize_corner == 1) {
				rect.position.y = MIN(drag_rect.position.y + delta.y, end.y - 1.0f);
				rect.size.y = end.y - rect.position.y;
			} else { rect.size.y = MAX(1.0f, drag_rect.size.y + delta.y); }
		} else {
			if (move_axis == 0) { delta.y = 0; }
			if (move_axis == 1) { delta.x = 0; }
			rect.position += delta;
		}
		Dictionary update;
		update["rect"] = rect;
		preview->set_ui(ids[selected], update);
		queue_redraw();
		accept_event();
	}
}
bool ECSUICanvasEditor::run_self_test() {
	Ref<ECSScene> resource;
	resource.instantiate();
	Dictionary ui;
	ui["kind"] = "panel";
	ui["rect"] = Rect2(20, 30, 100, 50);
	Dictionary entity;
	entity["ui"] = ui;
	Array entities;
	entities.push_back(entity);
	resource->set_entities(entities);
	set_size(Size2(800, 600));
	edit_scene(resource, 0);
	auto mouse = [&](Vector2 position, bool pressed) {
		Ref<InputEventMouseButton> event;
		event.instantiate();
		event->set_button_index(MouseButton::LEFT);
		event->set_pressed(pressed);
		event->set_position(position * display_scale() + pan);
		gui_input(event);
	};
	auto move = [&](Vector2 position) {
		Ref<InputEventMouseMotion> event;
		event.instantiate();
		event->set_position(position * display_scale() + pan);
		gui_input(event);
	};
	auto stored = [&]() -> Rect2 { Dictionary e=resource->get_entities()[0]; Dictionary value=e["ui"]; return value["rect"]; };
	set_tool(1);
	mouse(Vector2(40, 45), true);
	move(Vector2(70, 65));
	mouse(Vector2(70, 65), false);
	if (!stored().is_equal_approx(Rect2(50, 50, 100, 50))) {
		ERR_PRINT("UI canvas check 33"); return false;
	}
	auto *undo = EditorUndoRedoManager::get_singleton();
	if (!undo->undo() || !stored().is_equal_approx(Rect2(20, 30, 100, 50)) || !undo->redo() || !stored().is_equal_approx(Rect2(50, 50, 100, 50))) {
		ERR_PRINT("UI canvas check 37"); return false;
	}
	edit_scene(resource, 0);
	set_tool(4);
	mouse(Vector2(150, 100), true);
	move(Vector2(180, 120));
	mouse(Vector2(180, 120), false);
	if (!stored().is_equal_approx(Rect2(50, 50, 130, 70))) {
		ERR_PRINT("UI canvas check 44"); return false;
	}
	edit_scene(resource, 0);
	mouse(Vector2(70, 65), true);
	move(Vector2(90, 95));
	Ref<InputEventKey> escape;
	escape.instantiate();
	escape->set_keycode(Key::ESCAPE);
	escape->set_pressed(true);
	gui_input(escape);
	if (!stored().is_equal_approx(Rect2(50, 50, 130, 70)) || !get_selected_rect().is_equal_approx(stored())) {
		ERR_PRINT("UI canvas check 55"); return false;
	}
	Ref<InputEventMouseButton> wheel;
	wheel.instantiate();
	wheel->set_button_index(MouseButton::WHEEL_UP);
	wheel->set_pressed(true);
	wheel->set_position(Vector2(100, 80));
	gui_input(wheel);
	if (zoom <= 1) {
		ERR_PRINT("UI canvas check 64"); return false;
	}
	Ref<InputEventMouseButton> middle;
	middle.instantiate();
	middle->set_button_index(MouseButton::MIDDLE);
	middle->set_pressed(true);
	gui_input(middle);
	Ref<InputEventMouseMotion> pan_event;
	pan_event.instantiate();
	pan_event->set_relative(Vector2(20, 15));
	gui_input(pan_event);
	middle->set_pressed(false);
	gui_input(middle);
	mouse(Vector2(70, 65), true);
	move(Vector2(80, 85));
	mouse(Vector2(80, 85), false);
	if (!stored().is_equal_approx(Rect2(60, 70, 130, 70))) {
		ERR_PRINT("UI canvas check 81"); return false;
	}
	Ref<InputEventKey> home;
	home.instantiate();
	home->set_keycode(Key::HOME);
	home->set_pressed(true);
	gui_input(home);
	if (zoom != 1 || pan != Vector2(32, 32) * EDSCALE) {
		ERR_PRINT("UI canvas check 89"); return false;
	}
	set_tool(0);
	const Vector2 previous_pan = pan;
	const Rect2 previous_rect = stored();
	mouse(Vector2(80, 85), true);
	gui_input(pan_event);
	mouse(Vector2(80, 85), false);
	if (pan != previous_pan + Vector2(20, 15) || stored() != previous_rect || panning) { ERR_PRINT("UI canvas check 97"); return false; }
	set_tool(3);
	mouse(Vector2(80, 85), true);
	move(Vector2(90, 100));
	mouse(Vector2(90, 100), false);
	if (!stored().is_equal_approx(Rect2(previous_rect.position, previous_rect.size + Vector2(10, 15)))) { ERR_PRINT("UI canvas check 102"); return false; }
	if (!EditorUndoRedoManager::get_singleton()->undo() || !stored().is_equal_approx(previous_rect)) { ERR_PRINT("UI canvas check 103"); return false; }
	set_tool(1);
	if (!tool_buttons[1]->is_pressed() || tool_buttons[3]->is_pressed()) { ERR_PRINT("UI canvas check 105"); return false; }
	set_tool(2);
	if (tool_mode != 2 || tool_buttons[2]->is_disabled()) { ERR_PRINT("UI canvas check 107"); return false; }
	set_tool(4);
	set_tool(1);
	for (int axis = 0; axis < 2; axis++) {
		edit_scene(resource, 0);
		const Rect2 initial = stored();
		if (!selected_pivot().is_equal_approx(initial.get_center())) { ERR_PRINT("UI canvas check 113"); return false; }
		const Vector2 at = selected_pivot() + (axis == 0 ? Vector2(30, 0) : Vector2(0, -30)) * EDSCALE / display_scale();
		mouse(at, true);
		if (move_axis != axis) { ERR_PRINT("UI canvas check 116"); return false; }
		move(at + Vector2(10, 15));
		mouse(at + Vector2(10, 15), false);
		const Rect2 expected(initial.position + (axis == 0 ? Vector2(10, 0) : Vector2(0, 15)), initial.size);
		if (!stored().is_equal_approx(expected) || !undo->undo()) { ERR_PRINT("UI canvas check 120"); return false; }
	}
	set_tool(4);
	for (int corner = 0; corner < 4; corner++) {
		edit_scene(resource, 0);
		const Rect2 initial = stored();
		const Vector2 corners[] = { initial.position, Vector2(initial.get_end().x, initial.position.y), initial.get_end(), Vector2(initial.position.x, initial.get_end().y) };
		const Vector2 delta(-10, -5);
		mouse(corners[corner], true);
		if (!resizing || resize_corner != corner) { ERR_PRINT("UI canvas check 129"); return false; }
		move(corners[corner] + delta);
		mouse(corners[corner] + delta, false);
		Rect2 expected = initial;
		if (corner == 0 || corner == 3) { expected.position.x += delta.x; expected.size.x -= delta.x; }
		else { expected.size.x += delta.x; }
		if (corner == 0 || corner == 1) { expected.position.y += delta.y; expected.size.y -= delta.y; }
		else { expected.size.y += delta.y; }
		if (!stored().is_equal_approx(expected) || !undo->undo() || !stored().is_equal_approx(initial)) { ERR_PRINT("UI canvas check 137"); return false; }
	}
	Ref<ECSScene> bones; bones.instantiate(); Array bone_entities; Dictionary bone_entity;
	bone_entity["position"]=Vector3(120,100,0); bone_entity["bone_2d"]=Dictionary(); bone_entities.push_back(bone_entity); bones->set_entities(bone_entities);
	set_tool(1);
	for(int axis=0;axis<2;axis++) {
		edit_scene(bones,0);
		if(!selected_pivot().is_equal_approx(Vector2(120,100))) { ERR_PRINT("UI canvas check 144"); return false; }
		Vector2 at=selected_pivot()+(axis==0?Vector2(30,0):Vector2(0,-30))*EDSCALE/display_scale();
		mouse(at,true); if(!bone_drag || move_axis!=axis) { ERR_PRINT("UI canvas check 146"); return false; }
		move(at+Vector2(10,15)); mouse(at+Vector2(10,15),false);
		Vector3 expected=Vector3(120,100,0)+(axis==0?Vector3(10,0,0):Vector3(0,15,0));
		if(!Vector3(Dictionary(bones->get_entities()[0])["position"]).is_equal_approx(expected) || !undo->undo()) { ERR_PRINT("UI canvas check 149"); return false; }
	}
	edit_scene(bones,0); set_tool(1); queue_redraw();
	// Parent rotation and nonuniform scale must affect both picking and world-space dragging.
	Dictionary parent; parent["position"]=Vector3(120,100,0); parent["rotation"]=Vector3(0,0,.4); parent["scale"]=Vector3(2,1,1);
	Dictionary child; child["parent"]=0; child["position"]=Vector3(40,0,0); child["rotation"]=Vector3(0,0,.7); child["bone_2d"]=Dictionary();
	Array hierarchy; hierarchy.push_back(parent); hierarchy.push_back(child); bones->set_entities(hierarchy);
	for(int axis=0;axis<2;axis++) {
		edit_scene(bones,1); Vector2 direction=selected_axis(axis), origin=selected_pivot();
		if(Math::abs(direction.y)<.1 || !Math::is_equal_approx(direction.length(),real_t(1))) { return false; }
		Vector2 at=origin+direction*30*EDSCALE/display_scale(),side(-direction.y,direction.x);
		mouse(at,true); if(move_axis!=axis || !bone_drag) { return false; }
		move(at+direction*12+side*8);
		if(!selected_pivot().is_equal_approx(origin+direction*12)) { return false; }
		mouse(at+direction*12+side*8,false);
		if(!undo->undo() || !Vector3(Dictionary(bones->get_entities()[1])["position"]).is_equal_approx(Vector3(40,0,0))) { return false; }
	}
	set_skeleton_authoring(true); edit_scene(bones,1); set_tool(3);
	Vector2 scale_origin=selected_pivot(),scale_direction=selected_axis(0); Vector2 scale_at=scale_origin+scale_direction*30;
	mouse(scale_at,true); if(!bone_drag) { return false; } move(scale_origin+scale_direction*60); mouse(scale_origin+scale_direction*60,false);
	if(!Vector3(Dictionary(bones->get_entities()[1]).get("scale",Vector3(1,1,1))).is_equal_approx(Vector3(2,2,1)) || !undo->undo()) { ERR_PRINT("Bone scale drag failed"); return false; }
	set_axis_space(2); if(!selected_axis(0).is_equal_approx(Vector2(1,0))) { return false; } set_axis_space(0); set_skeleton_authoring(false);
	print_line("ECS_BONE_2D_GIZMO_PASS pivot x_axis y_axis undo rotated_parent nonuniform_scale local_axis_drag");
	print_line("ECS_UI_AUTHORING_PASS drag resize cancel undo redo zoom pan toolbar left_pan scale four_corners pivot_axes");
	edit_scene(Ref<ECSScene>(), -1);
	return true;
}
void ECSUICanvasEditor::start_visual_test() {
	Ref<ECSScene> resource;
	resource.instantiate();
	Array entities;
	for (int i = 0; i < 3; i++) {
		Dictionary ui;
		ui["kind"] = i == 0 ? "panel" : i == 1 ? "label"
											   : "button";
		ui["rect"] = i == 0 ? Rect2(20, 20, 400, 230) : i == 1 ? Rect2(40, 40, 350, 80)
															   : Rect2(60, 150, 200, 60);
		ui["text"] = i == 1 ? String(U"纯 ECS UI 画布\n拖动 · 调整大小 · 撤销") : i == 2 ? String(U"按钮预览")
																						 : String(U"");
		if (i == 0) {
			Ref<Shader> shader;
			shader.instantiate();
			shader->set_code("shader_type canvas_item; void fragment() { COLOR = vec4(0.8, 0.15, 0.7, 1.0); }");
			Ref<ShaderMaterial> material;
			material.instantiate();
			material->set_shader(shader);
			ui["material"] = material;
		}
		Dictionary entity;
		entity["ui"] = ui;
		entities.push_back(entity);
	}
	resource->set_entities(entities);
	edit_scene(resource, 2);
	set_tool(1);
	if (auto *tabs = Object::cast_to<TabContainer>(get_parent())) {
		tabs->set_current_tab(1);
	}
	get_tree()->create_timer(2.0)->connect("timeout", callable_mp(this, &ECSUICanvasEditor::capture_visual_test));
}
void ECSUICanvasEditor::capture_visual_test() {
	Ref<Image> image = viewport->get_texture()->get_image();
	bool ok = image.is_valid() && !image->is_empty() && image->get_width() > 420 && image->get_height() > 250 && image->get_pixel(25, 25).a > .5f && image->get_pixel(image->get_width() - 1, image->get_height() - 1).a < .1f;
	if (ok) {
		Color pixel = image->get_pixel(25, 25);
		ok = pixel.r > .7f && pixel.g < .3f && pixel.b > .6f;
	}
	if (ok) {
		ok = image->save_png("D:/GODOTS/TempSDK/ecs-ui-authoring-canvas.png") == OK;
	}
	Ref<Image> editor = get_viewport()->get_texture()->get_image();
	if (editor.is_valid()) {
		editor->save_png("D:/GODOTS/TempSDK/ecs-ui-authoring-editor.png");
	}
	if (ok) {
		print_line("ECS_UI_AUTHORING_VISUAL_PASS canvas pixels and PNG");
	} else {
		ERR_PRINT("ECS_UI_AUTHORING_VISUAL_FAILED");
	}
	get_tree()->quit(ok ? 0 : 1);
}
#endif
