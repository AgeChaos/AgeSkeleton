#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"
#include "scene/gui/tree.h"

class SkeletonAttachmentTree : public Tree {
	Ref<ECSScene> scene;
	mutable int hovered_attachment = -1;

public:
	void set_scene(const Ref<ECSScene> &p_scene) { scene = p_scene; hovered_attachment = -1; }
	String get_tooltip(const Point2 &p_position) const override;
	Control *make_custom_tooltip(const String &p_text) const override;
	Control *make_attachment_preview(int p_index) const;
};
#endif
