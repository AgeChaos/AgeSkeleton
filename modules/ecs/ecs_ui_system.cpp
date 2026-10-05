// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_ui_system.h"

#include "ecs_ui_draw_commands.h"

#include "scene/resources/atlas_texture.h"
#include "scene/resources/style_box.h"
#include "scene/theme/theme_db.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

uint64_t ECSUISystem::editor_pick(const Vector2 &position, const Size2 &size) {
	sync_layout(size);
	for (int i = draw_order.size() - 1; i >= 0; i--) {
		const LayoutItem &item = items[draw_order[i]];
		if (item.visible && item.rect.has_point(position) && item.clip.has_point(position)) {
			return item.data.entity;
		}
	}
	return 0;
}
Rect2 ECSUISystem::editor_rect(uint64_t entity, const Size2 &size) {
	sync_layout(size);
	const int *index = lookup.getptr(entity);
	return index ? items[*index].rect : Rect2();
}

void ECSUISystem::attach(const Ref<ECSWorld> &p_world, RID viewport) {
	clear();
	world = p_world;
	if (!viewport.is_valid()) {
		return;
	}
	auto *rs = RenderingServer::get_singleton();
	canvas = rs->canvas_create();
	rs->viewport_attach_canvas(viewport, canvas);
	rs->viewport_set_canvas_stacking(viewport, canvas, 100, 0);
}
void ECSUISystem::sync_layout(const Size2 &size) {
	if (world.is_null() || (revision == world->get_ui_revision() && viewport_size == size)) {
		return;
	}
	viewport_size = size;
	revision = world->get_ui_revision();
	Vector<ECSUIItem> source = world->get_ui_items();
	items.resize(source.size());
	lookup.clear();
	for (int i = 0; i < source.size(); i++) {
		items.write[i] = LayoutItem();
		items.write[i].data = source[i];
		lookup[source[i].entity] = i;
	}
	for (int i = 0; i < items.size(); i++) {
		uint64_t parent = world->get_parent(items[i].data.entity);
		while (parent && !lookup.has(parent)) {
			parent = world->get_parent(parent);
		}
		items.write[i].parent = parent ? lookup[parent] : -1;
	}
	// Layout is a pass over component data; containers do not construct child objects.
	Vector<Vector2> flow_offsets;
	flow_offsets.resize(items.size());
	HashMap<int, Vector<int>> siblings;
	for (int i = 0; i < items.size(); i++) {
		if (items[i].parent >= 0 && items[i].data.style.visible) {
			siblings[items[i].parent].push_back(i);
		}
	}
	for (auto &entry : siblings) {
		const LayoutItem &parent = items[entry.key];
		int kind = parent.data.input.kind;
		auto sibling_order = [&](int a, int b) { return items[a].data.entity < items[b].data.entity; };
		entry.value.sort_custom<decltype(sibling_order)>(sibling_order);
		Vector<float> column_widths;
		if (kind == ECSUIInput::GRID) {
			column_widths.resize(MIN(parent.data.layout.columns, entry.value.size()));
			column_widths.fill(0);
			for (int i = 0; i < entry.value.size(); i++) {
				int col = i % column_widths.size();
				column_widths.write[col] = MAX(column_widths[col], float(items[entry.value[i]].data.layout.rect.size.x));
			}
		}
		Vector2 cursor;
		float row_height = 0;
		int column = 0;
		for (int child : entry.value) {
			flow_offsets.write[child] = cursor;
			Vector2 extent = items[child].data.layout.rect.size;
			if (kind == ECSUIInput::HBOX) {
				cursor.x += MAX(real_t(0), extent.x) + parent.data.layout.gap;
			} else if (kind == ECSUIInput::VBOX) {
				cursor.y += MAX(real_t(0), extent.y) + parent.data.layout.gap;
			} else if (kind == ECSUIInput::GRID) {
				row_height = MAX(row_height, float(extent.y));
				cursor.x += column_widths[column] + parent.data.layout.gap;
				if (++column >= parent.data.layout.columns) {
					column = 0;
					cursor.x = 0;
					cursor.y += row_height + parent.data.layout.gap;
					row_height = 0;
				}
			}
		}
	}
	Vector<uint8_t> resolved;
	resolved.resize(items.size());
	resolved.fill(0);
	Vector<int> chain;
	for (int i = 0; i < items.size(); i++) {
		if (resolved[i]) {
			continue;
		}
		chain.clear();
		int current = i;
		while (current >= 0 && !resolved[current]) {
			chain.push_back(current);
			current = items[current].parent;
		}
		for (int j = chain.size() - 1; j >= 0; j--) {
			int index = chain[j];
			LayoutItem &item = items.write[index];
			Rect2 parent_rect(Vector2(), size), parent_clip(Vector2(), size);
			if (item.parent >= 0) {
				const LayoutItem &parent = items[item.parent];
				parent_rect = parent.rect;
				parent_rect.position += parent.data.layout.padding - parent.data.layout.scroll;
				parent_rect.size -= parent.data.layout.padding * 2;
				parent_rect.size = parent_rect.size.max(Vector2());
				parent_clip = parent.clip;
				item.visible = parent.visible;
				item.disabled = parent.disabled;
				item.depth = parent.depth + 1;
				item.inherited_modulate = parent.inherited_modulate;
			}
			item.inherited_modulate *= item.data.style.modulate;
			const Vector4 &anchors = item.data.layout.anchors;
			Vector2 origin = parent_rect.position + Vector2(anchors.x, anchors.y) * parent_rect.size + item.data.layout.rect.position + flow_offsets[index];
			Vector2 extent = Vector2(anchors.z - anchors.x, anchors.w - anchors.y) * parent_rect.size + item.data.layout.rect.size;
			if (!origin.is_finite() || !extent.is_finite()) {
				origin = Vector2();
				extent = Vector2();
				item.visible = false;
			}
			item.rect = Rect2(origin, Vector2(MAX(real_t(0), extent.x), MAX(real_t(0), extent.y)));
			item.clip = item.data.style.clip ? parent_clip.intersection(item.rect) : parent_clip;
			item.visible = item.visible && item.data.style.visible && item.clip.has_area();
			item.disabled = item.disabled || item.data.input.disabled;
			resolved.write[index] = 1;
		}
	}
	draw_order.resize(items.size());
	for (int i = 0; i < items.size(); i++) {
		draw_order.write[i] = i;
	}
	auto compare = [&](int a, int b) {
		if (items[a].data.layout.z_index != items[b].data.layout.z_index) {
			return items[a].data.layout.z_index < items[b].data.layout.z_index;
		}
		if (items[a].depth != items[b].depth) {
			return items[a].depth < items[b].depth;
		}
		return items[a].data.entity < items[b].data.entity;
	};
	draw_order.sort_custom<decltype(compare)>(compare);
	if (!interactive(pressed)) {
		pressed = 0;
		keyboard_pressed = false;
	}
	if (!interactive(focused)) {
		set_focus(0);
	}
	if (hovered && (!lookup.has(hovered) || !items[lookup[hovered]].visible)) {
		hovered = 0;
	}
}
bool ECSUISystem::interactive(uint64_t id) const {
	const int *index = lookup.getptr(id);
	if (!index) {
		return false;
	}
	const LayoutItem &item = items[*index];
	int kind = item.data.input.kind;
	return item.visible && !item.disabled && (kind == ECSUIInput::BUTTON || kind == ECSUIInput::TOGGLE || kind == ECSUIInput::SLIDER || kind == ECSUIInput::TEXT_FIELD || kind == ECSUIInput::TEXT_AREA);
}
uint64_t ECSUISystem::hit_test(const Vector2 &position) const {
	for (int i = draw_order.size() - 1; i >= 0; i--) {
		const LayoutItem &item = items[draw_order[i]];
		if (item.visible && item.rect.has_point(position) && item.clip.has_point(position) && (item.data.input.block_pointer || interactive(item.data.entity))) {
			return item.data.entity;
		}
	}
	return 0;
}
void ECSUISystem::set_hover(uint64_t id) {
	if (hovered == id) {
		return;
	}
	if (hovered) {
		world->queue_ui_event(hovered, "mouse_exited", Variant());
	}
	hovered = id;
	if (hovered) {
		world->queue_ui_event(hovered, "mouse_entered", Variant());
	}
}
void ECSUISystem::set_focus(uint64_t id) {
	if (focused == id) {
		return;
	}
	if (focused) {
		world->queue_ui_event(focused, "focus_exited", Variant());
	}
	if (keyboard_pressed) {
		if (pressed) {
			world->queue_ui_event(pressed, "canceled", Variant());
		}
		pressed = 0;
		keyboard_pressed = false;
	}
	focused = id;
	composition = String();
	auto *display = DisplayServer::get_singleton();
	bool editable = is_text_field(id);
	if (display->has_feature(DisplayServerEnums::FEATURE_IME)) {
		display->window_set_ime_active(editable);
	}
	if (editable) {
		const LayoutItem &item = items[lookup[id]];
		TextState &state = text_states[id];
		state.caret = state.anchor = item.data.content.text.length();
		if (display->has_feature(DisplayServerEnums::FEATURE_IME)) {
			display->window_set_ime_position(item.rect.position + Vector2(6, item.data.style.font_size + 6));
		}
		if (display->has_feature(DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD)) {
			display->virtual_keyboard_show(item.data.content.text, item.rect, DisplayServerEnums::KEYBOARD_TYPE_DEFAULT, item.data.input.max_length, state.caret, state.caret);
		}
	} else if (display->has_feature(DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD)) {
		display->virtual_keyboard_hide();
	}
	if (focused) {
		world->queue_ui_event(focused, "focus_entered", Variant());
	}
}
void ECSUISystem::set_slider(uint64_t id, float value) {
	if (!interactive(id)) {
		return;
	}
	Dictionary update;
	update["value"] = CLAMP(value, 0.0f, 1.0f);
	if (world->set_ui(id, update)) {
		world->queue_ui_event(id, "value_changed", update["value"]);
	}
}
void ECSUISystem::activate(uint64_t id) {
	if (!interactive(id)) {
		return;
	}
	const LayoutItem &item = items[lookup[id]];
	if (item.data.input.kind == ECSUIInput::TOGGLE) {
		bool enabled = item.data.input.value < .5f;
		Dictionary update;
		update["value"] = enabled ? 1.0 : 0.0;
		world->set_ui(id, update);
		world->queue_ui_event(id, "toggled", enabled);
	}
	world->queue_ui_event(id, "pressed", Variant());
}
bool ECSUISystem::input(const Ref<InputEvent> &event, const Size2 &size) {
	if (world.is_null() || event.is_null() || event->get_device() == InputEvent::DEVICE_ID_EMULATION) {
		return false;
	}
	sync_layout(size);
	Ref<InputEventKey> key = event;
	if (key.is_valid()) {
		if (key->get_keycode() == Key::TAB && key->is_pressed() && !key->is_echo()) {
			Vector<uint64_t> candidates;
			for (int index : draw_order) {
				if (interactive(items[index].data.entity)) {
					candidates.push_back(items[index].data.entity);
				}
			}
			if (candidates.is_empty()) {
				return false;
			}
			int index = candidates.find(focused);
			index = index < 0 ? (key->is_shift_pressed() ? candidates.size() - 1 : 0) : (index + (key->is_shift_pressed() ? -1 : 1) + candidates.size()) % candidates.size();
			set_focus(candidates[index]);
			return true;
		}
		if (!interactive(focused)) {
			return false;
		}
		if (is_text_field(focused)) {
			return text_input_key(key);
		}
		if (items[lookup[focused]].data.input.kind == ECSUIInput::SLIDER && key->is_pressed() && (key->get_keycode() == Key::LEFT || key->get_keycode() == Key::RIGHT)) {
			set_slider(focused, items[lookup[focused]].data.input.value + (key->get_keycode() == Key::RIGHT ? .05f : -.05f));
			return true;
		}
		if (key->get_keycode() == Key::SPACE || key->get_keycode() == Key::ENTER || key->get_keycode() == Key::KP_ENTER) {
			if (key->is_echo()) {
				return true;
			}
			if (key->is_pressed()) {
				pressed = focused;
				keyboard_pressed = true;
			} else if (keyboard_pressed) {
				uint64_t id = pressed;
				pressed = 0;
				keyboard_pressed = false;
				if (id == focused) {
					activate(id);
				}
			}
			return true;
		}
		return false;
	}
	if (Ref<InputEventScreenTouch>(event).is_valid() || Ref<InputEventScreenDrag>(event).is_valid()) {
		return input_touch(event);
	}
	Vector2 position;
	bool down = false, up = false, motion = false;
	Ref<InputEventMouseButton> mouse = event;
	Ref<InputEventMouseMotion> move = event;
	if (mouse.is_valid()) {
		position = mouse->get_position();
		if (mouse->is_pressed() && (mouse->get_button_index() == MouseButton::WHEEL_UP || mouse->get_button_index() == MouseButton::WHEEL_DOWN)) {
			return scroll_at(hit_test(position), mouse->get_button_index() == MouseButton::WHEEL_DOWN ? 40 : -40);
		}
		if (mouse->get_button_index() != MouseButton::LEFT) {
			return hit_test(position) != 0;
		}
		down = mouse->is_pressed();
		up = !down;
	} else if (move.is_valid()) {
		position = move->get_position();
		motion = true;

	} else {
		return false;
	}
	uint64_t target = hit_test(position);
	if (move.is_valid() || mouse.is_valid()) {
		set_hover(target);
	}
	bool consumed = target || pressed;
	if (down) {
		keyboard_pressed = false;
		pressed = interactive(target) ? target : 0;
		set_focus(pressed);
		if (pressed) {
			world->queue_ui_event(pressed, "button_down", position);
		}
	}
	if ((down || motion) && is_text_field(pressed)) {
		place_caret(pressed, position, !down || (mouse.is_valid() && mouse->is_shift_pressed()));
	}
	if ((down || motion) && interactive(pressed) && items[lookup[pressed]].data.input.kind == ECSUIInput::SLIDER) {
		const Rect2 &rect = items[lookup[pressed]].rect;
		if (rect.size.x > 0) {
			set_slider(pressed, (position.x - rect.position.x) / rect.size.x);
		}
	}
	if (up) {
		uint64_t id = pressed;
		pressed = 0;
		if (id) {
			world->queue_ui_event(id, "button_up", position);
			if (target == id && !is_text_field(id)) {
				activate(id);
			}
		}
	}
	return consumed;
}
bool ECSUISystem::is_pressed(uint64_t entity) const {
	if (pressed == entity) {
		return true;
	}
	for (const auto &entry : touches) {
		if (entry.value.pressed == entity) {
			return true;
		}
	}
	return false;
}

