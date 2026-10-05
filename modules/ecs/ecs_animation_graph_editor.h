#pragma once
#ifdef TOOLS_ENABLED
#include "scene/gui/box_container.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/option_button.h"
#include "scene/resources/animation.h"

class ECSAnimationGraphEditor : public VBoxContainer {
	GDCLASS(ECSAnimationGraphEditor, VBoxContainer);
	GraphEdit *canvas = nullptr;
	OptionButton *types = nullptr;
	Array nodes;
	Ref<Animation> clip;
	int output = -1;
	void rebuild();
	void add_node();
	void remove_node(int p_index);
	void connect_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port);
	void disconnect_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port);
	void parameter_changed(double p_value, int p_index);
	void state_changed(double p_value,int p_index,const String &p_field);
	void mask_changed(double p_value,int p_index,int p_track);
	void space_changed(double p_value,int p_index,int p_point,int p_axis);
	void clip_changed(const Ref<Resource> &p_clip, int p_index);
	void moved(const Vector2 &p_from, const Vector2 &p_to, int p_index);
	void apply();
protected:
	static void _bind_methods();
public:
	ECSAnimationGraphEditor();
	void edit(const Dictionary &p_graph, const Ref<Animation> &p_clip);
};
#endif
