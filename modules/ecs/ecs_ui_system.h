// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "ecs_world.h"

#include "core/input/input_event.h"
#include "scene/resources/text_paragraph.h"

// A World system and server bridge, not a second entity tree or a Control hierarchy.
class ECSUISystem {
	struct LayoutItem {
		ECSUIItem data;
		Rect2 rect, clip;
		Color inherited_modulate = Color(1, 1, 1, 1);
		bool visible = true, disabled = false;
		int parent = -1, depth = 0;
	};
	struct CanvasItem {
		RID clip, content;
	};
	Ref<ECSWorld> world;
	RID canvas;
	HashMap<uint64_t, CanvasItem> canvas_items;
	Vector<LayoutItem> items;
	HashMap<uint64_t, int> lookup;
	Vector<int> draw_order;
	Size2 viewport_size;
	uint64_t revision = 0, hovered = 0, pressed = 0, focused = 0;
	struct TouchCapture {
		uint64_t pressed = 0, scroll = 0;
		Vector2 start;
		bool scrolling = false;
	};
	HashMap<int, TouchCapture> touches;
	bool input_touch(const Ref<InputEvent> &p_event);
	bool is_pressed(uint64_t p_entity) const;
	bool keyboard_pressed = false;
	struct TextState {
		int caret = 0, anchor = 0;
		Vector2 scroll;
		Ref<TextParagraph> shaped;
		String shaped_text;
		Ref<Font> font;
		int font_size = 0;
		float width = 0;
	};
	HashMap<uint64_t, TextState> text_states;
	String composition;
	Ref<TextParagraph> shape_text(const LayoutItem &p_item, const String &p_text);
	void draw_editable(const LayoutItem &p_item, RID p_canvas, Color p_color);
	void place_caret(uint64_t p_entity, const Vector2 &p_position, bool p_extend);
	bool text_input_key(const Ref<InputEventKey> &p_key);
	bool is_text_field(uint64_t p_entity) const;
	bool scroll_at(uint64_t p_entity, double p_amount);
	void sync_layout(const Size2 &p_size);
	uint64_t hit_test(const Vector2 &p_position) const;
	bool interactive(uint64_t p_entity) const;
	void set_hover(uint64_t p_entity);
	void set_focus(uint64_t p_entity);
	void activate(uint64_t p_entity);
	void set_slider(uint64_t p_entity, float p_value);

public:
	uint64_t editor_pick(const Vector2 &p_position, const Size2 &p_size);
	Rect2 editor_rect(uint64_t p_entity, const Size2 &p_size);
	void attach(const Ref<ECSWorld> &p_world, RID p_viewport);
	void draw(const Size2 &p_size);
	bool input(const Ref<InputEvent> &p_event, const Size2 &p_size);
	void input_text(const String &p_text);
	void update_ime();
	void cancel_input();
	void clear();
	~ECSUISystem() { clear(); }
};