bool ECSUISystem::input_touch(const Ref<InputEvent> &event) {
	Ref<InputEventScreenTouch> touch = event;
	Ref<InputEventScreenDrag> drag = event;
	int index = touch.is_valid() ? touch->get_index() : drag->get_index();
	Vector2 position = touch.is_valid() ? touch->get_position() : drag->get_position();
	uint64_t target = hit_test(position);
	if (touch.is_valid() && touch->is_pressed() && !touch->is_canceled()) {
		if (const TouchCapture *previous = touches.getptr(index)) {
			if (world->is_alive(previous->pressed)) {
				world->queue_ui_event(previous->pressed, "canceled", Variant());
			}
			touches.erase(index);
		}
		if (!target) {
			return false;
		}
		TouchCapture capture;
		capture.start = position;
		capture.pressed = interactive(target) && !is_pressed(target) ? target : 0;
		capture.scroll = target;
		while (capture.scroll && lookup.has(capture.scroll) && items[lookup[capture.scroll]].data.input.kind != ECSUIInput::SCROLL) {
			int parent = items[lookup[capture.scroll]].parent;
			capture.scroll = parent < 0 ? 0 : items[parent].data.entity;
		}
		// Only one pointer may manipulate a given scroll container at a time.
		for (const auto &entry : touches) {
			if (entry.value.scroll == capture.scroll) {
				capture.scroll = 0;
				break;
			}
		}
		touches[index] = capture;
		if (capture.pressed) {
			set_focus(capture.pressed);
			world->queue_ui_event(capture.pressed, "button_down", position);
		}
	}
	TouchCapture *capture = touches.getptr(index);
	if (!capture) {
		return false;
	}
	if (touch.is_valid() && (touch->is_canceled() || !touch->is_pressed())) {
		uint64_t id = capture->pressed;
		touches.erase(index);
		if (world->is_alive(id)) {
			world->queue_ui_event(id, touch->is_canceled() ? "canceled" : "button_up", position);
			if (!touch->is_canceled() && id == target && !is_text_field(id)) {
				activate(id);
			}
		}
		return true;
	}
	if (drag.is_valid() && capture->scroll) {
		if (!capture->scrolling && position.distance_to(capture->start) > 8) {
			capture->scrolling = true;
			if (world->is_alive(capture->pressed)) {
				world->queue_ui_event(capture->pressed, "canceled", Variant());
			}
			capture->pressed = 0;
		}
		if (capture->scrolling) {
			scroll_at(capture->scroll, -drag->get_relative().y);
			return true;
		}
	}
	uint64_t id = capture->pressed;
	if (id == focused && is_text_field(id)) {
		place_caret(id, position, drag.is_valid());
	}
	if (interactive(id) && items[lookup[id]].data.input.kind == ECSUIInput::SLIDER) {
		const Rect2 &rect = items[lookup[id]].rect;
		if (rect.size.x > 0) {
			set_slider(id, (position.x - rect.position.x) / rect.size.x);
		}
	}
	return true;
}

