#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "skeleton_workspace_docking.h"
#include "ecs_skeleton_icons.h"
#include "ecs_timeline_layout.h"
#include "ecs_spine_import.h"
#include "ecs_ai_value.h"
#include "core/io/file_access.h"
#include "core/input/shortcut.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "core/object/callable_mp.h"
#include "core/math/geometry_2d.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/button.h"
#include "scene/gui/split_container.h"
#include "scene/gui/foldable_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/item_list.h"
#include "editor/editor_node.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"
#include "scene/gui/check_box.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/slider.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/texture_rect.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/dialogs.h"
#include "editor/gui/editor_file_dialog.h"
class ECSAnimationTimeline : public Control {
	GDCLASS(ECSAnimationTimeline, Control);
	ECSAnimationEditor *editor=nullptr;
	double zoom=1200, offset=0, drag_time=0;
	int row_offset=0;
    HScrollBar *time_scroll=nullptr;
    bool panning=false;
    void scroll_time(double value) { offset=MAX(0.0,value); queue_redraw(); }
    float time_bar_height() const { return time_scroll->get_combined_minimum_size().y; }
    void update_time_scroll() {
        const double page=MAX(1.0,double(logical_size().x-280-row_scroll->get_combined_minimum_size().x/EDSCALE))/zoom;
        Ref<Animation> clip=editor->current_clip();
        double end=MAX(page*2,MAX(clip.is_valid()?clip->get_length()+page:page*2,offset+page));
        time_scroll->set_block_signals(true);
        time_scroll->set_step(1.0/editor->timeline_fps); time_scroll->set_max(end); time_scroll->set_page(page); time_scroll->set_value(offset);
        time_scroll->set_block_signals(false);
        time_scroll->set_position(Vector2(280*EDSCALE,get_size().y-time_bar_height()));
        time_scroll->set_size(Vector2(MAX(0.0f,get_size().x-280*EDSCALE-row_scroll->get_combined_minimum_size().x),time_bar_height()));
    }
	VScrollBar *row_scroll=nullptr;
	void scroll_rows(double value) { row_offset=int(value); queue_redraw(); }
	bool dragging=false, scrubbing=false;
	Vector2i drag_key=Vector2i(-1,-1);
    HashSet<String> folded;
    SkeletonTimelineLayout layout;
    HashMap<String, Ref<Texture2D>> icons;
    Ref<Texture2D> key_glyph, overview_key_glyph;
    float key_glyph_scale=0;
    void draw_key_glyph(const Vector2 &center, const Color &color, bool overview=false) {
        if(key_glyph_scale!=EDSCALE) {
            // Rasterize once at the actual display density, with transparent padding
            // for antialiased edges. Do not magnify a low-resolution polygon mask.
            Ref<Image> image; image.instantiate();
            image->load_svg_from_string("<svg xmlns='http://www.w3.org/2000/svg' width='12' height='16'><path d='M6 1L11 8L6 15L1 8Z' fill='white'/></svg>",EDSCALE);
            key_glyph=ImageTexture::create_from_image(image);
            image.instantiate();
            image->load_svg_from_string("<svg xmlns='http://www.w3.org/2000/svg' width='9' height='10'><path d='M4.5 1L8 5L4.5 9L1 5Z' fill='white'/></svg>",EDSCALE);
            overview_key_glyph=ImageTexture::create_from_image(image);
            key_glyph_scale=EDSCALE;
        }
        Ref<Texture2D> glyph=overview?overview_key_glyph:key_glyph;
        Vector2 size=glyph->get_size()/EDSCALE;
        // Align the texture to physical pixels even at fractional UI scales or times.
        Vector2 origin=((center-size*.5)*EDSCALE).round()/EDSCALE;
        draw_texture_rect(glyph,Rect2(origin,size),false,color);
    }
    int visible_rows() const { return MAX(1, int((logical_size().y-62-time_bar_height()/EDSCALE)/26)); }
    void rebuild_rows() {
        Array entities=editor->scene.is_valid()?editor->scene->get_entities():Array();
        int owner=editor->owner->get_selected_id();
        Dictionary animation=owner>=0 && owner<entities.size()?Dictionary(Dictionary(entities[owner]).get("animation",Dictionary())):Dictionary();
        layout.build(editor->current_clip(),animation.get("targets",PackedInt64Array()),entities,owner,folded);
        row_offset=CLAMP(row_offset,0,MAX(0,layout.rows.size()-visible_rows()));
        row_scroll->set_block_signals(true);
        row_scroll->set_max(MAX(visible_rows(),layout.rows.size()));
        row_scroll->set_page(visible_rows());
        row_scroll->set_value(row_offset);
        row_scroll->set_block_signals(false);
        row_scroll->set_visible(layout.rows.size()>visible_rows());
        row_scroll->set_position(Vector2(get_size().x-row_scroll->get_combined_minimum_size().x,62*EDSCALE));
        row_scroll->set_size(Vector2(row_scroll->get_combined_minimum_size().x,MAX(0.0f,get_size().y-62*EDSCALE-time_bar_height())));
    }
    Color channel_color(const String &path) const {
        String field=path.begins_with("slot:")?path.get_slice(":",2):path;
        if(field=="position") { return Color(.48,.80,.86); }
        if(field=="rotation") { return Color(.5,.82,.5); }
        if(field=="scale") { return Color(.87,.55,.60); }
        if(field=="shear") { return Color(.94,.65,.3); }
        if(field=="color" || field=="dark") { return Color("de78c8"); }
        if(field=="attachment") { return Color("83d8d6"); }
        return Color("eab77e");
    }
    void draw_keys(const Ref<Animation> &clip,int track,float y,bool summary,HashSet<int> &pixels) {
        int begin=MAX(0,clip->track_find_key(track,offset,Animation::FIND_MODE_NEAREST));
        for(int key=begin;key<clip->track_get_key_count(track);++key) {
            double time=dragging && drag_key==Vector2i(track,key)?drag_time:clip->track_get_key_time(track,key);
            if(time<offset) { continue; }
            float x=280+(time-offset)*zoom;
            if(x>logical_size().x) { if(!dragging) { break; } continue; }
            int pixel=Math::round(x);
            bool selected=editor->selected_key==Vector2i(track,key);
            if(pixels.has(pixel) && !selected) { continue; }
            pixels.insert(pixel);
            Color color=selected?Color(1,1,1):summary?Color(.87,.90,.92):channel_color(clip->track_get_path(track).get_concatenated_subnames());
            draw_key_glyph(Vector2(x,y+13),color);
        }
    }
	Vector2 logical_size() const { return get_size()/EDSCALE; }
	double at(float x) const { return MAX(0.0, Math::snapped((x-280)/zoom+offset,1.0/editor->timeline_fps)); }
protected:
	static void _bind_methods() {}
	void _notification(int what) {
		if(what!=NOTIFICATION_DRAW) { return; }
		draw_set_transform(Vector2(),0,Vector2(EDSCALE,EDSCALE));
		draw_rect(Rect2(Vector2(),logical_size()),Color(.18,.18,.18));
		draw_rect(Rect2(0,0,logical_size().x,36),Color(.30,.30,.30));
		Ref<Font> font=get_theme_default_font(); int fs=13;
		Color text=Color(.86,.87,.89), accent=Color(.48,.80,.86);
		Ref<Animation> clip=editor->current_clip();
		double step=5.0/editor->timeline_fps; while(step*zoom<40) { step*=2; }
		for(double t=Math::ceil(offset/step)*step;t<offset+MAX(0.0,double(logical_size().x-280))/zoom;t+=step) {
			float x=280+(t-offset)*zoom; draw_line(Vector2(x,24),Vector2(x,logical_size().y),Color(.4,.4,.4,.3));
			draw_string(font,Vector2(x+3,18),itos(Math::round(t*editor->timeline_fps)),HORIZONTAL_ALIGNMENT_LEFT,-1,fs,text);
		}
		for(double t=Math::ceil(offset*editor->timeline_fps)/editor->timeline_fps;t<offset+MAX(0.0,double(logical_size().x-280))/zoom;t+=1.0/editor->timeline_fps) {
			float tick=280+(t-offset)*zoom; draw_line(Vector2(tick,26),Vector2(tick,27),Color(.55,.55,.55));
		}
        // Animation overview: aggregate all track times, independent of collapsed groups.
        draw_rect(Rect2(0,36,logical_size().x,26),Color(.24,.34,.35));
        draw_line(Vector2(0,61),Vector2(logical_size().x,61),Color(.14,.19,.20));
        draw_string(font,Vector2(10,54),editor->editing_state.is_empty()?String(U"默认动画"):editor->editing_state,HORIZONTAL_ALIGNMENT_LEFT,260,13,Color(.96,.98,.98));
        if(clip.is_valid()) {
            HashSet<int> overview_pixels,diamond_pixels;
            for(int t=0;t<clip->get_track_count();t++) {
                draw_keys(clip,t,36,true,overview_pixels);
                for(int k=MAX(0,clip->track_find_key(t,offset,Animation::FIND_MODE_NEAREST));k<clip->track_get_key_count(t);k++) {
                    double at=clip->track_get_key_time(t,k); float x=280+(at-offset)*zoom;
                    if(x<280) { continue; } if(x>logical_size().x) { break; }
                    int bucket=int(Math::round(x/7.0)); if(diamond_pixels.has(bucket)) { continue; } diamond_pixels.insert(bucket);
                    draw_key_glyph(Vector2(x,30),Color(.94,.65,.3),true);
                }
            }
        }
        rebuild_rows(); update_time_scroll();
        for(int r=row_offset;r<layout.rows.size();++r) {
            float y=62+(r-row_offset)*26; if(y>=logical_size().y) { break; }
            const SkeletonTimelineRow &row=layout.rows[r];
            const SkeletonTimelineGroup &group=layout.groups[row.group];
            bool summary=row.track<0;
            bool selected=!summary && editor->selected_key.x==row.track;
            draw_rect(Rect2(0,y,logical_size().x,26),selected?Color(.28,.42,.44):summary?Color(.28,.28,.28):r%2?Color(.23,.23,.23):Color(.30,.30,.30));
            draw_line(Vector2(0,y+25),Vector2(logical_size().x,y+25),summary?Color(.12,.15,.16):Color(.19,.20,.21));
            if(summary) {
                bool closed=folded.has(group.id);
                PackedVector2Array arrow=closed?PackedVector2Array({Vector2(8,y+6),Vector2(13,y+11),Vector2(8,y+16)}):PackedVector2Array({Vector2(6,y+8),Vector2(16,y+8),Vector2(11,y+14)});
                draw_colored_polygon(arrow,Color(.68,.73,.73));
                if(!icons.has(group.icon)) { icons[group.icon]=skeleton_workspace_icon(group.icon); }
                draw_texture_rect(icons[group.icon],Rect2(23,y+3,16,16),false);
                draw_string(font,Vector2(48,y+18),group.label,HORIZONTAL_ALIGNMENT_LEFT,224,fs,Color(.83,.87,.87));
            } else {
                String field=clip->track_get_path(row.track).get_concatenated_subnames(); field=field.begins_with("slot:")?field.get_slice(":",2):field;
                String icon_key=field.begins_with("event:")?String("sheet"):field;
                if(!icons.has(icon_key)) { icons[icon_key]=skeleton_workspace_icon(icon_key); }
                draw_texture_rect(icons[icon_key],Rect2(26,y+5,16,16),false);
                draw_string(font,Vector2(48,y+18),SkeletonTimelineLayout::channel_label(clip->track_get_path(row.track).get_concatenated_subnames()),HORIZONTAL_ALIGNMENT_LEFT,224,fs,text);
            }
        }
        // Draw each guide once, above row fills but below key glyphs.
        for(double t=Math::ceil(offset/step)*step;t<offset+MAX(0.0,double(logical_size().x-280))/zoom;t+=step) {
            float x=280+(t-offset)*zoom; draw_line(Vector2(x,62),Vector2(x,logical_size().y),Color(.5,.5,.5,.22));
        }
        for(int r=row_offset;r<layout.rows.size();++r) {
            float y=62+(r-row_offset)*26; if(y>=logical_size().y) { break; }
            const SkeletonTimelineRow &row=layout.rows[r];
            HashSet<int> pixels;
            if(row.track<0) { for(int track:layout.groups[row.group].tracks) { draw_keys(clip,track,y,true,pixels); } }
            else { draw_keys(clip,row.track,y,false,pixels); }
        }
		float cursor=280+(editor->time->get_value()-offset)*zoom;
		if(cursor>=280 && cursor<logical_size().x) { draw_line(Vector2(cursor,26),Vector2(cursor,logical_size().y),accent,1); draw_colored_polygon(PackedVector2Array({Vector2(cursor-5,20),Vector2(cursor+5,20),Vector2(cursor,27)}),accent); }
		draw_line(Vector2(278,0),Vector2(278,logical_size().y),Color(.4,.4,.4));
        draw_rect(Rect2(0,logical_size().y-time_bar_height()/EDSCALE,logical_size().x,time_bar_height()/EDSCALE),Color(.14,.15,.16));
	}
	void gui_input(const Ref<InputEvent> &event) override {
		Ref<InputEventMouseButton> b=event;
        if(b.is_valid()) {
            if(b->get_button_index()==MouseButton::MIDDLE) { panning=b->is_pressed(); dragging=false; scrubbing=false; if(panning) { grab_focus(); } accept_event(); return; }
            if(b->is_pressed() && (b->get_button_index()==MouseButton::WHEEL_LEFT || b->get_button_index()==MouseButton::WHEEL_RIGHT)) {
                scroll_time(offset+(b->get_button_index()==MouseButton::WHEEL_LEFT?-1:1)*80.0*MAX(.1,double(b->get_factor()))/zoom); accept_event(); return;
            }
			if(b->is_pressed() && (b->get_button_index()==MouseButton::WHEEL_UP || b->get_button_index()==MouseButton::WHEEL_DOWN)) {
				int direction=b->get_button_index()==MouseButton::WHEEL_UP?-1:1;
				if(b->is_ctrl_pressed()) { double x=MAX(0.0,double(b->get_position().x/EDSCALE-280)); double anchor=offset+x/zoom; zoom=CLAMP(zoom*(direction<0?1.2:1/1.2),30.0,3200.0); offset=MAX(0.0,anchor-x/zoom); }
				else if(b->is_shift_pressed()) { scroll_time(offset+direction*80.0*MAX(.1,double(b->get_factor()))/zoom); }
				else { rebuild_rows(); row_offset=CLAMP(row_offset+direction*MAX(1,int(Math::round(b->get_factor()))),0,MAX(0,layout.rows.size()-visible_rows())); }
				queue_redraw(); accept_event(); return;
			}
			if(b->get_button_index()!=MouseButton::LEFT) { return; }
			if(!b->is_pressed()) {
				if(dragging) { editor->move_key(drag_key.x,drag_key.y,drag_time); }
				dragging=false; scrubbing=false; queue_redraw(); accept_event(); return;
			}
			Ref<Animation> clip=editor->current_clip(); Vector2 p=b->get_position()/EDSCALE;
            rebuild_rows();
            int row_index=p.y>=62?int((p.y-62)/26)+row_offset:-1;
            if(p.x<280) {
                if(row_index>=0 && row_index<layout.rows.size()) {
                    const SkeletonTimelineRow &row=layout.rows[row_index];
                    if(row.track<0) {
                        String id=layout.groups[row.group].id;
                        if(folded.has(id)) { folded.erase(id); } else { folded.insert(id); }
                        rebuild_rows(); queue_redraw();
                    } else {
                        editor->selected_key=Vector2i(row.track,-1); queue_redraw();
                    }
                    accept_event();
                }
                return;
            }
            int track=row_index>=0 && row_index<layout.rows.size()?layout.rows[row_index].track:-1;
            if(clip.is_valid() && track>=0) {
                int nearest=-1; double distance=9;
                for(int key=0;key<clip->track_get_key_count(track);++key) {
                    double next=Math::abs(280+(clip->track_get_key_time(track,key)-offset)*zoom-p.x);
                    if(next<distance) { nearest=key; distance=next; }
                }
                if(nearest>=0) {
                    drag_key=Vector2i(track,nearest); drag_time=clip->track_get_key_time(track,nearest); dragging=!editor->timeline_locked;
                    editor->select_key(track,nearest); accept_event(); return;
                }
            }
			scrubbing=true; editor->seek(at(p.x)); accept_event();
		}
		Ref<InputEventMouseMotion> m=event;
        if(m.is_valid() && panning) { scroll_time(offset-m->get_relative().x/(EDSCALE*zoom)); accept_event(); return; }
        Ref<InputEventPanGesture> pan=event;
        if(pan.is_valid()) { scroll_time(offset+pan->get_delta().x*40.0/zoom); rebuild_rows(); row_offset=CLAMP(row_offset+int(Math::round(pan->get_delta().y)),0,MAX(0,layout.rows.size()-visible_rows())); queue_redraw(); accept_event(); return; }
		if(m.is_valid() && (dragging || scrubbing)) { if(dragging) { drag_time=at(m->get_position().x/EDSCALE); queue_redraw(); } else { editor->seek(at(m->get_position().x/EDSCALE)); } accept_event(); }
		Ref<InputEventKey> key=event;
		if(key.is_valid() && key->is_pressed() && key->get_keycode()==Key::ESCAPE) { panning=false; dragging=false; scrubbing=false; queue_redraw(); accept_event(); }
        if(key.is_valid() && key->is_pressed() && !key->is_ctrl_pressed() && !key->is_alt_pressed() && !dragging) {
            rebuild_rows(); int next=row_offset; int last=MAX(0,layout.rows.size()-visible_rows());
            switch(key->get_keycode()) {
                case Key::LEFT: scroll_time(offset-80.0/zoom); accept_event(); return;
                case Key::RIGHT: scroll_time(offset+80.0/zoom); accept_event(); return;
                case Key::HOME: next=0; break;
                case Key::END: next=last; break;
                case Key::PAGEUP: next-=visible_rows(); break;
                case Key::PAGEDOWN: next+=visible_rows(); break;
                case Key::UP: --next; break;
                case Key::DOWN: ++next; break;
                default: return;
            }
            row_offset=CLAMP(next,0,last); queue_redraw(); accept_event();
        }
	}
public:
    void reset_view() { panning=false; folded.clear(); row_offset=0; offset=0; dragging=false; scrubbing=false; queue_redraw(); }
    void fold_all(bool collapse) {
        rebuild_rows(); folded.clear();
        if(collapse) { for(const SkeletonTimelineGroup &group:layout.groups) { folded.insert(group.id); } }
        row_offset=0; rebuild_rows(); queue_redraw();
    }
    void reveal_track(int track) {
        rebuild_rows();
        for(const SkeletonTimelineGroup &group:layout.groups) { if(group.tracks.has(track)) { folded.erase(group.id); break; } }
        rebuild_rows(); int row=layout.find_track(track);
        if(row>=0 && (row<row_offset || row>=row_offset+visible_rows())) { row_offset=MAX(0,row-visible_rows()+1); }
        queue_redraw();
    }
    bool run_layout_self_test() {
        Ref<Animation> clip; clip.instantiate();
        const char *paths[]={".:position",".:event:hit",".:slot:hand:color",".:rotation",".:event:step",".:slot:hand:attachment",".:position"};
        PackedInt64Array targets; const int ids[]={1,0,0,1,0,0,2};
        for(int i=0;i<7;i++) { int track=clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(track,NodePath(paths[i])); targets.push_back(ids[i]); }
        Array entities; Dictionary rig,bone; rig["name"]="Rig"; bone["name"]="SameName"; entities.push_back(rig); entities.push_back(bone); entities.push_back(bone.duplicate());
        SkeletonTimelineLayout test; HashSet<String> collapsed;
        test.build(clip,targets,entities,0,collapsed);
        bool ok=test.groups.size()==4 && test.rows.size()==11 && test.groups[0].tracks.size()==2 && test.groups[0].tracks[1]==3;
        collapsed.insert("entity:1"); test.build(clip,targets,entities,0,collapsed);
        ok &= test.rows.size()==9 && test.find_track(0)==-1 && test.find_track(3)==-1 && test.find_track(6)>=0;
        for(const SkeletonTimelineGroup &group:test.groups) { collapsed.insert(group.id); }
        test.build(clip,targets,entities,0,collapsed); ok &= test.rows.size()==4;
        collapsed.erase("events"); test.build(clip,targets,entities,0,collapsed);
        ok &= test.rows.size()==6 && test.find_track(1)>=0 && test.find_track(4)>=0 && test.find_track(2)==-1;
        test.build(Ref<Animation>(),targets,entities,0,collapsed); ok &= test.rows.is_empty();
        fold_all(true); rebuild_rows(); ok &= layout.rows.size()==layout.groups.size();
        reveal_track(0); ok &= layout.find_track(0)>=0;
        fold_all(false);
        double saved_offset=offset,saved_zoom=zoom; offset=0; update_time_scroll();
        Ref<InputEventMouseButton> horizontal; horizontal.instantiate(); horizontal->set_pressed(true); horizontal->set_button_index(MouseButton::WHEEL_RIGHT); gui_input(horizontal); ok &= offset>0;
        horizontal->set_button_index(MouseButton::MIDDLE); gui_input(horizontal);
        Ref<InputEventMouseMotion> motion; motion.instantiate(); motion->set_relative(Vector2(-120*EDSCALE,0)); double before=offset; gui_input(motion); ok &= offset>before;
        horizontal->set_pressed(false); gui_input(horizontal); before=offset; gui_input(motion); ok &= offset==before;
        update_time_scroll(); time_scroll->set_value(0); ok &= offset==0; time_scroll->set_value(time_scroll->get_max()-time_scroll->get_page()); ok &= offset>0;
        offset=saved_offset; zoom=saved_zoom; update_time_scroll();
        print_line(ok?"SKELETON_TIMELINE_GROUPS_PASS noncontiguous_tracks duplicate_names folded_mapping event_reveal empty_clip":"SKELETON_TIMELINE_GROUPS_FAIL");
        return ok;
    }
	void change_fps(double ratio) { set_zoom(zoom*ratio); }
	void set_zoom(double value) { zoom=CLAMP(value,30.0,3200.0); queue_redraw(); }
	ECSAnimationTimeline(ECSAnimationEditor *p_editor) { editor=p_editor; time_scroll=memnew(HScrollBar); time_scroll->set_name("TimeScroll"); time_scroll->set_custom_minimum_size(Size2(0,16)*EDSCALE); add_child(time_scroll); time_scroll->connect("value_changed",callable_mp(this,&ECSAnimationTimeline::scroll_time)); set_tooltip_text(String(U"中键拖动 / Shift + 滚轮：左右平移；Ctrl + 滚轮：缩放；滚轮：上下滚动轨道。")); row_scroll=memnew(VScrollBar); row_scroll->set_name("TrackScroll"); row_scroll->set_step(1); add_child(row_scroll); row_scroll->connect("value_changed",callable_mp(this,&ECSAnimationTimeline::scroll_rows)); set_clip_contents(true); set_focus_mode(FOCUS_ALL); set_custom_minimum_size(Size2(400,190)*EDSCALE); set_v_size_flags(SIZE_EXPAND_FILL); }
};
void ECSAnimationEditor::apply_timeline_spacing(double spacing) { timeline->set_zoom(spacing*timeline_fps); }
class ECSAnimationCurve : public Control {
	GDCLASS(ECSAnimationCurve,Control);
	ECSAnimationEditor *editor;
	bool dragging=false;
protected:
	static void _bind_methods() {}
	void _notification(int what) {
		if(what!=NOTIFICATION_DRAW) { return; }
		draw_rect(Rect2(Vector2(),get_size()),get_theme_color("dark_color_1","Editor")); PackedVector2Array line;
		for(int i=0;i<=64;i++) { double t=i/64.0; line.push_back(Vector2(8+t*(get_size().x-16),get_size().y-8-Math::ease(t,editor->curve_ease->get_value())*(get_size().y-16))); }
		draw_polyline(line,get_theme_color("accent_color","Editor"),2,true);
	}
	void gui_input(const Ref<InputEvent> &event) override {
		Ref<InputEventMouseButton> b=event; Ref<InputEventMouseMotion> m=event;
		if(b.is_valid() && b->get_button_index()==MouseButton::LEFT) { dragging=b->is_pressed(); if(!dragging) { editor->action(20); } accept_event(); }
		if(m.is_valid() && dragging) { editor->curve_ease->set_value(Math::exp((m->get_position().y/get_size().y-.5)*6)); queue_redraw(); accept_event(); }
	}
public:
	ECSAnimationCurve(ECSAnimationEditor *p_editor) { editor=p_editor; set_custom_minimum_size(Size2(160,90)); }
};
ECSAnimationEditor::ECSAnimationEditor() {
	auto button=[&](BoxContainer *r,const String &label,int command) { auto *b=memnew(Button); b->set_text(label); b->connect("pressed",callable_mp(this,&ECSAnimationEditor::action).bind(command)); r->add_child(b); if(command==1 || command==2 || command==3 || command==5 || command==7 || command==8 || command==9 || command==15 || command==20) { animation_buttons.push_back(b); } };
	auto *menu_panel=memnew(PanelContainer); add_child(menu_panel);
	Ref<StyleBoxFlat> menu_background; menu_background.instantiate();
	menu_background->set_bg_color(Color(.18,.18,.18));
	menu_background->set_border_width(SIDE_BOTTOM,1); menu_background->set_border_color(Color(.10,.11,.12));
	menu_background->set_content_margin(SIDE_LEFT,12*EDSCALE); menu_background->set_content_margin(SIDE_RIGHT,12*EDSCALE);
	menu_background->set_content_margin(SIDE_TOP,5*EDSCALE); menu_background->set_content_margin(SIDE_BOTTOM,5*EDSCALE);
	menu_panel->add_theme_style_override("panel",menu_background);
	auto *bar=memnew(HBoxContainer); bar->add_theme_constant_override("separation",4*EDSCALE); menu_panel->add_child(bar);
	auto divider=[&]() { auto *space=memnew(Control); space->set_custom_minimum_size(Size2(8,0)*EDSCALE); bar->add_child(space); auto *line=memnew(PanelContainer); Ref<StyleBoxFlat> box; box.instantiate(); box->set_bg_color(Color(.30,.32,.33)); line->add_theme_style_override("panel",box); line->set_custom_minimum_size(Size2(1,18)*EDSCALE); line->set_v_size_flags(SIZE_SHRINK_CENTER); bar->add_child(line); auto *end=memnew(Control); end->set_custom_minimum_size(Size2(8,0)*EDSCALE); bar->add_child(end); };
	auto *brand=memnew(Label); brand->set_text("AgeSkeleton"); brand->add_theme_font_size_override("font_size",14*EDSCALE); brand->add_theme_color_override("font_color",Color(.80,.84,.86)); brand->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER); bar->add_child(brand); divider();
	auto *project_menu=memnew(MenuButton); project_menu->set_text(String(U"工程")); project_menu->set_flat(true); bar->add_child(project_menu);
	const char32_t *labels[]={U"新建骨骼动画工程",U"打开工程",U"保存工程",U"工程另存为",U"新建骨架",U"添加子骨骼",U"导入 Spine JSON",U"添加图片"};
	for(int i=0;i<8;i++) { project_menu->get_popup()->add_item(String(labels[i]),i); }
	project_menu->get_popup()->add_separator(); project_menu->get_popup()->add_item(String(U"导出…"),8); project_menu->get_popup()->add_item(String(U"设置…"),11); 
	project_menu->get_popup()->connect("id_pressed",callable_mp(this,&ECSAnimationEditor::project_action));
	project_dialog=memnew(EditorFileDialog); add_child(project_dialog); project_dialog->connect("file_selected",callable_mp(this,&ECSAnimationEditor::project_file_selected));
	replace_dialog=memnew(ConfirmationDialog); replace_dialog->set_text(String(U"切换工程将关闭当前编辑内容。请先保存需要保留的修改。继续？")); add_child(replace_dialog); replace_dialog->connect("confirmed",callable_mp(this,&ECSAnimationEditor::confirm_project_action));
	mode_button=memnew(Button); mode_button->set_text(String(U"设置")); mode_button->set_flat(true); mode_button->set_custom_minimum_size(Size2(100,0)); mode_button->connect("pressed",callable_mp(this,&ECSAnimationEditor::toggle_authoring_mode)); bar->add_child(mode_button);
	owner=memnew(OptionButton); owner->set_h_size_flags(SIZE_EXPAND_FILL); bar->add_child(owner); owner->hide(); owner->connect("item_selected",callable_mp(this,&ECSAnimationEditor::refresh_tracks));
	divider();
	for(int command:{16,17,0}) { auto *b=memnew(Button); const char *icon=command==16?"UndoRedo":command==17?"Redo":"Reload"; b->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(icon,"EditorIcons")); b->set_tooltip_text(command==16?String(U"撤销"):command==17?String(U"重做"):String(U"刷新")); if(command==16 || command==17) {
        Ref<Shortcut> shortcut; shortcut.instantiate(); Array events;
        Ref<InputEventKey> key; key.instantiate(); key->set_keycode(Key::Z); key->set_command_or_control_autoremap(true); key->set_shift_pressed(command==17); events.push_back(key);
        if(command==17) { Ref<InputEventKey> alternate; alternate.instantiate(); alternate->set_keycode(Key::Y); alternate->set_command_or_control_autoremap(true); events.push_back(alternate); }
        shortcut->set_events(events); shortcut->set_name(command==16?String(U"撤销"):String(U"重做")); b->set_shortcut(shortcut); b->set_shortcut_context(this);
    }
    b->connect("pressed",callable_mp(this,&ECSAnimationEditor::action).bind(command)); bar->add_child(b); }
	divider();
	button(bar,String(U"创建动画"),1);
	auto *save=memnew(Button); save->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon("Save","EditorIcons")); save->set_tooltip_text(String(U"保存工程")); save->connect("pressed",callable_mp(this,&ECSAnimationEditor::save_scene)); save->set_text(String(U"保存")); bar->add_child(save);
	for (int i=0; i<bar->get_child_count(); i++) {
		auto *item=Object::cast_to<Button>(bar->get_child(i));
		if (!item || item==mode_button || item==owner) { continue; }
		item->set_flat(true); item->set_custom_minimum_size(Size2(item->get_button_icon().is_valid() && !item->get_text().is_empty() ? 78 : 32,30)*EDSCALE);
		item->set_expand_icon(true); item->add_theme_constant_override("icon_max_width",18*EDSCALE);
		item->add_theme_constant_override("h_separation",6*EDSCALE); item->add_theme_font_size_override("font_size",14*EDSCALE);
		for (const char *state : {"normal","disabled","hover","pressed","hover_pressed","focus"}) {
			Ref<StyleBoxFlat> box; box.instantiate(); String name=state;
			box->set_bg_color(name=="hover" ? Color(.25,.28,.30) : name=="pressed" || name=="hover_pressed" ? Color(.28,.42,.44) : Color(0,0,0,0));
			box->set_corner_radius_all(4*EDSCALE); box->set_content_margin(SIDE_LEFT,8*EDSCALE); box->set_content_margin(SIDE_RIGHT,8*EDSCALE);
			box->set_content_margin(SIDE_TOP,4*EDSCALE); box->set_content_margin(SIDE_BOTTOM,4*EDSCALE); item->add_theme_style_override(state,box);
		}
		for(const char *name:{"font_color","icon_normal_color"}) { item->add_theme_color_override(name,Color(.80,.83,.85)); }
		for(const char *name:{"font_hover_color","font_pressed_color","font_hover_pressed_color","icon_hover_color","icon_pressed_color","icon_hover_pressed_color"}) { item->add_theme_color_override(name,Color(.52,.82,.83)); }
		for(const char *name:{"font_disabled_color","icon_disabled_color"}) { item->add_theme_color_override(name,Color(.44,.47,.49)); }
	}
	image_tools=memnew(HBoxContainer); add_child(image_tools);
	for(int i=0;i<4;i++) { auto *b=memnew(Button); const char32_t *names[]={U"添加图片",U"细分网格",U"绑定骨骼",U"权重"}; b->set_text(String(names[i])); b->connect("pressed",callable_mp(this,&ECSAnimationEditor::image_action).bind(i)); image_tools->add_child(b); }
	mesh_edit=memnew(CheckBox); mesh_edit->set_text(String(U"编辑网格")); image_tools->add_child(mesh_edit); mesh_edit->connect("toggled",callable_mp(this,&ECSAnimationEditor::mesh_toggled));
	binding_dialog=memnew(ConfirmationDialog); binding_dialog->set_title(String(U"绑定骨骼 · 选择参与蒙皮的骨骼")); add_child(binding_dialog);
	binding_bones=memnew(ItemList); binding_bones->set_select_mode(ItemList::SELECT_MULTI); binding_bones->set_custom_minimum_size(Size2(360,300)); binding_dialog->add_child(binding_bones); binding_dialog->connect("confirmed",callable_mp(this,&ECSAnimationEditor::apply_binding));
	auto framed=[&](Control *parent) { auto *panel=memnew(PanelContainer); panel->set_theme_type_variation("RigPanel"); panel->set_h_size_flags(SIZE_EXPAND_FILL); panel->set_v_size_flags(SIZE_EXPAND_FILL); parent->add_child(panel); return panel; };
	auto header=[&](BoxContainer *parent,const String &label) { auto *caption=memnew(Label); caption->set_text(label); caption->set_theme_type_variation("RigHeader"); parent->add_child(caption); return caption; };
	auto *outer=memnew(HSplitContainer); outer->set_v_size_flags(SIZE_EXPAND_FILL); add_child(outer);
	auto *left=memnew(VSplitContainer); left->set_h_size_flags(SIZE_EXPAND_FILL); outer->add_child(left);
	auto *upper=memnew(HBoxContainer); upper->set_v_size_flags(SIZE_EXPAND_FILL); upper->set_stretch_ratio(3); left->add_child(upper);
	auto *tool_frame=framed(upper); tool_frame->set_h_size_flags(SIZE_FILL); tool_frame->set_theme_type_variation("RigToolbarDark");
	canvas_tool_host=memnew(VBoxContainer); canvas_tool_host->set_name("CanvasTools"); tool_frame->add_child(canvas_tool_host);
	local_canvas=memnew(ECSUICanvasEditor); local_canvas->set_custom_minimum_size(Size2(350,220)); local_canvas->set_h_size_flags(SIZE_EXPAND_FILL); local_canvas->set_skeleton_authoring(true); local_canvas->set_tooltip_text(String(U"选择骨骼或图片并拖动；中键平移，滚轮缩放，Home 适应视图，Esc 取消当前拖动。")); upper->add_child(local_canvas);
	mode_button->set_name("AuthoringMode"); mode_button->set_toggle_mode(true);
	mode_button->set_custom_minimum_size(Size2(0,30)*EDSCALE);
	mode_button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	mode_button->set_icon_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	mode_button->add_theme_constant_override("h_separation",6*EDSCALE);
	mode_button->add_theme_font_size_override("font_size",14*EDSCALE);
	mode_button->set_expand_icon(true); mode_button->add_theme_constant_override("icon_max_width",18*EDSCALE);
	local_canvas->connect("bone_create_requested",callable_mp(this,&ECSAnimationEditor::create_bone_from_drag));
	local_canvas->connect("images_dropped",callable_mp(this,&ECSAnimationEditor::images_dropped));
	local_canvas->connect("mesh_edited",callable_mp(this,&ECSAnimationEditor::mesh_changed));
	local_canvas->connect("entity_selected",callable_mp(this,&ECSAnimationEditor::select_target));
	local_canvas->connect("bone_pose_edited",callable_mp(this,&ECSAnimationEditor::pose_edited));

	auto *sheet_split=memnew(HSplitContainer); sheet_split->set_name("AnimationWorkspace"); left->add_child(sheet_split); sheet_split->set_v_size_flags(SIZE_EXPAND_FILL); sheet_split->set_stretch_ratio(1.3);
	auto *animations=memnew(VBoxContainer); animations->set_custom_minimum_size(Size2(160,0)*EDSCALE); animations->set_h_size_flags(SIZE_FILL); sheet_split->add_child(animations);
	auto *title=build_panel_header(animations,TTR("Animations"),"animation",true); animation_title=title;
	animation_list=memnew(ItemList); animation_list->set_name("AnimationClips"); animation_list->set_v_size_flags(SIZE_EXPAND_FILL); animations->add_child(animation_list); animation_list->connect("item_selected",callable_mp(this,&ECSAnimationEditor::choose_animation));
	auto *bottom=memnew(VBoxContainer); bottom->set_h_size_flags(SIZE_EXPAND_FILL); sheet_split->add_child(bottom); bottom->set_v_size_flags(SIZE_EXPAND_FILL);
	auto *sheet_header=memnew(HBoxContainer); bottom->add_child(sheet_header);
	auto *sheet_icon=memnew(TextureRect); sheet_icon->set_texture(skeleton_workspace_icon("sheet")); sheet_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED); sheet_header->add_child(sheet_icon);
	header(sheet_header,TTR("Timeline"));
	auto *range_frame=framed(bottom); range_frame->set_v_size_flags(SIZE_FILL); range_frame->set_theme_type_variation("RigToolbar");
	auto *range_scroll=memnew(ScrollContainer); range_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED); range_frame->add_child(range_scroll);
	auto *range=memnew(HBoxContainer); range_scroll->add_child(range);
	auto frame_box=[&](const String &label) { auto *text=memnew(Label); text->set_text(label); range->add_child(text); auto *value=memnew(SpinBox); value->set_max(216000); value->set_step(1); value->set_custom_minimum_size(Size2(64,30)*EDSCALE); range->add_child(value); return value; };
	frame=frame_box(String(U"当前")); frame->connect("value_changed",callable_mp(this,&ECSAnimationEditor::frame_changed)); loop_start=frame_box(String(U"循环开始")); loop_end=frame_box(String(U"结束")); loop_end->set_value(120);
	timeline_fps_label=memnew(Label); timeline_fps_label->set_text("30 FPS"); timeline_fps_label->set_tooltip_text(String(U"时间轴帧率：自动匹配动画采样，保持动画秒数与播放速度不变")); range->add_child(timeline_fps_label);
	auto_key=memnew(Button); auto_key->set_toggle_mode(true); auto_key->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon("AutoKey","EditorIcons")); auto_key->set_text(String(U"自动关键帧")); auto_key->set_pressed(true); range->add_child(auto_key);
	auto *graph_button=memnew(Button); graph_button->set_text(String(U"图表")); graph_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon("GraphEdit","EditorIcons")); graph_button->connect("pressed",callable_mp(this,&ECSAnimationEditor::show_curve_graph)); range->add_child(graph_button);
	auto *edit_frame=framed(bottom); edit_frame->set_v_size_flags(SIZE_FILL); edit_frame->set_theme_type_variation("RigToolbarDark");
	auto *edit_scroll=memnew(ScrollContainer); edit_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED); edit_frame->add_child(edit_scroll);
	auto *edit_bar=memnew(HBoxContainer); edit_scroll->add_child(edit_bar);
	const char *edit_icons[]={"CollapseTree","ExpandTree","Lock","ActionCopy","ActionCut","ActionPaste","Remove"};
	const char32_t *edit_tips[]={U"折叠轨道",U"展开轨道",U"锁定关键帧编辑",U"复制关键帧",U"剪切关键帧",U"粘贴到当前帧",U"删除关键帧"};
	for(int i=0;i<7;i++) { auto *b=memnew(Button); b->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(edit_icons[i],"EditorIcons")); b->set_tooltip_text(String(edit_tips[i])); if(i==2) { b->set_toggle_mode(true); } b->connect("pressed",callable_mp(this,&ECSAnimationEditor::timeline_action).bind(i)); edit_bar->add_child(b); }
	Ref<ButtonGroup> drag_group; drag_group.instantiate();
	for(int i=0;i<3;i++) { auto *b=memnew(Button); const char32_t *labels[]={U"移位",U"偏移",U"调整"}; const char32_t *tips[]={U"拖动单个关键帧",U"拖动此轨道当前帧及后续帧",U"以第 0 帧为原点缩放此轨道时间"}; b->set_text(String(labels[i])); b->set_tooltip_text(String(tips[i])); b->set_toggle_mode(true); b->set_button_group(drag_group); b->set_pressed(i==0); b->connect("pressed",callable_mp(this,&ECSAnimationEditor::timeline_action).bind(10+i)); edit_bar->add_child(b); }
	timeline=memnew(ECSAnimationTimeline(this)); bottom->add_child(timeline);
	auto *r=memnew(HBoxContainer); timeline->add_child(r); r->set_position(Vector2(4,3)*EDSCALE); r->set_custom_minimum_size(Size2(270,30)*EDSCALE);
	// Draw readable transport symbols at the actual UI scale, independent of editor icon aliases.
	const char *transport_paths[]={
		"M2 3h2v10H2z M13 3L5 8l8 5z",
		"M3 3h2v10H3z M12 3L6 8l6 5z",
		"M13 2L3 8l10 6z",
		"M3 2l10 6L3 14z",
		"M11 3h2v10h-2z M4 3l6 5-6 5z",
		"M12 3h2v10h-2z M3 3l8 5-8 5z",
		"M3 3h3v10H3z M10 3h3v10h-3z"};
	const char32_t *transport_tips[]={U"跳到循环开始",U"上一帧",U"倒放",U"播放",U"下一帧",U"跳到循环结束",U"暂停"};
	for(int i=0;i<7;i++) { auto *b=memnew(Button); Ref<Image> glyph; glyph.instantiate();
		glyph->load_svg_from_string(String("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><path fill=\"#dedede\" d=\"")+transport_paths[i]+"\"/></svg>",EDSCALE);
		b->set_button_icon(ImageTexture::create_from_image(glyph)); b->set_tooltip_text(String(transport_tips[i])); b->connect("pressed",callable_mp(this,&ECSAnimationEditor::transport).bind(i)); b->set_expand_icon(true); b->add_theme_constant_override("icon_max_width",14*EDSCALE); b->set_custom_minimum_size(Size2(24,22)*EDSCALE); r->add_child(b); }
	loop_playback=memnew(Button); loop_playback->set_toggle_mode(true); loop_playback->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon("Loop","EditorIcons")); loop_playback->set_tooltip_text(String(U"循环播放")); loop_playback->set_pressed(true); loop_playback->set_expand_icon(true); loop_playback->add_theme_constant_override("icon_max_width",14*EDSCALE); loop_playback->set_custom_minimum_size(Size2(24,22)*EDSCALE); r->add_child(loop_playback);
	time=memnew(SpinBox); time->set_max(3600); time->set_step(1.0/timeline_fps); time->connect("value_changed",callable_mp(this,&ECSAnimationEditor::seek)); r->add_child(time); time->hide();
	button(edit_bar,String(U"插入关键帧"),2);
    // Spacious, consistent authoring controls; keep the existing three toolbar rows.
    auto style_tools=[&](BoxContainer *bar) {
        bar->add_theme_constant_override("separation",4*EDSCALE);
        for(int i=0;i<bar->get_child_count();i++) {
            Button *b=Object::cast_to<Button>(bar->get_child(i)); if(!b) { continue; }
            b->set_custom_minimum_size(Size2(b->get_text().is_empty()?30:MAX(68,b->get_text().length()*13+40),30)*EDSCALE);
            b->set_expand_icon(true); b->add_theme_constant_override("icon_max_width",18*EDSCALE); b->add_theme_constant_override("h_separation",6*EDSCALE); b->add_theme_font_size_override("font_size",13*EDSCALE);
            for(const char *state:{"normal","hover","pressed","hover_pressed","disabled"}) {
                Ref<StyleBoxFlat> box; box.instantiate(); String kind=state;
                box->set_bg_color(kind=="pressed" || kind=="hover_pressed"?Color(.19,.51,.62):kind=="hover"?Color(.4,.4,.4):Color(.30,.30,.30));
                box->set_corner_radius_all(3*EDSCALE); box->set_border_width_all(1*EDSCALE); box->set_border_color(Color(.16,.18,.20));
                box->set_content_margin(SIDE_LEFT,6*EDSCALE); box->set_content_margin(SIDE_RIGHT,6*EDSCALE); box->set_content_margin(SIDE_TOP,3*EDSCALE); box->set_content_margin(SIDE_BOTTOM,3*EDSCALE); b->add_theme_style_override(state,box);
            }
        }
    };
    for(int i=7;i<10;i++) { Button *b=Object::cast_to<Button>(edit_bar->get_child(i)); if(b) { b->set_button_icon(skeleton_workspace_icon(i==9?"shear":"position")); } }
    Button *insert=Object::cast_to<Button>(edit_bar->get_child(edit_bar->get_child_count()-1)); if(insert) { insert->set_button_icon(skeleton_workspace_icon("animation")); }
    style_tools(range); style_tools(edit_bar); style_tools(r);
	auto *timeline_zoom=memnew(HSlider); timeline_zoom->set_min(30); timeline_zoom->set_max(3200); timeline_zoom->set_value(1200); timeline_zoom->set_custom_minimum_size(Size2(180,16)*EDSCALE); timeline_zoom->set_h_size_flags(SIZE_SHRINK_BEGIN); timeline_zoom->set_tooltip_text(String(U"时间轴缩放（Ctrl + 滚轮）；Shift + 滚轮水平滚动")); timeline_zoom->connect("value_changed",callable_mp(timeline,&ECSAnimationTimeline::set_zoom)); bottom->add_child(timeline_zoom);

	// Keep the hierarchy stationary while the contextual properties scroll independently.
	auto *sidebar=memnew(SplitContainer); sidebar->set_custom_minimum_size(Size2(360,0)*EDSCALE); sidebar->set_h_size_flags(SIZE_FILL); outer->add_child(sidebar);
	auto *tree_frame=framed(sidebar); tree_frame->set_stretch_ratio(1.05);
	auto *right=memnew(VBoxContainer); tree_frame->add_child(right);
	title=build_panel_header(right,String(U"层级树"),"skeleton",false);
	build_hierarchy_toolbar(right);
	hierarchy=memnew(Tree); hierarchy->set_columns(3); hierarchy->set_column_title(0,String(U"层级 / 名称")); hierarchy->set_column_title(1,String(U"显示")); hierarchy->set_column_title(2,String(U"锁定")); hierarchy->set_column_titles_visible(true);
    hierarchy->set_column_expand(1,false); hierarchy->set_column_expand(2,false); hierarchy->set_column_custom_minimum_width(1,42*EDSCALE); hierarchy->set_column_custom_minimum_width(2,42*EDSCALE);
    hierarchy->add_theme_font_size_override("font_size",14*EDSCALE); hierarchy->add_theme_constant_override("v_separation",4*EDSCALE); hierarchy->add_theme_constant_override("h_separation",8*EDSCALE); hierarchy->add_theme_constant_override("item_margin",20*EDSCALE); hierarchy->add_theme_constant_override("draw_guides",1); hierarchy->add_theme_color_override("guide_color",Color(.43,.46,.49,.5));
    hierarchy->connect("button_clicked",callable_mp(this,&ECSAnimationEditor::hierarchy_item_button)); hierarchy->set_hide_root(true); hierarchy->set_custom_minimum_size(Size2(0,220)); hierarchy->set_v_size_flags(SIZE_EXPAND_FILL); right->add_child(hierarchy); // Skin selection can rebuild the hierarchy; wait until Tree releases its input lock.
    hierarchy->connect("item_selected",callable_mp(this,&ECSAnimationEditor::hierarchy_selected),CONNECT_DEFERRED);
	auto *property_frame=framed(sidebar); property_frame->set_custom_minimum_size(Size2(0,260)*EDSCALE); property_frame->set_stretch_ratio(1.0);
	auto *right_scroll=memnew(ScrollContainer); right_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED); property_frame->add_child(right_scroll);
	right=memnew(VBoxContainer); right->set_h_size_flags(SIZE_EXPAND_FILL); right_scroll->add_child(right);
	auto section=[&](BoxContainer *host,const String &label,bool folded) { auto *group=memnew(FoldableContainer); group->set_title(label); group->set_folded(folded); host->add_child(group); auto *content=memnew(VBoxContainer); group->add_child(content); return content; };
	target=memnew(OptionButton); right->add_child(target); target->hide(); target->connect("item_selected",callable_mp(this,&ECSAnimationEditor::select_target));
	property=memnew(OptionButton); for(const char *p:{"position","rotation","scale","shear"}) { property->add_item(p); } right->add_child(property); property->connect("item_selected",callable_mp(this,&ECSAnimationEditor::select_target).bind(-1).unbind(1));
	auto number=[&](const String &label,double min,double max,double step) { auto *row=memnew(HBoxContainer); right->add_child(row); auto *l=memnew(Label); l->set_text(label); l->set_custom_minimum_size(Size2(75,0)); row->add_child(l); auto *s=memnew(SpinBox); s->set_min(min); s->set_max(max); s->set_step(step); s->set_allow_greater(true); s->set_h_size_flags(SIZE_EXPAND_FILL); row->add_child(s); return s; };
	x=number("X",-100000,100000,.01); y=number("Y",-100000,100000,.01); z=number(String(U"Z / 弧度"),-100000,100000,.01);
	property->hide(); x->get_parent()->call("hide"); y->get_parent()->call("hide"); z->get_parent()->call("hide");
	selection_title=header(right,String(U"属性")); selection_title->set_theme_type_variation("RigPropertyHeader");
    auto *name_row=memnew(HBoxContainer); right->add_child(name_row); auto *name_caption=memnew(Label); name_caption->set_text(String(U"名称")); name_caption->set_custom_minimum_size(Size2(75,0)); name_row->add_child(name_caption);
    selection_name=memnew(LineEdit); selection_name->set_h_size_flags(SIZE_EXPAND_FILL); name_row->add_child(selection_name); selection_name->connect("text_submitted",callable_mp(this,&ECSAnimationEditor::selection_property_changed).unbind(1)); selection_name->connect("focus_exited",callable_mp(this,&ECSAnimationEditor::selection_property_changed));
    selection_length=number(String(U"长度"),0,100000,.1); bone_properties=Object::cast_to<Control>(selection_length->get_parent()); selection_length->set_allow_greater(false); selection_length->connect("value_changed",callable_mp(this,&ECSAnimationEditor::selection_property_changed).unbind(1));
	transform_property_host=section(right,TTR("Transform"),false); transform_property_host->set_name("TransformProperties");
	build_slot_tools(section(right,String(U"皮肤与插槽"),true));
	auto *property_host=right; right=section(property_host,String(U"动画与关键帧"),false); animation_property_panel=Object::cast_to<Control>(right->get_parent());
	states=memnew(OptionButton); right->add_child(states); state_name=memnew(LineEdit); state_name->set_placeholder(String(U"动画名称")); right->add_child(state_name);
	button(right,String(U"新建空白动画"),23); button(right,String(U"复制为新动画"),7); button(right,String(U"重命名动画"),15); button(right,String(U"删除动画"),8);
	clip_length=number(String(U"动画时长"),.01,3600,.01); clip_length->set_allow_greater(false); clip_length->set_value(1);
	clip_loop=memnew(CheckBox); clip_loop->set_text(String(U"循环播放（走路 / 待机）")); clip_loop->set_pressed(true); right->add_child(clip_loop);
	button(right,String(U"应用时长与循环设置"),24);
	auto *animation_properties=right; right=section(animation_properties,String(U"事件"),true);
	event_name=memnew(LineEdit); event_name->set_placeholder(String(U"事件名称（footstep / hit）")); right->add_child(event_name);
	event_integer=number(String(U"事件整数"),-2147483648.,2147483647.,1); event_number=number(String(U"事件数值"),-100000,100000,.01);
	event_text=memnew(LineEdit); event_text->set_placeholder(String(U"事件文字参数")); right->add_child(event_text); button(right,String(U"在当前帧插入事件"),25);
	right=section(animation_properties,String(U"混合与曲线"),true);
	duration=number(String(U"过渡秒数"),0,60,.05); duration->set_value(.25); button(right,String(U"设为初始动画"),9);
	curve_interpolation=memnew(OptionButton); curve_interpolation->add_item(String(U"阶梯曲线")); curve_interpolation->add_item(String(U"线性曲线")); curve_interpolation->add_item(String(U"三次曲线")); curve_interpolation->select(1); right->add_child(curve_interpolation);
	curve_ease=number(String(U"缓动系数"),.05,20,.05); curve_ease->set_value(1); curve_graph=memnew(ECSAnimationCurve(this)); right->add_child(curve_graph); curve_ease->connect("value_changed",callable_mp(static_cast<CanvasItem *>(curve_graph),&CanvasItem::queue_redraw).unbind(1)); button(right,String(U"应用到选中关键帧曲线"),20);
	right=section(property_host,String(U"蒙皮与约束"),false); setup_property_panel=Object::cast_to<Control>(right->get_parent());
	image_tools->get_parent()->remove_child(image_tools); right->add_child(image_tools);
	// Mesh editing belongs to attachment properties, not the global application toolbar.
	image_tools->remove_child(mesh_edit); right->add_child(mesh_edit);
	button(right,String(U"绑定当前骨架姿态"),11);
	brush_enabled=memnew(CheckBox); brush_enabled->set_text(String(U"权重笔刷（设置模式）")); right->add_child(brush_enabled); brush_enabled->connect("toggled",callable_mp(this,&ECSAnimationEditor::configure_brush).unbind(1));
	brush_bone=memnew(OptionButton); right->add_child(brush_bone); brush_bone->connect("item_selected",callable_mp(this,&ECSAnimationEditor::configure_brush).unbind(1));
	brush_radius=number(String(U"笔刷半径"),1,300,1); brush_radius->set_value(60); brush_radius->set_allow_greater(false); brush_strength=number(String(U"笔刷强度"),.01,1,.01); brush_strength->set_value(.15); brush_strength->set_allow_greater(false);
	brush_radius->connect("value_changed",callable_mp(this,&ECSAnimationEditor::configure_brush).unbind(1)); brush_strength->connect("value_changed",callable_mp(this,&ECSAnimationEditor::configure_brush).unbind(1));
	ik_x=number(String(U"IK 目标 X"),-100000,100000,1); ik_y=number(String(U"IK 目标 Y"),-100000,100000,1); ik_length=number(String(U"IK 链长度"),1,16,1); ik_length->set_value(2); ik_length->set_allow_greater(false); button(right,String(U"设置所选骨骼的 IK"),21); button(right,String(U"移除骨架 IK"),22);
	feedback=memnew(Label); feedback->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART); feedback->set_text(String(U"设置模式编辑绑定姿态；动画模式编辑关键帧。保存写入当前 ECS 场景。")); add_child(feedback);
	curve_graph->get_parent()->remove_child(curve_graph); bottom->add_child(curve_graph); curve_graph->set_custom_minimum_size(Size2(400,190)); curve_graph->set_v_size_flags(SIZE_EXPAND_FILL); curve_graph->hide();
    Vector<Node *> property_nodes; property_nodes.push_back(property_host);
    while(!property_nodes.is_empty()) {
        Node *node=property_nodes[property_nodes.size()-1]; property_nodes.remove_at(property_nodes.size()-1);
        for(int i=0;i<node->get_child_count();i++) { property_nodes.push_back(node->get_child(i)); }
        Control *control=Object::cast_to<Control>(node); if(!control) { continue; }
        control->add_theme_font_size_override("font_size",14*EDSCALE);
        if(Object::cast_to<Button>(node) || Object::cast_to<LineEdit>(node) || Object::cast_to<SpinBox>(node)) { Size2 size=control->get_custom_minimum_size(); size.y=MAX(size.y,32*EDSCALE); control->set_custom_minimum_size(size); }
        if(BoxContainer *box=Object::cast_to<BoxContainer>(node)) { box->add_theme_constant_override("separation",6*EDSCALE); }
    }
	// Each authoring pane is a separate dockable tab, including properties.
	Vector<TabContainer *> dock_tabs;
	auto dock_panel=[&](Control *panel,const String &name,const String &icon) {
		auto *parent=panel->get_parent(); int index=panel->get_index();
		auto *tabs=memnew(TabContainer); tabs->set_h_size_flags(panel->get_h_size_flags()); tabs->set_v_size_flags(SIZE_EXPAND_FILL);
		tabs->set_stretch_ratio(panel->get_stretch_ratio()); tabs->set_custom_minimum_size(Size2(160,120)*EDSCALE);
		parent->add_child(tabs); parent->move_child(tabs,index); panel->reparent(tabs); panel->set_name(name); panel->set_meta("skeleton_dock_icon",skeleton_workspace_icon(icon));
		tabs->set_tab_title(0,TTR(name)); tabs->set_tab_icon(0,skeleton_workspace_icon(icon)); tabs->set_tab_icon_max_width(0,16*EDSCALE);
		dock_tabs.push_back(tabs); return tabs;
	};
	auto *canvas_tabs=dock_panel(upper,"Canvas","skeleton"); canvas_tabs->set_stretch_ratio(3);
	dock_panel(animations,"Animations","animation")->set_custom_minimum_size(Size2(160,120)*EDSCALE);
	dock_panel(bottom,"Timeline","sheet");
	dock_panel(tree_frame,"Hierarchy","skeleton");
	dock_panel(property_frame,"Properties","position");
	// Header actions live in the tab's three-dot menu.
	animation_title->get_parent()->get_parent()->get_parent()->call("hide");
	title->get_parent()->get_parent()->get_parent()->call("hide");
	if(title->get_parent()->get_parent()->get_parent()->get_parent()->has_meta("skeleton_panel_options")) { tree_frame->set_meta("skeleton_panel_options",title->get_parent()->get_parent()->get_parent()->get_parent()->get_meta("skeleton_panel_options")); }
	sheet_header->hide();
	workspace_docking=memnew(SkeletonWorkspaceDocking); add_child(workspace_docking);
	auto *panels_menu=memnew(MenuButton); panels_menu->set_text(TTR("Panels")); panels_menu->set_name("WorkspacePanels"); bar->add_child(panels_menu);
	Vector<SplitContainer *> dock_splits; dock_splits.push_back(outer); dock_splits.push_back(left); dock_splits.push_back(sheet_split); dock_splits.push_back(sidebar);
	build_canvas_tools();
	workspace_docking->setup(this,panels_menu,dock_tabs,dock_splits);
	load_workspace_preferences();
	set_mode(0);
	set_process(true);
}
Ref<Animation> ECSAnimationEditor::current_clip() const {
	if(scene.is_null() || owner->get_selected_id()<0 || owner->get_selected_id()>=scene->get_entities().size()) { return Ref<Animation>(); }
	Dictionary data=Dictionary(scene->get_entities()[owner->get_selected_id()]).get("animation",Dictionary());
	Dictionary named=data.get("states",Dictionary());
	return named.has(editing_state)?Ref<Animation>(named[editing_state]):Ref<Animation>(data.get("clip",Variant()));
}
void ECSAnimationEditor::_notification(int what) {
	if(what==NOTIFICATION_DRAW) { draw_rect(Rect2(Vector2(),get_size()),Color(.18,.18,.18)); }
	if(what==NOTIFICATION_EXIT_TREE) { cancel_motion_export(); skeleton_ai_stop(); }
	if(what==NOTIFICATION_READY) { skeleton_ai_start(); get_window()->connect("files_dropped",callable_mp(this,&ECSAnimationEditor::external_images_dropped)); }
	if(what==NOTIFICATION_PROCESS && pending_property_reveal && --property_reveal_frames<=0) {
        if(pending_property_reveal->is_visible_in_tree()) {
            for(Node *parent=pending_property_reveal->get_parent();parent;parent=parent->get_parent()) {
                if(auto *scroll=Object::cast_to<ScrollContainer>(parent)) { scroll->ensure_control_visible(pending_property_reveal); break; }
            }
        }
        pending_property_reveal=nullptr;
    }
    if(what==NOTIFICATION_PROCESS && playing && is_visible_in_tree()) {
		Ref<Animation> clip=current_clip(); if(clip.is_null()) { playing=false; return; }
		double start=loop_start->get_value()/timeline_fps,end=MIN(clip->get_length(),loop_end->get_value()/timeline_fps); if(end<=start) { start=0; end=clip->get_length(); }
		double next=playback_time+get_process_delta_time()*playback_direction;
		Array crossed=ECSWorld::sample_animation_events(clip,playback_time,next,false);
		if(!crossed.is_empty()) { String names; for(const Variant &item:crossed) { if(!names.is_empty()) { names+="、"; } names+=String(Dictionary(item).get("name",String())); } feedback->set_text(String(U"动画事件：")+names); }
		if(next>=end || next<start) { if(loop_playback->is_pressed() && end>start) { next=start+Math::fposmod(next-start,end-start); } else { next=CLAMP(next,start,end); playing=false; } }
		playback_time=next; time->set_value_no_signal(next); frame->set_value_no_signal(Math::round(next*timeline_fps)); canvas->preview_animation(owner->get_selected_id(),next,false,editing_state); timeline->queue_redraw(); refresh_canvas_values();
	}
}
void ECSAnimationEditor::toggle_authoring_mode() {
	set_mode(animation_mode ? 0 : 1);
}
void ECSAnimationEditor::set_mode(int mode) {
	stop_preview(); if(mode==1 && canvas_tools[2] && (canvas_tools[2]->is_pressed() || canvas_tools[1]->is_pressed())) { canvas_tool_action(1); } animation_mode=mode==1;  mode_button->set_button_icon(skeleton_workspace_icon(animation_mode?"mode_animation":"mode_setup")); mode_button->set_text(animation_mode?TTR("Animate"):TTR("Rig")); mode_button->set_pressed_no_signal(animation_mode);
	mode_button->set_tooltip_text(animation_mode ? TTR("Switch to rig editing") : TTR("Switch to animation editing"));
	for(Button *button:compensation_tools) { if(button) { button->set_disabled(animation_mode); } }
	for(Button *button:animation_buttons) { button->set_disabled(!animation_mode); }
	animation_list->set_mouse_filter(animation_mode?Control::MOUSE_FILTER_STOP:Control::MOUSE_FILTER_IGNORE);
	animation_list->set_focus_mode(animation_mode?Control::FOCUS_ALL:Control::FOCUS_NONE);
	animation_list->set_modulate(animation_mode?Color(1,1,1):Color(1,1,1,.35));
	animation_title->set_modulate(animation_mode?Color(1,1,1):Color(1,1,1,.35));
	states->set_disabled(!animation_mode); state_name->set_editable(animation_mode);
	curve_interpolation->set_disabled(!animation_mode); curve_ease->set_editable(animation_mode);
	curve_graph->set_mouse_filter(animation_mode?Control::MOUSE_FILTER_STOP:Control::MOUSE_FILTER_IGNORE);
	brush_enabled->set_disabled(animation_mode); image_tools->set_visible(!animation_mode); animation_property_panel->set_visible(animation_mode); setup_property_panel->set_visible(!animation_mode); if(canvas_tools[2]) { canvas_tools[2]->set_disabled(animation_mode); canvas_tools[1]->set_disabled(animation_mode); } mesh_edit->set_pressed(false); configure_brush();
	local_canvas->set_keyframe_edit_mode(animation_mode); refresh_selection_properties();
}
void ECSAnimationEditor::show_curve_graph() { bool graph=!curve_graph->is_visible(); curve_graph->set_visible(graph); timeline->set_visible(!graph); }
void ECSAnimationEditor::hierarchy_item_button(Object *object,int column,int id,int mouse) {
    if(mouse!=int(MouseButton::LEFT)) { return; } TreeItem *item=Object::cast_to<TreeItem>(object); if(!item || item->get_metadata(0).get_type()!=Variant::INT) { return; }
    int index=item->get_metadata(0); bool hidden=local_canvas->is_authoring_hidden(index),locked=local_canvas->is_authoring_locked(index);
    if(id==0) { hidden=!hidden; } else if(id==1) { locked=!locked; } else { return; }
    local_canvas->set_authoring_item_state(index,hidden,locked);
    item->set_button_color(1,0,hidden?Color(.4,.42,.44):Color(.85,.92,.95)); item->set_button_color(2,0,locked?Color(1,.65,.32):Color(.42,.46,.49));
    item->set_custom_color(0,hidden?Color(.48,.51,.53):Color(.88,.9,.92));
}
void ECSAnimationEditor::fold_hierarchy(bool collapsed) { for(TreeItem *row=hierarchy->get_root()?hierarchy->get_root()->get_next_in_tree():nullptr;row;row=row->get_next_in_tree()) { row->set_collapsed(collapsed); } }
void ECSAnimationEditor::timeline_action(int command) {
	if(command==0 || command==1) { timeline->fold_all(command==0); return; }
	if(command==2) { timeline_locked=!timeline_locked; return; }
	if(command>=10) { timeline_drag_mode=command-10; return; }
	Ref<Animation> source=current_clip(); if(source.is_null()) { return; }
	int track=selected_key.x,key=selected_key.y;
	if(command==3 || command==4) { if(track<0 || track>=source->get_track_count() || key<0 || key>=source->track_get_key_count(track)) { return; } copied_key=source->track_get_key_value(track,key); copied_transition=source->track_get_key_transition(track,key); if(command==3) { return; } }
	if(timeline_locked) { return; }
	if(command==4 || command==6) { if(track>=0 && key>=0 && track<source->get_track_count() && key<source->track_get_key_count(track)) { select_key(track,key); action(3); } return; }
	if(command==5 && copied_key.get_type()!=Variant::NIL && track>=0 && track<source->get_track_count()) {
		if(source->track_find_key(track,time->get_value(),Animation::FIND_MODE_APPROX)>=0) { feedback->set_text(String(U"当前帧已有关键帧，未覆盖。")); return; }
		Ref<Animation> clip=source->duplicate(true); int inserted=clip->track_insert_key(track,time->get_value(),copied_key,copied_transition); clip->set_length(MAX(clip->get_length(),time->get_value())); Dictionary definition=Dictionary(scene->get_entities()[owner->get_selected_id()]).get("animation",Dictionary()); definition=definition.duplicate(true); definition["clip"]=clip; commit_animation(definition,String(U"粘贴关键帧")); selected_key=Vector2i(track,inserted); seek(time->get_value());
	}
}
void ECSAnimationEditor::save_scene() { project_action(2); }
void ECSAnimationEditor::hierarchy_selected() {
    if(refreshing || !hierarchy->get_selected()) { return; }
    TreeItem *selected=hierarchy->get_selected(); Variant metadata=selected->get_metadata(0);
    if(metadata.get_type()!=Variant::DICTIONARY) { select_target(metadata); return; }
    Dictionary slot=metadata; select_target(slot["rig"]);
    if(slot.has("skin")) { for(int i=0;i<skin_choice->get_item_count();i++) { if(skin_choice->get_item_text(i)==String(slot["skin"])) { skin_choice->select(i);slot_action(0);break; } } }
    if(slot.has("slot")) { for(int i=0;i<slot_choice->get_item_count();i++) { if(slot_choice->get_item_text(i)==String(slot["slot"])) { slot_choice->select(i); break; } } }
    refresh_slot_tools();
    if(slot.has("placeholder")) { attachment_name_edit->set_text(slot["placeholder"]); for(int i=0;i<attachment_choice->get_item_count();i++) { if(String(attachment_choice->get_item_metadata(i))==String(slot["placeholder"])) { attachment_choice->select(i); break; } } }
    Object::cast_to<FoldableContainer>(skin_choice->get_parent()->get_parent()->get_parent())->set_folded(false);
    selection_title->set_text(slot.has("skin")?String(U"皮肤：")+String(slot["skin"]):slot.has("slot")?String(U"插槽：")+String(slot["slot"]):String(U"皮肤设置"));
}
void ECSAnimationEditor::choose_animation(int index) {
	if(!animation_mode || index<0 || index>=animation_list->get_item_count()) { return; }
	playing=false; editing_state=animation_list->get_item_metadata(index); selected_key=Vector2i(-1,-1); refresh_tracks(); Ref<Animation> clip=current_clip(); loop_start->set_value(0); if(clip.is_valid()) { loop_end->set_value(Math::round(clip->get_length()*timeline_fps)); } seek(0);
}
void ECSAnimationEditor::seek(double value) {
	if(refreshing) { return; } playing=false; time->set_value_no_signal(MAX(0.0,value)); frame->set_value_no_signal(Math::round(MAX(0.0,value)*timeline_fps));
	if(canvas && animation_mode) { canvas->preview_animation(owner->get_selected_id(),time->get_value(),false,editing_state); refresh_canvas_values(); }
	timeline->queue_redraw();
}
void ECSAnimationEditor::select_key(int track,int key) {
	Ref<Animation> clip=current_clip(); if(clip.is_null() || track<0 || track>=clip->get_track_count() || key<0 || key>=clip->track_get_key_count(track)) { return; }
	selected_key=Vector2i(track,key); timeline->reveal_track(track); Variant raw=clip->track_get_key_value(track,key);
    auto reveal_property=[this](Control *control) {
        // Nested fold containers need a complete layout pass before scroll bounds are final.
        pending_property_reveal=control; property_reveal_frames=2;
    };
    if(raw.get_type()!=Variant::VECTOR3) {
        String path=clip->track_get_path(track).get_concatenated_subnames();
        if(path.begins_with("event:")) {
            Object::cast_to<FoldableContainer>(animation_property_panel)->set_folded(false);
            Object::cast_to<FoldableContainer>(event_name->get_parent()->get_parent())->set_folded(false);
            Dictionary event=raw; event_name->set_text(event.get("name",String())); event_integer->set_value(event.get("int",0)); event_number->set_value(event.get("float",0.0)); event_text->set_text(event.get("string",String()));
            selection_title->set_text(String(U"事件：")+path.substr(6).uri_decode()); reveal_property(Object::cast_to<Control>(event_name->get_parent()));
        } else if(path.begins_with("slot:")) {
            select_target(owner->get_selected_id());
            String name=path.get_slice(":",1).uri_decode(),field=path.get_slice(":",2);
            for(int i=0;i<slot_choice->get_item_count();i++) { if(slot_choice->get_item_text(i)==name) { slot_choice->select(i); break; } }
            refresh_slot_tools(); refreshing_slots=true;
            if(field=="color" && raw.get_type()==Variant::COLOR) { slot_tint->set_pick_color(raw); }
            else if(field=="z_index") { slot_order->set_value_no_signal(raw); }
            else if(field=="attachment") { for(int i=0;i<attachment_choice->get_item_count();i++) { if(attachment_choice->get_item_metadata(i)==raw) { attachment_choice->select(i); break; } } }
            refreshing_slots=false;
            Object::cast_to<FoldableContainer>(skin_choice->get_parent()->get_parent()->get_parent())->set_folded(false);
            selection_title->set_text(String(U"插槽：")+name); reveal_property(slot_choice);
        }
        seek(clip->track_get_key_time(track,key)); return;
    }
	Vector3 value=raw;
	Dictionary data=Dictionary(scene->get_entities()[owner->get_selected_id()]).get("animation",Dictionary()); PackedInt64Array targets=data.get("targets",PackedInt64Array());
	if(track<targets.size()) { select_target(targets[track]); }
	String field=clip->track_get_path(track).get_concatenated_subnames(); for(int i=0;i<property->get_item_count();i++) { if(property->get_item_text(i)==field) { property->select(i); } }
	x->set_value(value.x); y->set_value(value.y); z->set_value(value.z); curve_ease->set_value(clip->track_get_key_transition(track,key)); curve_interpolation->select(MIN(2,int(clip->track_get_interpolation_type(track)))); seek(clip->track_get_key_time(track,key));
}
void ECSAnimationEditor::move_key(int track,int key,double value) {
	Ref<Animation> source=current_clip(); if(timeline_locked || source.is_null() || track<0 || track>=source->get_track_count() || key<0 || key>=source->track_get_key_count(track)) { return; }
	value=MAX(0.0,value); if(Math::is_equal_approx(source->track_get_key_time(track,key),value)) { return; }
	int existing=source->track_find_key(track,value,Animation::FIND_MODE_APPROX); if(existing>=0 && existing!=key) { feedback->set_text(String(U"目标时刻已有关键帧，未覆盖。")); return; }
	if(timeline_drag_mode!=0) {
		double old=source->track_get_key_time(track,key); if(timeline_drag_mode==2 && old<=0) { feedback->set_text(String(U"第 0 帧不能作为时间缩放手柄。")); return; }
		Vector<double> times; for(int k=0;k<source->track_get_key_count(track);k++) { double t=source->track_get_key_time(track,k); double next=timeline_drag_mode==1?(t>=old?t+value-old:t):t*value/old; next=Math::snapped(next,1.0/timeline_fps); if(next<0 || (!times.is_empty() && next<=times[times.size()-1])) { feedback->set_text(String(U"调整会使关键帧重叠或早于第 0 帧，未修改。")); return; } times.push_back(next); }
		Ref<Animation> clip=source->duplicate(true); for(int k=clip->track_get_key_count(track)-1;k>=0;k--) { clip->track_remove_key(track,k); }
		for(int k=0;k<times.size();k++) { clip->track_insert_key(track,times[k],source->track_get_key_value(track,k),source->track_get_key_transition(track,k)); }
		clip->set_length(MAX(clip->get_length(),times[times.size()-1])); Dictionary definition=Dictionary(scene->get_entities()[owner->get_selected_id()]).get("animation",Dictionary()); definition=definition.duplicate(true); definition["clip"]=clip; commit_animation(definition,String(U"调整轨道时间")); selected_key=Vector2i(track,key); seek(value); return;
	}
	Ref<Animation> clip=source->duplicate(true); Variant v=clip->track_get_key_value(track,key); double transition=clip->track_get_key_transition(track,key); clip->track_remove_key(track,key); int inserted=clip->track_insert_key(track,value,v,transition); clip->set_length(MAX(clip->get_length(),value));
	Dictionary data=Dictionary(Dictionary(scene->get_entities()[owner->get_selected_id()]).get("animation",Dictionary())).duplicate(true); data["clip"]=clip; commit_animation(data,String(U"移动动画关键帧")); selected_key=Vector2i(track,inserted); seek(value);
}
void ECSAnimationEditor::pose_edited(int entity,const String &field,const Vector3 &value) {
	if(!animation_mode) { return; } target->select(entity); for(int i=0;i<property->get_item_count();i++) { if(property->get_item_text(i)==field) { property->select(i); } }
	x->set_value(value.x); y->set_value(value.y); z->set_value(value.z); if(auto_key->is_pressed()) { action(2); seek(time->get_value()); } refresh_canvas_values();
}
void ECSAnimationEditor::stop_preview() {
	playing=false;
	if(canvas) { canvas->stop_animation_preview(); }
}
void ECSAnimationEditor::select_target(int index) {
	if(index<0) { index=target->get_selected_id(); }
	if(scene.is_null() || index<0 || index>=target->get_item_count()) { return; }
	Dictionary chosen=scene->get_entities()[index];
	if(!animation_mode && brush_enabled->is_pressed() && chosen.has("bone_2d") && image_selection>=0) {
		int brush_index=brush_bone->get_item_index(index); if(brush_index>=0) { brush_bone->select(brush_index); configure_brush(); }
		local_canvas->edit_scene(scene,image_selection); return;
	}
	if(chosen.has("polygon_2d")) { image_selection=index; } else { brush_enabled->set_pressed(false); mesh_edit->set_pressed(false); }
	int rig=find_rig(index); if(rig>=0 && owner->get_selected_id()!=rig) { owner->select(rig); refresh_tracks(); }
	if(!navigating_history && (selection_history_cursor<0 || selection_history[selection_history_cursor]!=index)) {
		selection_history.resize(selection_history_cursor+1); selection_history.push_back(index); selection_history_cursor=selection_history.size()-1;
	}
	target->select(index);
	refreshing=true;
	for(TreeItem *row=hierarchy->get_root()?hierarchy->get_root()->get_next_in_tree():nullptr;row;row=row->get_next_in_tree()) { if(row->get_metadata(0).get_type()==Variant::INT && int(row->get_metadata(0))==index) { row->select(0); break; } }
	refreshing=false;
	local_canvas->edit_scene(scene,index);
	if(animation_mode) { seek(time->get_value()); }
	Dictionary e=scene->get_entities()[index];
	String field=property->get_item_text(property->get_selected());
	Vector3 value=animation_mode?local_canvas->get_authoring_vector(index,field):Vector3(e.get(field,field=="scale"?Vector3(1,1,1):Vector3()));
	x->set_value(value.x); y->set_value(value.y); z->set_value(value.z); selection_title->set_text((e.has("bone_2d")?String(U"骨骼："):e.has("polygon_2d")?String(U"图片："):String(U"骨架："))+String(e.get("name","Entity"))); refresh_canvas_values(); refresh_selection_properties(); refresh_slot_tools();
    if(!animation_mode && canvas_tools[1]->is_pressed() && chosen.has("polygon_2d")) { image_action(3); }
}
void ECSAnimationEditor::edit_scene(const Ref<ECSScene> &value,ECSUICanvasEditor *) {
	++skeleton_ai_revision;
	if(independent_project && scene!=value) { return; }
	refreshing=true; int previous=owner->get_selected_id(), previous_target=target->get_selected_id(), previous_brush=brush_bone->get_selected_id(); bool different=scene!=value;
	if(different) { selection_history.clear(); selection_history_cursor=-1; timeline->reset_view(); image_selection=-1; mesh_edit->set_pressed(false); brush_enabled->set_pressed(false); editing_state=String(); playing=false; selected_key=Vector2i(-1,-1); }
	scene=value; canvas=local_canvas; local_canvas->edit_scene(value,MAX(0,previous_target)); local_canvas->set_keyframe_edit_mode(animation_mode);
	owner->clear(); target->clear(); hierarchy->clear(); brush_bone->clear();
	if(scene.is_null()) { animation_list->clear(); refreshing=false; return; }
	Array entities=scene->get_entities(); auto *root=hierarchy->create_item(); Vector<TreeItem *> rows;
	for(int i=0;i<entities.size();i++) { Dictionary e=entities[i]; String label=String(e.get("name","Entity")); if(e.has("bone_2d")) { brush_bone->add_item(label,i); } owner->add_item(itos(i)+"  "+label,i); target->add_item(label,i); auto *item=hierarchy->create_item(root); item->set_text(0,e.has("polygon_2d")?label.get_slice(" / ",label.get_slice_count(" / ")-1):label); item->set_tooltip_text(0,label); item->set_metadata(0,i); item->set_icon(0,skeleton_workspace_icon(e.has("bone_2d")?"bone":e.has("skeleton_2d")?"skeleton":"attachment")); item->set_custom_minimum_height(24*EDSCALE); item->set_icon_max_width(0,20*EDSCALE);
        item->add_button(1,skeleton_workspace_icon("eye"),0,false,String(U"切换此对象在画布中的显示（不写入动画）")); item->add_button(2,EditorNode::get_singleton()->get_editor_theme()->get_icon("Lock","EditorIcons"),1,false,String(U"锁定此对象的画布选择与拖动，属性仍可编辑"));
        item->set_button_color(1,0,local_canvas->is_authoring_hidden(i)?Color(.4,.42,.44):Color(.85,.92,.95)); item->set_button_color(2,0,local_canvas->is_authoring_locked(i)?Color(1,.65,.32):Color(.42,.46,.49));
        rows.push_back(item); }
	for(int i=0;i<entities.size();i++) { int parent=Dictionary(entities[i]).get("parent",-1); if(parent>=0 && parent<i) { root->remove_child(rows[i]); rows[parent]->add_child(rows[i]); } }
	build_slot_hierarchy(entities,rows);
	if(previous>=0 && previous<entities.size()) { owner->select(previous); }
	if(previous_target>=0 && previous_target<entities.size()) { target->select(previous_target); }
	for(int i=0;i<brush_bone->get_item_count();i++) { if(brush_bone->get_item_id(i)==previous_brush) { brush_bone->select(i); break; } }
	configure_brush(); filter_hierarchy(hierarchy_search->get_text());
	refreshing=false; refresh_tracks(); refresh_canvas_values(); refresh_selection_properties(); refresh_slot_tools();
	if(animation_mode && is_visible_in_tree()) { bool was_playing=playing; seek(time->get_value()); playing=was_playing; }
}
double ECSAnimationEditor::detect_timeline_fps(const Ref<Animation> &clip) {
    if(clip.is_null()) { return 30; }
    if(double(clip->get_meta("agechaos_timeline_fps",0))==60) { return 60; }
    // Older imports have no metadata. Recognize their 60 Hz samples without rewriting keys.
    for(int t=0;t<clip->get_track_count();t++) { for(int k=0;k<clip->track_get_key_count(t);k++) {
        double at=clip->track_get_key_time(t,k);
        if(Math::abs(at*30-Math::round(at*30))>.001 && Math::abs(at*60-Math::round(at*60))<.001) { return 60; }
    } }
    return 30;
}
void ECSAnimationEditor::refresh_tracks(int) {
	states->clear(); animation_list->clear(); timeline->queue_redraw();
	if(scene.is_null() || owner->get_selected_id()<0) { return; }
	Dictionary entity=scene->get_entities()[owner->get_selected_id()], data=entity.get("animation",Dictionary()); Ref<Animation> clip=current_clip();
    double next_fps=detect_timeline_fps(clip);
    if(next_fps!=timeline_fps) {
        double start_seconds=loop_start->get_value()/timeline_fps,end_seconds=loop_end->get_value()/timeline_fps;
        timeline->change_fps(next_fps/timeline_fps); timeline_fps=next_fps; loop_start->set_value_no_signal(start_seconds*timeline_fps); loop_end->set_value_no_signal(end_seconds*timeline_fps);
    }
    timeline_fps_label->set_text(itos(int(timeline_fps))+" FPS"); time->set_step(1.0/timeline_fps); frame->set_value_no_signal(Math::round(time->get_value()*timeline_fps));
	if(clip.is_null()) { return; }
	clip_length->set_value_no_signal(clip->get_length()); clip_loop->set_pressed_no_signal(clip->get_loop_mode()!=Animation::LOOP_NONE); loop_playback->set_pressed_no_signal(clip->get_loop_mode()!=Animation::LOOP_NONE);
	if(loop_end->get_value()>Math::round(clip->get_length()*timeline_fps) || loop_end->get_value()<=loop_start->get_value()) { loop_start->set_value(0); loop_end->set_value(Math::round(clip->get_length()*timeline_fps)); }
	Dictionary named=data.get("states",Dictionary());
	animation_list->add_item(String(U"默认动画"),skeleton_workspace_icon("animation")); animation_list->set_item_metadata(0,String());
	for(const Variant &key:named.keys()) { states->add_item(key); animation_list->add_item(key,skeleton_workspace_icon("animation")); animation_list->set_item_metadata(animation_list->get_item_count()-1,key); if(String(key)==editing_state) { states->select(states->get_item_count()-1); } }
	for(int i=0;i<animation_list->get_item_count();i++) { if(String(animation_list->get_item_metadata(i))==editing_state) { animation_list->select(i); } }
}
void ECSAnimationEditor::commit_animation(const Dictionary &definition,const String &label,bool edit_clip) {
	Dictionary final=definition.duplicate(true); Dictionary prior=Dictionary(scene->get_entities()[owner->get_selected_id()]).get("animation",Dictionary());
	if(edit_clip) {
		String state=editing_state.is_empty()?String(prior.get("state",String())):editing_state;
		if(!state.is_empty()) { Dictionary named=final.get("states",Dictionary()); named[state]=final["clip"]; final["states"]=named; }
		if(!editing_state.is_empty() && String(prior.get("state",String()))!=editing_state) { final["clip"]=prior.get("clip",Variant()); }
	}
	Array before=scene->get_entities(), after=before.duplicate(true); Dictionary entity=after[owner->get_selected_id()]; entity["animation"]=final;
	Ref<ECSScene> check; check.instantiate(); check->set_entities(after); if(check->instantiate().is_null()) { feedback->set_text(String(U"动画配置无效，未保存。")); return; }
	auto *undo=EditorUndoRedoManager::get_singleton(); undo->create_action(label,UndoRedo::MERGE_DISABLE,scene.ptr()); undo->add_do_method(scene.ptr(),"set_entities",after); undo->add_undo_method(scene.ptr(),"set_entities",before); undo->commit_action(); refresh_tracks();
}
void ECSAnimationEditor::action(int command) {
	if(timeline_locked && (command==2 || command==3 || command==20)) { return; }
	if(!animation_mode && (command==1 || command==2 || command==3 || command==5 || command==7 || command==8 || command==9 || command==15 || command==20)) { return; }
	if(scene.is_null() || owner->get_selected_id()<0) { return; }
	if(command==0) { edit_scene(scene,canvas); return; }
	if(command==23 || command==24) {
		if(!animation_mode) { return; }
		String error; if(!configure_animation(command==23?state_name->get_text().strip_edges():editing_state,clip_length->get_value(),clip_loop->is_pressed(),command==23,error)) { feedback->set_text(error); }
		return;
	}
	if(command==25) { Dictionary event; event["name"]=event_name->get_text().strip_edges(); event["int"]=int64_t(event_integer->get_value()); event["float"]=event_number->get_value(); event["string"]=event_text->get_text(); insert_channel_key("event",event); return; }
	if(command==2) {
		Dictionary request; request["operation"]="key"; request["revision"]=skeleton_ai_revision; request["entity"]=target->get_selected_id(); request["animation"]=editing_state;
		request["field"]=property->get_item_text(property->get_selected()); request["time"]=time->get_value(); request["value"]=ECSAIValue::encode(Vector3(x->get_value(),y->get_value(),z->get_value()));
		Dictionary reply=skeleton_ai_request(request); if(!bool(reply["ok"])) { feedback->set_text(reply.get("error",String())); } return;
	}
	if(command==16 || command==17) { auto *undo=EditorUndoRedoManager::get_singleton(); if(command==16) { undo->undo(); } else { undo->redo(); } edit_scene(scene,canvas); return; }
	Dictionary entity=scene->get_entities()[owner->get_selected_id()], data=Dictionary(entity.get("animation",Dictionary())).duplicate(true); Ref<Animation> clip=current_clip();
	if(command==20) {
		if(clip.is_null() || selected_key.x<0 || selected_key.x>=clip->get_track_count() || selected_key.y<0 || selected_key.y>=clip->track_get_key_count(selected_key.x)) { feedback->set_text(String(U"先选择时间轴关键帧。")); return; }
		String field=clip->track_get_path(selected_key.x).get_concatenated_subnames(); if(field.begins_with("event:") || field.ends_with(":attachment") || field.ends_with(":z_index")) { feedback->set_text(String(U"事件、附件和绘制顺序使用离散关键帧，不应用插值曲线。")); return; }
		clip=clip->duplicate(true); clip->track_set_interpolation_type(selected_key.x,Animation::InterpolationType(curve_interpolation->get_selected())); clip->track_set_key_transition(selected_key.x,selected_key.y,curve_ease->get_value()); data["clip"]=clip; commit_animation(data,String(U"编辑动画曲线")); seek(time->get_value()); return;
	}
	if(command==21 || command==22) {
		int rig=owner->get_selected_id(); Array entities=scene->get_entities().duplicate(true); Dictionary e=entities[rig]; if(!e.has("skeleton_2d")) { feedback->set_text(String(U"顶部请选择骨架实体。")); return; } Dictionary skeleton=e["skeleton_2d"]; Array constraints;
		if(command==21) { PackedInt64Array bones=skeleton.get("bones",PackedInt64Array()); PackedInt32Array chain; int bone=target->get_selected_id(); for(int i=0;i<int(ik_length->get_value());i++) { int index=bones.find(bone); if(index<0) { feedback->set_text(String(U"所选骨骼父链不够长。")); return; } chain.insert(0,index); bone=Dictionary(entities[bone]).get("parent",-1); } Dictionary c; c["chain"]=chain; c["target"]=Vector2(ik_x->get_value(),ik_y->get_value()); constraints.push_back(c); }
		skeleton["ik"]=constraints; commit_entities(entities,String(U"编辑骨架 IK")); return;
	}
	if(command==13) { playing=false; return; }
	if(command==14) {
		if(animation_mode) { action(2); seek(time->get_value()); }
		else {
			int selected=target->get_selected_id();
			Array after=compensated_pose(selected,property->get_item_text(property->get_selected()),Vector3(x->get_value(),y->get_value(),z->get_value()));
			if(!after.is_empty()) { commit_entities(after,String(U"编辑绑定姿态与补偿")); select_target(selected); }
			else { refresh_canvas_values(); }
		}
		return;
	}
	if(command==6) { stop_preview(); time->set_value_no_signal(0); timeline->queue_redraw(); return; }
	if(command==12) {
		int polygon_index=owner->get_selected_id(),rig_index=target->get_selected_id();
		Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { return; } PackedInt64Array ids=world->query(PackedStringArray(),true);
		Dictionary polygon=world->get_polygon_2d(ids[polygon_index]),rig=world->get_skeleton_2d(ids[rig_index]);
		if(polygon.is_empty() || rig.is_empty()) { feedback->set_text(String(U"上方选择 Polygon2D 实体，目标选择 Skeleton2D 实体。")); return; }
		PackedInt64Array bones=rig["bones"]; PackedVector2Array points=polygon["polygon"]; PackedInt32Array indices; PackedFloat32Array weights;
		Transform3D transform=world->get_global_transform(ids[polygon_index]);
		for(const Vector2 &point:points) {
			Vector3 p=transform.xform(Vector3(point.x,point.y,0)); double best[4]={1e30,1e30,1e30,1e30}; int selected[4]={0,0,0,0};
			for(int i=0;i<bones.size();i++) { Transform3D bone=world->get_global_transform(bones[i]); Vector3 end=bone.xform(Vector3(double(world->get_bone_2d(bones[i])["length"]),0,0)); Vector2 segment[2]={Vector2(bone.origin.x,bone.origin.y),Vector2(end.x,end.y)}; double distance=Geometry2D::get_closest_point_to_segment(Vector2(p.x,p.y),segment).distance_squared_to(Vector2(p.x,p.y)); for(int slot=0;slot<4;slot++) { if(distance<best[slot]) { for(int j=3;j>slot;j--) { best[j]=best[j-1]; selected[j]=selected[j-1]; } best[slot]=distance; selected[slot]=i; break; } } }
			double sum=0; for(int j=0;j<MIN(4,bones.size());j++) { sum+=1.0/MAX(1.0,best[j]); }
			for(int j=0;j<4;j++) { indices.push_back(selected[j]); weights.push_back(j<bones.size()?(1.0/MAX(1.0,best[j]))/sum:0); }
		}
		Array before=scene->get_entities(),after=before.duplicate(true); Dictionary e=after[polygon_index], authored=e["polygon_2d"]; authored["skeleton"]=rig_index; authored["bones"]=indices; authored["weights"]=weights;
		auto *undo=EditorUndoRedoManager::get_singleton(); undo->create_action(String(U"自动分配 2D 蒙皮权重"),UndoRedo::MERGE_DISABLE,scene.ptr()); undo->add_do_method(scene.ptr(),"set_entities",after); undo->add_undo_method(scene.ptr(),"set_entities",before); undo->commit_action(); return;
	}
	if(command==11) {
		Ref<ECSWorld> world=scene->instantiate(); if(world.is_null()) { return; } PackedInt64Array ids=world->query(PackedStringArray(),true); Dictionary rig=world->get_skeleton_2d(ids[owner->get_selected_id()]); if(rig.is_empty()) { feedback->set_text(String(U"请选择带 Skeleton2D 组件的实体。")); return; }
		rig["bind_poses"]=Array(); if(!world->set_skeleton_2d(ids[owner->get_selected_id()],rig)) { return; }
		Array before=scene->get_entities(),after=before.duplicate(true); Dictionary e=after[owner->get_selected_id()], authored=e["skeleton_2d"]; authored["bind_poses"]=world->get_skeleton_2d(ids[owner->get_selected_id()])["bind_poses"];
		auto *undo=EditorUndoRedoManager::get_singleton(); undo->create_action(String(U"绑定 2D 骨架姿态"),UndoRedo::MERGE_DISABLE,scene.ptr()); undo->add_do_method(scene.ptr(),"set_entities",after); undo->add_undo_method(scene.ptr(),"set_entities",before); undo->commit_action(); return;
	}
	if(command==1) { if(clip.is_valid()) { feedback->set_text(String(U"已有动画，直接插入关键帧。")); return; } clip.instantiate(); clip->set_length(4); clip->set_loop_mode(Animation::LOOP_LINEAR); data["clip"]=clip; data["targets"]=PackedInt64Array(); commit_animation(data,String(U"创建 ECS 动画")); return; }
	if(clip.is_null()) { feedback->set_text(String(U"请先创建动画。")); return; }
	if(command==4 || command==5 || command==10) { seek(time->get_value()); playing=command==5; playback_time=time->get_value(); return; }
	clip=clip->duplicate(true); if(command==2 || command==3) { data["clip"]=clip; }
	String current=data.get("state",String()); Dictionary named=data.get("states",Dictionary()); if((command==2 || command==3) && editing_state.is_empty() && !current.is_empty()) { named[current]=clip; data["states"]=named; }
	if(command==3) { Vector2i key=selected_key; if(key.x<0 || key.x>=clip->get_track_count() || key.y<0 || key.y>=clip->track_get_key_count(key.x)) { return; } clip->track_remove_key(key.x,key.y); selected_key=Vector2i(-1,-1); }
	else if(command==7) { String name=state_name->get_text().strip_edges(); if(name.is_empty() || named.has(name)) { feedback->set_text(String(U"请填写未使用的状态名。")); return; } named[name]=clip->duplicate(true); data["states"]=named; }
	else if(command==15) { String name=state_name->get_text().strip_edges(); if(editing_state.is_empty() || name.is_empty() || named.has(name)) { feedback->set_text(String(U"选择已命名动画并输入未使用的新名称。")); return; } named[name]=named[editing_state]; named.erase(editing_state); if(current==editing_state) { data["state"]=name; } editing_state=name; data["states"]=named; }
	else if(command==8 || command==9) { if(states->get_selected()<0) { return; } String name=states->get_item_text(states->get_selected()); if(command==8) { named.erase(name); if(editing_state==name) { editing_state=String(); } if(current==name) { data["state"]=String(); } data["states"]=named; } else { data["clip"]=named[name]; data["state"]=name; data["time"]=0.0; data["blend_duration"]=duration->get_value(); } }
	else { return; }
	commit_animation(data,String(U"编辑 ECS 动画"),command==2 || command==3);
}

