#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "ecs_skeleton_icons.h"
#include "scene/gui/foldable_container.h"
#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "scene/resources/image_texture.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/theme.h"

void ECSAnimationEditor::build_canvas_tools() {
	Ref<Theme> theme; theme.instantiate();
    const int base_font=MAX(8,int(EditorSettings::get_singleton()->get("interface/editor/fonts/main_font_size"))-2);
	theme->set_default_font_size(base_font*EDSCALE);
	auto style=[](Color color) { Ref<StyleBoxFlat> box; box.instantiate(); box->set_bg_color(color); box->set_corner_radius_all(3*EDSCALE); box->set_border_width_all(1); box->set_border_color(Color(.17,.17,.17)); box->set_content_margin_all(2*EDSCALE); return box; };
	theme->set_stylebox("normal","Button",style(Color(.35,.35,.35)));
	theme->set_stylebox("hover","Button",style(Color(.4,.4,.4)));
	theme->set_stylebox("pressed","Button",style(Color(.19,.51,.62)));
	auto label_style=style(Color(0,0,0,0)); label_style->set_border_width_all(0); theme->set_stylebox("normal","Label",label_style);
	theme->set_stylebox("disabled","Button",style(Color(.27,.27,.27)));
	theme->set_stylebox("panel","PanelContainer",style(Color(.27,.27,.27)));
	theme->set_stylebox("panel","Tree",style(Color(.27,.27,.27)));
	theme->set_stylebox("panel","ItemList",style(Color(.27,.27,.27)));
	theme->set_stylebox("normal","LineEdit",style(Color(.32,.32,.32)));
	theme->set_color("font_color","Button",Color(.92,.92,.92));
	theme->set_color("font_disabled_color","Button",Color(.55,.55,.55));
	theme->set_constant("separation","HBoxContainer",3*EDSCALE);
	theme->set_constant("separation","VBoxContainer",2*EDSCALE);
	for(const char *type:{"Tree","ItemList","OptionButton","PopupMenu"}) { theme->set_font_size("font_size",type,base_font*EDSCALE); }
	theme->set_constant("v_separation","Tree",0);
	theme->set_constant("inner_item_margin_top","Tree",1*EDSCALE);
	theme->set_constant("inner_item_margin_bottom","Tree",1*EDSCALE);
	theme->set_constant("v_separation","ItemList",2*EDSCALE);
	theme->set_stylebox("selected","Tree",style(Color(.43,.53,.54)));
	theme->set_stylebox("selected_focus","Tree",style(Color(.43,.53,.54)));
	theme->set_stylebox("selected","ItemList",style(Color(.43,.53,.54)));
	theme->set_stylebox("selected_focus","ItemList",style(Color(.43,.53,.54)));
	theme->set_stylebox("panel","Tree",style(Color(.34,.34,.34)));
	theme->set_stylebox("panel","ItemList",style(Color(.34,.34,.34)));
	for(const char *type:{"OptionButton","MenuButton"}) {
		for(const char *state:{"normal","hover","pressed","disabled"}) { theme->set_stylebox(state,type,theme->get_stylebox(state,"Button")); }
	}
	theme->set_type_variation("RigPanel", "PanelContainer");
	auto panel=style(Color(.18,.18,.18)); panel->set_content_margin_all(3*EDSCALE); theme->set_stylebox("panel","RigPanel",panel);
	auto list_panel=style(Color(.34,.34,.34)); list_panel->set_border_width_all(0); list_panel->set_content_margin_all(3*EDSCALE);
	for(const char *type:{"Tree","ItemList"}) { theme->set_stylebox("panel",type,list_panel); }
	theme->set_type_variation("RigToolbar", "PanelContainer");
	auto toolbar=style(Color(.30,.30,.30)); toolbar->set_content_margin_all(4*EDSCALE); toolbar->set_border_width_all(0);
	theme->set_stylebox("panel","RigToolbar",toolbar);
	theme->set_type_variation("RigToolbarDark", "PanelContainer");
	auto dark_toolbar=style(Color(.18,.18,.18)); dark_toolbar->set_content_margin_all(2*EDSCALE); dark_toolbar->set_border_width_all(0);
	theme->set_stylebox("panel","RigToolbarDark",dark_toolbar);
	theme->set_type_variation("RigHeader", "Label");
	auto header=style(Color(.18,.18,.18)); header->set_content_margin_all(4*EDSCALE); theme->set_stylebox("normal","RigHeader",header);
	theme->set_color("font_color","RigHeader",Color(.87,.89,.89));
	theme->set_type_variation("RigPropertyHeader", "Label");
	theme->set_stylebox("normal","RigPropertyHeader",header);
	theme->set_color("font_color","RigPropertyHeader",Color(.52,.82,.83));
	for(const char *state:{"title_panel","title_collapsed_panel","title_hover_panel","title_collapsed_hover_panel"}) {
		auto fold_title=style(Color(.20,.20,.20)); fold_title->set_content_margin_all(4*EDSCALE); fold_title->set_border_width_all(0); theme->set_stylebox(state,"FoldableContainer",fold_title);
	}
	auto fold_panel=style(Color(.18,.18,.18)); fold_panel->set_border_width_all(0); fold_panel->set_content_margin_all(3*EDSCALE); theme->set_stylebox("panel","FoldableContainer",fold_panel);
	theme->set_font_size("font_size","FoldableContainer",base_font*EDSCALE);
	for(const char *state:{"font_color","hover_font_color","collapsed_font_color"}) { theme->set_color(state,"FoldableContainer",Color(.72,.81,.81)); }
	theme->set_stylebox("hover_pressed","Button",style(Color(.24,.55,.64)));
	// A focused field must not inherit the game's saturated blue editor frame.
	auto focus=style(Color(0,0,0,0)); focus->set_border_color(Color(.55,.68,.68)); focus->set_content_margin_all(0);
	for(const char *type:{"Button","LineEdit","SpinBoxInnerLineEdit","Tree","ItemList","OptionButton","MenuButton","FoldableContainer"}) { theme->set_stylebox("focus",type,focus); }
	theme->set_font_size("font_size","Tree",base_font*EDSCALE);
	theme->set_constant("separation","HSplitContainer",4*EDSCALE);
	theme->set_constant("separation","VSplitContainer",4*EDSCALE);
	theme->set_font_size("font_size","Button",base_font*EDSCALE); theme->set_font_size("font_size","Label",base_font*EDSCALE); theme->set_font_size("font_size","LineEdit",base_font*EDSCALE);
	Ref<Image> empty_image=Image::create_empty(1,1,false,Image::FORMAT_RGBA8); empty_image->fill(Color(0,0,0,0)); Ref<Texture2D> empty_icon=ImageTexture::create_from_image(empty_image);
	for(const char *name:{"updown","up","up_hover","up_pressed","up_disabled","down","down_hover","down_pressed","down_disabled"}) { theme->set_icon(name,"SpinBox",empty_icon); }
	theme->set_font_size("font_size","SpinBoxInnerLineEdit",base_font*EDSCALE); theme->set_stylebox("normal","SpinBoxInnerLineEdit",style(Color(.32,.32,.32)));
	theme->set_constant("buttons_width","SpinBox",0); theme->set_constant("field_and_buttons_separation","SpinBox",0); theme->set_constant("set_min_buttons_width_from_icons","SpinBox",0);
	for(const char *state:{"normal","hover","pressed","disabled","hover_pressed"}) { auto check=style(Color(0,0,0,0)); check->set_border_width_all(0); check->set_content_margin_all(1*EDSCALE); theme->set_stylebox(state,"CheckBox",check); }
    theme->set_font_size("font_size","CheckBox",base_font*EDSCALE);
    set_theme(theme);

	auto tool=[&](const String &name,const String &icon,int command) {
		auto *button=memnew(Button); button->set_name(name.replace(" ","")); button->set_tooltip_text(TTR(name));
		button->set_button_icon(skeleton_workspace_icon(icon,24)); button->set_expand_icon(false);
		button->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER); button->set_vertical_icon_alignment(VERTICAL_ALIGNMENT_CENTER);
		button->set_texture_filter(CanvasItem::TEXTURE_FILTER_LINEAR);
		button->set_custom_minimum_size(Size2(44,44)*EDSCALE); button->set_toggle_mode(true);
		button->connect("pressed",callable_mp(this,&ECSAnimationEditor::canvas_tool_action).bind(command)); canvas_tool_host->add_child(button); return button;
	};
	canvas_tools[0]=tool("Select","select",7);
	transform_tools[1]=tool("Move","position",1); transform_tools[0]=tool("Rotate","rotation",2); transform_tools[2]=tool("Scale","scale",3);
	canvas_tools[2]=tool("Create Bone","bone",12); canvas_tools[1]=tool("Paint Weights","brush",11);
	auto *spacer=memnew(Control); spacer->set_v_size_flags(SIZE_EXPAND_FILL); canvas_tool_host->add_child(spacer);
	canvas_tools[3]=tool("Pan","pan",0);

	const char *names[]={"Rotation","Position","Scale","Shear"};
	for(int row=0;row<4;row++) {
		auto *line=memnew(HBoxContainer); transform_property_host->add_child(line);
		auto *label=memnew(Label); label->set_text(TTR(names[row])); label->set_custom_minimum_size(Size2(64,0)*EDSCALE); line->add_child(label);
		int count=row==0?1:2;
		for(int axis=0;axis<count;axis++) {
			auto *value=memnew(SpinBox); value->set_min(-100000); value->set_max(100000); value->set_step(.01);
			value->set_custom_minimum_size(Size2(64,30)*EDSCALE); value->set_h_size_flags(SIZE_EXPAND_FILL); value->get_line_edit()->set_expand_to_text_length_enabled(false);
			value->set_tooltip_text(row==0?TTR("Rotation in degrees"):axis==0?"X":"Y"); line->add_child(value);
			int index=row==0?0:row==1?1+axis:row==2?3+axis:5+axis; canvas_values[index]=value;
			value->set_name("Value"+itos(index)); value->connect("value_changed",callable_mp(this,&ECSAnimationEditor::canvas_value_changed).bind(row,axis));
		}
		auto *key=memnew(Button); key->set_name("InsertKey"+itos(row)); key->set_button_icon(skeleton_workspace_icon("keyframe"));
		key->set_tooltip_text(TTR("Insert a keyframe for this transform")); key->set_custom_minimum_size(Size2(30,30)*EDSCALE);
		key->connect("pressed",callable_mp(this,&ECSAnimationEditor::canvas_tool_action).bind(20+row)); animation_buttons.push_back(key); line->add_child(key);
	}
	auto *options=memnew(FoldableContainer); options->set_title(TTR("Canvas Options")); options->set_folded(true); transform_property_host->add_child(options);
	auto *settings=memnew(VBoxContainer); options->add_child(settings);
	auto *axes=memnew(OptionButton); axes->set_tooltip_text(TTR("Transform axes"));
	for(const char *name:{"Local","Parent","World"}) { axes->add_item(TTR(name)); } settings->add_child(axes);
	axes->connect("item_selected",callable_mp(local_canvas,&ECSUICanvasEditor::set_axis_space));
	for(int i=0;i<2;i++) {
		auto *check=memnew(CheckBox); compensation_tools[i]=check;
		check->set_text(i==0?TTR("Preserve child bone poses"):TTR("Preserve image positions")); settings->add_child(check);
	}
	local_canvas->set_setup_pose_callback(callable_mp(this,&ECSAnimationEditor::compensated_pose));
	const char *labels[]={"Show Bones","Show Images","Select Bones","Select Images"};
	for(int i=0;i<4;i++) { auto *check=memnew(CheckBox); check->set_text(TTR(labels[i])); check->set_pressed(true); settings->add_child(check); check->connect("toggled",callable_mp(local_canvas,&ECSUICanvasEditor::set_authoring_option).bind(i)); }
	canvas_tool_action(1); refresh_canvas_values();
}
void ECSAnimationEditor::canvas_tool_action(int tool) {
	if(tool==13) { canvas_values[5]->get_line_edit()->grab_focus(); return; }
	if(tool==11) { for(Button *button:transform_tools) { button->set_pressed_no_signal(false); } canvas_tools[3]->set_pressed_no_signal(false); image_action(3); return; }
	if(tool==12) {
		if(animation_mode) { return; }
		for(Button *button:transform_tools) { button->set_pressed_no_signal(false); } canvas_tools[3]->set_pressed_no_signal(false);
		brush_enabled->set_pressed(false); mesh_edit->set_pressed(false); canvas_tools[0]->set_pressed_no_signal(false); canvas_tools[1]->set_pressed_no_signal(false); canvas_tools[2]->set_pressed_no_signal(true);
		local_canvas->set_authoring_tool(6); feedback->set_text(String(U"在画布拖动创建子骨骼；松开确认，Esc 或右键取消。")); return;
	}
	if(tool>=20) { int field=tool-20; property->select(field==0?1:field==1?0:field==2?2:3); if(field==0) { x->set_value(0); y->set_value(0); z->set_value(Math::deg_to_rad(canvas_values[0]->get_value())); } else { int start=field==1?1:field==2?3:5; x->set_value(field==3?Math::deg_to_rad(canvas_values[start]->get_value()):canvas_values[start]->get_value()); y->set_value(field==3?Math::deg_to_rad(canvas_values[start+1]->get_value()):canvas_values[start+1]->get_value()); z->set_value(field==2?1:0); } action(2); return; }
	if(tool==10) { tool=active_canvas_tool; }
	active_canvas_tool=tool; for(int i=0;i<3;i++) { transform_tools[i]->set_pressed_no_signal(tool==(i==0?2:i==1?1:3)); } canvas_tools[0]->set_pressed_no_signal(tool==7); canvas_tools[3]->set_pressed_no_signal(tool==0); canvas_tools[1]->set_pressed_no_signal(false); canvas_tools[2]->set_pressed_no_signal(false); brush_enabled->set_pressed(false); mesh_edit->set_pressed(false); local_canvas->set_authoring_tool(tool);
}
void ECSAnimationEditor::refresh_canvas_values() {
	if(!canvas_values[0] || scene.is_null()) { return; } int entity=target->get_selected_id(); if(entity<0 || entity>=scene->get_entities().size()) { return; }
	Dictionary e=scene->get_entities()[entity]; Vector3 position=e.get("position",Vector3()),rotation=e.get("rotation",Vector3()),scale=e.get("scale",Vector3(1,1,1)),shear=e.get("shear",Vector3());
	if(animation_mode) { position=local_canvas->get_authoring_vector(entity,"position"); rotation=local_canvas->get_authoring_vector(entity,"rotation"); scale=local_canvas->get_authoring_vector(entity,"scale"); shear=local_canvas->get_authoring_vector(entity,"shear"); }
	refreshing_canvas=true; double values[]={Math::rad_to_deg(rotation.z),position.x,position.y,scale.x,scale.y,Math::rad_to_deg(shear.x),Math::rad_to_deg(shear.y)}; for(int i=0;i<7;i++) { canvas_values[i]->set_value_no_signal(values[i]); } refreshing_canvas=false;
}
void ECSAnimationEditor::canvas_value_changed(double,int field,int) {
	if(refreshing_canvas || refreshing || scene.is_null()) { return; }
	property->select(field==0?1:field==1?0:field==2?2:3);
	if(field==0) { x->set_value(0); y->set_value(0); z->set_value(Math::deg_to_rad(canvas_values[0]->get_value())); }
	else { int start=field==1?1:field==2?3:5; x->set_value(field==3?Math::deg_to_rad(canvas_values[start]->get_value()):canvas_values[start]->get_value()); y->set_value(field==3?Math::deg_to_rad(canvas_values[start+1]->get_value()):canvas_values[start+1]->get_value()); z->set_value(field==2?1:0); }
	if(animation_mode && !auto_key->is_pressed()) { local_canvas->preview_authoring_vector(target->get_selected_id(),property->get_item_text(property->get_selected()),Vector3(x->get_value(),y->get_value(),z->get_value())); feedback->set_text(TTR("Auto key is off. Use the diamond button to record this transform.")); return; }
	action(14); refresh_canvas_values();
}
void ECSAnimationEditor::refresh_selection_properties() {
    if(!selection_name || scene.is_null()) { return; }
    int index=target->get_selected_id(); if(index<0 || index>=scene->get_entities().size()) { return; }
    Dictionary entity=scene->get_entities()[index]; bool was_refreshing=refreshing; refreshing=true;
    if(!selection_name->has_focus()) { selection_name->set_text(entity.get("name",String())); }
    selection_name->set_editable(!animation_mode); bone_properties->set_visible(entity.has("bone_2d"));
    selection_length->set_editable(!animation_mode);
    selection_length->set_value_no_signal(double(Dictionary(entity.get("bone_2d",Dictionary())).get("length",100.0)));
    selection_title->set_text((entity.has("bone_2d")?String(U"骨骼："):entity.has("polygon_2d")?String(U"图片："):String(U"骨架："))+String(entity.get("name","")));
    refreshing=was_refreshing;
}
void ECSAnimationEditor::selection_property_changed() {
    if(refreshing || animation_mode || scene.is_null()) { return; }
    int index=target->get_selected_id(); if(index<0 || index>=scene->get_entities().size()) { return; }
    String name=selection_name->get_text().strip_edges(); if(name.is_empty()) { refresh_selection_properties(); return; }
    Array entities=scene->get_entities().duplicate(true); Dictionary entity=entities[index];
    bool changed=String(entity.get("name",""))!=name; entity["name"]=name;
    if(entity.has("bone_2d")) { Dictionary bone=entity["bone_2d"]; changed |= double(bone.get("length",100.0))!=selection_length->get_value(); bone["length"]=selection_length->get_value(); }
    if(changed) { commit_entities(entities,String(U"修改骨骼属性")); }
}
void ECSAnimationEditor::frame_changed(double value) { seek(value/timeline_fps); }
void ECSAnimationEditor::transport(int command) {
	Ref<Animation> clip=current_clip(); if(clip.is_null() || !animation_mode) { return; }
	double start=loop_start->get_value()/timeline_fps,end=MIN(clip->get_length(),loop_end->get_value()/timeline_fps); if(end<=start) { start=0; end=clip->get_length(); }
	if(command==0) { seek(start); } else if(command==1) { seek(MAX(start,time->get_value()-1.0/timeline_fps)); } else if(command==4) { seek(MIN(end,time->get_value()+1.0/timeline_fps)); } else if(command==5) { seek(end); } else if(command==6) { playing=false; } else { playback_direction=command==2?-1:1; if(time->get_value()<start || time->get_value()>end || (playback_direction>0 && time->get_value()>=end) || (playback_direction<0 && time->get_value()<=start)) { seek(playback_direction>0?start:end); } playing=true; playback_time=time->get_value(); }
}

