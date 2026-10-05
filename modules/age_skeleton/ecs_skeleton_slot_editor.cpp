#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "ecs_skeleton_icons.h"
#include "core/object/callable_mp.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/color_picker.h"

namespace {
int slot_bone_entity(const Array &entities, int rig, const Dictionary &slot, const Vector<int> &attachments) {
	Dictionary definition=Dictionary(entities[rig])["skeleton_2d"];
	PackedInt64Array bones=definition.get("bones",PackedInt64Array());
	int declared=slot.get("bone",-1);
	if(declared>=0 && declared<bones.size()) { return bones[declared]; }
	// Older files did not save slot ownership. A rigid attachment's parent or the
	// nearest common ancestor of weighted influences gives a stable legacy fallback.
	Vector<int> influences;
	auto add=[&](int bone) { if(bone>=0 && bone<entities.size() && bones.has(bone) && !influences.has(bone)) { influences.push_back(bone); } };
	for(int id:attachments) {
		Dictionary entity=entities[id],mesh=entity.get("polygon_2d",Dictionary());
		if(int(mesh.get("skeleton",-1))==rig) {
			PackedInt32Array indices=mesh.get("bones",PackedInt32Array());
			PackedFloat32Array weights=mesh.get("weights",PackedFloat32Array());
			for(int i=0;i<MIN(indices.size(),weights.size());i++) { if(weights[i]>0 && indices[i]>=0 && indices[i]<bones.size()) { add(bones[indices[i]]); } }
		} else {
			int parent=entity.get("parent",-1);
			for(int step=0;parent>=0 && parent<entities.size() && step<entities.size();step++) {
				if(bones.has(parent)) { add(parent); break; }
				parent=Dictionary(entities[parent]).get("parent",-1);
			}
		}
	}
	if(influences.is_empty()) { return rig; }
	int candidate=influences[0];
	for(int steps=0;candidate>=0 && candidate<entities.size() && steps<entities.size();steps++) {
		bool common=true;
		for(int bone:influences) {
			int ancestor=bone;
			for(int depth=0;ancestor>=0 && ancestor<entities.size() && ancestor!=candidate && depth<entities.size();depth++) { ancestor=Dictionary(entities[ancestor]).get("parent",-1); }
			if(ancestor!=candidate) { common=false; break; }
		}
		if(common) { return candidate==rig || bones.has(candidate) ? candidate : rig; }
		candidate=Dictionary(entities[candidate]).get("parent",-1);
	}
	return rig;
}
}

void ECSAnimationEditor::build_slot_hierarchy(const Array &entities, const Vector<TreeItem *> &rows) {
	for (int rig = 0; rig < entities.size(); ++rig) {
		Dictionary definition = Dictionary(entities[rig]).get("skeleton_2d", Dictionary());
		Array slots = definition.get("slots", Array());
		Dictionary skins = definition.get("skins", Dictionary());
        if(!definition.is_empty()) {
            auto *group=hierarchy->create_item(rows[rig]); group->set_text(0,String(U"皮肤")); group->set_icon(0,skeleton_workspace_icon("skin")); Dictionary group_meta; group_meta["rig"]=rig; group_meta["skin_group"]=true; group->set_metadata(0,group_meta);
            for(const Variant &skin_name:skins.keys()) {
                auto *skin_row=hierarchy->create_item(group); skin_row->set_text(0,skin_name); skin_row->set_icon(0,skeleton_workspace_icon("skin")); Dictionary meta; meta["rig"]=rig; meta["skin"]=skin_name; skin_row->set_metadata(0,meta);
                Dictionary named_slots=skins[skin_name];
                for(const Variant &slot_name:named_slots.keys()) { Dictionary bindings=named_slots[slot_name]; for(const Variant &name:bindings.keys()) {
                    auto *binding=hierarchy->create_item(skin_row); binding->set_text(0,String(slot_name)+" / "+String(name)); binding->set_icon(0,skeleton_workspace_icon("placeholder")); Dictionary m=meta.duplicate();m["slot"]=slot_name;m["placeholder"]=name;binding->set_metadata(0,m);
                } }
            }

        }
		HashSet<int> grouped;
		for (const Variant &entry : slots) {
			String name = Dictionary(entry).get("name", String());
			Vector<int> attachments;
			for (const Variant &skin_name : skins.keys()) {
				Dictionary named = Dictionary(skins[skin_name]).get(name, Dictionary());
				for (const Variant &attachment : named.keys()) {
					int id = named[attachment];
					if (id >= 0 && id < rows.size() && !grouped.has(id) && Dictionary(entities[id]).has("polygon_2d")) {
						attachments.push_back(id);
						grouped.insert(id);
					}
				}
			}
			int bone=slot_bone_entity(entities,rig,entry,attachments);
			TreeItem *parent = rows[bone>=0 && bone<rows.size() ? bone : rig];
			auto *slot = hierarchy->create_item(parent);
			slot->set_text(0, name);
			slot->set_icon(0, skeleton_workspace_icon("slot"));
			Dictionary metadata;
			metadata["rig"] = rig;
			metadata["slot"] = name;
			slot->set_metadata(0, metadata);
			slot->set_tooltip_text(0, String(U"插槽：") + name);
            Dictionary declared=definition.get("placeholders",Dictionary()); PackedStringArray placeholder_names=declared.get(name,PackedStringArray());
            for(const Variant &skin_name:skins.keys()) { Dictionary bindings=Dictionary(skins[skin_name]).get(name,Dictionary());for(const Variant &key:bindings.keys()) { if(!placeholder_names.has(key)) { placeholder_names.push_back(key); } } }
            for(const String &placeholder:placeholder_names) { auto *item=hierarchy->create_item(slot);item->set_text(0,placeholder);item->set_icon(0,skeleton_workspace_icon("placeholder"));Dictionary m=metadata.duplicate();m["placeholder"]=placeholder;item->set_metadata(0,m); }

			for (int id : attachments) {
				rows[id]->get_parent()->remove_child(rows[id]);
				slot->add_child(rows[id]);
			}
		}
	}
}

