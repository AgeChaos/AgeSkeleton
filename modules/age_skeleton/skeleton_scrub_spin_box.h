#pragma once
#ifdef TOOLS_ENABLED
#include "scene/gui/spin_box.h"

// Editor-only number field: idle pointer drags scrub, a click enters text editing.
class SkeletonScrubSpinBox : public SpinBox {
    GDCLASS(SkeletonScrubSpinBox, SpinBox);
    bool pending=false, scrubbing=false;
    Vector2 origin;
    double initial=0, accumulated=0, sensitivity=.1;
    void editing_changed(bool editing);
    void finish(bool cancel);
protected:
    static void _bind_methods();
    void _notification(int what);
public:
    SkeletonScrubSpinBox();
    void set_scrub_sensitivity(double value) { sensitivity=value; }
    void gui_input(const Ref<InputEvent> &event) override;
};
#endif