#endif

#ifdef TOOLS_ENABLED
void ECSAnimationEditor::create_bone_from_drag(const Vector2 &start,const Vector2 &end) {
	if(animation_mode || scene.is_null()) { return; }
	int parent=target->get_selected_id(),rig=find_rig(parent); if(rig<0) { feedback->set_text(String(U"请先选择骨架或父骨骼。")); return; }
	Array after=scene->get_entities().duplicate(true); Dictionary parent_entity=after[parent]; if(!parent_entity.has("bone_2d") && parent!=rig) { parent=rig; }
	Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { return; } auto ids=world->query(PackedStringArray(),true); Transform3D transform=world->get_global_transform(ids[parent]);
	if(Math::abs(transform.basis.determinant())<.000001) { feedback->set_text(String(U"父骨骼缩放为零，无法创建。")); return; }
	Transform3D inverse=transform.affine_inverse(); Vector3 a=inverse.xform(Vector3(start.x,start.y,0)),b=inverse.xform(Vector3(end.x,end.y,0)); Vector2 delta(b.x-a.x,b.y-a.y);
	if(delta.length()<.00001) { return; }
	Dictionary bone,definition; definition["length"]=delta.length(); bone["name"]="Bone "+itos(after.size()); bone["parent"]=parent; bone["position"]=a; bone["rotation"]=Vector3(0,0,delta.angle()); bone["bone_2d"]=definition;
	Dictionary root=after[rig],skeleton=root["skeleton_2d"]; PackedInt64Array bones=skeleton["bones"]; int index=after.size(); bones.push_back(index); skeleton["bones"]=bones; skeleton["bind_poses"]=Array(); after.push_back(bone);
	commit_entities(after,String(U"拖动创建骨骼")); select_target(index);
}
#endif