bool ECSAnimationEditor::run_slot_hierarchy_self_test() {
	Ref<ECSScene> saved=scene,test; test.instantiate();
	Array entities; Dictionary root,first,second,rig,skins,skin;
	root["name"]="Rig"; first["name"]="Base"; first["parent"]=0; first["bone_2d"]=Dictionary();
	second["name"]="Tip"; second["parent"]=1; second["position"]=Vector3(50,0,0); second["bone_2d"]=Dictionary();
	entities.push_back(root); entities.push_back(first); entities.push_back(second);
	Array slots;
	for(int i=0;i<4;i++) {
		String name=i==0?"rigid":i==1?"legacy":i==2?"explicit":"empty";
		Dictionary slot; slot["name"]=name; slot["attachment"]=i==3?String():String("image");
		if(i>=2) { slot["bone"]=1; } slots.push_back(slot);
		if(i==3) { continue; }
		Dictionary image,mesh,attachment; image["name"]=name; image["parent"]=i==0?1:0;
		mesh["polygon"]=PackedVector2Array({Vector2(),Vector2(10,0),Vector2(0,10)});
		if(i>0) {
			mesh["skeleton"]=0; mesh["bones"]=PackedInt32Array({0,1,0,0,0,1,0,0,0,1,0,0});
			mesh["weights"]=PackedFloat32Array({.5,.5,0,0,.5,.5,0,0,.5,.5,0,0});
		}
		image["polygon_2d"]=mesh; attachment["image"]=entities.size(); skin[name]=attachment; entities.push_back(image);
	}
	skins["default"]=skin; rig["bones"]=PackedInt64Array({1,2}); rig["slots"]=slots; rig["skins"]=skins; root["skeleton_2d"]=rig;
	test->set_entities(entities); edit_scene(test,nullptr);
	bool ok=true; int found=0;
	for(TreeItem *item=hierarchy->get_root()->get_next_in_tree();item;item=item->get_next_in_tree()) {
		Variant metadata=item->get_metadata(0); if(metadata.get_type()!=Variant::DICTIONARY) { continue; }
		Dictionary meta=metadata; if(!meta.has("slot") || meta.has("skin") || meta.has("placeholder")) { continue; }
		String name=meta["slot"]; int expected=name=="rigid" || name=="legacy"?1:2;
		ok &= item->get_parent()->get_metadata(0).get_type()==Variant::INT && int(item->get_parent()->get_metadata(0))==expected;
		if(name!="empty") { bool image_found=false; for(TreeItem *child=item->get_first_child();child;child=child->get_next()) { image_found |= child->get_metadata(0).get_type()==Variant::INT; } ok &= image_found; }
		found++;
	}
	ok &= found==4 && test->get_entities()==entities;
	Ref<ECSWorld> world=test->instantiate(); ok &= world.is_valid();
	if(world.is_valid()) {
		Ref<ECSScene> captured; captured.instantiate(); ok &= captured->capture(world);
		Array saved_slots=Dictionary(Dictionary(captured->get_entities()[0])["skeleton_2d"])["slots"];
		ok &= int(Dictionary(saved_slots[2])["bone"])==1 && captured->instantiate().is_valid();
	}
	edit_scene(saved,nullptr);
	if(ok) { print_line("SKELETON_SLOT_HIERARCHY_PASS rigid weighted_legacy explicit_owner empty_slot unchanged_mesh snapshot_roundtrip"); }
	return ok;
}

