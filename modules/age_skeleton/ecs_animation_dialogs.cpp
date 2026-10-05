#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "skeleton_runtime_export.h"
#include "ecs_skeleton_icons.h"
#include "core/io/config_file.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/gui/editor_file_dialog.h"
#include "scene/gui/check_box.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/theme.h"

static String preferences_path() { return EditorPaths::get_singleton()->get_config_dir().path_join("age_skeleton.cfg"); }
static Ref<ConfigFile> read_preferences() {
    Ref<ConfigFile> config;
    config.instantiate();
    if (config->load(preferences_path()) == ERR_FILE_NOT_FOUND) {
        // Read existing settings from older releases; subsequent saves use the new name.
        config->load(EditorPaths::get_singleton()->get_config_dir().path_join("agechaos_skeleton.cfg"));
    }
    return config;
}
static Label *dialog_label(BoxContainer *parent,const String &text) {
    auto *label=memnew(Label); label->set_text(text); parent->add_child(label); return label;
}
static HBoxContainer *dialog_row(BoxContainer *parent,const String &text,Control *control) {
    auto *row=memnew(HBoxContainer); row->add_theme_constant_override("separation",16*EDSCALE); parent->add_child(row);
    auto *label=dialog_label(row,text); label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    control->set_custom_minimum_size(Size2(200,32)*EDSCALE); row->add_child(control); return row;
}
static SpinBox *dialog_number(double minimum,double maximum,double step) {
    auto *number=memnew(SpinBox); number->set_min(minimum); number->set_max(maximum); number->set_step(step); return number;
}
static void apply_skeleton_dialog_theme(ConfirmationDialog *dialog) {
        // A local theme keeps export styling independent of the canvas tool strips.
        Ref<Theme> export_theme; export_theme.instantiate();
        auto surface=[](Color color, int padding) {
            Ref<StyleBoxFlat> box; box.instantiate(); box->set_bg_color(color);
            box->set_corner_radius_all(6*EDSCALE); box->set_content_margin_all(padding*EDSCALE);
            return box;
        };
        export_theme->set_default_font_size(14*EDSCALE);
        export_theme->set_stylebox("panel","AcceptDialog",surface(Color(.145,.155,.17),18));
        export_theme->set_constant("buttons_separation","AcceptDialog",10*EDSCALE);
        export_theme->set_constant("buttons_min_height","AcceptDialog",36*EDSCALE);
        for(const char *type:{"Button","LineEdit","SpinBoxInnerLineEdit","ItemList","Label","CheckBox"}) {
            export_theme->set_font_size("font_size",type,14*EDSCALE);
            export_theme->set_color("font_color",type,Color(.87,.89,.92));
        }
        export_theme->set_stylebox("normal","Button",surface(Color(.23,.25,.28),9));
        export_theme->set_stylebox("hover","Button",surface(Color(.29,.32,.36),9));
        export_theme->set_stylebox("pressed","Button",surface(Color(.17,.34,.45),9));
        for(const char *type:{"LineEdit","SpinBoxInnerLineEdit"}) {
            auto field=surface(Color(.105,.115,.13),9); field->set_border_width_all(1); field->set_border_color(Color(.31,.34,.38));
            export_theme->set_stylebox("normal",type,field);
            auto focus=surface(Color(0,0,0,0),9); focus->set_border_width_all(1); focus->set_border_color(Color(.32,.65,.84)); export_theme->set_stylebox("focus",type,focus);
        }
        for(const char *state:{"normal","hover","pressed","hover_pressed","disabled"}) {
            export_theme->set_stylebox(state,"CheckBox",surface(Color(0,0,0,0),4));
        }
        export_theme->set_stylebox("panel","ItemList",surface(Color(.115,.125,.14),8));
        export_theme->set_constant("v_separation","ItemList",16*EDSCALE);
        export_theme->set_constant("icon_margin","ItemList",10*EDSCALE);
        export_theme->set_stylebox("hovered","ItemList",surface(Color(.20,.23,.27),0));
        for(const char *state:{"selected","selected_focus","hovered_selected","hovered_selected_focus"}) {
            export_theme->set_stylebox(state,"ItemList",surface(Color(.19,.34,.43),0));
        }
        export_theme->set_color("font_selected_color","ItemList",Color(.94,.98,1));
        for (const char *state : {"tab_unselected", "tab_hovered", "tab_selected"}) {
            const bool selected=String(state)=="tab_selected";
            auto tab=surface(selected?Color(.23,.29,.34):String(state)=="tab_hovered"?Color(.20,.22,.25):Color(.16,.175,.19),10);
            tab->set_corner_radius_all(0);
            tab->set_content_margin(SIDE_LEFT,16*EDSCALE); tab->set_content_margin(SIDE_RIGHT,16*EDSCALE);
            tab->set_border_width(SIDE_BOTTOM,2*EDSCALE);
            tab->set_border_color(selected?Color(.32,.65,.84):Color(.26,.28,.31));
            export_theme->set_stylebox(state,"TabBar",tab);
        }
        export_theme->set_font_size("font_size","TabBar",14*EDSCALE);
        export_theme->set_color("font_selected_color","TabBar",Color(.95,.97,1));
        export_theme->set_color("font_unselected_color","TabBar",Color(.68,.72,.78));
        dialog->set_theme(export_theme);
        auto primary=surface(Color(.18,.43,.58),9);
        dialog->get_ok_button()->add_theme_style_override("normal",primary);
        dialog->get_ok_button()->add_theme_style_override("hover",surface(Color(.23,.52,.69),9));
}
void ECSAnimationEditor::preferences_category(int index) { for(int i=0;i<5;i++) { preference_pages[i]->set_visible(i==index); } }
void ECSAnimationEditor::open_preferences() {
    if(!preferences_dialog) {
        preferences_dialog=memnew(ConfirmationDialog); preferences_dialog->set_name("SkeletonPreferences"); preferences_dialog->set_title(String(U"AgeSkeleton 设置")); preferences_dialog->set_ok_button_text(String(U"保存")); add_child(preferences_dialog);
        apply_skeleton_dialog_theme(preferences_dialog);
        preferences_dialog->get_cancel_button()->set_text(String(U"取消")); preferences_dialog->set_hide_on_ok(false);
        preferences_dialog->connect("confirmed",callable_mp(this,&ECSAnimationEditor::save_preferences));
        preferences_dialog->add_button(String(U"恢复默认"),true)->connect("pressed",callable_mp(this,&ECSAnimationEditor::reset_preferences));
        auto *body=memnew(HBoxContainer); body->add_theme_constant_override("separation",22*EDSCALE); preferences_dialog->add_child(body);
        preference_categories=memnew(ItemList); preference_categories->set_custom_minimum_size(Size2(160,280)*EDSCALE); body->add_child(preference_categories);
        const char32_t *names[]={U"应用程序",U"文件",U"画布",U"摄影表",U"权重笔刷"};
        auto *host=memnew(VBoxContainer); host->set_h_size_flags(SIZE_EXPAND_FILL); body->add_child(host);
        for(int i=0;i<5;i++) { preference_categories->add_item(String(names[i]),skeleton_workspace_icon(i==3?"sheet":i==4?"bone":i==2?"mesh":i==1?"attachment":"mode_setup")); auto *page=memnew(VBoxContainer); page->add_theme_constant_override("separation",18*EDSCALE); host->add_child(page); preference_pages[i]=page; auto *title=dialog_label(page,String(names[i])); title->add_theme_font_size_override("font_size",18*EDSCALE); title->add_theme_color_override("font_color",Color(.9,.94,.98)); title->set_custom_minimum_size(Size2(0,38)*EDSCALE); }
        preference_categories->connect("item_selected",callable_mp(this,&ECSAnimationEditor::preferences_category));
        auto *page=Object::cast_to<VBoxContainer>(preference_pages[0]);
        dialog_label(page,String(U"AgeSkeleton · 独立 2D 骨骼动画工具"));
        preference_scale=memnew(OptionButton); for(const char *v:{"自动","75%","100%","125%","150%","175%","200%","自定义（沿用现值）"}) { preference_scale->add_item(String::utf8(v)); }
        dialog_row(page,String(U"界面比例"),preference_scale);
        preference_font=dialog_number(8,48,1); dialog_row(page,String(U"界面字号"),preference_font);
        dialog_label(page,String(U"界面比例与字号保存后，重新打开工具生效。"));
        page=Object::cast_to<VBoxContainer>(preference_pages[1]); preference_directory=memnew(LineEdit); dialog_label(page,String(U"默认导出目录")); auto *directory_row=memnew(HBoxContainer); directory_row->add_theme_constant_override("separation",10*EDSCALE); page->add_child(directory_row); preference_directory->set_h_size_flags(SIZE_EXPAND_FILL); preference_directory->set_custom_minimum_size(Size2(0,36)*EDSCALE); directory_row->add_child(preference_directory); auto *directory_browse=memnew(Button); directory_browse->set_text(String(U"浏览…")); directory_row->add_child(directory_browse); directory_browse->connect("pressed",callable_mp(this,&ECSAnimationEditor::browse_preference_directory)); dialog_label(page,String(U"填写已存在的绝对目录。留空时使用当前工程目录。"));
        page=Object::cast_to<VBoxContainer>(preference_pages[2]); preference_grid=memnew(CheckBox); preference_grid->set_text(String(U"显示原点参考线")); page->add_child(preference_grid);
        page=Object::cast_to<VBoxContainer>(preference_pages[3]); preference_spacing=dialog_number(4,50,1); dialog_row(page,String(U"默认每帧间距（像素）"),preference_spacing); dialog_label(page,String(U"按动画实际帧率换算；滚轮缩放仍可临时调整。"));
        page=Object::cast_to<VBoxContainer>(preference_pages[4]); preference_radius=dialog_number(1,300,1); dialog_row(page,String(U"笔刷半径"),preference_radius); preference_strength=dialog_number(.01,1,.01); dialog_row(page,String(U"笔刷强度"),preference_strength);
        preferences_status=dialog_label(host,String()); preferences_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    }
    Ref<ConfigFile> config=read_preferences();
    preference_scale->select(int(EditorSettings::get_singleton()->get("interface/editor/appearance/display_scale")));
    preference_font->set_value(EditorSettings::get_singleton()->get("interface/editor/fonts/main_font_size"));
    preference_directory->set_text(config->get_value("files","export_directory",""));
    preference_grid->set_pressed(config->get_value("canvas","origin_guides",true));
    preference_spacing->set_value(config->get_value("timeline","frame_spacing",20));
    preference_radius->set_value(config->get_value("brush","radius",60)); preference_strength->set_value(config->get_value("brush","strength",.15));
    preferences_status->set_text(String()); preference_categories->select(0); preferences_category(0); preferences_dialog->popup_centered(Size2(780,420)*EDSCALE);
}
void ECSAnimationEditor::browse_preference_directory() {
    if(!preference_directory_dialog) {
        preference_directory_dialog=memnew(EditorFileDialog); preference_directory_dialog->set_title(String(U"选择默认导出目录")); preference_directory_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM); preference_directory_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR); preferences_dialog->add_child(preference_directory_dialog); preference_directory_dialog->connect("dir_selected",callable_mp(this,&ECSAnimationEditor::preference_directory_chosen));
    }
    String directory=preference_directory->get_text().strip_edges();
    if(directory.is_empty() || !DirAccess::dir_exists_absolute(directory)) { directory=ProjectSettings::get_singleton()->globalize_path("res://"); }
    preference_directory_dialog->set_current_dir(directory); preference_directory_dialog->popup_centered_ratio(.65);
}
void ECSAnimationEditor::preference_directory_chosen(const String &path) { preference_directory->set_text(ProjectSettings::get_singleton()->globalize_path(path)); }
void ECSAnimationEditor::reset_preferences() { preference_scale->select(0); preference_font->set_value(14); preference_directory->clear(); preference_grid->set_pressed(true); preference_spacing->set_value(20); preference_radius->set_value(60); preference_strength->set_value(.15); }
void ECSAnimationEditor::save_preferences() {
    String directory=preference_directory->get_text().strip_edges();
    if(!directory.is_empty() && (!directory.is_absolute_path() || !DirAccess::dir_exists_absolute(directory))) { preferences_status->set_text(String(U"导出目录必须是已存在的绝对路径。")); return; }
    Ref<ConfigFile> config=read_preferences(); config->set_value("files","export_directory",directory); config->set_value("canvas","origin_guides",preference_grid->is_pressed()); config->set_value("timeline","frame_spacing",preference_spacing->get_value()); config->set_value("brush","radius",preference_radius->get_value()); config->set_value("brush","strength",preference_strength->get_value());
    Error error=config->save(preferences_path()); if(error!=OK) { preferences_status->set_text(String(U"设置保存失败：")+itos(error)); return; }
    EditorSettings::get_singleton()->set("interface/editor/appearance/display_scale",preference_scale->get_selected()); EditorSettings::get_singleton()->set("interface/editor/fonts/main_font_size",int(preference_font->get_value())); EditorSettings::save();
    if(export_path && !directory.is_empty()) { export_path->set_text(directory.path_join(export_path->get_text().get_file())); }
    load_workspace_preferences(); preferences_status->set_text(String(U"已保存。画布、摄影表和笔刷已生效；界面比例和字号重启后生效。"));
}
void ECSAnimationEditor::load_workspace_preferences() {
    Ref<ConfigFile> config=read_preferences(); local_canvas->set_grid_visible(config->get_value("canvas","origin_guides",true)); apply_timeline_spacing(config->get_value("timeline","frame_spacing",20)); brush_radius->set_value(config->get_value("brush","radius",60)); brush_strength->set_value(config->get_value("brush","strength",.15));
}
void ECSAnimationEditor::open_asset_export() {
    if(scene.is_null()) { feedback->set_text(String(U"请先打开骨骼工程。")); return; }
    stop_preview();
    if(!asset_export_dialog) {
        asset_export_dialog=memnew(ConfirmationDialog); asset_export_dialog->set_name("SkeletonExport"); asset_export_dialog->set_title(String(U"导出")); asset_export_dialog->set_ok_button_text(String(U"导出")); asset_export_dialog->get_cancel_button()->set_text(String(U"取消")); asset_export_dialog->set_hide_on_ok(false); add_child(asset_export_dialog); asset_export_dialog->connect("confirmed",callable_mp(this,&ECSAnimationEditor::perform_asset_export));
        apply_skeleton_dialog_theme(asset_export_dialog);
        auto *body=memnew(HBoxContainer); body->add_theme_constant_override("separation",22*EDSCALE); asset_export_dialog->add_child(body);
        export_formats=memnew(ItemList); export_formats->set_custom_minimum_size(Size2(160,280)*EDSCALE); body->add_child(export_formats);
        export_formats->add_item(TTR("Engine runtime"),skeleton_workspace_icon("bone")); for(const char *format:{"PNG","JPEG","WebP","PNG 序列帧","精灵图集","GIF 动画","AVI 视频"}) { export_formats->add_item(String::utf8(format),skeleton_workspace_icon("attachment")); }
        export_formats->connect("item_selected",callable_mp(this,&ECSAnimationEditor::export_format_changed));
        auto *options=memnew(VBoxContainer); options->add_theme_constant_override("separation",10*EDSCALE); options->set_h_size_flags(SIZE_EXPAND_FILL); body->add_child(options);
        export_engine_tabs=memnew(TabBar);export_engine_tabs->set_name("ExportEngineTabs");export_engine_tabs->set_tab_alignment(TabBar::ALIGNMENT_LEFT);options->add_child(export_engine_tabs);
        for(const char *engine:{"AgeChaos","Godot","Unreal","Unity","Cocos"}) export_engine_tabs->add_tab(engine);
        export_engine_tabs->set_current_tab(0);
        export_engine_tabs->connect("tab_changed",callable_mp(this,&ECSAnimationEditor::export_engine_changed));
        export_description=dialog_label(options,String()); export_description->set_custom_minimum_size(Size2(470,64)*EDSCALE); export_description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART); export_description->add_theme_color_override("font_color",Color(.68,.73,.79));
        export_path=memnew(LineEdit); export_path->set_name("ExportPath"); export_path_label=dialog_label(options,String(U"输出文件")); auto *path_row=memnew(HBoxContainer); path_row->add_theme_constant_override("separation",10*EDSCALE); options->add_child(path_row); path_row->add_child(export_path); export_path->set_custom_minimum_size(Size2(0,36)*EDSCALE); export_path->set_h_size_flags(SIZE_EXPAND_FILL); auto *browse=memnew(Button); browse->set_text(String(U"浏览…")); path_row->add_child(browse); browse->connect("pressed",callable_mp(this,&ECSAnimationEditor::browse_export_file));
        auto *image_options=memnew(VBoxContainer); image_options->add_theme_constant_override("separation",12*EDSCALE); options->add_child(image_options); export_image_options=image_options;
        export_scale=dialog_number(10,400,1); export_scale->set_suffix("%"); dialog_row(image_options,String(U"大小缩放"),export_scale);
        export_crop=memnew(CheckBox); export_crop->set_text(String(U"裁去透明边缘")); image_options->add_child(export_crop);
        export_alpha=memnew(CheckBox); export_alpha->set_text(String(U"透明背景（视频不支持）")); image_options->add_child(export_alpha);
        export_background=memnew(ColorPickerButton); export_background->set_edit_alpha(false); dialog_row(image_options,String(U"实色背景"),export_background);
        export_quality=dialog_number(1,100,1); dialog_row(image_options,String(U"JPEG 质量"),export_quality);
        auto *motion=memnew(VBoxContainer); motion->add_theme_constant_override("separation",12*EDSCALE); options->add_child(motion); export_motion_options=motion;
        export_animation=memnew(OptionButton); dialog_row(motion,String(U"动画"),export_animation);
        export_fps=dialog_number(1,60,1); export_fps->set_value(30); auto *fps_row=memnew(HBoxContainer); fps_row->add_theme_constant_override("separation",14*EDSCALE); motion->add_child(fps_row);
        dialog_label(fps_row,String(U"导出帧率"))->set_custom_minimum_size(Size2(100,0)*EDSCALE);
        export_fps->set_custom_minimum_size(Size2(120,32)*EDSCALE); fps_row->add_child(export_fps);
        asset_export_dialog->connect("canceled",callable_mp(this,&ECSAnimationEditor::cancel_motion_export));
        export_open=memnew(CheckBox); export_open->set_text(String(U"导出后打开所在目录")); options->add_child(export_open);
        export_status=dialog_label(options,String()); export_status->set_custom_minimum_size(Size2(470,24)*EDSCALE); export_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART); export_status->set_max_lines_visible(3); export_status->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        auto *action_space=memnew(Control); action_space->set_v_size_flags(SIZE_EXPAND_FILL); options->add_child(action_space);
        auto *footer=memnew(HBoxContainer); footer->set_name("ExportPageActions"); footer->add_theme_constant_override("separation",8*EDSCALE); options->add_child(footer);
        auto *reset=memnew(Button); reset->set_text(TTR("Reset to Defaults")); footer->add_child(reset);
        reset->connect("pressed",callable_mp(this,&ECSAnimationEditor::reset_asset_export));
        export_preview_button=memnew(Button); export_preview_button->set_text(TTR("Preview")); footer->add_child(export_preview_button);
        export_preview_button->connect("pressed",callable_mp(this,&ECSAnimationEditor::preview_asset_export));
        footer->add_spacer();
        // Place actions inside the current page; retain the dialog's existing confirm/cancel signals.
        auto *dialog_footer=Object::cast_to<Control>(asset_export_dialog->get_ok_button()->get_parent());
        asset_export_dialog->get_cancel_button()->reparent(footer,false);
        asset_export_dialog->get_ok_button()->reparent(footer,false);
        dialog_footer->hide(); asset_export_dialog->add_theme_constant_override("buttons_separation",0);
        for(Button *button:{reset,export_preview_button,asset_export_dialog->get_cancel_button(),asset_export_dialog->get_ok_button()}) {
            button->set_custom_minimum_size(Size2(96,36)*EDSCALE); button->set_h_size_flags(SIZE_SHRINK_BEGIN);
        }
        export_file_dialog=memnew(EditorFileDialog); export_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM); export_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE); asset_export_dialog->add_child(export_file_dialog); export_file_dialog->connect("file_selected",callable_mp(this,&ECSAnimationEditor::export_file_chosen)); export_file_dialog->connect("dir_selected",callable_mp(this,&ECSAnimationEditor::export_file_chosen));
        reset_asset_export();
        Ref<ConfigFile> config=read_preferences();
        export_scale->set_value(config->get_value("export","scale",100)); export_quality->set_value(config->get_value("export","quality",90)); export_crop->set_pressed(config->get_value("export","crop",true)); export_alpha->set_pressed(config->get_value("export","alpha",true)); export_background->set_pick_color(config->get_value("export","background",Color(.2,.2,.2))); export_open->set_pressed(config->get_value("export","open",false));
    }
    export_animation->clear();
    for(int i=0;i<animation_list->get_item_count();i++) { export_animation->add_item(animation_list->get_item_text(i)); export_animation->set_item_metadata(i,animation_list->get_item_metadata(i)); if(String(animation_list->get_item_metadata(i))==editing_state) { export_animation->select(i); } }
    export_formats->select(0); export_format_changed(0); asset_export_dialog->popup_centered(Size2(780,420)*EDSCALE);
}
void ECSAnimationEditor::reset_asset_export() { if(motion_exporting) { return; } if(export_fps) { export_fps->set_value(30); } export_scale->set_value(100); export_quality->set_value(90); export_crop->set_pressed(true); export_alpha->set_pressed(true); export_background->set_pick_color(Color(.2,.2,.2)); export_open->set_pressed(false); }
void ECSAnimationEditor::export_engine_changed(int p_engine) {
    export_engine_paths[export_engine_index]=export_path->get_text();export_engine_index=p_engine;
    export_format_changed(0);
    if(!export_engine_paths[p_engine].is_empty()) export_path->set_text(export_engine_paths[p_engine]);
}
int ECSAnimationEditor::selected_export_format() const {
    int index=export_formats->get_selected_items()[0];return index==0 && export_engine_tabs->get_current_tab()>0?8:index;
}
void ECSAnimationEditor::export_format_changed(int index) {
    export_engine_tabs->set_visible(index==0);
    export_preview_button->set_visible(index!=0);
    if(index==0 && export_engine_tabs->get_current_tab()>0) index=8;
    if(index==8) {
        export_path_label->set_text(TTR("New runtime package folder")); export_image_options->hide(); export_motion_options->show();
        Object::cast_to<Control>(export_animation->get_parent())->hide();
        String engine=export_engine_tabs->get_tab_title(export_engine_tabs->get_current_tab());
        String detail;
        switch(export_engine_tabs->get_current_tab()) {
            case 1: detail=TTR("Godot 4.4+: use the C++ GDExtension and imported atlas textures.");break;
            case 2: detail=TTR("Unreal Engine 5: import Texture2D pages and use the C++ plugin with a translucent material.");break;
            case 3: detail=TTR("Unity 2022.3 / 6: import atlas textures with the UPM runtime, or use Unity Sprite Atlas.");break;
            case 4: detail=TTR("Cocos Creator 3.8: use the TypeScript component with textures or a native SpriteAtlas (no trim or rotation).");break;
        }
        export_description->set_text(detail+"\n"+TTR("Exports all clips and skins with PNG atlases, animation crossfades, runtime IK targets and animation events. Choose a new folder."));
        String name=scene->get_path().get_file().get_basename().trim_suffix(".ecsrig");
        export_path->set_text(ProjectSettings::get_singleton()->globalize_path(scene->get_path().get_base_dir().path_join(name+"_"+engine.to_lower()))); export_status->set_text(String());return;
    }
    Object::cast_to<Control>(export_animation->get_parent())->show();
    const char *extensions[]={"res","png","jpg","webp","json","png","gif","avi"};
    export_path_label->set_text(index==0?String(U"动画资源文件"):String(U"输出文件"));
    export_image_options->set_visible(index!=0); export_alpha->set_disabled(index==2 || index==7); export_crop->set_disabled(index>=4); export_motion_options->set_visible(index>=4); export_quality->set_editable(index==2); Object::cast_to<Control>(export_quality->get_parent())->set_visible(index==2); export_crop->set_visible(index<4);
    export_description->set_text(index==0?TTR("Native support"):String(U"当前视口姿态 · 导出可见图片，不包含参考线、骨骼线和操作控件。\n视口以外的内容不会导出；WebP 使用无损编码。"));
    if(index>=4) { export_description->set_text(String(U"按所选动画逐帧导出，保持视口尺寸与原点一致；不裁切单帧。\n序列帧 / 图集附带 JSON；GIF 循环播放；AVI 为无音轨 MJPEG 视频。")); }
    if(index==0) {
        String name=scene->get_path().get_file().get_basename().trim_suffix(".ecsrig");if(name.is_empty()) name="Skeleton";
        export_path->set_text(export_engine_paths[0].is_empty()?ProjectSettings::get_singleton()->globalize_path(scene->get_path().get_base_dir().path_join(name+"_agechaos.res")):export_engine_paths[0]);export_status->set_text(String());return;
    }
    String base=export_path->get_text(); if(base.is_empty()) { String directory=read_preferences()->get_value("files","export_directory",""); if(directory.is_empty()) { directory=scene->get_path().get_base_dir(); } if(directory.is_empty()) { directory="res://"; } String name=scene->get_path().get_file().get_basename().trim_suffix(".ecsrig"); if(name.is_empty()) { name="Skeleton"; } base=ProjectSettings::get_singleton()->globalize_path(directory).path_join(name+"_export"); } else { base=base.get_basename(); }
    export_path->set_text(ProjectSettings::get_singleton()->globalize_path(base+"."+extensions[index])); export_status->set_text(String());
}
void ECSAnimationEditor::browse_export_file() { if(motion_exporting) { return; } int index=selected_export_format(); if(index==8) { export_file_dialog->clear_filters();export_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);export_file_dialog->set_current_dir(export_path->get_text().get_base_dir());export_file_dialog->popup_centered_ratio(.7);return; } export_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE); const char *extensions[]={"*.res","*.png","*.jpg","*.webp","*.json","*.png","*.gif","*.avi"}; export_file_dialog->clear_filters(); export_file_dialog->add_filter(extensions[index]); export_file_dialog->set_current_path(export_path->get_text()); export_file_dialog->popup_centered_ratio(.7); }
void ECSAnimationEditor::export_file_chosen(const String &path) { export_path->set_text(selected_export_format()==8?path.path_join("Skeleton_runtime"):path); }
Ref<Image> ECSAnimationEditor::prepare_export_image() {
    Ref<Image> image=local_canvas->capture_canvas(); if(image.is_null() || image->is_empty()) { export_status->set_text(String(U"画布尚未完成渲染。")); return Ref<Image>(); }
    image=image->duplicate(); image->convert(Image::FORMAT_RGBA8);
    if(export_crop->is_pressed() && !motion_exporting) { Rect2i rect=image->get_used_rect(); if(rect.size.x<=0 || rect.size.y<=0) { export_status->set_text(String(U"当前视口没有可导出的图片。")); return Ref<Image>(); } image=image->get_region(rect); }
    int width=MAX(1,int(Math::round(image->get_width()*export_scale->get_value()/100.0))),height=MAX(1,int(Math::round(image->get_height()*export_scale->get_value()/100.0)));
    if(int64_t(width)*height>64000000 || width>16384 || height>16384) { export_status->set_text(String(U"导出图片过大，请降低缩放。")); return Ref<Image>(); }
    image->resize(width,height,Image::INTERPOLATE_LANCZOS);
    if(!export_alpha->is_pressed() || (selected_export_format()==2 || selected_export_format()==7)) { Ref<Image> background=Image::create_empty(width,height,false,Image::FORMAT_RGBA8); background->fill(export_background->get_pick_color()); background->blend_rect(image,Rect2i(0,0,width,height),Point2i()); image=background; }
    return image;
}
void ECSAnimationEditor::preview_asset_export() {
    if(motion_exporting) { return; }
    const int format=selected_export_format();
    if(format==8) { export_status->set_text(TTR("Preview this package in the target engine using its AgeSkeleton runtime."));return; }
    if(format>=4) {
        String path=export_path->get_text();
        if(!FileAccess::exists(path)) { export_status->set_text(String(U"请先导出，再预览生成的动画或图集。")); return; }
        if(format==4) { OS::get_singleton()->shell_show_in_file_manager(path); }
        else { Error result=OS::get_singleton()->shell_open(path); if(result!=OK) { export_status->set_text(String(U"未找到该格式的查看程序，请从导出目录打开。")); } }
        return;
    }
    if(selected_export_format()==0) { export_status->set_text(String(U"资源包含 ")+itos(scene->get_entities().size())+String(U" 个实体；图片预览请先选择图片格式。")); return; }
    Ref<Image> image=prepare_export_image(); if(image.is_null()) { return; }
    if(export_preview_dialog) { export_preview_dialog->queue_free(); }
    export_preview_dialog=memnew(ConfirmationDialog); export_preview_dialog->set_title(vformat(String(U"导出预览 · %d × %d"),image->get_width(),image->get_height())); asset_export_dialog->add_child(export_preview_dialog);
    auto *texture=memnew(TextureRect); texture->set_texture(ImageTexture::create_from_image(image)); texture->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE); texture->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED); texture->set_custom_minimum_size(Size2(600,400)*EDSCALE); export_preview_dialog->add_child(texture); export_preview_dialog->popup_centered();
}
void ECSAnimationEditor::confirm_asset_overwrite() { confirmed_export_path=pending_export_path; perform_asset_export(); }
void ECSAnimationEditor::perform_asset_export() {
    if(motion_exporting) { return; }
    if(selected_export_format()==8) {
        Dictionary result=export_skeleton_runtime(scene,find_rig(target->get_selected_id()),export_path->get_text().strip_edges(),int(export_fps->get_value()));
        export_status->set_text(bool(result.get("ok",false))?TTR("Runtime package exported")+String(": ")+String(result["path"]):TTR("Runtime export failed")+String(": ")+String(result.get("error",String())));
        if(bool(result.get("ok",false)) && export_open->is_pressed()) { OS::get_singleton()->shell_show_in_file_manager(result["path"]); }return;
    }
    String error;
    String path=export_path->get_text().strip_edges(); int index=selected_export_format(); const char *extensions[]={"res","png","jpg","webp","json","png","gif","avi"};
    if(path.is_empty() || path.get_extension().to_lower()!=extensions[index]) { export_status->set_text(String(U"输出文件扩展名应为 .")+extensions[index]); return; }
    if(!DirAccess::dir_exists_absolute(path.get_base_dir())) { export_status->set_text(String(U"输出目录不存在，请先选择已有目录。")); return; }
    if((FileAccess::exists(path) || (index==5 && FileAccess::exists(path+".json"))) && confirmed_export_path!=path) {
        if(!export_overwrite_dialog) { export_overwrite_dialog=memnew(ConfirmationDialog); export_overwrite_dialog->set_title(String(U"确认覆盖")); asset_export_dialog->add_child(export_overwrite_dialog); export_overwrite_dialog->connect("confirmed",callable_mp(this,&ECSAnimationEditor::confirm_asset_overwrite)); }
        pending_export_path=path; export_overwrite_dialog->set_text(String(U"文件已存在，是否覆盖？\n")+path); export_overwrite_dialog->popup_centered(); return;
    }
    confirmed_export_path=String();
    if(index>=4) { start_motion_export(path,index); return; }
    Ref<ConfigFile> config=read_preferences(); config->set_value("export","scale",export_scale->get_value()); config->set_value("export","quality",export_quality->get_value()); config->set_value("export","crop",export_crop->is_pressed()); config->set_value("export","alpha",export_alpha->is_pressed()); config->set_value("export","background",export_background->get_pick_color()); config->set_value("export","open",export_open->is_pressed()); config->save(preferences_path());
    if(index==0) { project_operation=8; project_file_selected(path); export_status->set_text(feedback->get_text()); if(export_open->is_pressed() && feedback->get_text().begins_with(String(U"骨骼资源已导出"))) { OS::get_singleton()->shell_show_in_file_manager(path); } return; }
    Ref<Image> image=prepare_export_image(); if(image.is_null()) { return; }
    Error result=index==1?image->save_png(path):index==2?image->save_jpg(path,export_quality->get_value()/100.0):image->save_webp(path,false);
    export_status->set_text(result==OK?String(U"已导出：")+path:String(U"导出失败：")+itos(result));
    if(result==OK && export_open->is_pressed()) { OS::get_singleton()->shell_show_in_file_manager(path); }
}
bool skeleton_image_codec_self_test() {
    Ref<Image> source=Image::create_empty(16,16,false,Image::FORMAT_RGBA8); source->fill(Color(0,0,0,0));
    for(int y=4;y<12;y++) { for(int x=4;x<12;x++) { source->set_pixel(x,y,Color(.2,.6,.9,1)); } }
    bool ok=source->get_used_rect()==Rect2i(4,4,8,8);
    Vector<uint8_t> buffers[]={source->save_png_to_buffer(),source->save_jpg_to_buffer(.9),source->save_webp_to_buffer(false)};
    for(int index=0;index<3;index++) {
        Ref<Image> decoded; decoded.instantiate();
        Error error=index==0?decoded->load_png_from_buffer(buffers[index]):index==1?decoded->load_jpg_from_buffer(buffers[index]):decoded->load_webp_from_buffer(buffers[index]);
        ok &= !buffers[index].is_empty() && error==OK && decoded->get_size()==Vector2i(16,16);
        if(error==OK && index!=1) { ok &= decoded->get_pixel(0,0).a==0 && decoded->get_pixel(8,8).a==1; }
    }
    print_line(ok?"SKELETON_IMAGE_CODECS_PASS png jpeg webp crop alpha":"SKELETON_IMAGE_CODECS_FAIL"); return ok;
}
#endif
