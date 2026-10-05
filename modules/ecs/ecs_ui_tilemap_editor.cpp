#ifdef TOOLS_ENABLED
#include "ecs_ui_editor.h"
#include "scene/resources/2d/tile_set.h"
#include "editor/editor_undo_redo_manager.h"

bool ECSUICanvasEditor::tilemap_input(const Ref<InputEvent> &event) {
    if(runtime_preview || preview.is_null() || selected<0 || selected>=ids.size() || tilemap_source<0) { return false; }
    if(preview->get_tilemap_2d(ids[selected]).is_empty()) { return false; }
    Ref<InputEventKey> key=event;
    if(key.is_valid() && key->is_pressed() && key->get_keycode()==Key::ESCAPE) { finish_tilemap(false); return true; }
    Ref<InputEventMouseButton> button=event; Ref<InputEventMouseMotion> motion=event;
    if(button.is_valid() && (button->get_button_index()==MouseButton::LEFT || button->get_button_index()==MouseButton::RIGHT)) {
        if(!button->is_pressed()) { finish_tilemap(true); return true; }
        tilemap_before=Dictionary(scene->get_entities()[selected])["tilemap_2d"]; tilemap_work=tilemap_before.duplicate(true); tilemap_painting=true; tilemap_erasing=button->get_button_index()==MouseButton::RIGHT; tilemap_last_valid=false;
        tilemap_paint_at(button->get_position()); return true;
    }
    if(motion.is_valid() && tilemap_painting) { tilemap_paint_at(motion->get_position()); return true; }
    return false;
}
void ECSUICanvasEditor::tilemap_paint_at(const Vector2 &position) {
    Ref<TileSet> tiles=tilemap_work.get("tile_set",Variant()); if(tiles.is_null()) { return; }
    Transform3D transform=preview->get_global_transform(ids[selected]); if(Math::is_zero_approx(transform.basis.determinant())) { return; }
    Vector2 world=(position-pan)/display_scale(); Vector3 local=transform.affine_inverse().xform(Vector3(world.x,world.y,0)); Vector2i coord=tiles->local_to_map(Vector2(local.x,local.y));
    if(tilemap_last_valid && coord==tilemap_last_cell) { return; }
    PackedInt32Array cells=tilemap_work.get("cells",PackedInt32Array()); Vector2i first=tilemap_last_valid?tilemap_last_cell:coord;
    int steps=MAX(Math::abs(coord.x-first.x),Math::abs(coord.y-first.y)); if(steps>4096) { return; }
    for(int step=0;step<=steps;step++) {
        Vector2i at=Vector2(first).lerp(Vector2(coord),steps?double(step)/steps:0).round(); int found=-1;
        for(int i=0;i<cells.size();i+=6) { if(cells[i]==at.x && cells[i+1]==at.y) { found=i; break; } }
        if(tilemap_erasing) { if(found>=0) { for(int i=0;i<6;i++) { cells.remove_at(found); } } }
        else if(found>=0) { cells.set(found+2,tilemap_source); cells.set(found+3,tilemap_atlas.x); cells.set(found+4,tilemap_atlas.y); cells.set(found+5,tilemap_alternative); }
        else { cells.append_array(PackedInt32Array({at.x,at.y,tilemap_source,tilemap_atlas.x,tilemap_atlas.y,tilemap_alternative})); }
    }
    tilemap_work["cells"]=cells;
    if(preview->set_tilemap_2d(ids[selected],tilemap_work)) { preview->sync_skeletal_2d(viewport->get_viewport_rid()); tilemap_last_cell=coord; tilemap_last_valid=true; queue_redraw(); }
}
void ECSUICanvasEditor::finish_tilemap(bool commit) {
    if(!tilemap_painting) { return; } tilemap_painting=false; tilemap_last_valid=false;
    if(preview.is_valid() && selected>=0 && selected<ids.size()) { preview->set_tilemap_2d(ids[selected],tilemap_before); preview->sync_skeletal_2d(viewport->get_viewport_rid()); }
    Dictionary work=tilemap_work; bool changed=tilemap_before!=work; tilemap_before=Dictionary(); tilemap_work=Dictionary();
    if(commit && changed) { emit_signal("tilemap_edited",selected,work); } queue_redraw();
}
bool ECSUICanvasEditor::run_tilemap_self_test(const Ref<ECSScene> &value) {
    edit_scene(value,1); configure_tilemap_brush(0,Vector2i());
    Dictionary original=Dictionary(value->get_entities()[1])["tilemap_2d"]; int count=PackedInt32Array(original["cells"]).size(); Ref<TileSet> tiles=original["tile_set"];
    Vector2 cell=tiles->map_to_local(Vector2i(20,6)); Vector3 global=preview->get_global_transform(ids[1]).xform(Vector3(cell.x,cell.y,0)); Vector2 point=Vector2(global.x,global.y)*display_scale()+pan;
    auto click=[&](bool pressed,MouseButton button) { Ref<InputEventMouseButton> event; event.instantiate(); event->set_button_index(button); event->set_pressed(pressed); event->set_position(point); gui_input(event); };
    click(true,MouseButton::LEFT); click(false,MouseButton::LEFT);
    bool ok=PackedInt32Array(Dictionary(Dictionary(value->get_entities()[1])["tilemap_2d"])["cells"]).size()==count+6;
    ok &= EditorUndoRedoManager::get_singleton()->undo(); edit_scene(value,1);
    ok &= PackedInt32Array(Dictionary(Dictionary(value->get_entities()[1])["tilemap_2d"])["cells"]).size()==count;
    click(true,MouseButton::LEFT); Ref<InputEventKey> escape; escape.instantiate(); escape->set_keycode(Key::ESCAPE); escape->set_pressed(true); gui_input(escape); click(false,MouseButton::LEFT);
    ok &= PackedInt32Array(Dictionary(Dictionary(value->get_entities()[1])["tilemap_2d"])["cells"]).size()==count;
    click(true,MouseButton::LEFT); click(false,MouseButton::LEFT); click(true,MouseButton::RIGHT); click(false,MouseButton::RIGHT);
    ok &= PackedInt32Array(Dictionary(Dictionary(value->get_entities()[1])["tilemap_2d"])["cells"]).size()==count;
    configure_tilemap_brush(-1,Vector2i()); print_line(ok?"ECS_TILEMAP_BRUSH_PASS paint erase cancel undo":"ECS_TILEMAP_BRUSH_FAILED"); return ok;
}
#endif