void ECSAnimationEditor::build_slot_tools(BoxContainer *parent) {
	auto row=[&]() { auto *line=memnew(HBoxContainer); parent->add_child(line); return line; };
	auto choice=[&](BoxContainer *line,int action) { auto *option=memnew(OptionButton); option->set_h_size_flags(SIZE_EXPAND_FILL); line->add_child(option); option->connect("item_selected",callable_mp(this,&ECSAnimationEditor::slot_action).bind(action).unbind(1)); return option; };
	auto text=[&](BoxContainer *line,const String &placeholder) { auto *edit=memnew(LineEdit); edit->set_placeholder(placeholder); edit->set_h_size_flags(SIZE_EXPAND_FILL); line->add_child(edit); return edit; };
	auto button=[&](BoxContainer *line,const String &label,int action) { auto *control=memnew(Button);control->set_text(label);line->add_child(control);control->connect("pressed",callable_mp(this,&ECSAnimationEditor::slot_action).bind(action)); };
	skin_choice=choice(row(),0);
    auto *create_row=row(); button(create_row,String(U"新建皮肤"),12); button(create_row,String(U"重命名"),13);
    skin_layers=memnew(ItemList); skin_layers->set_select_mode(ItemList::SELECT_MULTI); skin_layers->set_custom_minimum_size(Size2(0,90)*EDSCALE); skin_layers->set_tooltip_text(String(U"Ctrl 多选需要组合的皮肤；下面的同名附件覆盖上面的。")); parent->add_child(skin_layers);
    auto *layer_row=row(); button(layer_row,String(U"应用组合"),14); button(layer_row,String(U"上移"),15); button(layer_row,String(U"下移"),16);
	auto *line=row();skin_name_edit=text(line,String(U"新皮肤名称"));button(line,String(U"复制皮肤"),1);button(line,String(U"删除"),2);
	slot_choice=choice(row(),3);
	line=row();slot_name_edit=text(line,String(U"新插槽名称"));button(line,String(U"添加插槽"),4);button(line,String(U"删除"),5);
	attachment_choice=choice(row(),6);
	line=row();attachment_name_edit=text(line,String(U"皮肤占位符名称"));button(line,String(U"创建占位符"),17);
    skin_attachment_target=memnew(OptionButton); parent->add_child(skin_attachment_target);
    line=row();button(line,String(U"绑定图片到当前皮肤"),7);button(line,String(U"解除当前绑定"),19);
    line=row();button(line,String(U"重命名占位符"),20);button(line,String(U"删除占位符"),18);
	line=row();slot_tint=memnew(ColorPickerButton);slot_tint->set_h_size_flags(SIZE_EXPAND_FILL);slot_tint->set_tooltip_text(String(U"插槽颜色"));line->add_child(slot_tint);slot_tint->connect("color_changed",callable_mp(this,&ECSAnimationEditor::slot_action).bind(8).unbind(1));
	slot_order=memnew(SpinBox);slot_order->set_min(-4096);slot_order->set_max(4096);slot_order->set_step(1);slot_order->set_tooltip_text(String(U"绘制顺序"));line->add_child(slot_order);slot_order->connect("value_changed",callable_mp(this,&ECSAnimationEditor::slot_action).bind(8).unbind(1));
	line=row(); button(line,String(U"附件关键帧"),9); button(line,String(U"颜色关键帧"),10); button(line,String(U"顺序关键帧"),11);
	slot_blend=choice(row(),8);slot_blend->add_item(String(U"正常混合"));slot_blend->add_item(String(U"加法混合"));slot_blend->add_item(String(U"正片叠底"));
}