void ECSUISystem::draw(const Size2 &size) {
	if (world.is_null() || !canvas.is_valid()) {
		return;
	}
	sync_layout(size);
	auto *rs = RenderingServer::get_singleton();
	Vector<uint64_t> removed;
	for (const KeyValue<uint64_t, CanvasItem> &entry : canvas_items) {
		if (!lookup.has(entry.key)) {
			removed.push_back(entry.key);
		}
	}
	for (uint64_t id : removed) {
		rs->free_rid(canvas_items[id].content);
		rs->free_rid(canvas_items[id].clip);
		canvas_items.erase(id);
		text_states.erase(id);
	}
	for (int order = 0; order < draw_order.size(); order++) {
		const LayoutItem &item = items[draw_order[order]];
		uint64_t id = item.data.entity;
		CanvasItem *draw = canvas_items.getptr(id);
		if (!draw) {
			CanvasItem created;
			created.clip = rs->canvas_item_create();
			created.content = rs->canvas_item_create();
			rs->canvas_item_set_parent(created.clip, canvas);
			rs->canvas_item_set_parent(created.content, created.clip);
			canvas_items[id] = created;
			draw = canvas_items.getptr(id);
		}
		rs->canvas_item_set_visible(draw->clip, item.visible);
		if (!item.visible) {
			continue;
		}
		rs->canvas_item_set_draw_index(draw->clip, order);
		rs->canvas_item_set_custom_rect(draw->clip, true, item.clip);
		rs->canvas_item_set_clip(draw->clip, true);
		rs->canvas_item_set_transform(draw->content, Transform2D(0, item.rect.position));
		rs->canvas_item_clear(draw->content);
		rs->canvas_item_set_material(draw->content, item.data.content.material.is_valid() ? item.data.content.material->get_rid() : RID());
		Rect2 rect(Vector2(), item.rect.size);
		Color background = item.data.style.background, foreground = item.data.style.foreground;
		Color modulation = item.inherited_modulate * item.data.style.self_modulate;
		rs->canvas_item_set_modulate(draw->content, modulation);
		String state = item.disabled ? "disabled" : is_pressed(id) ? (hovered == id ? "hover_pressed" : "pressed")
				: hovered == id									   ? "hover"
																   : "normal";
		const Dictionary &colors = item.data.content.theme_colors;
		String font_color = state == "normal" ? "font_color" : "font_" + state + "_color";
		foreground = colors.get(font_color, colors.get("font_color", colors.get("default_color", foreground)));
		Color texture_color = colors.get("icon_" + state + "_color", colors.get("icon_normal_color", foreground));
		String style_name = item.data.input.kind == ECSUIInput::SCROLL ? "panel" : item.disabled && is_text_field(id) ? "read_only"
																													  : state;
		const Dictionary &styles = item.data.content.theme_styles;
		Ref<StyleBox> style = styles.get(style_name, styles.get("normal", Variant()));
		if (item.disabled) {
			background.a *= .45f;
			if (!colors.has(font_color)) {
				foreground.a *= .45f;
			}
			if (!colors.has("icon_disabled_color")) {
				texture_color.a *= .45f;
			}
		} else if (is_pressed(id)) {
			background = background.darkened(.25f);
		} else if (hovered == id && interactive(id)) {
			background = background.lightened(.15f);
		}
		if (style.is_valid()) {
			style->draw(draw->content, rect);
		} else if (background.a > 0) {
			rs->canvas_item_add_rect(draw->content, rect, background);
		}
		if (item.data.content.texture.is_valid()) {
			const Ref<Texture2D> &texture = item.data.content.texture;
			Vector2 native_size = texture->get_size();
			if (native_size.x > 0 && native_size.y > 0 && rect.has_area()) {
				Rect2 destination = rect, source;
				int stretch = item.data.style.image_stretch;
				if (stretch == 2 || stretch == 3) {
					destination.size = native_size;
					if (stretch == 3) {
						destination.position = (rect.size - native_size) * 0.5;
					}
				} else if (stretch == 4 || stretch == 5) {
					destination.size = native_size * MIN(rect.size.x / native_size.x, rect.size.y / native_size.y);
					if (stretch == 5) {
						destination.position = (rect.size - destination.size) * 0.5;
					}
				} else if (stretch == 6) {
					real_t scale = MAX(rect.size.x / native_size.x, rect.size.y / native_size.y);
					source.size = rect.size / scale;
					source.position = (native_size - source.size) * 0.5;
				}
				destination.size.x *= item.data.style.flip_h ? -1 : 1;
				destination.size.y *= item.data.style.flip_v ? -1 : 1;
				if (source.has_area()) {
					texture->draw_rect_region(draw->content, destination, source, texture_color);
				} else if (stretch == 1 && Object::cast_to<AtlasTexture>(*texture)) {
					// Match TextureRect's atlas tiling path without creating a Control.
					source = Rect2(Vector2(), native_size);
					Ref<AtlasTexture> atlas = texture;
					bool valid = true;
					while (valid && atlas.is_valid()) {
						valid = atlas->get_rect_region(destination, source, destination, source);
						atlas = atlas->get_atlas();
					}
					if (valid) {
						rs->canvas_item_add_nine_patch(draw->content, destination, source, texture->get_scaled_rid(), Vector2(), Vector2(), RSE::NINE_PATCH_TILE, RSE::NINE_PATCH_TILE, true, texture_color);
					}
				} else {
					texture->draw_rect(draw->content, destination, stretch == 1, texture_color);
				}
			}
		}
		ECSUIDrawCommands::draw(draw->content, item.data.content.draw_commands, Color(1, 1, 1, 1));
		if (item.data.input.kind == ECSUIInput::PROGRESS || item.data.input.kind == ECSUIInput::SLIDER) {
			Rect2 fill = rect;
			fill.size.x *= item.data.input.value;
			rs->canvas_item_add_rect(draw->content, fill, foreground);
		}
		float text_left=6;
		const Dictionary &icons=item.data.content.theme_icons;
		String icon_key=item.data.input.kind==ECSUIInput::TOGGLE?(item.data.input.value>=.5f?"checked":"unchecked"):"icon";
		if(item.disabled && icons.has(icon_key+"_disabled")) { icon_key+="_disabled"; }
		Ref<Texture2D> theme_icon=icons.get(icon_key,Variant());
		if(theme_icon.is_valid()) {
			Vector2 size=theme_icon->get_size(); float maximum=MIN(rect.size.y-8,rect.size.x-12);
			if(maximum>0 && size.x>0 && size.y>0) {
				size*=MIN(1.f,maximum/MAX(size.x,size.y)); theme_icon->draw_rect(draw->content,Rect2(Vector2(6,(rect.size.y-size.y)*.5),size),false,texture_color);
				text_left+=size.x+MAX(0,int(item.data.content.theme_constants.get("h_separation",4)));
			}
		}
		String text = item.data.content.text;
		if (is_text_field(id)) {
			draw_editable(item, draw->content, foreground);
			text = String();
		}
		if (item.data.input.kind == ECSUIInput::TOGGLE && theme_icon.is_null()) {
			text = (item.data.input.value >= .5f ? "[x] " : "[ ] ") + text;
		}
		Ref<Font> font = item.data.content.font;
		if (font.is_null() && ThemeDB::get_singleton()) {
			font = ThemeDB::get_singleton()->get_fallback_font();
		}
		if (font.is_valid() && !text.is_empty() && rect.size.x > 12) {
			if (item.data.style.outline_size > 0) {
				Color outline = colors.get("font_outline_color", Color(0, 0, 0, 1));
				font->draw_multiline_string_outline(draw->content, Vector2(text_left, 4 + font->get_ascent(item.data.style.font_size)), text, HORIZONTAL_ALIGNMENT_LEFT, MAX(0.f,rect.size.x-text_left-6), item.data.style.font_size, -1, item.data.style.outline_size, outline);
			}
			font->draw_multiline_string(draw->content, Vector2(text_left, 4 + font->get_ascent(item.data.style.font_size)), text, HORIZONTAL_ALIGNMENT_LEFT, MAX(0.f,rect.size.x-text_left-6), item.data.style.font_size, -1, foreground);
		}
		if (focused == id) {
			Ref<StyleBox> focus_style = styles.get("focus", Variant());
			if (focus_style.is_valid()) {
				focus_style->draw(draw->content, rect);
				continue;
			}
			Color focus(0.35, 0.75, 1, 1);
			rs->canvas_item_add_rect(draw->content, Rect2(0, 0, rect.size.x, 2), focus);
			rs->canvas_item_add_rect(draw->content, Rect2(0, MAX(0.0f, rect.size.y - 2), rect.size.x, 2), focus);
		}
	}
}
void ECSUISystem::cancel_input() {
	if (world.is_valid()) {
		for (const auto &entry : touches) {
			if (world->is_alive(entry.value.pressed)) {
				world->queue_ui_event(entry.value.pressed, "canceled", Variant());
			}
		}
	}
	if (world.is_valid() && pressed) {
		world->queue_ui_event(pressed, "canceled", Variant());
	}
	if (world.is_valid()) {
		set_focus(0);
		set_hover(0);
	}
	pressed = hovered = focused = 0;
	touches.clear();
	keyboard_pressed = false;
}
void ECSUISystem::clear() {
	auto *rs = RenderingServer::get_singleton();
	if (rs) {
		for (const KeyValue<uint64_t, CanvasItem> &entry : canvas_items) {
			rs->free_rid(entry.value.content);
			rs->free_rid(entry.value.clip);
		}
		if (canvas.is_valid()) {
			rs->free_rid(canvas);
		}
	}
	canvas_items.clear();
	canvas = RID();
	items.clear();
	lookup.clear();
	draw_order.clear();
	text_states.clear();
	composition = String();
	world.unref();
	revision = 0;
	pressed = hovered = focused = 0;
	touches.clear();
	keyboard_pressed = false;
}

