#ifdef TOOLS_ENABLED
#include "ecs_animation_graph_editor.h"
#include "ecs_animation_graph.h"
#include "core/object/callable_mp.h"
#include "editor/inspector/editor_resource_picker.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/spin_box.h"

void ECSAnimationGraphEditor::_bind_methods() {
	ADD_SIGNAL(MethodInfo("graph_applied",PropertyInfo(Variant::DICTIONARY,"graph")));
}
ECSAnimationGraphEditor::ECSAnimationGraphEditor() {
	auto *bar=memnew(HBoxContainer); add_child(bar);
	types=memnew(OptionButton); for(const String &type:{String("clip"),String("blend"),String("add"),String("time_scale"),String("output"),String("blend_space_2d"),String("state_machine")}) { types->add_item(type); } bar->add_child(types);
	auto *add=memnew(Button); add->set_text(String(U"添加节点")); bar->add_child(add); add->connect("pressed",callable_mp(this,&ECSAnimationGraphEditor::add_node));
	auto *save=memnew(Button); save->set_text(String(U"应用混合图")); bar->add_child(save); save->connect("pressed",callable_mp(this,&ECSAnimationGraphEditor::apply));
	canvas=memnew(GraphEdit); canvas->set_v_size_flags(SIZE_EXPAND_FILL); canvas->set_custom_minimum_size(Size2(360,240)); add_child(canvas);
	canvas->connect("connection_request",callable_mp(this,&ECSAnimationGraphEditor::connect_request));
	canvas->connect("disconnection_request",callable_mp(this,&ECSAnimationGraphEditor::disconnect_request));
}
void ECSAnimationGraphEditor::edit(const Dictionary &graph,const Ref<Animation> &p_clip) {
	clip=p_clip; nodes=Array(graph.get("nodes",Array())).duplicate(true); output=graph.get("output",-1); rebuild();
}
void ECSAnimationGraphEditor::add_node() {
	if(nodes.size()>=64) { return; }
	String type=types->get_item_text(types->get_selected()); Dictionary node; node["type"]=type; node["position"]=Vector2(nodes.size()*40,40);
	if(type=="clip") { node["clip"]=clip; }
	else { PackedInt32Array inputs; inputs.resize(type=="blend_space_2d"?3:type=="blend" || type=="add" || type=="state_machine"?2:1); inputs.fill(-1); node["inputs"]=inputs; if(type=="output") { output=nodes.size(); } }
	if(type=="blend_space_2d") { node["points"]=PackedVector2Array({Vector2(),Vector2(1,0),Vector2(0,1)}); node["blend_position"]=Vector2(); }
	nodes.push_back(node); rebuild();
}
void ECSAnimationGraphEditor::remove_node(int index) {
	nodes.remove_at(index); if(output==index) { output=-1; } else if(output>index) { output--; }
	for(int i=0;i<nodes.size();i++) { Dictionary node=nodes[i]; if(!node.has("inputs")) { continue; } PackedInt32Array inputs=node["inputs"]; for(int k=0;k<inputs.size();k++) { if(inputs[k]==index) { inputs.set(k,-1); } else if(inputs[k]>index) { inputs.set(k,inputs[k]-1); } } node["inputs"]=inputs; }
	rebuild();
}
void ECSAnimationGraphEditor::rebuild() {
	canvas->clear_connections();
	for(int i=canvas->get_child_count()-1;i>=0;i--) { GraphNode *node=Object::cast_to<GraphNode>(canvas->get_child(i)); if(node) { canvas->remove_child(node); node->queue_free(); } }
	for(int i=0;i<nodes.size();i++) {
		Dictionary data=nodes[i]; String type=data.get("type",""); auto *node=memnew(GraphNode); node->set_name(itos(i)); node->set_title(type); node->set_position_offset(data.get("position",Vector2(i*200,40))); canvas->add_child(node);
		node->connect("dragged",callable_mp(this,&ECSAnimationGraphEditor::moved).bind(i));
		int count=type=="state_machine"?PackedInt32Array(data.get("inputs",PackedInt32Array())).size():type=="blend_space_2d"?3:type=="blend" || type=="add" || type=="state_machine"?2:1;
		for(int k=0;k<count;k++) { auto *label=memnew(Label); label->set_text(type=="clip"?String(U"动画输出"):String(U"输入 ")+itos(k+1)); node->add_child(label); node->set_slot(k,type!="clip",0,Color(.5,.8,1),k==0 && type!="output",0,Color(.5,.8,1)); }
		if(type=="clip") { auto *picker=memnew(EditorResourcePicker); picker->set_base_type("Animation"); picker->set_edited_resource(data.get("clip",Variant())); node->add_child(picker); picker->connect("resource_changed",callable_mp(this,&ECSAnimationGraphEditor::clip_changed).bind(i)); }
		if(type=="blend" || type=="add" || type=="time_scale") { auto *value=memnew(SpinBox); bool speed=type=="time_scale"; value->set_min(speed?-100:0); value->set_max(speed?100:1); value->set_step(.01); value->set_value(data.get(speed?"speed":"weight",speed?1.:.5)); node->add_child(value); value->connect("value_changed",callable_mp(this,&ECSAnimationGraphEditor::parameter_changed).bind(i)); }
		if(type=="state_machine") {
			for(const String &field:{String("count"),String("request"),String("duration")}) {
				auto *row=memnew(HBoxContainer); node->add_child(row); auto *label=memnew(Label); label->set_text(field=="count"?String(U"状态数"):field=="request"?String(U"目标状态"):String(U"过渡秒数")); row->add_child(label);
				auto *value=memnew(SpinBox); value->set_min(field=="count"?2:0); value->set_max(field=="count"?32:field=="request"?count-1:3600); value->set_step(field=="duration"?.01:1); value->set_value(field=="count"?double(count):double(data.get(field,field=="duration"?.2:0.))); row->add_child(value); value->connect("value_changed",callable_mp(this,&ECSAnimationGraphEditor::state_changed).bind(i,field),CONNECT_DEFERRED);
			}
		}
		if(type=="blend" || type=="add") {
			PackedFloat32Array weights=data.get("track_weights",PackedFloat32Array());
			for(int t=0;clip.is_valid() && t<clip->get_track_count();t++) {
				auto *row=memnew(HBoxContainer); node->add_child(row); auto *label=memnew(Label); label->set_text(String(clip->track_get_path(t))); row->add_child(label);
				auto *value=memnew(SpinBox); value->set_max(1); value->set_step(.01); value->set_value(t<weights.size()?weights[t]:1); row->add_child(value); value->connect("value_changed",callable_mp(this,&ECSAnimationGraphEditor::mask_changed).bind(i,t));
			}
		}
		if(type=="blend_space_2d") {
			PackedVector2Array points=data.get("points",PackedVector2Array());
			for(int point=-1;point<points.size();point++) {
				auto *row=memnew(HBoxContainer); node->add_child(row); auto *label=memnew(Label); label->set_text(point<0?String(U"混合位置"):String(U"点 ")+itos(point+1)); row->add_child(label);
				Vector2 position=point<0?Vector2(data.get("blend_position",Vector2())):points[point];
				for(int axis=0;axis<2;axis++) { auto *value=memnew(SpinBox); value->set_min(-10000); value->set_max(10000); value->set_step(.01); value->set_value(position[axis]); row->add_child(value); value->connect("value_changed",callable_mp(this,&ECSAnimationGraphEditor::space_changed).bind(i,point,axis)); }
			}
		}
		auto *remove=memnew(Button); remove->set_text(String(U"删除")); node->add_child(remove); remove->connect("pressed",callable_mp(this,&ECSAnimationGraphEditor::remove_node).bind(i));
	}
	for(int i=0;i<nodes.size();i++) { Dictionary node=nodes[i]; PackedInt32Array inputs=node.get("inputs",PackedInt32Array()); for(int k=0;k<inputs.size();k++) { if(inputs[k]>=0 && inputs[k]<nodes.size()) { canvas->connect_node(itos(inputs[k]),0,itos(i),k); } } }
}
void ECSAnimationGraphEditor::connect_request(const StringName &from,int,const StringName &to,int port) {
	int source=String(from).to_int(),target=String(to).to_int(); if(source==target || source<0 || source>=nodes.size() || target<0 || target>=nodes.size()) { return; }
	Dictionary node=nodes[target]; PackedInt32Array inputs=node.get("inputs",PackedInt32Array()); if(port<0 || port>=inputs.size()) { return; } inputs.set(port,source); node["inputs"]=inputs; rebuild();
}
void ECSAnimationGraphEditor::disconnect_request(const StringName &,int,const StringName &to,int port) {
	int target=String(to).to_int(); if(target<0 || target>=nodes.size()) { return; } Dictionary node=nodes[target]; PackedInt32Array inputs=node.get("inputs",PackedInt32Array()); if(port<0 || port>=inputs.size()) { return; } inputs.set(port,-1); node["inputs"]=inputs; rebuild();
}
void ECSAnimationGraphEditor::parameter_changed(double value,int index) { Dictionary node=nodes[index]; node[String(node["type"])=="time_scale"?"speed":"weight"]=value; }
void ECSAnimationGraphEditor::state_changed(double value,int index,const String &field) {
	Dictionary node=nodes[index]; if(field=="count") { PackedInt32Array inputs=node["inputs"]; int old=inputs.size(); inputs.resize(int(value)); for(int i=old;i<inputs.size();i++) { inputs.set(i,-1); } node["inputs"]=inputs; node["state"]=0; node["request"]=0; node.erase("previous"); node.erase("elapsed"); rebuild(); }
	else { node[field]=field=="request"?Variant(int(value)):Variant(value); }
}
void ECSAnimationGraphEditor::mask_changed(double value,int index,int track) {
	Dictionary node=nodes[index]; PackedFloat32Array weights=node.get("track_weights",PackedFloat32Array()); if(weights.size()!=clip->get_track_count()) { weights.resize(clip->get_track_count()); weights.fill(1); } weights.set(track,value); node["track_weights"]=weights;
}
void ECSAnimationGraphEditor::space_changed(double value,int index,int point,int axis) {
	Dictionary node=nodes[index]; if(point<0) { Vector2 position=node["blend_position"]; position[axis]=value; node["blend_position"]=position; }
	else { PackedVector2Array points=node["points"]; Vector2 position=points[point]; position[axis]=value; points.set(point,position); node["points"]=points; }
}
void ECSAnimationGraphEditor::clip_changed(const Ref<Resource> &value,int index) { Dictionary node=nodes[index]; node["clip"]=value; }
void ECSAnimationGraphEditor::moved(const Vector2 &,const Vector2 &to,int index) { Dictionary node=nodes[index]; node["position"]=to; }
void ECSAnimationGraphEditor::apply() { Dictionary graph; graph["nodes"]=nodes.duplicate(true); graph["output"]=output; emit_signal("graph_applied",graph); }
#endif