void ECSAnimationEditor::refresh_slot_tools() {
	if(!skin_choice || scene.is_null() || refreshing_slots) { return; }
	refreshing_slots=true;
	String selected_slot=slot_choice->get_selected()<0?String():slot_choice->get_item_text(slot_choice->get_selected());
	skin_choice->clear();slot_choice->clear();attachment_choice->clear(); skin_layers->clear(); skin_attachment_target->clear();
	int rig=find_rig(target->get_selected_id());
	Dictionary definition=rig<0?Dictionary():Dictionary(Dictionary(scene->get_entities()[rig]).get("skeleton_2d",Dictionary()));
	refresh_wardrobe_tools(rig,definition);
	Dictionary skins=definition.get("skins",Dictionary());String active=definition.get("skin",String("default"));
	PackedStringArray stack=definition.get("active_skins",PackedStringArray());
    PackedStringArray layer_names=stack; for(const Variant &name:skins.keys()) { if(!layer_names.has(name)) { layer_names.push_back(name); } }
    for(const String &name:layer_names) { skin_layers->add_item(name,skeleton_workspace_icon("skin")); if(stack.has(name)) { skin_layers->select(skin_layers->get_item_count()-1,false); } }
    Array all_entities=scene->get_entities(); for(int i=0;i<all_entities.size();i++) { Dictionary entity=all_entities[i]; if(entity.has("polygon_2d") && find_rig(i)==rig) { skin_attachment_target->add_item(entity.get("name",String()),i); if(i==image_selection || i==target->get_selected_id()) { skin_attachment_target->select(skin_attachment_target->get_item_count()-1); } } }
    for(const Variant &name:skins.keys()) { skin_choice->add_icon_item(skeleton_workspace_icon("skin"),name);if(String(name)==active) { skin_choice->select(skin_choice->get_item_count()-1); } }
	Array slots=definition.get("slots",Array());
	for(int i=0;i<slots.size();i++) { String name=Dictionary(slots[i]).get("name",String());slot_choice->add_item(name,i);if(name==selected_slot) { slot_choice->select(i); } }
	int selected=slot_choice->get_selected_id();
	attachment_choice->add_item(String(U"无附件（隐藏）"));attachment_choice->set_item_metadata(0,String());
	if(selected>=0 && selected<slots.size()) {
		Dictionary slot=slots[selected],fallback=skins.get("default",Dictionary()),skin=skins.get(active,Dictionary());String name=slot["name"];
		Dictionary available=Dictionary(fallback.get(name,Dictionary())).duplicate();available.merge(skin.get(name,Dictionary()),true);
		Dictionary declared=definition.get("placeholders",Dictionary());
        for(const String &placeholder:PackedStringArray(declared.get(name,PackedStringArray()))) { if(!available.has(placeholder)) { available[placeholder]=-1; } }
        for(const Variant &skin_name:skins.keys()) { Dictionary names=Dictionary(skins[skin_name]).get(name,Dictionary()); for(const Variant &key:names.keys()) { if(!available.has(key)) { available[key]=-1; } } }
        for(const Variant &attachment:available.keys()) { attachment_choice->add_item(attachment);int index=attachment_choice->get_item_count()-1;attachment_choice->set_item_metadata(index,attachment);if(String(attachment)==String(slot.get("attachment",String()))) { attachment_choice->select(index); } }
		slot_tint->set_pick_color(slot.get("color",Color(1,1,1)));slot_order->set_value_no_signal(slot.get("z_index",selected));slot_blend->select(slot.get("blend",0));
	}
	slot_tint->set_disabled(selected<0);slot_order->set_editable(selected>=0);slot_blend->set_disabled(selected<0);
	refreshing_slots=false;
}

