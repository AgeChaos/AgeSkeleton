#include "ecs_ui_components.h"

#include "core/math/rect2.h"
#include "core/math/vector4.h"

namespace ECSUIComponents {
static const char *names[] = { "ui_layout", "ui_image", "ui_text", "ui_button", "ui_toggle", "ui_input_field", "ui_slider", "ui_progress", "ui_scroll" };
bool is_component(const String &name) {
	for (const char *key : names) {
		if (name == key) {
			return true;
		}
	}
	return false;
}
String title(const String &name) {
	if (name == "particles") { return String(U"粒子特效"); }
	const char32_t *labels[] = { U"UI 布局", U"UI 图片与背景", U"UI 文字", U"UI 按钮", U"UI 开关", U"UI 输入框", U"UI 滑条", U"UI 进度条", U"UI 滚动" };
	for (int i = 0; i < 9; i++) {
		if (name == names[i]) {
			return String(labels[i]);
		}
	}
	return name.capitalize();
}
Dictionary defaults(const String &name) {
	Dictionary d;
	if (name == "ui_layout") {
		d["rect"] = Rect2(20, 20, 240, 60);
		d["anchors"] = Vector4();
		d["pivot"] = Vector2(.5, .5);
		d["alignment"] = 0;
		d["margins"] = Vector4();
		d["padding"] = Vector2();
		d["gap"] = 8.0;
		d["columns"] = 2;
		d["arrangement"] = 0;
		d["z_index"] = 0;
		d["visible"] = true;
		d["clip"] = true;
		d["modulate"] = Color(1, 1, 1, 1);
		d["self_modulate"] = Color(1, 1, 1, 1);
	} else if (name == "ui_image") {
		d["background"] = Color(.12, .16, .22, 1);
		d["texture"] = Variant();
		d["material"] = Variant();
		d["image_stretch"] = 0;
		d["flip_h"] = false;
		d["flip_v"] = false;
	} else if (name == "ui_text") {
		d["text"] = String(U"文字");
		d["font"] = Variant();
		d["font_size"] = 20;
		d["foreground"] = Color(1, 1, 1, 1);
	} else if (name == "ui_scroll") {
		d["scroll"] = Vector2();
		d["content_size"] = Vector2(480, 640);
	} else {
		if (name != "ui_progress") {
			d["disabled"] = false;
			d["block_pointer"] = true;
		}
		if (name == "ui_toggle" || name == "ui_slider" || name == "ui_progress") {
			d["value"] = 0.0;
		}
		if (name == "ui_input_field") {
			d["max_length"] = 4096;
			d["placeholder"] = String(U"请输入…");
			d["multiline"] = false;
		}
	}
	return d;
}
bool has_ui(const Dictionary &entity) {
	return entity.has("ui") || entity.has("ui_layout");
}
Dictionary migrate(const Dictionary &entity) {
	Dictionary result = entity.duplicate(true);
	if (!result.has("ui")) {
		for (const char *key : names) {
			if (result.has(key) && result[key].get_type() == Variant::DICTIONARY) {
				Dictionary full = defaults(key);
				full.merge(result[key], true);
				result[key] = full;
			}
		}
		return result;
	}
	if (!result.has("ui") || result["ui"].get_type() != Variant::DICTIONARY || result.has("ui_layout")) {
		return result;
	}
	Dictionary old = result["ui"];
	String kind = old.get("kind", "panel");
	result.erase("ui");
	Dictionary layout = defaults("ui_layout");
	// Preserve the legacy default rectangle and pointer interception.
	layout["rect"] = Rect2(0, 0, 180, 40);
	layout["block_pointer"] = old.get("block_pointer", true);
	layout["disabled"] = old.get("disabled", false);
	layout["arrangement"] = kind == "hbox" ? 1 : kind == "vbox" ? 2
			: kind == "grid"									? 3
																: 0;
	for (const Variant &key : layout.keys()) {
		if (old.has(key)) {
			layout[key] = old[key];
		}
	}
	result["ui_layout"] = layout;
	Dictionary image = defaults("ui_image");
	image["foreground"] = old.get("foreground", Color(1, 1, 1, 1));
	result["ui_image"] = image;
	if (old.has("text") || kind == "label" || kind == "button" || kind == "toggle" || kind == "text_field" || kind == "text_area") {
		Dictionary text = defaults("ui_text");
		text["text"] = old.get("text", "");
		result["ui_text"] = text;
	}
	String behavior = kind == "button" ? "ui_button" : kind == "toggle" ? "ui_toggle"
			: kind == "slider"											? "ui_slider"
			: kind == "progress"										? "ui_progress"
			: kind == "scroll"											? "ui_scroll"
			: (kind == "text_field" || kind == "text_area")				? "ui_input_field"
																		: "";
	if (!behavior.is_empty()) {
		Dictionary d = defaults(behavior);
		if (behavior == "ui_input_field") {
			d["multiline"] = kind == "text_area";
		}
		result[behavior] = d;
	}
	for (const char *name : names) {
		if (!result.has(name)) {
			continue;
		}
		Dictionary d = result[name];
		for (const Variant &key : d.keys()) {
			if (old.has(key)) {
				d[key] = old[key];
			}
		}
	}
	return result;
}
bool compose(const Dictionary &entity, Dictionary &out) {
	out.clear();
	if (entity.has("ui")) {
		if (entity.has("ui_layout") || entity["ui"].get_type() != Variant::DICTIONARY) {
			return false;
		}
		out = entity["ui"].duplicate(true);
		return true;
	}
	bool found = false;
	for (const char *name : names) {
		if (entity.has(name)) {
			found = true;
		}
	}
	if (!found) {
		return true;
	}
	if (!entity.has("ui_layout")) {
		return false;
	}
	out["kind"] = "panel";
	out["background"] = Color(0, 0, 0, 0);
	out["block_pointer"] = false;
	int behaviors = 0;
	for (int i = 0; i < 9; i++) {
		String name = names[i];
		if (!entity.has(name)) {
			continue;
		}
		if (entity[name].get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary d = defaults(name);
		d.merge(entity[name], true);
		if (i >= 3) {
			if (++behaviors > 1) {
				return false;
			}
			out["kind"] = i == 3 ? "button" : i == 4 ? "toggle"
					: i == 5						 ? (bool(d.get("multiline", false)) ? "text_area" : "text_field")
					: i == 6						 ? "slider"
					: i == 7						 ? "progress"
													 : "scroll";
		}
		for (const Variant &key : d.keys()) {
			if (String(key) == "arrangement" || String(key) == "multiline" || String(key) == "alignment" || String(key) == "margins" || String(key) == "pivot") {
				continue;
			}
			out[key] = d[key];
		}
	}
	Dictionary layout = entity["ui_layout"];
	Variant pivot = layout.get("pivot", Vector2(.5, .5));
	if (pivot.get_type() != Variant::VECTOR2 || !Vector2(pivot).is_finite()) { return false; }
	int alignment = layout.get("alignment", 0);
	if (alignment < 0 || alignment > 10) {
		return false;
	}
	if (alignment == 10) {
		out["anchors"] = Vector4(0, 0, 1, 1);
	} else if (alignment > 0) {
		int cell = alignment - 1;
		float x = (cell % 3) * .5f, y = (cell / 3) * .5f;
		out["anchors"] = Vector4(x, y, x, y);
	}
	Variant raw_margins = layout.get("margins", Vector4());
	if (raw_margins.get_type() != Variant::VECTOR4 || !Vector4(raw_margins).is_finite() || out["rect"].get_type() != Variant::RECT2) {
		return false;
	}
	Vector4 margins = raw_margins;
	Rect2 rect = out["rect"];
	rect.position += Vector2(margins.x, margins.y);
	rect.size -= Vector2(margins.x + margins.z, margins.y + margins.w);
	out["rect"] = rect;

	int arrangement = layout.get("arrangement", 0);
	if (arrangement < 0 || arrangement > 3 || (arrangement && behaviors)) {
		return false;
	}
	if (arrangement) {
		out["kind"] = arrangement == 1 ? "hbox" : arrangement == 2 ? "vbox"
																   : "grid";
	}
	return true;
}
bool add(Dictionary &entity, const String &component) {
	if (!is_component(component)) {
		return false;
	}
	entity = migrate(entity);
	if (entity.has(component)) {
		return false;
	}
	if (component != "ui_layout" && !entity.has("ui_layout")) {
		entity["ui_layout"] = defaults("ui_layout");
	}
	if (component != "ui_layout" && component != "ui_image" && component != "ui_text") {
		for (int i = 3; i < 9; i++) {
			if (entity.has(names[i])) {
				return false;
			}
		}
		if (int(Dictionary(entity["ui_layout"]).get("arrangement", 0)) != 0) {
			return false;
		}
	}
	entity[component] = defaults(component);
	if (component == "ui_input_field" && !entity.has("ui_text")) {
		Dictionary d = defaults("ui_text");
		d["text"] = "";
		entity["ui_text"] = d;
	}
	return true;
}
void remove(Dictionary &entity, const String &component) {
	entity = migrate(entity);
	if (component == "ui_layout") {
		for (const char *name : names) {
			entity.erase(name);
		}
	} else {
		entity.erase(component);
	}
}
Dictionary preset(const String &kind) {
	Dictionary legacy;
	Dictionary ui;
	ui["kind"] = kind;
	ui["rect"] = Rect2(20, 20, 240, 60);
	if (kind == "label" || kind == "button" || kind == "toggle") {
		ui["text"] = kind == "button" ? String(U"按钮") : kind == "toggle" ? String(U"开关")
																		   : String(U"文字");
	}
	if (kind == "label") {
		ui["background"] = Color(0, 0, 0, 0);
		ui["block_pointer"] = false;
	}
	legacy["ui"] = ui;
	Dictionary result = migrate(legacy);
	if (kind == "label") {
		result.erase("ui_image");
	}
	return result;
}
} //namespace ECSUIComponents
