#ifdef TOOLS_ENABLED
#include "ecs_tilemap_editor.h"
#include "ecs_ui_editor.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "editor/gui/editor_file_dialog.h"
#include "scene/gui/item_list.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/check_box.h"
#include "scene/resources/2d/tile_set.h"
#include "scene/resources/2d/navigation_polygon.h"
#include "scene/resources/atlas_texture.h"

void ECSTilemapEditor::_bind_methods() { ADD_SIGNAL(MethodInfo("tilemap_edited",PropertyInfo(Variant::INT,"index"),PropertyInfo(Variant::DICTIONARY,"definition"))); }
ECSTilemapEditor::ECSTilemapEditor() {
    auto *bar=memnew(HBoxContainer); add_child(bar); auto *load=memnew(Button); load->set_text(String(U"从图片创建 TileSet")); bar->add_child(load); load->connect("pressed",callable_mp(this,&ECSTilemapEditor::import_atlas));
    auto *label=memnew(Label); label->set_text(String(U"瓦片尺寸")); bar->add_child(label); tile_size=memnew(SpinBox); tile_size->set_min(1); tile_size->set_max(512); tile_size->set_value(32); bar->add_child(tile_size);
    collision=memnew(CheckBox); collision->set_text(String(U"生成碰撞")); bar->add_child(collision); navigation=memnew(CheckBox); navigation->set_text(String(U"生成可行走区域")); bar->add_child(navigation);
    auto *stop=memnew(Button); stop->set_text(String(U"退出笔刷")); bar->add_child(stop); stop->connect("pressed",callable_mp(this,&ECSTilemapEditor::stop_brush));
    status=memnew(Label); status->set_text(String(U"选择带 TileMap2D 组件的实体，再选择瓦片。在 Scene 2D 中左键绘制、右键擦除，Esc 取消本笔。")); status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART); add_child(status);
    palette=memnew(ItemList); palette->set_v_size_flags(SIZE_EXPAND_FILL); palette->set_custom_minimum_size(Size2(300,140)); palette->set_icon_mode(ItemList::ICON_MODE_TOP); palette->set_fixed_icon_size(Size2i(48,48)); palette->set_max_columns(0); add_child(palette); palette->connect("item_selected",callable_mp(this,&ECSTilemapEditor::tile_selected));
    file=memnew(EditorFileDialog); add_child(file); file->set_access(EditorFileDialog::ACCESS_RESOURCES); file->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE); file->add_filter("*.png,*.jpg,*.webp",String(U"瓦片图集")); file->connect("file_selected",callable_mp(this,&ECSTilemapEditor::atlas_selected));
}
void ECSTilemapEditor::edit(const Ref<ECSScene> &value,int index,ECSUICanvasEditor *view) {
    bool changed=scene!=value || entity!=index; scene=value; entity=index; canvas=view;
    if(changed) { stop_brush(); }
    palette->clear(); if(scene.is_null() || entity<0 || entity>=scene->get_entities().size()) { return; }
    Dictionary definition=Dictionary(scene->get_entities()[entity]).get("tilemap_2d",Dictionary()); Ref<TileSet> tiles=definition.get("tile_set",Variant()); if(tiles.is_null()) { return; }
    for(int source=0;source<tiles->get_source_count();source++) {
        int id=tiles->get_source_id(source); Ref<TileSetAtlasSource> atlas=tiles->get_source(id); if(atlas.is_null()) { continue; }
        for(int i=0;i<atlas->get_tiles_count();i++) {
            Vector2i coords=atlas->get_tile_id(i); Ref<AtlasTexture> icon; icon.instantiate(); icon->set_atlas(atlas->get_texture()); icon->set_region(atlas->get_tile_texture_region(coords));
            for(int alternative=0;alternative<atlas->get_alternative_tiles_count(coords);alternative++) {
                int alternative_id=atlas->get_alternative_tile_id(coords,alternative); int item=palette->add_item(itos(id)+":"+itos(coords.x)+","+itos(coords.y)+" / "+itos(alternative_id),icon);
                palette->set_item_metadata(item,PackedInt32Array({id,coords.x,coords.y,alternative_id}));
            }
        }
    }
}
void ECSTilemapEditor::import_atlas() { if(scene.is_valid() && entity>=0 && Dictionary(scene->get_entities()[entity]).has("tilemap_2d")) { file->popup_centered_ratio(); } else { status->set_text(String(U"先给所选实体添加 TileMap2D 组件。")); } }
void ECSTilemapEditor::atlas_selected(const String &path) {
    if(scene.is_null() || entity<0 || entity>=scene->get_entities().size()) { return; }
    Ref<Texture2D> image=ResourceLoader::load(path); if(image.is_null()) { status->set_text(String(U"图片尚未导入或格式无效。")); return; }
    int size=tile_size->get_value(),columns=image->get_width()/size,rows=image->get_height()/size;
    if(columns<1 || rows<1 || int64_t(columns)*rows>4096) { status->set_text(String(U"瓦片尺寸不合适，或图集超过 4096 块。")); return; }
    Dictionary definition=Dictionary(scene->get_entities()[entity]).get("tilemap_2d",Dictionary());
    if(!PackedInt32Array(definition.get("cells",PackedInt32Array())).is_empty()) { status->set_text(String(U"已有地图不会被覆盖；请在新地图层创建图集，或直接指定已有 TileSet。")); return; }
    Ref<TileSet> tiles; tiles.instantiate(); tiles->set_tile_size(Size2i(size,size)); Ref<TileSetAtlasSource> atlas; atlas.instantiate(); atlas->set_texture(image); atlas->set_texture_region_size(Size2i(size,size)); tiles->add_source(atlas,0);
    if(collision->is_pressed()) { tiles->add_physics_layer(); tiles->set_physics_layer_collision_layer(0,1); }
    if(navigation->is_pressed()) { tiles->add_navigation_layer(); tiles->set_navigation_layer_layers(0,1); }
    PackedVector2Array square({Vector2(-size*.5,-size*.5),Vector2(size*.5,-size*.5),Vector2(size*.5,size*.5),Vector2(-size*.5,size*.5)});
    for(int y=0;y<rows;y++) { for(int x=0;x<columns;x++) {
        Vector2i coords(x,y); atlas->create_tile(coords); TileData *tile=atlas->get_tile_data(coords,0);
        if(collision->is_pressed()) { tile->add_collision_polygon(0); tile->set_collision_polygon_points(0,0,square); }
        if(navigation->is_pressed()) { Ref<NavigationPolygon> polygon; polygon.instantiate(); polygon->set_vertices(square); polygon->add_polygon(PackedInt32Array({0,1,2,3})); tile->set_navigation_polygon(0,polygon); }
    } }
    definition=definition.duplicate(true); definition["tile_set"]=tiles; emit_signal("tilemap_edited",entity,definition); status->set_text(String(U"选择瓦片后在 Scene 2D 中绘制；每笔可撤销，右键擦除。"));
}
void ECSTilemapEditor::tile_selected(int index) { if(!canvas || index<0 || index>=palette->get_item_count()) { return; } PackedInt32Array cell=palette->get_item_metadata(index); canvas->configure_tilemap_brush(cell[0],Vector2i(cell[1],cell[2]),cell[3]); }
void ECSTilemapEditor::stop_brush() { if(canvas) { canvas->configure_tilemap_brush(-1,Vector2i()); } }
#endif