void ECSAnimationEditor::refresh_wardrobe_tools(int rig, const Dictionary &definition) {
	if(!wardrobe_tools) { return; }
	for(int i=wardrobe_tools->get_child_count()-1;i>=0;i--) { Node *child=wardrobe_tools->get_child(i); wardrobe_tools->remove_child(child); child->queue_free(); }
	Dictionary skins=definition.get("skins",Dictionary()),groups;
	// Skin folders express mutually exclusive parts without introducing runtime data.
	for(const Variant &key:skins.keys()) {
		String name=key; int slash=name.find("/"); if(slash<=0 || slash==name.length()-1) { continue; }
		String group=name.substr(0,slash); PackedStringArray names=groups.get(group,PackedStringArray()); names.push_back(name); groups[group]=names;
	}
	Object::cast_to<Control>(wardrobe_tools->get_parent())->set_visible(!groups.is_empty());
	PackedStringArray active=definition.get("active_skins",PackedStringArray());
	if(active.is_empty()) { active.push_back(definition.get("skin",String("default"))); }
	for(const Variant &key:groups.keys()) {
		String group=key; PackedStringArray names=groups[group]; names.insert(0,String());
		auto *row=memnew(HBoxContainer); wardrobe_tools->add_child(row);
		auto *label=memnew(Label); label->set_text(TTR(group)); label->set_custom_minimum_size(Size2(70,0)*EDSCALE); row->add_child(label);
		auto *choice=memnew(OptionButton); choice->set_name(group.validate_node_name()); choice->set_h_size_flags(SIZE_EXPAND_FILL); choice->set_fit_to_longest_item(false); row->add_child(choice);
		choice->set_tooltip_text(TTR("Choose one skin per part. Other parts and the current animation are preserved."));
		choice->add_item(TTR("Default"));
		for(int i=1;i<names.size();i++) { choice->add_item(TTR(names[i].substr(group.length()+1))); }
		for(const String &name:active) { int index=names.find(name); if(index>0) { choice->select(index); } }
		choice->connect("item_selected",callable_mp(this,&ECSAnimationEditor::wardrobe_selected).bind(rig,group,names));
	}
}

void ECSAnimationEditor::wardrobe_selected(int index, int rig, const String &group, const PackedStringArray &names) {
	if(index>=0 && index<names.size()) { apply_wardrobe_skin(rig,group,names[index]); }
}

bool ECSAnimationEditor::apply_wardrobe_skin(int rig, const String &group, const String &skin) {
	if(scene.is_null() || rig<0 || rig>=scene->get_entities().size() || group.is_empty() || group.contains("/")) { return false; }
	Array entities=scene->get_entities().duplicate(true); Dictionary root=entities[rig],definition=root.get("skeleton_2d",Dictionary());
	Dictionary skins=definition.get("skins",Dictionary()); String prefix=group+"/"; bool found=false;
	for(const Variant &name:skins.keys()) { if(String(name).begins_with(prefix) && String(name).length()>prefix.length()) { found=true; } }
	if(!found || (!skin.is_empty() && (!skin.begins_with(prefix) || !skins.has(skin)))) { return false; }
	PackedStringArray active=definition.get("active_skins",PackedStringArray()),next;
	if(active.is_empty()) { String current=definition.get("skin",String("default")); if(current!="default") { active.push_back(current); } }
	for(const String &name:active) { if(!name.begins_with(prefix)) { next.push_back(name); } }
	if(!skin.is_empty()) { next.push_back(skin); }
	if(!skins.has("default")) { skins["default"]=Dictionary(); }
	definition["skins"]=skins; definition["skin"]="default"; definition["active_skins"]=next;
	root["skeleton_2d"]=definition;
	if(entities==scene->get_entities()) { return true; }
	Ref<ECSScene> check; check.instantiate(); check->set_entities(entities); if(check->instantiate().is_null()) { return false; }
	// edit_scene reapplies the current playhead and keeps playback running.
	commit_entities(entities,TTR("Change outfit part")); return true;
}

