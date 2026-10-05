// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "core/math/rect2.h"
#include "core/math/vector4.h"
#include "scene/resources/font.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// POD columns belong to the same sparse-set World as gameplay components.
struct ECSUILayout {
	Rect2 rect = Rect2(0, 0, 180, 40);
	Vector4 anchors;
	int32_t z_index = 0;
	Vector2 padding, scroll, content_size;
	float gap = 8;
	int32_t columns = 2;
};
struct ECSUIStyle {
	Color background = Color(0.12, 0.16, 0.22, 1);
	Color foreground = Color(1, 1, 1, 1);
	Color modulate = Color(1, 1, 1, 1);
	Color self_modulate = Color(1, 1, 1, 1);
	int32_t image_stretch = 0;
	bool flip_h = false, flip_v = false;
	int32_t font_size = 20;
	int32_t outline_size = 0;
	bool visible = true;
	bool clip = true;
};
struct ECSUIInput {
	enum Kind { PANEL,
		LABEL,
		BUTTON,
		TOGGLE,
		PROGRESS,
		IMAGE,
		SLIDER,
		TEXT_FIELD,
		TEXT_AREA,
		SCROLL,
		HBOX,
		VBOX,
		GRID };
	int32_t kind = PANEL;
	bool disabled = false;
	bool block_pointer = true;
	float value = 0;
	int32_t max_length = 4096;
};
// Reference-owning content stays outside POD columns.
struct ECSUIText {
	Array draw_commands;
	Dictionary theme_styles;
	Dictionary theme_colors;
	Dictionary theme_icons;
	Dictionary theme_constants;
	String text;
	String placeholder;
	Ref<Font> font;
	Ref<Texture2D> texture;
	Ref<Material> material;
};
struct ECSUIItem {
	uint64_t entity = 0;
	ECSUILayout layout;
	ECSUIStyle style;
	ECSUIInput input;
	ECSUIText content;
};
