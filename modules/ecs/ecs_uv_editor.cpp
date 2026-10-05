#ifdef TOOLS_ENABLED
#include "ecs_uv_editor.h"
#include "core/input/input_event.h"
#include "scene/resources/font.h"
#include "scene/theme/theme_db.h"

void ECSUVEditor::_bind_methods() { ADD_SIGNAL(MethodInfo("uv_edited", PropertyInfo(Variant::DICTIONARY, "polygon"))); }
Rect2 ECSUVEditor::texture_rect() const {
	Vector2 size = texture.is_valid() ? texture->get_size() : Vector2(256,256);
	float scale = MIN(MAX(32.f, get_size().x - 64) / MAX(1.f, size.x), MAX(32.f, get_size().y - 64) / MAX(1.f, size.y)) * zoom;
	size *= scale;
	return Rect2((get_size() - size) * .5 + pan, size);
}
void ECSUVEditor::edit(const Dictionary &p_polygon) {
	cancel_drag(); polygon = p_polygon; uv = polygon.get("uv", PackedVector2Array()); texture = polygon.get("texture", Variant()); queue_redraw();
}
void ECSUVEditor::cancel_drag() { if (dragging >= 0) { uv = before; dragging = -1; } panning = false; queue_redraw(); }
void ECSUVEditor::_notification(int p_what) {
	if (p_what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree()) { cancel_drag(); }
	if (p_what != NOTIFICATION_DRAW) { return; }
	draw_rect(Rect2(Vector2(), get_size()), Color(.09,.1,.12));
	Rect2 rect = texture_rect();
	for (int y=0; y<16; y++) { for (int x=0; x<16; x++) { draw_rect(Rect2(rect.position + rect.size * Vector2(x/16.f,y/16.f),rect.size/16), ((x+y)%2) ? Color(.23,.23,.23) : Color(.3,.3,.3)); } }
	if (texture.is_valid()) { draw_texture_rect(texture, rect, false); }
	draw_rect(rect, Color(.8,.8,.8), false);
	for (int i=0; i<uv.size(); i++) {
		Vector2 point = rect.position + uv[i] * rect.size;
		if (uv.size()>1) { draw_line(point,rect.position + uv[(i+1)%uv.size()] * rect.size,Color(.25,.8,1),2,true); }
		draw_circle(point,5,i==dragging?Color(1,.65,.2):Color(.8,.95,1));
		draw_string(ThemeDB::get_singleton()->get_fallback_font(),point+Vector2(7,-7),itos(i),HORIZONTAL_ALIGNMENT_LEFT,-1,14);
	}
}
void ECSUVEditor::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key=p_event;
	if (key.is_valid() && key->is_pressed() && key->get_keycode()==Key::ESCAPE) { cancel_drag(); accept_event(); return; }
	Ref<InputEventMouseButton> button=p_event;
	if (button.is_valid()) {
		if (button->get_button_index()==MouseButton::MIDDLE) { panning=button->is_pressed(); accept_event(); }
		if (button->is_pressed() && (button->get_button_index()==MouseButton::WHEEL_UP || button->get_button_index()==MouseButton::WHEEL_DOWN)) { zoom=CLAMP(zoom*(button->get_button_index()==MouseButton::WHEEL_UP?1.15f:1/1.15f),.1f,20.f); queue_redraw(); accept_event(); }
		if (button->get_button_index()!=MouseButton::LEFT) { return; }
		if (button->is_pressed()) {
			grab_focus(); Rect2 rect=texture_rect(); float best=10;
			for (int i=0;i<uv.size();i++) { float distance=(rect.position+uv[i]*rect.size).distance_to(button->get_position()); if(distance<best) { best=distance; dragging=i; } }
			before=uv;
		} else if (dragging>=0) {
			dragging=-1;
			if (uv!=before) { Dictionary result=polygon.duplicate(true); result["uv"]=uv; emit_signal("uv_edited",result); }
		}
		queue_redraw(); accept_event();
	}
	Ref<InputEventMouseMotion> motion=p_event;
	if (motion.is_valid()) {
		if (panning) { pan+=motion->get_relative(); queue_redraw(); accept_event(); }
		else if (dragging>=0) { Rect2 rect=texture_rect(); Vector2 value=(motion->get_position()-rect.position)/rect.size; if(motion->is_ctrl_pressed()) { value=value.snapped(Vector2(.01,.01)); } if(value.is_finite()) { uv.set(dragging,value); } queue_redraw(); accept_event(); }
	}
}
#endif