void ECSAnimationEditor::slot_action(int action) {
	if(refreshing_slots || scene.is_null()) { return; }
	if(action==3) { refresh_slot_tools(); return; }
    if(action==15 || action==16) {
        auto selected_layers=skin_layers->get_selected_items(); if(selected_layers.size()!=1) { feedback->set_text(String(U"调整顺序时请选择一个皮肤。")); return; }
        int index=selected_layers[0],next=index+(action==15?-1:1); if(next>=0 && next<skin_layers->get_item_count()) { skin_layers->move_item(index,next); skin_layers->select(next);
            int rig=find_rig(target->get_selected_id()); if(rig>=0) { Array entities=scene->get_entities().duplicate(true);Dictionary root=entities[rig],definition=root["skeleton_2d"];PackedStringArray active=definition.get("active_skins",PackedStringArray()),ordered;
                for(int i=0;i<skin_layers->get_item_count();i++) { String name=skin_layers->get_item_text(i);if(active.has(name)) { ordered.push_back(name); } }
                if(!active.is_empty()) { definition["active_skins"]=ordered;commit_entities(entities,String(U"调整皮肤组合顺序"));refresh_slot_tools(); }
            } } return;
    }
	if(animation_mode && (action==6 || action==8)) { return; }
	int selected=target->get_selected_id(),rig=find_rig(selected);if(rig<0) { return; }
	Array entities=scene->get_entities().duplicate(true);Dictionary root=entities[rig],definition=root["skeleton_2d"];
	Dictionary skins=definition.get("skins",Dictionary());Array slots=definition.get("slots",Array());String active=definition.get("skin",String("default"));
	if(skins.is_empty()) { skins["default"]=Dictionary();active="default"; }
	PackedInt64Array library=definition.get("attachment_library",PackedInt64Array());
	for(const Variant &skin_name:skins.keys()) { Dictionary skin=skins[skin_name];for(const Variant &slot_name:skin.keys()) { Dictionary named=skin[slot_name];for(const Variant &name:named.keys()) { int64_t id=named[name];if(!library.has(id)) { library.push_back(id); } } } }
	int index=slot_choice->get_selected_id();
	if(action>=9 && action<=11) {
		if(index<0 || index>=slots.size()) { return; } Dictionary slot=slots[index]; String path="slot:"+String(slot["name"]).uri_encode()+":";
		if(action==9) { if(attachment_choice->get_selected()<0) { return; } insert_channel_key(path+"attachment",attachment_choice->get_item_metadata(attachment_choice->get_selected())); }
		else if(action==10) { insert_channel_key(path+"color",slot_tint->get_pick_color()); }
		else { insert_channel_key(path+"z_index",int64_t(slot_order->get_value())); } return;
	}
	if(action==12) { String name=skin_name_edit->get_text().strip_edges(); if(name.is_empty() || skins.has(name)) { feedback->set_text(String(U"请输入未使用的皮肤名称。")); return; } skins[name]=Dictionary();active=name;definition["active_skins"]=PackedStringArray(); }
    else if(action==13) { String name=skin_name_edit->get_text().strip_edges();if(active=="default" || name.is_empty() || skins.has(name)) { feedback->set_text(String(U"默认皮肤不可重命名，新名称必须唯一。"));return; }skins[name]=skins[active];skins.erase(active);PackedStringArray stack=definition.get("active_skins",PackedStringArray());for(int i=0;i<stack.size();i++) { if(stack[i]==active) { stack.set(i,name); } }definition["active_skins"]=stack;active=name; }
    else if(action==14) { PackedStringArray stack;for(int i=0;i<skin_layers->get_item_count();i++) { if(skin_layers->is_selected(i)) { stack.push_back(skin_layers->get_item_text(i)); } }definition["active_skins"]=stack; }
    else if(action==0) { definition["active_skins"]=PackedStringArray(); if(skin_choice->get_selected()<0) { return; }active=skin_choice->get_item_text(skin_choice->get_selected()); }
	else if(action==1) { String name=skin_name_edit->get_text().strip_edges();if(name.is_empty() || skins.has(name)) { feedback->set_text(String(U"请输入未使用的皮肤名称。"));return; }skins[name]=Dictionary(skins.get(active,Dictionary())).duplicate(true);active=name; }
	else if(action==2) { if(active=="default") { feedback->set_text(String(U"默认皮肤保留用于附件回退。"));return; }PackedStringArray stack=definition.get("active_skins",PackedStringArray());int removed=stack.find(active);if(removed>=0) { stack.remove_at(removed); }definition["active_skins"]=stack;skins.erase(active);active="default";if(!skins.has(active)) { skins[active]=Dictionary(); } }
	else if(action==4) { String name=slot_name_edit->get_text().strip_edges();if(name.is_empty()) { return; }for(const Variant &entry:slots) { if(String(Dictionary(entry)["name"])==name) { feedback->set_text(String(U"插槽名称已存在。"));return; } }Dictionary slot;slot["name"]=name; int selected_bone=PackedInt64Array(definition.get("bones",PackedInt64Array())).find(target->get_selected_id()); if(selected_bone>=0) { slot["bone"]=selected_bone; } slot["attachment"]=String();slot["z_index"]=slots.size();slots.push_back(slot); }
	else {
		if(index<0 || index>=slots.size()) { return; }Dictionary slot=slots[index];String name=slot["name"];
		if(action==5) { Dictionary declared=definition.get("placeholders",Dictionary());declared.erase(name);definition["placeholders"]=declared;slots.remove_at(index);for(const Variant &skin_name:skins.keys()) { Dictionary skin=skins[skin_name];skin.erase(name); } }
		else if(action==6) { if(attachment_choice->get_selected()<0) { return; }slot["attachment"]=attachment_choice->get_item_metadata(attachment_choice->get_selected()); }
        else if(action==17) { String placeholder=attachment_name_edit->get_text().strip_edges(); if(placeholder.is_empty()) { return; } Dictionary declared=definition.get("placeholders",Dictionary());PackedStringArray names=declared.get(name,PackedStringArray());if(names.has(placeholder)) { feedback->set_text(String(U"占位符已存在。"));return; }names.push_back(placeholder);declared[name]=names;definition["placeholders"]=declared;slot["attachment"]=placeholder; }
        else if(action==18 || action==20) {
            String old=attachment_choice->get_selected()>=0?String(attachment_choice->get_item_metadata(attachment_choice->get_selected())):String();String renamed=action==18?String():attachment_name_edit->get_text().strip_edges();
            if(old.is_empty() || (action==20 && (renamed.is_empty() || renamed==old))) { return; }
            Dictionary declared=definition.get("placeholders",Dictionary());PackedStringArray names=declared.get(name,PackedStringArray());
            if(action==20) { if(names.has(renamed)) { feedback->set_text(String(U"占位符名称已存在。"));return; }for(const Variant &skin_name:skins.keys()) { if(Dictionary(Dictionary(skins[skin_name]).get(name,Dictionary())).has(renamed)) { feedback->set_text(String(U"其他皮肤已使用此占位符名称。"));return; } } }
            int found=names.find(old);if(found>=0) { names.remove_at(found); }if(action==20) { names.push_back(renamed); }declared[name]=names;definition["placeholders"]=declared;
            for(const Variant &skin_name:skins.keys()) { Dictionary skin=skins[skin_name],named=skin.get(name,Dictionary());if(named.has(old)) { if(action==20) { named[renamed]=named[old]; }named.erase(old);skin[name]=named; } }
            if(String(slot.get("attachment",String()))==old) { slot["attachment"]=renamed; }
            Dictionary animation=root.get("animation",Dictionary());Dictionary states=animation.get("states",Dictionary());
            auto update_clip=[&](const Ref<Animation> &source) { if(source.is_null()) { return source; }Ref<Animation> clip=source->duplicate(true);for(int t=0;t<clip->get_track_count();t++) { if(clip->track_get_path(t).get_concatenated_subnames()=="slot:"+name.uri_encode()+":attachment") { for(int k=0;k<clip->track_get_key_count(t);k++) { if(String(clip->track_get_key_value(t,k))==old) { clip->track_set_key_value(t,k,renamed); } } } }return clip; };
            if(animation.has("clip")) { animation["clip"]=update_clip(animation["clip"]); }for(const Variant &state:states.keys()) { states[state]=update_clip(states[state]); }if(!states.is_empty()) { animation["states"]=states; }if(!animation.is_empty()) { root["animation"]=animation; }
        }
        else if(action==19) { String placeholder=attachment_choice->get_selected()>=0?String(attachment_choice->get_item_metadata(attachment_choice->get_selected())):String();Dictionary skin=skins[active],named=skin.get(name,Dictionary());named.erase(placeholder);skin[name]=named; }
        else if(action==7) { int image=skin_attachment_target->get_selected_id(); if(image<0 || image>=entities.size() || !Dictionary(entities[image]).has("polygon_2d")) { feedback->set_text(String(U"请选择要绑定的图片。"));return; }String attachment=attachment_name_edit->get_text().strip_edges();if(attachment.is_empty() && attachment_choice->get_selected()>=0) { attachment=attachment_choice->get_item_metadata(attachment_choice->get_selected()); }if(attachment.is_empty()) { feedback->set_text(String(U"请创建或选择皮肤占位符。"));return; }Dictionary skin=skins[active],named=skin.get(name,Dictionary());named[attachment]=image;skin[name]=named;slot["attachment"]=attachment;if(!library.has(image)) { library.push_back(image); } }

		else if(action==8) { slot["color"]=slot_tint->get_pick_color();slot["z_index"]=int(slot_order->get_value());slot["blend"]=slot_blend->get_selected(); }
	}
	definition["attachment_library"]=library;definition["slots"]=slots;definition["skins"]=skins;definition["skin"]=active;
	stop_preview();commit_entities(entities,String(U"编辑皮肤与插槽"));select_target(selected);refresh_slot_tools();
}
#endif
