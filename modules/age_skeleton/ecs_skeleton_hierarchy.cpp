#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "skeleton_attachment_tree.h"
#include "core/object/callable_mp.h"
#include "ecs_skeleton_icons.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/check_box.h"
#include "scene/gui/item_list.h"
#include "scene/resources/style_box_flat.h"
#include "modules/regex/regex.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"

namespace {
Ref<Texture2D> editor_icon(const String &name) { return EditorNode::get_singleton()->get_editor_theme()->get_icon(name,"EditorIcons"); }
void style_tool(Button *button) {
    button->set_custom_minimum_size(Size2(26,25)*EDSCALE);
    button->add_theme_color_override("icon_pressed_color",Color(1,1,1)); button->add_theme_color_override("icon_hover_pressed_color",Color(1,1,1));
    button->set_expand_icon(true); button->add_theme_constant_override("icon_max_width",16*EDSCALE);
    for(const char *state:{"normal","hover","pressed","hover_pressed"}) {
        Ref<StyleBoxFlat> box; box.instantiate(); String s=state;
        box->set_bg_color(s=="pressed" || s=="hover_pressed"?Color(.19,.51,.62):s=="hover"?Color(.4,.4,.4):Color(.30,.30,.30));
        box->set_corner_radius_all(3*EDSCALE); box->set_content_margin_all(4*EDSCALE); button->add_theme_style_override(state,box);
    }
}
}
Label *ECSAnimationEditor::build_panel_header(BoxContainer *parent,const String &caption,const String &icon,bool animation) {
    auto *row=memnew(HBoxContainer); parent->add_child(row);
    auto *tab=memnew(PanelContainer); row->add_child(tab);
    Ref<StyleBoxFlat> background; background.instantiate(); background->set_bg_color(Color(.30,.30,.30));
    background->set_corner_radius(CORNER_TOP_LEFT,4*EDSCALE); background->set_corner_radius(CORNER_TOP_RIGHT,4*EDSCALE); background->set_content_margin_all(5*EDSCALE); tab->add_theme_style_override("panel",background);
    auto *content=memnew(HBoxContainer); tab->add_child(content);
    auto *glyph=memnew(TextureRect); glyph->set_texture(skeleton_workspace_icon(icon)); glyph->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED); content->add_child(glyph);
    auto *label=memnew(Label); label->set_text(caption); content->add_child(label);
    auto *space=memnew(Control); space->set_h_size_flags(SIZE_EXPAND_FILL); row->add_child(space);
    auto *menu=memnew(MenuButton); menu->set_button_icon(editor_icon("GuiTabMenu")); menu->set_tooltip_text(String(U"面板选项")); row->add_child(menu);
    if(animation) {
        menu->get_popup()->add_item(String(U"新建动画"),23); menu->get_popup()->add_item(String(U"复制动画"),7);
        menu->get_popup()->connect("id_pressed",callable_mp(this,&ECSAnimationEditor::action));
    } else {
        menu->get_popup()->add_item(String(U"重置过滤"),10); menu->get_popup()->add_item(String(U"折叠全部"),20); menu->get_popup()->add_item(String(U"展开全部"),21);
        menu->get_popup()->connect("id_pressed",callable_mp(this,&ECSAnimationEditor::hierarchy_filter_action));
    }
    parent->set_meta("skeleton_panel_options",menu->get_popup());
    return label;
}
int ECSAnimationEditor::hierarchy_entity_kind(const Dictionary &e) const {
    if(e.has("bone_2d")) { return 0; }
    if(e.has("polygon_2d")) { Dictionary mesh=e["polygon_2d"]; return mesh.has("bones") || mesh.has("triangles") ? 3 : 2; }
    return 4;
}
void ECSAnimationEditor::build_hierarchy_toolbar(BoxContainer *parent) {
    auto *row=memnew(HBoxContainer); row->add_theme_constant_override("separation",3*EDSCALE); parent->add_child(row);
    hierarchy_search=memnew(LineEdit); hierarchy_search->set_placeholder(String(U"搜索")); hierarchy_search->set_right_icon(editor_icon("Search")); hierarchy_search->set_h_size_flags(SIZE_EXPAND_FILL); hierarchy_search->set_custom_minimum_size(Size2(80,25)*EDSCALE); row->add_child(hierarchy_search);
    hierarchy_search->connect("text_changed",callable_mp(this,&ECSAnimationEditor::filter_hierarchy));
    auto tool=[&](const String &icon,const String &tip,int action) { auto *b=memnew(Button); b->set_button_icon(editor_icon(icon)); b->set_tooltip_text(tip); style_tool(b); row->add_child(b); b->connect("pressed",callable_mp(this,&ECSAnimationEditor::hierarchy_tool_action).bind(action)); return b; };
    tool("Back",String(U"上一个选择"),0); tool("Forward",String(U"下一个选择"),1);
    for(int i=0;i<3;i++) {
        auto *b=memnew(Button); hierarchy_quick[i]=b; b->set_button_icon(skeleton_workspace_icon(i==0?"bone":i==1?"slot":"attachment"));
        b->set_tooltip_text(i==0?String(U"显示骨骼"):i==1?String(U"显示插槽"):String(U"显示图片和网格附件"));
        b->set_toggle_mode(true); b->set_pressed(true); style_tool(b); row->add_child(b); b->connect("toggled",callable_mp(this,&ECSAnimationEditor::hierarchy_quick_filter).bind(i));
    }
    hierarchy_filter=memnew(MenuButton); hierarchy_filter->set_button_icon(editor_icon("AnimationFilter")); hierarchy_filter->set_tooltip_text(String(U"过滤")); style_tool(hierarchy_filter); row->add_child(hierarchy_filter);
    auto *popup=hierarchy_filter->get_popup(); popup->set_hide_on_checkable_item_selection(false); popup->add_item(String(U"重置"),10); popup->add_separator();
    const char32_t *labels[]={U"骨骼",U"插槽",U"图片区域",U"网格",U"骨架"};
    for(int i=0;i<5;i++) { popup->add_check_item(String(labels[i]),i); popup->set_item_checked(popup->get_item_index(i),true); }
    popup->add_separator(); popup->add_check_item(String(U"文本搜索区分大小写"),11); popup->add_check_item(String(U"文本搜索完整匹配"),12);
    popup->connect("id_pressed",callable_mp(this,&ECSAnimationEditor::hierarchy_filter_action));
    tool("Search",String(U"定位当前选择"),2); tool("Tools",String(U"查找和替换"),3);
    tool("CollapseTree",String(U"折叠全部"),4); tool("ExpandTree",String(U"展开全部"),5);
}
void ECSAnimationEditor::hierarchy_quick_filter(bool enabled,int kind) {
    hierarchy_types[kind]=enabled; if(kind==2) { hierarchy_types[3]=enabled; }
    auto *popup=hierarchy_filter->get_popup(); for(int i=0;i<5;i++) { popup->set_item_checked(popup->get_item_index(i),hierarchy_types[i]); }
    filter_hierarchy(hierarchy_search->get_text());
}
void ECSAnimationEditor::hierarchy_filter_action(int action) {
    if(action==20 || action==21) { fold_hierarchy(action==20); return; }
    if(action<5) { hierarchy_types[action]=!hierarchy_types[action]; }
    if(action==10) { for(bool &enabled:hierarchy_types) { enabled=true; } hierarchy_case=false; hierarchy_exact=false; hierarchy_search->set_text(""); }
    if(action==11) { hierarchy_case=!hierarchy_case; } if(action==12) { hierarchy_exact=!hierarchy_exact; }
    auto *popup=hierarchy_filter->get_popup(); for(int i=0;i<5;i++) { popup->set_item_checked(popup->get_item_index(i),hierarchy_types[i]); }
    popup->set_item_checked(popup->get_item_index(11),hierarchy_case); popup->set_item_checked(popup->get_item_index(12),hierarchy_exact);
    for(int i=0;i<3;i++) { hierarchy_quick[i]->set_pressed_no_signal(i==2?hierarchy_types[2] || hierarchy_types[3]:hierarchy_types[i]); }
    filter_hierarchy(hierarchy_search->get_text());
}
void ECSAnimationEditor::filter_hierarchy(const String &filter) {
    if(!hierarchy || scene.is_null()) { return; }
    Array entities=scene->get_entities(); Vector<TreeItem *> matches;
    for(TreeItem *row=hierarchy->get_root()?hierarchy->get_root()->get_next_in_tree():nullptr;row;row=row->get_next_in_tree()) {
        Variant meta=row->get_metadata(0); int kind=1;
        if(meta.get_type()==Variant::DICTIONARY) { Dictionary m=meta; if(m.has("placeholder")) { kind=2; } else if(m.has("skin") || m.has("skin_group")) { kind=4; } }
        if(meta.get_type()==Variant::INT) { int id=meta; kind=id>=0 && id<entities.size()?hierarchy_entity_kind(entities[id]):4; }
        String value=row->get_text(0),query=filter; if(!hierarchy_case) { value=value.to_lower(); query=query.to_lower(); }
        bool visible=hierarchy_types[kind] && (query.is_empty() || (hierarchy_exact?value==query:value.contains(query)));
        row->set_visible(visible); row->set_custom_color(0,get_theme_color("font_color","Tree")); if(visible) { matches.push_back(row); }
    }
    for(TreeItem *row:matches) { for(TreeItem *parent=row->get_parent();parent;parent=parent->get_parent()) { if(!parent->is_visible()) { parent->set_custom_color(0,Color(.5,.52,.53)); parent->set_visible(true); } if(!filter.is_empty()) { parent->set_collapsed(false); } } }
}
void ECSAnimationEditor::hierarchy_tool_action(int action) {
    if(action<2) {
        int next=selection_history_cursor+(action==0?-1:1); if(next<0 || next>=selection_history.size()) { return; }
        selection_history_cursor=next; navigating_history=true; select_target(selection_history[next]); navigating_history=false;
    } else if(action==2) {
        TreeItem *row=hierarchy->get_selected(); if(row) { for(TreeItem *p=row->get_parent();p;p=p->get_parent()) { p->set_collapsed(false); } hierarchy->scroll_to_item(row); }
    } else if(action==3) { open_find_replace(); }
    else { fold_hierarchy(action==4); }
}
void ECSAnimationEditor::open_find_replace() {
    if(!find_dialog) {
        find_dialog=memnew(ConfirmationDialog); find_dialog->set_title(String(U"查找和替换")); find_dialog->set_ok_button_text(String(U"替换")); find_dialog->set_cancel_button_text(String(U"取消")); find_dialog->set_hide_on_ok(false); add_child(find_dialog);
        find_dialog->get_ok_button()->set_button_icon(editor_icon("ImportCheck")); find_dialog->get_cancel_button()->set_button_icon(editor_icon("Close"));
        for(Button *b:{find_dialog->get_ok_button(),find_dialog->get_cancel_button()}) { b->set_custom_minimum_size(Size2(100,32)*EDSCALE); b->add_theme_font_size_override("font_size",14*EDSCALE); }
        auto *surface=memnew(PanelContainer); find_dialog->add_child(surface); Ref<StyleBoxFlat> frame; frame.instantiate(); frame->set_bg_color(Color(.21,.22,.23)); frame->set_content_margin_all(12*EDSCALE); surface->add_theme_style_override("panel",frame);
        auto *body=memnew(HBoxContainer); body->add_theme_constant_override("separation",12*EDSCALE); surface->add_child(body);
        auto *left=memnew(VBoxContainer); left->set_custom_minimum_size(Size2(280,0)*EDSCALE); left->add_theme_constant_override("separation",7*EDSCALE); body->add_child(left);
        auto label=[&](const String &text) { auto *l=memnew(Label); l->set_text(text); left->add_child(l); };
        auto input=[&](const String &caption,const String &icon) { auto *row=memnew(HBoxContainer); left->add_child(row); auto *l=memnew(Label); l->set_text(caption); l->set_custom_minimum_size(Size2(48,0)*EDSCALE); row->add_child(l); auto *edit=memnew(LineEdit); edit->set_h_size_flags(SIZE_EXPAND_FILL); edit->set_custom_minimum_size(Size2(0,28)*EDSCALE); edit->set_right_icon(editor_icon(icon)); row->add_child(edit); return edit; };
        find_text=input(String(U"查找"),"Search"); replace_text=input(String(U"替换"),"Edit");
        find_case=memnew(CheckBox); find_case->set_text(String(U"区分大小写")); left->add_child(find_case);
        find_first=memnew(CheckBox); find_first->set_text(String(U"每个名称只替换第一次出现")); find_first->set_pressed(true); left->add_child(find_first);
        find_regex=memnew(CheckBox); find_regex->set_text(String(U"正则表达式")); left->add_child(find_regex);
        label(String(U"范围")); find_scope=memnew(ItemList); find_scope->set_custom_minimum_size(Size2(0,80)*EDSCALE); left->add_child(find_scope);
        for(const char32_t *s:{U"整个工程",U"所选层级",U"当前皮肤的附件"}) { find_scope->add_item(String(s),editor_icon(find_scope->get_item_count()==0?"Folder":find_scope->get_item_count()==1?"Node":"Image")); } find_scope->select(0);
        label(String(U"字段")); auto *field=memnew(ItemList); field->set_custom_minimum_size(Size2(0,34)*EDSCALE); field->add_item(String(U"对象名称"),editor_icon("Edit")); field->select(0); left->add_child(field); label(String(U"类型")); find_types=memnew(ItemList); find_types->set_select_mode(ItemList::SELECT_MULTI); find_types->set_custom_minimum_size(Size2(0,125)*EDSCALE); left->add_child(find_types);
        for(const char32_t *s:{U"骨架",U"骨骼",U"图片区域",U"网格"}) { find_types->add_item(String(s),skeleton_workspace_icon(find_types->get_item_count()==0?"skeleton":find_types->get_item_count()==1?"bone":find_types->get_item_count()==2?"attachment":"mesh")); find_types->select(find_types->get_item_count()-1,false); }
        for(ItemList *list:{find_scope,find_types,field}) {
            list->set_fixed_icon_size(Size2(16,16)*EDSCALE); list->add_theme_constant_override("v_separation",6*EDSCALE);
            Ref<StyleBoxFlat> box; box.instantiate(); box->set_bg_color(Color(.30,.30,.30)); box->set_content_margin_all(6*EDSCALE); box->set_corner_radius_all(4*EDSCALE); list->add_theme_style_override("panel",box);
        }
        auto *right=memnew(VBoxContainer); right->set_h_size_flags(SIZE_EXPAND_FILL); body->add_child(right);
        find_results=memnew(Tree); Ref<StyleBoxFlat> result_panel; result_panel.instantiate(); result_panel->set_bg_color(Color(.12,.13,.14)); result_panel->set_corner_radius_all(4*EDSCALE); result_panel->set_content_margin_all(6*EDSCALE); find_results->add_theme_style_override("panel",result_panel);
        find_results->set_columns(3); find_results->set_column_titles_visible(true); find_results->set_column_title(0,String(U"选择")); find_results->set_column_title(1,String(U"原名称")); find_results->set_column_title(2,String(U"替换后")); find_results->set_column_expand(0,false); find_results->set_column_custom_minimum_width(0,46*EDSCALE); find_results->set_hide_root(true); find_results->set_v_size_flags(SIZE_EXPAND_FILL); right->add_child(find_results);
        auto *footer=memnew(HBoxContainer); right->add_child(footer); find_status=memnew(Label); find_status->set_h_size_flags(SIZE_EXPAND_FILL); footer->add_child(find_status);
        for(int i=0;i<2;i++) { auto *b=memnew(Button); b->set_text(i==0?String(U"全部"):String(U"无")); b->set_button_icon(editor_icon(i==0?"ImportCheck":"Close")); b->set_custom_minimum_size(Size2(66,28)*EDSCALE); footer->add_child(b); b->connect("pressed",callable_mp(this,&ECSAnimationEditor::select_find_results).bind(i==0)); }
        find_text->connect("text_changed",callable_mp(this,&ECSAnimationEditor::refresh_find_replace).unbind(1)); replace_text->connect("text_changed",callable_mp(this,&ECSAnimationEditor::refresh_find_replace).unbind(1));
        for(CheckBox *b:{find_case,find_first,find_regex}) { b->connect("toggled",callable_mp(this,&ECSAnimationEditor::refresh_find_replace).unbind(1)); }
        find_scope->connect("item_selected",callable_mp(this,&ECSAnimationEditor::refresh_find_replace).unbind(1)); find_types->connect("multi_selected",callable_mp(this,&ECSAnimationEditor::refresh_find_replace).unbind(2));
        find_dialog->connect("confirmed",callable_mp(this,&ECSAnimationEditor::apply_find_replace));
    }
    refresh_find_replace(); find_dialog->popup_centered(Size2(860,610)*EDSCALE); find_text->grab_focus();
}
void ECSAnimationEditor::refresh_find_replace() {
    find_results->clear(); auto *root=find_results->create_item(); find_candidates.clear(); find_revision=skeleton_ai_revision;
    find_dialog->get_ok_button()->set_disabled(true); if(scene.is_null() || find_text->get_text().is_empty()) { find_status->set_text(String(U"请输入查找内容")); return; }
    String pattern=find_text->get_text(); if(!find_regex->is_pressed()) { String escaped; for(int i=0;i<pattern.length();i++) { if(String("\\.^$|()[]{}*+?").contains(String::chr(pattern[i]))) { escaped+="\\"; } escaped+=String::chr(pattern[i]); } pattern=escaped; }
    Ref<RegEx> regex; regex.instantiate(); if(regex->compile((find_case->is_pressed()?String():String("(?i)"))+pattern,false)!=OK) { find_status->set_text(String(U"正则表达式无效")); return; }
    Array entities=scene->get_entities(); int selected=target->get_selected_id(); int scope=find_scope->get_selected_items().is_empty()?0:find_scope->get_selected_items()[0]; HashSet<int> skin_ids;
    if(scope==2) { int rig=find_rig(selected); if(rig>=0) { Dictionary skeleton=Dictionary(entities[rig]).get("skeleton_2d",Dictionary()), skins=skeleton.get("skins",Dictionary()), skin=skins.get(skeleton.get("skin","default"),Dictionary()); for(const Variant &slot:skin.keys()) { Dictionary attachments=skin[slot]; for(const Variant &key:attachments.keys()) { skin_ids.insert(attachments[key]); } } } }
    for(int i=0;i<entities.size();i++) {
        Dictionary e=entities[i]; int kind=hierarchy_entity_kind(e),type=kind==4?0:kind==0?1:kind==2?2:3; if(!find_types->is_selected(type)) { continue; }
        if(scope==1) { int cursor=i; bool inside=false; for(int step=0;step<entities.size() && cursor>=0 && cursor<entities.size();step++) { if(cursor==selected) { inside=true; break; } cursor=Dictionary(entities[cursor]).get("parent",-1); } if(!inside) { continue; } }
        if(scope==2 && !skin_ids.has(i)) { continue; }
        String before=e.get("name",String()); if(regex->search(before).is_null()) { continue; }
        String after;
        if(find_regex->is_pressed()) { after=regex->sub(before,replace_text->get_text(),!find_first->is_pressed()); }
        else { int offset=0; auto matches=regex->search_all(before); for(int m=0;m<matches.size();m++) { Ref<RegExMatch> match=matches[m]; int start=match->get_start(0),end=match->get_end(0); after+=before.substr(offset,start-offset)+replace_text->get_text(); offset=end; if(find_first->is_pressed()) { break; } } after+=before.substr(offset); }
        Dictionary item; item["entity"]=i; item["before"]=before; item["after"]=after; find_candidates.push_back(item);
        auto *row=find_results->create_item(root); row->set_cell_mode(0,TreeItem::CELL_MODE_CHECK); row->set_editable(0,true); row->set_checked(0,true); row->set_metadata(0,find_candidates.size()-1); row->set_text(1,before); row->set_icon(1,skeleton_workspace_icon(type==0?"skeleton":type==1?"bone":type==2?"attachment":"mesh")); row->set_text(2,after);
    }
    find_status->set_text(String(U"匹配：")+itos(find_candidates.size())); find_dialog->get_ok_button()->set_disabled(find_candidates.is_empty());
}
void ECSAnimationEditor::select_find_results(bool selected) { for(TreeItem *r=find_results->get_root()?find_results->get_root()->get_first_child():nullptr;r;r=r->get_next()) { r->set_checked(0,selected); } }
void ECSAnimationEditor::apply_find_replace() {
    if(scene.is_null()) { return; } if(find_revision!=skeleton_ai_revision) { refresh_find_replace(); find_status->set_text(String(U"工程已变化，请确认更新后的结果再替换")); return; }
    Array after=scene->get_entities().duplicate(true); int count=0;
    for(TreeItem *r=find_results->get_root()?find_results->get_root()->get_first_child():nullptr;r;r=r->get_next()) {
        if(!r->is_checked(0)) { continue; } Dictionary item=find_candidates[int(r->get_metadata(0))]; String name=item["after"];
        if(name.strip_edges().is_empty()) { find_status->set_text(String(U"替换后名称不能为空，未执行")); return; }
        if(name!=String(item["before"])) { Dictionary e=after[int(item["entity"])]; e["name"]=name; count++; }
    }
    if(count) { commit_entities(after,String(U"批量替换对象名称")); }
    refresh_find_replace(); find_status->set_text(String(U"已替换：")+itos(count)+String(U"（可撤销）"));
}

