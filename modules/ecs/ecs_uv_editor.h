#pragma once
#ifdef TOOLS_ENABLED
#include "scene/gui/control.h"
#include "scene/resources/texture.h"

class ECSUVEditor : public Control {
	GDCLASS(ECSUVEditor, Control);
	Dictionary polygon;
	PackedVector2Array uv, before;
	Ref<Texture2D> texture;
	int dragging = -1;
	float zoom = 1;
	Vector2 pan;
	bool panning = false;
	Rect2 texture_rect() const;
	void cancel_drag();
protected:
	static void _bind_methods();
	void _notification(int p_what);
public:
	void edit(const Dictionary &p_polygon);
	void gui_input(const Ref<InputEvent> &p_event) override;
};
#endif
