// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_ui_draw_commands.h"
#include "ecs_world.h"

#include "scene/resources/style_box.h"

#include <cstring>

namespace {
const char *ui_kinds[] = { "panel", "label", "button", "toggle", "progress", "image", "slider", "text_field", "text_area", "scroll", "hbox", "vbox", "grid" };
bool finite_color(const Color &color) {
	return Math::is_finite(color.r) && Math::is_finite(color.g) && Math::is_finite(color.b) && Math::is_finite(color.a);
}
} //namespace
bool ECSWorld::is_ui_component(const StringName &name) {
	return name == StringName("ui_layout") || name == StringName("ui_style") || name == StringName("ui_input") || name == StringName("ui_text");
}
void ECSWorld::store_ui_column(uint32_t index, const StringName &name, const void *data) {
	Pool &pool = pools[name];
	int row;
	if (const int *existing = pool.rows.getptr(index)) {
		row = *existing;
	} else {
		row = pool.entities.size();
		pool.entities.push_back(index);
		pool.rows[index] = row;
		pool.bytes.resize((row + 1) * pool.stride);
	}
	memcpy(pool.bytes.ptrw() + row * pool.stride, data, pool.stride);
}
bool ECSWorld::set_ui(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_ui(id);
	data.merge(definition, true);
	ECSUIItem item;
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		Variant value = data[key];
		if (field == "padding" || field == "scroll" || field == "content_size") {
			if (value.get_type() != Variant::VECTOR2 || !Vector2(value).is_finite() || Vector2(value).x < 0 || Vector2(value).y < 0) {
				return false;
			}
			if (field == "padding") {
				item.layout.padding = value;
			} else if (field == "content_size") {
				item.layout.content_size = value;
			} else {
				item.layout.scroll = value;
			}
		} else if (field == "gap") {
			if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) || !Math::is_finite(double(value)) || double(value) < 0) {
				return false;
			}
			item.layout.gap = value;
		} else if (field == "columns" || field == "max_length") {
			if (value.get_type() != Variant::INT || int64_t(value) < 1 || int64_t(value) > 100000) {
				return false;
			}
			if (field == "columns") {
				item.layout.columns = value;
			} else {
				item.input.max_length = value;
			}
		} else if (field == "placeholder") {
			if (value.get_type() != Variant::STRING) {
				return false;
			}
			item.content.placeholder = value;
		} else if (field == "rect") {
			if (value.get_type() != Variant::RECT2) {
				return false;
			}
			item.layout.rect = value;
		} else if (field == "anchors") {
			if (value.get_type() != Variant::VECTOR4) {
				return false;
			}
			item.layout.anchors = value;
		} else if (field == "z_index") {
			if (value.get_type() != Variant::INT || int64_t(value) < -4096 || int64_t(value) > 4096) {
				return false;
			}
			item.layout.z_index = value;
		} else if (field == "background") {
			if (value.get_type() != Variant::COLOR) {
				return false;
			}
			item.style.background = value;
		} else if (field == "foreground") {
			if (value.get_type() != Variant::COLOR) {
				return false;
			}
			item.style.foreground = value;
		} else if (field == "modulate" || field == "self_modulate") {
			if (value.get_type() != Variant::COLOR || !finite_color(value)) {
				return false;
			}
			if (field == "modulate") {
				item.style.modulate = value;
			} else {
				item.style.self_modulate = value;
			}
		} else if (field == "image_stretch") {
			if (value.get_type() != Variant::INT || int64_t(value) < 0 || int64_t(value) > 6) {
				return false;
			}
			item.style.image_stretch = value;
		} else if (field == "flip_h" || field == "flip_v") {
			if (value.get_type() != Variant::BOOL) {
				return false;
			}
			if (field == "flip_h") {
				item.style.flip_h = value;
			} else {
				item.style.flip_v = value;
			}
		} else if (field == "outline_size") {
			if (value.get_type() != Variant::INT || int64_t(value) < 0 || int64_t(value) > 64) {
				return false;
			}
			item.style.outline_size = value;
		} else if (field == "theme_styles" || field == "theme_colors" || field == "theme_icons" || field == "theme_constants") {
			if (value.get_type() != Variant::DICTIONARY) {
				return false;
			}
			Dictionary overrides = value;
			for (const Variant &entry : overrides.keys()) {
				if (entry.get_type() != Variant::STRING && entry.get_type() != Variant::STRING_NAME) {
					return false;
				}
				if (field == "theme_styles") {
					if (overrides[entry].get_type() != Variant::OBJECT) {
						return false;
					}
					Ref<StyleBox> style = overrides[entry];
					if (style.is_null()) {
						return false;
					}
				} else if (field=="theme_icons") {
					if(overrides[entry].get_type()!=Variant::OBJECT || Ref<Texture2D>(overrides[entry]).is_null()) { return false; }
				} else if (field=="theme_constants") {
					if(overrides[entry].get_type()!=Variant::INT || Math::abs(int64_t(overrides[entry]))>1000000) { return false; }
				} else if (overrides[entry].get_type() != Variant::COLOR || !finite_color(overrides[entry])) {
					return false;
				}
			}
			if (field == "theme_styles") {
				item.content.theme_styles = overrides.duplicate();
			} else if(field=="theme_icons") { item.content.theme_icons=overrides.duplicate();
			} else if(field=="theme_constants") { item.content.theme_constants=overrides.duplicate();
			} else {
				item.content.theme_colors = overrides.duplicate();
			}
		} else if (field == "font_size") {
			if (value.get_type() != Variant::INT || int64_t(value) < 1 || int64_t(value) > 512) {
				return false;
			}
			item.style.font_size = value;
		} else if (field == "visible") {
			if (value.get_type() != Variant::BOOL) {
				return false;
			}
			item.style.visible = value;
		} else if (field == "clip") {
			if (value.get_type() != Variant::BOOL) {
				return false;
			}
			item.style.clip = value;
		} else if (field == "disabled") {
			if (value.get_type() != Variant::BOOL) {
				return false;
			}
			item.input.disabled = value;
		} else if (field == "block_pointer") {
			if (value.get_type() != Variant::BOOL) {
				return false;
			}
			item.input.block_pointer = value;
		} else if (field == "value") {
			if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) || !Math::is_finite(double(value)) || double(value) < 0 || double(value) > 1) {
				return false;
			}
			item.input.value = value;
		} else if (field == "kind") {
			if (value.get_type() != Variant::STRING) {
				return false;
			}
			int kind = -1;
			for (int i = 0; i < 13; i++) {
				if (String(value) == ui_kinds[i]) {
					kind = i;
					break;
				}
			}
			if (kind < 0) {
				return false;
			}
			item.input.kind = kind;
		} else if (field == "draw_commands") {
			if (value.get_type() != Variant::ARRAY || !ECSUIDrawCommands::validate(value)) {
				return false;
			}
			item.content.draw_commands = Array(value).duplicate(true);
		} else if (field == "text") {
			if (value.get_type() != Variant::STRING) {
				return false;
			}
			item.content.text = value;
		} else if (field == "font") {
			if (value.get_type() == Variant::NIL || (value.get_type() == Variant::OBJECT && value.is_null())) {
				item.content.font.unref();
				continue;
			}
			if (value.get_type() != Variant::OBJECT) {
				return false;
			}
			item.content.font = value;
			if (item.content.font.is_null()) {
				return false;
			}
		} else if (field == "material") {
			if (value.get_type() == Variant::NIL || (value.get_type() == Variant::OBJECT && value.is_null())) {
				item.content.material.unref();
				continue;
			}
			if (value.get_type() != Variant::OBJECT) {
				return false;
			}
			Ref<Material> material = value;
			if (material.is_null() || material->get_shader_mode() != Shader::MODE_CANVAS_ITEM) {
				return false;
			}
			item.content.material = material;
		} else if (field == "texture") {
			if (value.get_type() == Variant::NIL || (value.get_type() == Variant::OBJECT && value.is_null())) {
				item.content.texture.unref();
				continue;
			}
			if (value.get_type() != Variant::OBJECT) {
				return false;
			}
			item.content.texture = value;
			if (item.content.texture.is_null()) {
				return false;
			}
		} else {
			return false;
		}
	}
	if (!item.layout.rect.is_finite() || !item.layout.anchors.is_finite() || !finite_color(item.style.background) || !finite_color(item.style.foreground)) {
		return false;
	}
	if ((item.layout.rect.size.x < 0 && item.layout.anchors.x == item.layout.anchors.z) || (item.layout.rect.size.y < 0 && item.layout.anchors.y == item.layout.anchors.w) || item.layout.anchors.x > item.layout.anchors.z || item.layout.anchors.y > item.layout.anchors.w) {
		return false;
	}
	if ((item.input.kind == ECSUIInput::TEXT_FIELD || item.input.kind == ECSUIInput::TEXT_AREA) && item.content.text.length() > item.input.max_length) {
		return false;
	}
	uint32_t index = uint32_t(id);
	store_ui_column(index, "ui_layout", &item.layout);
	store_ui_column(index, "ui_style", &item.style);
	store_ui_column(index, "ui_input", &item.input);
	uint8_t tag = 1;
	store_ui_column(index, "ui_text", &tag);
	ui_texts[index] = item.content;
	ui_revision++;
	return true;
}
Dictionary ECSWorld::get_ui(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary data;
	if (!is_alive(id)) {
		return data;
	}
	uint32_t index = uint32_t(id);
	const ECSUIText *content = ui_texts.getptr(index);
	if (!content) {
		return data;
	}
	ECSUILayout layout;
	ECSUIStyle style;
	ECSUIInput input;
	const Pool &lp = pools["ui_layout"], &sp = pools["ui_style"], &ip = pools["ui_input"];
	memcpy(&layout, lp.bytes.ptr() + lp.rows[index] * lp.stride, sizeof(layout));
	memcpy(&style, sp.bytes.ptr() + sp.rows[index] * sp.stride, sizeof(style));
	memcpy(&input, ip.bytes.ptr() + ip.rows[index] * ip.stride, sizeof(input));
	data["padding"] = layout.padding;
	data["scroll"] = layout.scroll;
	data["content_size"] = layout.content_size;
	data["gap"] = layout.gap;
	data["columns"] = layout.columns;
	data["max_length"] = input.max_length;
	data["placeholder"] = content->placeholder;
	data["rect"] = layout.rect;
	data["anchors"] = layout.anchors;
	data["z_index"] = layout.z_index;
	data["background"] = style.background;
	data["foreground"] = style.foreground;
	data["modulate"] = style.modulate;
	data["self_modulate"] = style.self_modulate;
	data["image_stretch"] = style.image_stretch;
	data["flip_h"] = style.flip_h;
	data["flip_v"] = style.flip_v;
	data["font_size"] = style.font_size;
	data["outline_size"] = style.outline_size;
	data["theme_styles"] = content->theme_styles.duplicate();
	data["theme_colors"] = content->theme_colors.duplicate();
	data["theme_icons"]=content->theme_icons.duplicate();
	data["theme_constants"]=content->theme_constants.duplicate();
	data["visible"] = style.visible;
	data["clip"] = style.clip;
	data["kind"] = ui_kinds[input.kind];
	data["disabled"] = input.disabled;
	data["block_pointer"] = input.block_pointer;
	data["value"] = input.value;
	data["text"] = content->text;
	data["draw_commands"] = content->draw_commands.duplicate(true);
	if (content->font.is_valid()) {
		data["font"] = content->font;
	}
	if (content->texture.is_valid()) {
		data["texture"] = content->texture;
	}
	if (content->material.is_valid()) {
		data["material"] = content->material;
	}
	return data;
}
bool ECSWorld::remove_ui(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_active_in_hierarchy(id) || !ui_texts.has(uint32_t(id))) {
		return false;
	}
	for (const String &name : { String("ui_layout"), String("ui_style"), String("ui_input"), String("ui_text") }) {
		remove_row(pools[name], uint32_t(id));
	}
	ui_texts.erase(uint32_t(id));
	ui_revision++;
	return true;
}
Vector<ECSUIItem> ECSWorld::get_ui_items() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Vector<ECSUIItem>());
	Vector<ECSUIItem> items;
	const Pool &lp = pools["ui_layout"], &sp = pools["ui_style"], &ip = pools["ui_input"];
	items.resize(lp.entities.size());
	for (int i = 0; i < lp.entities.size(); i++) {
		uint32_t index = lp.entities[i];
		ECSUIItem &item = items.write[i];
		item.entity = (uint64_t(slots[index].generation) << 32) | index;
		memcpy(&item.layout, lp.bytes.ptr() + i * lp.stride, sizeof(item.layout));
		memcpy(&item.style, sp.bytes.ptr() + sp.rows[index] * sp.stride, sizeof(item.style));
		memcpy(&item.input, ip.bytes.ptr() + ip.rows[index] * ip.stride, sizeof(item.input));
		item.content = ui_texts[index];
		item.style.visible = item.style.visible && slots[index].active;
	}
	return items;
}
void ECSWorld::queue_ui_event(uint64_t id, const StringName &type, const Variant &value) {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	if (!is_active_in_hierarchy(id) || !ui_texts.has(uint32_t(id))) {
		return;
	}
	Dictionary event;
	event["entity"] = id;
	event["type"] = type;
	event["value"] = value;
	if (!pending_ui_events.is_empty() && type == StringName("value_changed")) {
		Dictionary last = pending_ui_events[pending_ui_events.size() - 1];
		if (uint64_t(int64_t(last["entity"])) == id && StringName(last["type"]) == type) {
			pending_ui_events[pending_ui_events.size() - 1] = event;
			return;
		}
	}
	ERR_FAIL_COND_MSG(pending_ui_events.size() >= 4096, "ECS UI event queue is full.");
	pending_ui_events.push_back(event);
}
void ECSWorld::dispatch_ui_events() {
	current_ui_events = pending_ui_events;
	pending_ui_events = Array();
	for (int i = 0; i < current_ui_events.size(); i++) {
		Dictionary event = current_ui_events[i];
		uint64_t id = uint64_t(int64_t(event["entity"]));
		if (is_active_in_hierarchy(id) && ui_texts.has(uint32_t(id))) {
			ui_events_dispatched++;
			last_ui_event = event.duplicate(true);
			emit_signal("ui_event", id, event["type"], event["value"]);
		}
	}
}
Array ECSWorld::get_ui_events() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Array());
	Array result;
	for (int i = 0; i < current_ui_events.size(); i++) {
		Dictionary event = current_ui_events[i];
		uint64_t id = uint64_t(int64_t(event["entity"]));
		if (is_active_in_hierarchy(id) && ui_texts.has(uint32_t(id))) {
			result.push_back(event.duplicate(true));
		}
	}
	return result;
}