bool ECSAnimationEditor::run_workspace_tools_self_test() {
    Ref<ECSScene> saved=scene; bool was_animation=animation_mode;
    Ref<ECSScene> test; test.instantiate(); Array entities; Dictionary root,bone,rig,definition;
    root["name"]="Skeleton"; rig["bones"]=PackedInt64Array({1}); root["skeleton_2d"]=rig;
    bone["name"]="Bone test Bone"; bone["parent"]=0; definition["length"]=50.; bone["bone_2d"]=definition; entities.push_back(root); entities.push_back(bone); test->set_entities(entities); edit_scene(test,nullptr); set_mode(0); select_target(1);
    canvas_tool_action(12); bool ok=scene->get_entities().size()==2;
    auto mouse=[&](bool pressed,Vector2 position) { Ref<InputEventMouseButton> event; event.instantiate(); event->set_button_index(MouseButton::LEFT); event->set_pressed(pressed); event->set_position(position); local_canvas->gui_input(event); };
    mouse(true,Vector2(220,160)); mouse(false,Vector2(220,160)); ok &= scene->get_entities().size()==2;
    mouse(true,Vector2(220,160)); Ref<InputEventKey> escape; escape.instantiate(); escape->set_keycode(Key::ESCAPE); escape->set_pressed(true); local_canvas->gui_input(escape); mouse(false,Vector2(290,200)); ok &= scene->get_entities().size()==2;
    mouse(true,Vector2(220,160)); mouse(false,Vector2(290,200)); ok &= scene->get_entities().size()==3;
    auto *undo=EditorUndoRedoManager::get_singleton(); ok &= undo->undo(); edit_scene(test,nullptr); ok &= scene->get_entities().size()==2;
    canvas_tool_action(1); select_target(1); hierarchy_quick_filter(false,0);
    bool hidden=false; for(TreeItem *r=hierarchy->get_root()->get_next_in_tree();r;r=r->get_next_in_tree()) { if(r->get_metadata(0).get_type()==Variant::INT && int(r->get_metadata(0))==1) { hidden=!r->is_visible(); } } ok &= hidden; hierarchy_filter_action(10);
    open_find_replace(); find_text->set_text("Bone"); replace_text->set_text("Joint"); refresh_find_replace(); ok &= find_candidates.size()==1;
    apply_find_replace(); ok &= String(Dictionary(scene->get_entities()[1])["name"])=="Joint test Bone";
    ok &= undo->undo(); edit_scene(test,nullptr); ok &= String(Dictionary(scene->get_entities()[1])["name"])=="Bone test Bone";
    find_regex->set_pressed(true); find_text->set_text("["); refresh_find_replace(); ok &= find_candidates.is_empty() && find_dialog->get_ok_button()->is_disabled();
    find_regex->set_pressed(false); find_text->set_text(""); replace_text->set_text(""); find_dialog->hide();
    // Exercise authoring operations, not just raw runtime dictionaries.
    Ref<ECSScene> skins_test; skins_test.instantiate(); Array skin_entities=entities.duplicate(true);
    Dictionary image_a,image_b,polygon;polygon["polygon"]=PackedVector2Array({Vector2(),Vector2(10,0),Vector2(0,10)});image_a["name"]="Red";image_a["parent"]=1;image_a["polygon_2d"]=polygon;image_b=image_a.duplicate(true);image_b["name"]="Blue";skin_entities.push_back(image_a);skin_entities.push_back(image_b);skins_test->set_entities(skin_entities);edit_scene(skins_test,nullptr);select_target(1);
    slot_name_edit->set_text("body");slot_action(4);attachment_name_edit->set_text("outfit");slot_action(17);
    skin_attachment_target->select(0);slot_action(7);skin_name_edit->set_text("blue");slot_action(1);skin_attachment_target->select(1);slot_action(7);
    Ref<ECSWorld> dressed=scene->instantiate();auto dressed_ids=dressed->query(PackedStringArray(),true);ok &= !dressed->is_skeleton_attachment_visible(dressed_ids[2]) && dressed->is_skeleton_attachment_visible(dressed_ids[3]);
    attachment_name_edit->set_text("costume");slot_action(20);Dictionary authored=Dictionary(scene->get_entities()[0])["skeleton_2d"];ok &= Dictionary(Dictionary(Dictionary(authored["skins"])["blue"])["body"]).has("costume");
    for(int i=0;i<skin_layers->get_item_count();i++) { skin_layers->select(i,false); }slot_action(14);authored=Dictionary(scene->get_entities()[0])["skeleton_2d"];ok &= PackedStringArray(authored["active_skins"]).size()==2;
    Ref<ECSScene> captured;captured.instantiate();ok &= captured->capture(scene->instantiate()) && captured->instantiate().is_valid();
    ok &= ResourceSaver::save(captured,"user://skin-workflow-regression.tres",ResourceSaver::FLAG_BUNDLE_RESOURCES)==OK;
    Ref<ECSScene> reopened=ResourceLoader::load("user://skin-workflow-regression.tres","ECSScene",ResourceLoader::CACHE_MODE_IGNORE);
    ok &= reopened.is_valid() && reopened->instantiate().is_valid();
    if(reopened.is_valid()) { Dictionary saved_rig=Dictionary(reopened->get_entities()[0])["skeleton_2d"];ok &= PackedStringArray(saved_rig.get("active_skins",PackedStringArray())).size()==2 && Dictionary(saved_rig.get("placeholders",Dictionary())).has("body"); }

    skin_name_edit->set_text("empty");slot_action(12);authored=Dictionary(scene->get_entities()[0])["skeleton_2d"];ok &= Dictionary(authored["skins"]).has("empty");
    skin_name_edit->set_text("renamed");slot_action(13);slot_action(2);authored=Dictionary(scene->get_entities()[0])["skeleton_2d"];ok &= !Dictionary(authored["skins"]).has("renamed");ok &= undo->undo();edit_scene(skins_test,nullptr);authored=Dictionary(scene->get_entities()[0])["skeleton_2d"];ok &= Dictionary(authored["skins"]).has("renamed");
    // Preview and frame a non-active skin attachment without changing its visibility or resource.
    Array before_preview=scene->get_entities().duplicate(true);
    select_target(2);
    ok &= local_canvas->frame_selected_attachment();
    Control *swatch=static_cast<SkeletonAttachmentTree *>(hierarchy)->make_attachment_preview(2);
    ok &= swatch!=nullptr;
    if(swatch) { memdelete(swatch); }
    ok &= static_cast<SkeletonAttachmentTree *>(hierarchy)->make_attachment_preview(-1)==nullptr;
    ok &= static_cast<SkeletonAttachmentTree *>(hierarchy)->make_attachment_preview(1)==nullptr;
    ok &= scene->get_entities()==before_preview;
    if(ok) { print_line("SKELETON_ATTACHMENT_PREVIEW_PASS hidden_variant_frame missing_texture non_attachment_no_preview scene_unchanged"); }
    edit_scene(saved,nullptr); set_mode(was_animation?1:0);
    if(ok) { print_line("SKELETON_WORKSPACE_TOOLS_PASS drag_create click_no_create escape_cancel undo filter find_replace regex_error"); }
    return ok;
}
#endif