bool ECSUISystem::is_text_field(uint64_t id) const {
	const int *index = lookup.getptr(id);
	if (!index) {
		return false;
	}
	int kind = items[*index].data.input.kind;
	return kind == ECSUIInput::TEXT_FIELD || kind == ECSUIInput::TEXT_AREA;
}
void ECSUISystem::input_text(const String &insert) {
	if (world.is_null()) {
		return;
	}
	sync_layout(viewport_size);
	if (!interactive(focused) || !is_text_field(focused)) {
		return;
	}
	const LayoutItem &item = items[lookup[focused]];
	String text = item.data.content.text;
	TextState &state = text_states[focused];
	state.caret = CLAMP(state.caret, 0, text.length());
	state.anchor = CLAMP(state.anchor, 0, text.length());
	int first = MIN(state.caret, state.anchor), last = MAX(state.caret, state.anchor);
	String inserted = insert.replace("\r", "");
	if (item.data.input.kind == ECSUIInput::TEXT_FIELD) {
		inserted = inserted.replace("\n", "");
	}
	int available = MAX(0, item.data.input.max_length - (text.length() - (last - first)));
	inserted = inserted.substr(0, available);
	String updated = text.substr(0, first) + inserted + text.substr(last);
	Dictionary definition;
	definition["text"] = updated;
	if (world->set_ui(focused, definition)) {
		state.caret = state.anchor = first + inserted.length();
		composition = String();
		world->queue_ui_event(focused, "text_changed", updated);
	}
}
bool ECSUISystem::text_input_key(const Ref<InputEventKey> &key) {
	if (!key->is_pressed()) {
		return true;
	}
	const LayoutItem &item = items[lookup[focused]];
	const String &text = item.data.content.text;
	TextState &state = text_states[focused];
	state.caret = CLAMP(state.caret, 0, text.length());
	state.anchor = CLAMP(state.anchor, 0, text.length());
	if (key->is_command_or_control_pressed()) {
		if (key->get_keycode() == Key::A) {
			state.anchor = 0;
			state.caret = text.length();
			return true;
		}
		if (key->get_keycode() == Key::V) {
			input_text(DisplayServer::get_singleton()->clipboard_get());
			return true;
		}
		if (key->get_keycode() == Key::C || key->get_keycode() == Key::X) {
			if (state.caret != state.anchor) {
				DisplayServer::get_singleton()->clipboard_set(text.substr(MIN(state.caret, state.anchor), Math::abs(state.caret - state.anchor)));
				if (key->get_keycode() == Key::X) {
					input_text(String());
				}
			}
			return true;
		}
		return false;
	}
	Key code = key->get_keycode();
	Ref<TextParagraph> shaped = shape_text(item, text);
	if (code == Key::LEFT || code == Key::RIGHT || code == Key::HOME || code == Key::END || code == Key::UP || code == Key::DOWN) {
		if (code == Key::LEFT || code == Key::RIGHT) {
			if (state.caret != state.anchor && !key->is_shift_pressed()) {
				state.caret = code == Key::LEFT ? MIN(state.caret, state.anchor) : MAX(state.caret, state.anchor);
			} else {
				state.caret = code == Key::LEFT ? TS->shaped_text_prev_grapheme_pos(shaped->get_rid(), state.caret) : TS->shaped_text_next_grapheme_pos(shaped->get_rid(), state.caret);
			}
		} else {
			float y = 0;
			for (int line = 0; line < shaped->get_line_count(); line++) {
				Vector2i range = shaped->get_line_range(line);
				float height = shaped->get_line_size(line).y;
				if (state.caret >= range.x && (state.caret < range.y || line == shaped->get_line_count() - 1)) {
					if (code == Key::HOME || code == Key::END) {
						state.caret = code == Key::HOME ? range.x : range.y;
						if (code == Key::END && state.caret > 0 && text[state.caret - 1] == '\n') {
							state.caret--;
						}
					} else {
						CaretInfo caret = TS->shaped_text_get_carets(shaped->get_line_rid(line), state.caret);
						state.caret = shaped->hit_test(Vector2(caret.l_caret.position.x, y + (code == Key::UP ? -height * .5f : height * 1.5f)));
					}
					break;
				}
				y += height;
			}
		}
		state.caret = CLAMP(state.caret, 0, text.length());
		if (!key->is_shift_pressed()) {
			state.anchor = state.caret;
		}
		return true;
	}
	if (!composition.is_empty()) {
		return true;
	}
	if (code == Key::BACKSPACE || code == Key::KEY_DELETE) {
		if (state.anchor == state.caret) {
			if (code == Key::BACKSPACE) {
				state.anchor = TS->shaped_text_prev_grapheme_pos(shaped->get_rid(), state.caret);
			} else {
				state.anchor = TS->shaped_text_next_grapheme_pos(shaped->get_rid(), state.caret);
			}
		}
		input_text(String());
		return true;
	}
	if (code == Key::ENTER || code == Key::KP_ENTER) {
		if (item.data.input.kind == ECSUIInput::TEXT_AREA) {
			input_text("\n");
		} else {
			world->queue_ui_event(focused, "text_submitted", text);
		}
		return true;
	}
	if (!key->is_alt_pressed() && key->get_unicode() >= 32) {
		input_text(String::chr(key->get_unicode()));
		return true;
	}
	return false;
}
void ECSUISystem::update_ime() {
	if (is_text_field(focused)) {
		composition = DisplayServer::get_singleton()->ime_get_text();
	}
}
bool ECSUISystem::scroll_at(uint64_t id, double amount) {
	while (id && lookup.has(id)) {
		const LayoutItem &item = items[lookup[id]];
		if (item.data.input.kind == ECSUIInput::SCROLL && item.visible && !item.disabled) {
			float bottom = item.data.layout.content_size.y + item.data.layout.padding.y * 2;
			for (const LayoutItem &child : items) {
				if (child.parent == lookup[id] && child.data.style.visible) {
					bottom = MAX(bottom, float(child.rect.get_end().y - item.rect.position.y + item.data.layout.scroll.y));
				}
			}
			Vector2 scroll = item.data.layout.scroll;
			scroll.y = CLAMP(scroll.y + amount, 0.0, double(MAX(0.0f, bottom - item.rect.size.y)));
			Dictionary update;
			update["scroll"] = scroll;
			world->set_ui(id, update);
			world->queue_ui_event(id, "scrolled", scroll);
			return true;
		}
		id = item.parent >= 0 ? items[item.parent].data.entity : 0;
	}
	return false;
}

