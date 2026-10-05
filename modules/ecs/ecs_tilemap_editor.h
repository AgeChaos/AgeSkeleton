#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"
#include "scene/gui/box_container.h"
class ECSUICanvasEditor;
class EditorFileDialog;
class ItemList;
class SpinBox;
class Label;
class CheckBox;
class ECSTilemapEditor : public VBoxContainer {
    GDCLASS(ECSTilemapEditor,VBoxContainer);
    Ref<ECSScene> scene;
    int entity=-1;
    ECSUICanvasEditor *canvas=nullptr;
    EditorFileDialog *file=nullptr;
    ItemList *palette=nullptr;
    SpinBox *tile_size=nullptr;
    Label *status=nullptr;
    CheckBox *collision=nullptr,*navigation=nullptr;
    void import_atlas();
    void atlas_selected(const String &p_path);
    void tile_selected(int p_index);
    void stop_brush();
protected:
    static void _bind_methods();
public:
    void edit(const Ref<ECSScene> &p_scene,int p_entity,ECSUICanvasEditor *p_canvas);
    ECSTilemapEditor();
};
#endif
