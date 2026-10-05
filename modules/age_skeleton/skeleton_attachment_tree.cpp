#ifdef TOOLS_ENABLED
#include "skeleton_attachment_tree.h"
#include "core/math/geometry_2d.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/style_box_flat.h"
#include "servers/rendering/rendering_server.h"

namespace {
// Render mesh UVs, including rotated atlas regions, instead of showing the atlas page.
class AttachmentSwatch : public Control {
	GDCLASS(AttachmentSwatch, Control);

	Ref<Texture2D> texture;
	PackedVector2Array points, uv;
	PackedInt32Array triangles;
	Rect2 bounds;
	Color tint;

protected:
	void _notification(int p_what) {
		if (p_what != NOTIFICATION_DRAW) { return; }
		const float cell = 12 * EDSCALE;
		for (int y = 0; y * cell < get_size().y; ++y) {
			for (int x = 0; x * cell < get_size().x; ++x) {
				draw_rect(Rect2(Vector2(x, y) * cell, Vector2(cell, cell)), (x + y) % 2 ? Color(.26,.26,.28) : Color(.34,.34,.36));
			}
		}
		if (texture.is_null() || triangles.is_empty()) { return; }
		Vector2 available = get_size() - Vector2(20,20) * EDSCALE;
		float scale = MIN(available.x / MAX(1.0f,bounds.size.x), available.y / MAX(1.0f,bounds.size.y));
		PackedVector2Array vertices = points;
		for (int i = 0; i < vertices.size(); ++i) { vertices.set(i, (vertices[i] - bounds.get_center()) * scale + get_size() * .5f); }
		RenderingServer::get_singleton()->canvas_item_add_triangle_array(get_canvas_item(), triangles, vertices, PackedColorArray({tint}), uv, PackedInt32Array(), PackedFloat32Array(), texture->get_rid());
	}

public:
	AttachmentSwatch(const Dictionary &p_mesh) {
		set_mouse_filter(MOUSE_FILTER_IGNORE);
		set_clip_contents(true);
		set_texture_filter(TEXTURE_FILTER_LINEAR);
		set_custom_minimum_size(Size2(240,200) * EDSCALE);
		texture = p_mesh.get("texture", Variant());
		// AtlasTexture owns a region/margin; its RID alone refers to the whole page.
		if (Ref<AtlasTexture>(texture).is_valid()) {
			Ref<Image> image = texture->get_image();
			if (image.is_valid() && !image->is_empty()) { texture = ImageTexture::create_from_image(image); }
			else { texture.unref(); }
		}
		points = p_mesh.get("polygon", PackedVector2Array());
		uv = p_mesh.get("uv", PackedVector2Array());
		triangles = p_mesh.get("triangles", PackedInt32Array());
		tint = p_mesh.get("color", Color(1,1,1));
		if (!points.is_empty()) { bounds = Rect2(points[0], Vector2()); for (const Vector2 &p : points) { bounds.expand_to(p); } }
		if (triangles.is_empty()) { triangles = Geometry2D::triangulate_polygon(points); }
		for (int index : triangles) { if (index < 0 || index >= points.size()) { triangles.clear(); break; } }
		if (uv.size() != points.size()) { triangles.clear(); }
	}
};
}

String SkeletonAttachmentTree::get_tooltip(const Point2 &p_position) const {
	hovered_attachment = -1;
	TreeItem *item = get_item_at_position(p_position);
	if (item && get_column_at_position(p_position) == 0 && item->get_metadata(0).get_type() == Variant::INT && scene.is_valid()) {
		int index = item->get_metadata(0);
		Array entities = scene->get_entities();
		if (index >= 0 && index < entities.size() && Dictionary(entities[index]).has("polygon_2d")) {
			hovered_attachment = index;
			return item->get_tooltip_text(0) + "\n" + TTR("Double-click to frame attachment");
		}
	}
	return Tree::get_tooltip(p_position);
}

Control *SkeletonAttachmentTree::make_custom_tooltip(const String &p_text) const {
	Control *preview = make_attachment_preview(hovered_attachment);
	return preview ? preview : Tree::make_custom_tooltip(p_text);
}

Control *SkeletonAttachmentTree::make_attachment_preview(int p_index) const {
	if (scene.is_null() || p_index < 0 || p_index >= scene->get_entities().size()) { return nullptr; }
	Dictionary entity = scene->get_entities()[p_index];
	if (!entity.has("polygon_2d")) { return nullptr; }
	Dictionary mesh = entity["polygon_2d"];
	auto *panel = memnew(PanelContainer);
	panel->set_name("AttachmentPreview");
	panel->set_mouse_filter(MOUSE_FILTER_IGNORE);
	Ref<StyleBoxFlat> frame;
	frame.instantiate();
	frame->set_bg_color(get_theme_color(SNAME("dark_color_1"), SNAME("Editor")));
	frame->set_border_color(get_theme_color(SNAME("font_color"), SNAME("Editor")).darkened(0.55));
	frame->set_border_width_all(MAX(1, int(EDSCALE)));
	frame->set_corner_radius_all(4 * EDSCALE);
	for (int side = 0; side < 4; ++side) {
		frame->set_content_margin(Side(side), 8 * EDSCALE);
	}
	panel->add_theme_style_override(SNAME("panel"), frame);
	auto *box = memnew(VBoxContainer);
	box->set_mouse_filter(MOUSE_FILTER_IGNORE);
	box->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
	panel->add_child(box);
	auto *name = memnew(Label);
	name->set_text(entity.get("name", String()));
	name->set_custom_minimum_size(Size2(240,0) * EDSCALE);
	name->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	box->add_child(name);
	Ref<Texture2D> texture = mesh.get("texture", Variant());
	if (texture.is_valid()) { box->add_child(memnew(AttachmentSwatch(mesh))); }
	else { auto *missing = memnew(Label); missing->set_text(TTR("No texture assigned")); box->add_child(missing); }
	auto *hint = memnew(Label);
	hint->set_text(TTR("Double-click to frame attachment"));
	hint->add_theme_color_override(SNAME("font_color"), get_theme_color(SNAME("disabled_font_color"), SNAME("Editor")));
	box->add_child(hint);
	return panel;
}
#endif