Ref<TextParagraph> ECSUISystem::shape_text(const LayoutItem &item, const String &text) {
	TextState &state = text_states[item.data.entity];
	Ref<Font> font = item.data.content.font;
	if (font.is_null() && ThemeDB::get_singleton()) {
		font = ThemeDB::get_singleton()->get_fallback_font();
	}
	float width = item.data.input.kind == ECSUIInput::TEXT_AREA ? MAX(1.0f, item.rect.size.x - 12) : -1;
	if (state.shaped.is_null() || state.shaped_text != text || state.font != font || state.font_size != item.data.style.font_size || state.width != width) {
		state.shaped.instantiate();
		state.shaped->set_width(width);
		state.shaped->set_break_flags(item.data.input.kind == ECSUIInput::TEXT_AREA ? BitField<TextServer::LineBreakFlag>(TextServer::BREAK_MANDATORY | TextServer::BREAK_WORD_BOUND | TextServer::BREAK_ADAPTIVE) : BitField<TextServer::LineBreakFlag>());
		if (font.is_valid()) {
			state.shaped->add_string(text, font, item.data.style.font_size);
		}
		state.shaped_text = text;
		state.font = font;
		state.font_size = item.data.style.font_size;
		state.width = width;
	}
	return state.shaped;
}
void ECSUISystem::place_caret(uint64_t id, const Vector2 &position, bool extend) {
	if (!is_text_field(id)) {
		return;
	}
	const LayoutItem &item = items[lookup[id]];
	Ref<TextParagraph> shaped = shape_text(item, item.data.content.text);
	TextState &state = text_states[id];
	state.caret = CLAMP(shaped->hit_test(position - item.rect.position - Vector2(6, 4) + state.scroll), 0, item.data.content.text.length());
	if (!extend) {
		state.anchor = state.caret;
	}
}
void ECSUISystem::draw_editable(const LayoutItem &item, RID canvas_item, Color color) {
	uint64_t id = item.data.entity;
	TextState &state = text_states[id];
	const String &original = item.data.content.text;
	state.caret = CLAMP(state.caret, 0, original.length());
	state.anchor = CLAMP(state.anchor, 0, original.length());
	bool active = focused == id;
	String text = original;
	if (active && !composition.is_empty()) {
		text = text.substr(0, state.caret) + composition + text.substr(state.caret);
	}
	if (text.is_empty() && !active) {
		text = item.data.content.placeholder;
		color.a *= .6f;
	}
	Ref<TextParagraph> shaped = shape_text(item, text);
	auto *rs = RenderingServer::get_singleton();
	int caret_pos = state.caret + (active ? composition.length() : 0);
	Vector2 caret_position;
	float caret_height = item.data.style.font_size;
	float y = 0;
	for (int line = 0; line < shaped->get_line_count(); line++) {
		Vector2i range = shaped->get_line_range(line);
		float height = shaped->get_line_size(line).y;
		if (caret_pos >= range.x && (caret_pos < range.y || line == shaped->get_line_count() - 1)) {
			CaretInfo caret = TS->shaped_text_get_carets(shaped->get_line_rid(line), caret_pos);
			caret_position = Vector2(caret.l_caret.position.x, y);
			caret_height = height;
		}
		y += height;
	}
	if (active) {
		Vector2 available = (item.rect.size - Vector2(12, 8)).max(Vector2(1, 1));
		state.scroll.x = MAX(0.0f, MAX(caret_position.x + 2 - available.x, MIN(state.scroll.x, caret_position.x)));
		state.scroll.y = MAX(0.0f, MAX(caret_position.y + caret_height - available.y, MIN(state.scroll.y, caret_position.y)));
	}
	Vector2 origin = Vector2(6, 4) - state.scroll;
	y = 0;
	for (int line = 0; line < shaped->get_line_count(); line++) {
		float height = shaped->get_line_size(line).y;
		if (active && state.caret != state.anchor && composition.is_empty()) {
			Vector<Vector2> spans = TS->shaped_text_get_selection(shaped->get_line_rid(line), MIN(state.caret, state.anchor), MAX(state.caret, state.anchor));
			for (const Vector2 &span : spans) {
				rs->canvas_item_add_rect(canvas_item, Rect2(origin + Vector2(span.x, y), Vector2(span.y - span.x, height)), Color(.18, .38, .65, .8));
			}
		}
		shaped->draw_line(canvas_item, origin + Vector2(0, y), line, color);
		y += height;
	}
	if (active) {
		rs->canvas_item_add_rect(canvas_item, Rect2(origin + caret_position, Vector2(1.5, caret_height)), color);
		auto *display = DisplayServer::get_singleton();
		if (display->has_feature(DisplayServerEnums::FEATURE_IME)) {
			display->window_set_ime_position(item.rect.position + origin + caret_position + Vector2(0, caret_height));
		}
	}
}
