// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_ui_draw_commands.h"

#include "core/variant/dictionary.h"
#include "scene/resources/texture.h"
#include "servers/rendering/rendering_server.h"

namespace ECSUIDrawCommands {
bool validate(const Array &commands) {
	if (commands.size() > 10000) {
		return false;
	}
	for (const Variant &raw : commands) {
		if (raw.get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary data = raw;
		if (data.get("type", Variant()).get_type() != Variant::STRING) {
			return false;
		}
		String type = data["type"];
		if (type != "line" && type != "rect" && type != "polygon" && type != "texture") {
			return false;
		}
		if (data.get("color", Variant()).get_type() != Variant::COLOR) {
			return false;
		}
		Color color = data["color"];
		if (!Math::is_finite(color.r) || !Math::is_finite(color.g) || !Math::is_finite(color.b) || !Math::is_finite(color.a)) {
			return false;
		}
		for (const Variant &key : data.keys()) {
			if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
				return false;
			}
			String field = key;
			if (field == "type" || field == "color") {
				continue;
			}
			Variant value = data[key];
			if ((field == "from" || field == "to") && type == "line") {
				if (value.get_type() != Variant::VECTOR2 || !Vector2(value).is_finite()) {
					return false;
				}
			} else if (field == "rect" && (type == "rect" || type == "texture")) {
				if (value.get_type() != Variant::RECT2 || !Rect2(value).is_finite()) {
					return false;
				}
			} else if (field == "width" && (type == "line" || type == "rect")) {
				if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) || !Math::is_finite(double(value)) || double(value) == 0) {
					return false;
				}
			} else if ((field == "antialiased" && (type == "line" || type == "rect")) || (field == "filled" && type == "rect") || ((field == "tile" || field == "transpose") && type == "texture")) {
				if (value.get_type() != Variant::BOOL) {
					return false;
				}
			} else if (field == "points" && type == "polygon") {
				if (value.get_type() != Variant::PACKED_VECTOR2_ARRAY) {
					return false;
				}
				PackedVector2Array points = value;
				if (points.size() < 3 || points.size() > 65536) {
					return false;
				}
				for (const Vector2 &point : points) {
					if (!point.is_finite()) {
						return false;
					}
				}
			} else if (field == "texture" && type == "texture") {
				if (value.get_type() != Variant::OBJECT) {
					return false;
				}
				Ref<Texture2D> texture = value;
				if (texture.is_null()) {
					return false;
				}
			} else {
				return false;
			}
		}
		if (type == "line" && (!data.has("from") || !data.has("to"))) {
			return false;
		}
		if ((type == "rect" || type == "texture") && !data.has("rect")) {
			return false;
		}
		if (type == "polygon" && !data.has("points")) {
			return false;
		}
		if (type == "texture" && !data.has("texture")) {
			return false;
		}
	}
	return true;
}
void draw(RID canvas, const Array &commands, const Color &modulate) {
	auto *rs = RenderingServer::get_singleton();
	for (const Variant &raw : commands) {
		Dictionary data = raw;
		String type = data["type"];
		Color color = Color(data["color"]) * modulate;
		real_t width = data.get("width", -1.0);
		bool antialiased = data.get("antialiased", false);
		if (type == "line") {
			rs->canvas_item_add_line(canvas, data["from"], data["to"], color, width, antialiased);
		} else if (type == "rect") {
			Rect2 rect = data["rect"];
			if (bool(data.get("filled", true))) {
				rs->canvas_item_add_rect(canvas, rect, color);
			} else {
				Vector2 points[] = { rect.position, rect.position + Vector2(rect.size.x, 0), rect.get_end(), rect.position + Vector2(0, rect.size.y), rect.position };
				for (int i = 0; i < 4; i++) {
					rs->canvas_item_add_line(canvas, points[i], points[i + 1], color, width, antialiased);
				}
			}
		} else if (type == "polygon") {
			Vector<Color> colors;
			colors.push_back(color);
			rs->canvas_item_add_polygon(canvas, data["points"], colors);
		} else if (type == "texture") {
			Ref<Texture2D> texture = data["texture"];
			texture->draw_rect(canvas, data["rect"], data.get("tile", false), color, data.get("transpose", false));
		}
	}
}
} //namespace ECSUIDrawCommands