bool ECSAnimationEditor::run_self_test() {
	if(scene.is_null()) { return false; }
	auto verify = [](bool passed, int line) { if(!passed) { print_line("SKELETON_AUTHOR_CHECK_FAILED line="+itos(line)); } return passed; };
	// The regression must not assume a particular project is already open.
	const String fixture_path="user://skeleton-authoring-fixture.tres";
	if(!ECSWorld::skeletal_2d_self_test(fixture_path)) { return false; }
	Ref<ECSScene> fixture_scene=ResourceLoader::load(fixture_path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE);
	if(fixture_scene.is_null()) { return false; }
	Ref<ECSScene> original=scene; ECSUICanvasEditor *view=canvas;
	const bool original_independent=independent_project;
	if(original_independent && scene->is_connected("changed",callable_mp(this,&ECSAnimationEditor::project_changed))) { scene->disconnect("changed",callable_mp(this,&ECSAnimationEditor::project_changed)); }
	independent_project=false;
	Ref<ECSScene> test; test.instantiate(); test->set_entities(fixture_scene->get_entities().duplicate(true)); edit_scene(test,nullptr);
	set_mode(0); bool modes_ok=!animation_mode && animation_buttons[0]->is_disabled(); mode_button->emit_signal("pressed"); modes_ok &= verify(animation_mode && !animation_buttons[0]->is_disabled(), __LINE__);
	owner->select(0); refresh_tracks(); target->select(2); property->select(1); time->set_value(.5); z->set_value(.4); action(2);
	Dictionary entity=test->get_entities()[0],data=entity["animation"]; Ref<Animation> clip=data["clip"];
	int key=clip->track_find_key(0,.5,Animation::FIND_MODE_APPROX); bool ok=timeline->run_layout_self_test() && run_compensation_self_test() && modes_ok && key>=0 && Vector3(clip->track_get_key_value(0,key)).is_equal_approx(Vector3(0,0,.4));
	seek(.5); ok &= verify(Math::abs(canvas_values[0]->get_value()-double(Math::rad_to_deg(local_canvas->get_authoring_vector(target->get_selected_id(),"rotation").z)))<.011, __LINE__);
	move_key(0,key,.75); clip=current_clip(); ok &= verify(clip->track_find_key(0,.75,Animation::FIND_MODE_APPROX)>=0 && clip->track_find_key(0,.5,Animation::FIND_MODE_APPROX)<0, __LINE__);
	auto *undo=EditorUndoRedoManager::get_singleton(); ok &= verify(undo->undo(), __LINE__); clip=current_clip(); ok &= verify(clip->track_find_key(0,.5,Animation::FIND_MODE_APPROX)>=0, __LINE__); ok &= verify(undo->redo(), __LINE__);
	String save_path="user://ecs-animation-window-test.tres"; ok &= verify(ResourceSaver::save(test,save_path)==OK, __LINE__); Ref<ECSScene> loaded=ResourceLoader::load(save_path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE); ok &= verify(loaded.is_valid() && loaded->instantiate().is_valid(), __LINE__);
	selected_key=Vector2i(0,0); timeline_action(3); frame->set_value(41); timeline_action(5); ok &= verify(current_clip()->track_find_key(0,41.0/timeline_fps,Animation::FIND_MODE_APPROX)>=0, __LINE__); ok &= verify(undo->undo(), __LINE__);
	frame->set_value(41); transport(1); ok &= verify(Math::is_equal_approx(time->get_value(),40.0/timeline_fps), __LINE__);
	int locked_count=current_clip()->track_get_key_count(0); timeline_action(2); action(2); ok &= verify(current_clip()->track_get_key_count(0)==locked_count, __LINE__); timeline_action(2);
	transport(2); ok &= verify(playing && playback_direction==-1, __LINE__); transport(6); ok &= verify(!playing, __LINE__);
	auto_key->set_pressed(false); int manual_count=current_clip()->track_get_key_count(0); pose_edited(2,"rotation",Vector3(0,0,.7)); ok &= verify(current_clip()->track_get_key_count(0)==manual_count, __LINE__); auto_key->set_pressed(true);
	state_name->set_text("test_state"); action(7); data=Dictionary(test->get_entities()[0])["animation"]; ok &= verify(Dictionary(data["states"]).has("test_state"), __LINE__);
	editing_state="test_state"; refresh_tracks(); int moved=current_clip()->track_find_key(0,.75,Animation::FIND_MODE_APPROX); move_key(0,moved,.8); ok &= verify(current_clip()->track_find_key(0,.8,Animation::FIND_MODE_APPROX)>=0, __LINE__);
	state_name->set_text("renamed_test"); action(15); ok &= verify(Dictionary(Dictionary(Dictionary(test->get_entities()[0])["animation"])["states"]).has("renamed_test"), __LINE__);
	// The GUI key button must extend all named layouts just like AI authoring does.
	int before_tracks=current_clip()->get_track_count(); target->select(2); property->select(3); x->set_value(.2); y->set_value(-.1); z->set_value(0); time->set_value_no_signal(.4); action(2);
	ok &= verify(current_clip()->get_track_count()==before_tracks+1, __LINE__);
	data=Dictionary(test->get_entities()[0])["animation"]; Ref<Animation> unchanged=data["clip"];
	ok &= verify(unchanged->get_track_count()==before_tracks+1 && Vector3(unchanged->track_get_key_value(before_tracks,0))==Vector3(), __LINE__);
	ok &= verify(undo->undo(), __LINE__);
	if(!ok) { print_line("CHARACTER_CHECKPOINT_GUI_KEY_FAIL"); }
	String settings_error; ok &= verify(configure_animation("walk_clean",1,true,true,settings_error), __LINE__);
	ok &= verify(current_clip()->get_loop_mode()==Animation::LOOP_LINEAR && current_clip()->track_get_key_count(0)==1, __LINE__);
	ok &= verify(configure_animation("walk_clean",1,false,false,settings_error), __LINE__);
	ok &= verify(current_clip()->get_loop_mode()==Animation::LOOP_NONE, __LINE__);
	Ref<ECSWorld> playback=test->instantiate(); auto playback_ids=playback->query(PackedStringArray(),true);
	ok &= verify(playback->travel_animation(playback_ids[0],"walk_clean",0), __LINE__);
	playback->advance_animation_preview(1.1); ok &= verify(!bool(playback->get_animation(playback_ids[0])["playing"]), __LINE__);
	if(!ok) { print_line("CHARACTER_CHECKPOINT_SETTINGS_FAIL "+settings_error); }
	ok &= verify(undo->undo(), __LINE__); ok &= verify(undo->undo(), __LINE__); editing_state="renamed_test"; refresh_tracks();
	editing_state=String(); owner->select(3); target->select(0); action(12); Dictionary polygon=Dictionary(test->get_entities()[3])["polygon_2d"];
	ok &= verify(int(polygon["skeleton"])==0 && PackedFloat32Array(polygon["weights"]).size()==24, __LINE__);
	Ref<ECSWorld> world=test->instantiate(); ok &= verify(world.is_valid(), __LINE__);
	editing_state=String(); owner->select(0); refresh_tracks(); Array bind_before=test->get_entities().duplicate(true); time->set_value_no_signal(.9); pose_edited(2,"rotation",Vector3(0,0,.2));
	ok &= verify(Vector3(Dictionary(test->get_entities()[2]).get("rotation",Vector3())).is_equal_approx(Vector3(Dictionary(bind_before[2]).get("rotation",Vector3()))), __LINE__);
	ok &= verify(current_clip()->track_find_key(0,.9,Animation::FIND_MODE_APPROX)>=0, __LINE__);
	// Curves change interpolation without modifying the setup pose.
	selected_key=Vector2i(0,0); curve_ease->set_value(2); curve_interpolation->select(1); action(20); ok &= verify(Math::is_equal_approx(current_clip()->track_get_key_transition(0,0),real_t(2)), __LINE__);
	ok &= verify(local_canvas->run_weight_brush_self_test(test), __LINE__);
	Dictionary brush=polygon.duplicate(true); Dictionary painted=ECSUICanvasEditor::paint_weights(brush,Transform3D(),Vector2(0,-30),100,1,.5); PackedFloat32Array painted_weights=painted["weights"]; PackedInt32Array painted_bones=painted["bones"];
	float selected_weight=0; for(int j=0;j<4;j++) { if(painted_bones[j]==1) { selected_weight+=painted_weights[j]; } } ok &= verify(selected_weight>.1, __LINE__);
	for(int i=0;i<painted_weights.size();i+=4) { float sum=0; for(int j=0;j<4;j++) { sum+=painted_weights[i+j]; ok &= verify(painted_weights[i+j]>=0, __LINE__); } ok &= verify(Math::is_equal_approx(sum,1.0f), __LINE__); }
	// Synthetic shortest-arc regression: independently chosen times, angles and names.
	String angle_fixture="user://spine-angle-wrap-test.json";
	Ref<FileAccess> angle_file=FileAccess::open(angle_fixture,FileAccess::WRITE);
	angle_file->store_string(R"({"bones":[{"name":"test_joint"}],"animations":{"arc_test":{"bones":{"test_joint":{"rotate":[{"time":0,"angle":0},{"time":0.25,"angle":18},{"time":0.75,"angle":345},{"time":1.25,"angle":0}]}}}}})"); angle_file.unref();
	String angle_report; Ref<ECSScene> angle_scene=ecs_import_spine_json(angle_fixture,angle_report); ok &= verify(angle_scene.is_valid(), __LINE__);
	if(angle_scene.is_valid()) {
		Ref<Animation> angle_clip=Dictionary(Dictionary(angle_scene->get_entities()[0])["animation"])["clip"];
		double previous=0;
		for(int k=0;k<angle_clip->track_get_key_count(1);k++) { double value=Vector3(angle_clip->track_get_key_value(1,k)).z; ok &= verify(Math::abs(value)<.5 && Math::abs(value-previous)<.1, __LINE__); previous=value; }
		ok &= verify(Math::abs(previous)<.00001, __LINE__);
	}
	String fixture="user://spine-native-test.json"; Ref<FileAccess> file=FileAccess::open(fixture,FileAccess::WRITE);
	file->store_string(R"JSON({"bones":[{"name":"root","length":100},{"name":"tip","parent":"root","x":100,"length":100}],"animations":{"wave":{"bones":{"tip":{"rotate":[{"angle":0,"curve":[0.25,0,0.75,1]},{"time":1,"angle":90}]}}}}})JSON"); file.unref();
	String report; Ref<ECSScene> imported=ecs_import_spine_json(fixture,report); ok &= verify(imported.is_valid(), __LINE__); if(imported.is_valid()) { Ref<ECSWorld> w=imported->instantiate(); ok &= verify(w.is_valid(), __LINE__); if(w.is_valid()) { w->advance_animation_preview(.5); auto ids=w->query(PackedStringArray(),true); ok &= verify(Math::abs(w->get_vector(ids[2],"rotation").z+Math::PI*.25)<.01, __LINE__); } }
	Ref<Image> test_image=Image::create_empty(16,8,false,Image::FORMAT_RGBA8); test_image->fill(Color(.3,.7,1)); ok &= verify(test_image->save_png("user://spine-atlas-page.png")==OK, __LINE__);
	file=FileAccess::open("user://spine-native-test.atlas",FileAccess::WRITE); file->store_string("spine-atlas-page.png\nsize: 16,8\nformat: RGBA8888\nfilter: Linear,Linear\nrepeat: none\npiece\n  rotate: false\n  xy: 0,0\n  size: 16,8\n  orig: 16,8\n  offset: 0,0\n  index: -1\n"); file.unref();
	file=FileAccess::open(fixture,FileAccess::WRITE); file->store_string(R"JSON({"bones":[{"name":"root","length":100}],"slots":[{"name":"body","bone":"root","attachment":"piece"}],"skins":{"default":{"body":{"piece":{"type":"mesh","uvs":[0,0,1,0,0,1],"triangles":[0,1,2],"vertices":[1,0,-8,4,1,1,0,8,4,1,1,0,-8,-4,1]}}}}})JSON"); file.unref();
	imported=ecs_import_spine_json(fixture,report); ok &= verify(imported.is_valid(), __LINE__); if(imported.is_valid()) { Dictionary mesh=Dictionary(imported->get_entities()[2])["polygon_2d"]; Ref<Texture2D> texture=mesh["texture"]; ok &= verify(texture.is_valid() && PackedFloat32Array(mesh["weights"]).size()==12, __LINE__); // The importer packs attachments into an extruded atlas; validate the content rectangle, not the page width.
        if(texture.is_valid()) {
            PackedVector2Array imported_uv=mesh["uv"];
            Vector2 page_size(texture->get_width(),texture->get_height());
            ok &= verify(imported_uv.size()==3, __LINE__);
            if(imported_uv.size()==3) {
                ok &= verify(((imported_uv[1]-imported_uv[0])*page_size).is_equal_approx(Vector2(16,0)), __LINE__);
                ok &= verify(((imported_uv[2]-imported_uv[0])*page_size).is_equal_approx(Vector2(0,8)), __LINE__);
                Ref<Image> pixels=texture->get_image(); Vector2 origin=imported_uv[0]*page_size;
                ok &= verify(pixels.is_valid() && pixels->get_pixel(int(origin.x),int(origin.y))==test_image->get_pixel(0,0), __LINE__);
            }
        }
        ok &= verify(ResourceSaver::save(imported,"user://spine-import-roundtrip.tres")==OK, __LINE__); Ref<ECSScene> roundtrip=ResourceLoader::load("user://spine-import-roundtrip.tres","ECSScene",ResourceLoader::CACHE_MODE_IGNORE); ok &= verify(roundtrip.is_valid() && roundtrip->instantiate().is_valid(), __LINE__); }
	set_mode(0); select_target(3); ok &= verify(owner->get_selected_id()==0 && image_selection==3, __LINE__);
	image_action(2); binding_bones->deselect_all(); binding_bones->select(0); binding_dialog->hide(); apply_binding();
	Dictionary bound=Dictionary(scene->get_entities()[3])["polygon_2d"]; PackedFloat32Array bound_weights=bound["weights"]; PackedInt32Array bound_bones=bound["bones"];
	for(int i=0;i<bound_weights.size();i++) { ok &= verify(bound_weights[i]==(i%4==0?1.0f:0.0f) && bound_bones[i]==0, __LINE__); }
	ok &= verify(undo->undo(), __LINE__); edit_scene(scene,nullptr); select_target(3);
	Dictionary before_mesh=Dictionary(scene->get_entities()[3])["polygon_2d"]; int old_count=PackedVector2Array(before_mesh["polygon"]).size();
	image_action(1); Dictionary divided=Dictionary(scene->get_entities()[3])["polygon_2d"];
	ok &= verify(PackedVector2Array(divided["polygon"]).size()>old_count && divided.get("texture",Variant())==before_mesh.get("texture",Variant()) && scene->instantiate().is_valid(), __LINE__);
	PackedFloat32Array divided_weights=divided["weights"]; for(int i=0;i<divided_weights.size();i+=4) { ok &= verify(Math::is_equal_approx(divided_weights[i]+divided_weights[i+1]+divided_weights[i+2]+divided_weights[i+3],1.0f), __LINE__); }
	ok &= verify(local_canvas->run_mesh_edit_self_test(scene), __LINE__);
	ok &= verify(ResourceSaver::save(scene,"user://mesh-workflow-roundtrip.tres")==OK, __LINE__);
	Ref<ECSScene> mesh_roundtrip=ResourceLoader::load("user://mesh-workflow-roundtrip.tres","ECSScene",ResourceLoader::CACHE_MODE_IGNORE); ok &= verify(mesh_roundtrip.is_valid() && mesh_roundtrip->instantiate().is_valid(), __LINE__);
	mesh_edit->set_pressed(false); image_action(3); select_target(1); ok &= verify(target->get_selected_id()==3 && brush_bone->get_selected_id()==1, __LINE__);
	brush_enabled->set_pressed(false);
	int image_count=scene->get_entities().size(); select_target(1); images_dropped(PackedStringArray({"user://spine-atlas-page.png"}),Vector2(123,45));
	ok &= verify(scene->get_entities().size()==image_count+1 && target->get_selected_id()==image_count, __LINE__);
	Ref<ECSWorld> dropped_world=scene->instantiate(); ok &= verify(dropped_world.is_valid(), __LINE__); if(dropped_world.is_valid()) { PackedInt64Array dropped_ids=dropped_world->query(PackedStringArray(),true); ok &= verify(dropped_world->get_global_transform(dropped_ids[image_count]).origin.is_equal_approx(Vector3(123,45,0)), __LINE__); }
	ok &= verify(undo->undo() && scene->get_entities().size()==image_count, __LINE__); edit_scene(scene,nullptr); set_mode(1);
	bool saved_independent=original_independent; pending_project_operation=-1; project_action(0); ok &= verify(independent_project && scene->get_entities().size()==2 && Vector3(Dictionary(scene->get_entities()[0])["position"])==Vector3(), __LINE__); project_action(5); ok &= verify(scene->get_entities().size()==3, __LINE__);
	if(scene->is_connected("changed",callable_mp(this,&ECSAnimationEditor::project_changed))) { scene->disconnect("changed",callable_mp(this,&ECSAnimationEditor::project_changed)); } independent_project=false;
	ok &= verify(run_workspace_tools_self_test(), __LINE__); ok &= verify(run_slot_hierarchy_self_test(), __LINE__); ok &= verify(workspace_docking->run_self_test(), __LINE__); edit_scene(original,view); independent_project=saved_independent; if(saved_independent) { scene->connect("changed",callable_mp(this,&ECSAnimationEditor::project_changed)); } if(ok) { print_line("ECS_ANIMATION_AUTHOR_PASS mesh_subdivision binding_selection mesh_drag_undo_cancel mesh_save_reload curve weight_stroke_undo_cancel weight_normalization spine_json atlas weighted_mesh import_roundtrip new_project add_bone mode_switch pose_keys_without_rest_mutation key_insert key_move undo_redo save_reload named_clip rename auto_weights valid_scene"); } return ok;
}
#endif
