#ifdef TOOLS_ENABLED
#include "skeleton_scrub_spin_box.h"
#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/line_edit.h"

void SkeletonScrubSpinBox::_bind_methods() {
    ADD_SIGNAL(MethodInfo("scrub_started"));
    ADD_SIGNAL(MethodInfo("scrub_preview",PropertyInfo(Variant::FLOAT,"value")));
    ADD_SIGNAL(MethodInfo("scrub_finished",PropertyInfo(Variant::BOOL,"cancelled")));
}
SkeletonScrubSpinBox::SkeletonScrubSpinBox() {
    set_focus_mode(FOCUS_ALL);
    set_default_cursor_shape(CURSOR_HSIZE);
    get_line_edit()->set_mouse_filter(MOUSE_FILTER_IGNORE);
    get_line_edit()->connect("editing_toggled",callable_mp(this,&SkeletonScrubSpinBox::editing_changed));
    set_tooltip_text(TTR("Drag left or right to adjust. Shift: fine adjustment. Click to type. Esc: cancel drag."));
}
void SkeletonScrubSpinBox::editing_changed(bool editing) {
    get_line_edit()->set_mouse_filter(editing?MOUSE_FILTER_STOP:MOUSE_FILTER_IGNORE);
}
void SkeletonScrubSpinBox::finish(bool cancel) {
    if(!pending) { return; }
    bool dragged=scrubbing; pending=false; scrubbing=false;
    if(dragged) {
        if(cancel) { set_value_no_signal(initial); emit_signal("scrub_preview",get_value()); }
        emit_signal("scrub_finished",cancel);
        // Preview never modifies the document. Commit the final value exactly once.
        if(!cancel && !Math::is_equal_approx(initial,get_value())) { emit_signal("value_changed",get_value()); }
    } else if(!cancel) {
        get_line_edit()->set_mouse_filter(MOUSE_FILTER_STOP);
        get_line_edit()->grab_focus(); get_line_edit()->edit(); get_line_edit()->select_all();
    }
}
void SkeletonScrubSpinBox::_notification(int what) {
    if(what==NOTIFICATION_FOCUS_EXIT || what==NOTIFICATION_WM_WINDOW_FOCUS_OUT || what==NOTIFICATION_EXIT_TREE || (what==NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree())) { finish(true); }
}
void SkeletonScrubSpinBox::gui_input(const Ref<InputEvent> &event) {
    if(!is_editable()) { finish(true); return; }
    Ref<InputEventKey> key=event;
    if(key.is_valid() && key->is_pressed()) {
        if(key->get_keycode()==Key::ESCAPE && pending) { finish(true); accept_event(); return; }
        if(key->get_keycode()==Key::ENTER || key->get_keycode()==Key::KP_ENTER) { get_line_edit()->grab_focus(); get_line_edit()->edit(); get_line_edit()->select_all(); accept_event(); return; }
    }
    Ref<InputEventMouseButton> button=event;
    if(button.is_valid()) {
        if(button->get_button_index()==MouseButton::RIGHT && pending) { finish(true); accept_event(); return; }
        if(button->get_button_index()==MouseButton::LEFT) {
            if(button->is_pressed()) {
                grab_focus(); initial=get_value(); accumulated=0; origin=button->get_position(); pending=true; scrubbing=false;
            } else { finish(false); }
            accept_event(); return;
        }
    }
    Ref<InputEventMouseMotion> motion=event;
    if(motion.is_valid() && pending) {
        if(!motion->get_button_mask().has_flag(MouseButtonMask::LEFT)) { finish(true); return; }
        if(!scrubbing && Math::abs(motion->get_position().x-origin.x)<3*EDSCALE) { return; }
        double delta=motion->get_relative().x;
        if(!scrubbing) { scrubbing=true; delta=motion->get_position().x-origin.x; emit_signal("scrub_started"); }
        accumulated+=delta/EDSCALE*sensitivity*(motion->is_shift_pressed()?.1:1.);
        set_value_no_signal(initial+accumulated);
        emit_signal("scrub_preview",get_value()); accept_event(); return;
    }
    // Preserve native keyboard/text behavior without its vertical mouse-drag path.
    if(button.is_null() && motion.is_null()) { SpinBox::gui_input(event); }
}
#endif
