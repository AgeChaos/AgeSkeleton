// Native ECS tile layers. Tiles allocate server resources, never TileMapLayer nodes.
#include "ecs_world.h"
#include "ecs_scene.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"
#include "scene/2d/tile_map_layer.h"
#include "scene/resources/2d/tile_set.h"
#include "scene/resources/2d/convex_polygon_shape_2d.h"
#include "scene/resources/2d/navigation_polygon.h"
#include "scene/resources/image_texture.h"
#include "servers/physics_2d/physics_server_2d.h"
#include "servers/navigation_2d/navigation_server_2d.h"
#include "servers/rendering/rendering_server.h"

Dictionary ECSWorld::get_tilemap_2d(uint64_t id) const {
    ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,Dictionary());
    auto *state=is_alive(id)?tilemaps_2d.getptr(uint32_t(id)):nullptr; return state?state->definition.duplicate(true):Dictionary();
}
bool ECSWorld::set_tilemap_2d(uint64_t id,const Dictionary &definition) {
    ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
    if(!is_alive(id)) { return false; }
    Dictionary data=get_tilemap_2d(id); data.merge(definition,true);
    for(const Variant &key:data.keys()) { if(!PackedStringArray({"tile_set","cells","collision_enabled","navigation_enabled","z_index","chunk_size"}).has(key)) { return false; } }
    Variant raw=data.get("tile_set",Variant()); Ref<TileSet> tiles;
    if(raw.get_type()!=Variant::NIL) { if(raw.get_type()!=Variant::OBJECT) { return false; } tiles=raw; if(tiles.is_null()) { return false; } }
    Variant cells_value=data.get("cells",PackedInt32Array()),chunk_value=data.get("chunk_size",16),z=data.get("z_index",0),collision=data.get("collision_enabled",true),navigation=data.get("navigation_enabled",true);
    if(cells_value.get_type()!=Variant::PACKED_INT32_ARRAY || chunk_value.get_type()!=Variant::INT || int(chunk_value)<1 || int(chunk_value)>128 || z.get_type()!=Variant::INT || int(z)<-4096 || int(z)>4096 || collision.get_type()!=Variant::BOOL || navigation.get_type()!=Variant::BOOL) { return false; }
    PackedInt32Array cells=cells_value; if(cells.size()%6 || cells.size()>6*1000000 || (tiles.is_null() && !cells.is_empty())) { return false; }
    HashSet<Vector2i> occupied;
    for(int i=0;i<cells.size();i+=6) {
        Vector2i pos(cells[i],cells[i+1]),atlas_pos(cells[i+3],cells[i+4]);
        if(Math::abs(int64_t(pos.x))>1000000 || Math::abs(int64_t(pos.y))>1000000 || occupied.has(pos) || !tiles->has_source(cells[i+2])) { return false; }
        Ref<TileSetAtlasSource> atlas=tiles->get_source(cells[i+2]);
        if(atlas.is_null() || atlas->get_texture().is_null() || !atlas->has_tile(atlas_pos) || !atlas->has_alternative_tile(atlas_pos,cells[i+5])) { return false; }
        occupied.insert(pos);
    }
    data["tile_set"]=tiles; data["cells"]=cells; data["chunk_size"]=chunk_value; data["z_index"]=z; data["collision_enabled"]=collision; data["navigation_enabled"]=navigation;
    if(!tilemaps_2d.has(uint32_t(id))) { tilemaps_2d[uint32_t(id)]=TilemapState(); }
    TilemapState &state=tilemaps_2d[uint32_t(id)]; Ref<TileSet> old=state.definition.get("tile_set",Variant());
    Callable changed=callable_mp(this,&ECSWorld::tilemap_resource_changed).bind(id);
    if(old!=tiles) {
        if(old.is_valid() && old->is_connected("changed",changed)) { old->disconnect("changed",changed); }
        if(tiles.is_valid()) { tiles->connect("changed",changed); }
    }
    Dictionary before=state.definition.duplicate(); before.erase("cells"); Dictionary after=data.duplicate(); after.erase("cells");
    state.rebuild_all |= before!=after; state.definition=data; state.dirty=true; uint8_t marker=1; store_ui_column(uint32_t(id),"tilemap_2d",&marker); return true;
}
void ECSWorld::tilemap_resource_changed(uint64_t id) { if(is_alive(id) && tilemaps_2d.has(uint32_t(id))) { tilemaps_2d[uint32_t(id)].dirty=true; tilemaps_2d[uint32_t(id)].rebuild_all=true; } }
void ECSWorld::free_tilemap_chunk(TilemapChunk &chunk) {
    for(RID item:chunk.items) { RenderingServer::get_singleton()->free_rid(item); }
    for(RID body:chunk.bodies) { physics_entities_2d.erase(body); PhysicsServer2D::get_singleton()->free_rid(body); }
    for(RID region:chunk.regions) { NavigationServer2D::get_singleton()->free_rid(region); }
    chunk=TilemapChunk();
}
bool ECSWorld::remove_tilemap_2d(uint64_t id) {
    ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,false);
    auto *state=is_alive(id)?tilemaps_2d.getptr(uint32_t(id)):nullptr; if(!state) { return false; }
    Ref<TileSet> tiles=state->definition.get("tile_set",Variant()); Callable changed=callable_mp(this,&ECSWorld::tilemap_resource_changed).bind(id);
    if(tiles.is_valid() && tiles->is_connected("changed",changed)) { tiles->disconnect("changed",changed); }
    for(auto &entry:state->chunks) { free_tilemap_chunk(entry.value); }
    tilemaps_2d.erase(uint32_t(id)); remove_row(pools["tilemap_2d"],uint32_t(id)); return true;
}
void ECSWorld::clear_tilemaps_2d() {
    Vector<uint64_t> ids; for(const auto &entry:tilemaps_2d) { ids.push_back((uint64_t(slots[entry.key].generation)<<32)|entry.key); }
    for(uint64_t id:ids) { remove_tilemap_2d(id); }
    if(tilemap_canvas.is_valid()) { RenderingServer::get_singleton()->free_rid(tilemap_canvas); tilemap_canvas=RID(); tilemap_viewport=RID(); }
    if(tilemap_navigation_map.is_valid()) { NavigationServer2D::get_singleton()->free_rid(tilemap_navigation_map); tilemap_navigation_map=RID(); }
}
void ECSWorld::rebuild_tilemap(uint64_t id) {
    TilemapState &state=tilemaps_2d[uint32_t(id)];
    PackedInt32Array cells=state.definition["cells"]; int chunk_size=state.definition["chunk_size"];
    HashMap<Vector2i,PackedInt32Array> grouped; HashSet<Vector2i> retained;
    for(int i=0;i<cells.size();i+=6) { Vector2i coord(Math::floor(double(cells[i])/chunk_size),Math::floor(double(cells[i+1])/chunk_size)); if(!grouped.has(coord)) { grouped[coord]=PackedInt32Array(); } for(int j=0;j<6;j++) { grouped[coord].push_back(cells[i+j]); } }
    Vector<Vector2i> removed;
    for(auto &entry:state.chunks) {
        if(!state.rebuild_all && grouped.has(entry.key) && entry.value.cells==grouped[entry.key]) { retained.insert(entry.key); }
        else { free_tilemap_chunk(entry.value); removed.push_back(entry.key); }
    }
    for(const Vector2i &coord:removed) { state.chunks.erase(coord); }
    state.dirty=false; state.rebuild_all=false; state.submitted=false;
    Ref<TileSet> tiles=state.definition["tile_set"]; if(tiles.is_null()) { return; }
    auto *rs=RenderingServer::get_singleton(); auto *ps=PhysicsServer2D::get_singleton(); auto *ns=NavigationServer2D::get_singleton();
    bool collision=state.definition["collision_enabled"],navigation=state.definition["navigation_enabled"];
    if(collision && tiles->get_physics_layers_count()>0 && !physics_space_2d.is_valid()) {
        physics_space_2d=ps->space_create(); ps->space_set_active(physics_space_2d,true);
        ps->area_set_param(physics_space_2d,PhysicsServer2D::AREA_PARAM_GRAVITY,GLOBAL_GET("physics/2d/default_gravity"));
        ps->area_set_param(physics_space_2d,PhysicsServer2D::AREA_PARAM_GRAVITY_VECTOR,GLOBAL_GET("physics/2d/default_gravity_vector"));
    }
    if(navigation && tiles->get_navigation_layers_count()>0 && !tilemap_navigation_map.is_valid()) { tilemap_navigation_map=ns->map_create(); ns->map_set_active(tilemap_navigation_map,true); }
    HashMap<Vector2i,HashMap<String,RID>> batches;
    for(int i=0;i<cells.size();i+=6) {
        Vector2i coord(cells[i],cells[i+1]),atlas_pos(cells[i+3],cells[i+4]); int source=cells[i+2],alternative=cells[i+5];
        Vector2i chunk_coord(Math::floor(double(coord.x)/chunk_size),Math::floor(double(coord.y)/chunk_size)); if(retained.has(chunk_coord)) { continue; }
        // Resources may have been edited since the component was validated.
        if(!tiles->has_source(source)) { continue; } Ref<TileSetAtlasSource> atlas=tiles->get_source(source);
        if(atlas.is_null() || !atlas->has_tile(atlas_pos) || !atlas->has_alternative_tile(atlas_pos,alternative)) { continue; }
        TileData *tile=atlas->get_tile_data(atlas_pos,alternative); if(!tile) { continue; }
        Vector2 position=tiles->map_to_local(coord);
        if(!state.chunks.has(chunk_coord)) { state.chunks[chunk_coord]=TilemapChunk(); state.chunks[chunk_coord].cells=grouped[chunk_coord]; batches[chunk_coord]=HashMap<String,RID>(); }
        TilemapChunk &chunk=state.chunks[chunk_coord]; auto &groups=batches[chunk_coord]; Ref<Material> material=tile->get_material();
        String key=itos(material.is_valid()?uint64_t(material->get_instance_id()):0)+":"+itos(tile->get_z_index());
        if(!groups.has(key)) {
            RID item=rs->canvas_item_create(); rs->canvas_item_set_parent(item,tilemap_canvas); rs->canvas_item_set_z_index(item,CLAMP(int(state.definition["z_index"])+tile->get_z_index(),-4096,4096));
            if(material.is_valid()) { rs->canvas_item_set_material(item,material->get_rid()); }
            groups[key]=item; chunk.items.push_back(item);
        }
        TileMapLayer::draw_tile(groups[key],position,tiles,source,atlas_pos,alternative);
        bool flip_h=alternative&TileSetAtlasSource::TRANSFORM_FLIP_H,flip_v=alternative&TileSetAtlasSource::TRANSFORM_FLIP_V,transpose=alternative&TileSetAtlasSource::TRANSFORM_TRANSPOSE;
        if(collision) {
            if(chunk.bodies.is_empty()) {
                for(int layer=0;layer<tiles->get_physics_layers_count();layer++) {
                    RID body=ps->body_create(); ps->body_set_mode(body,PhysicsServer2D::BODY_MODE_STATIC); ps->body_set_collision_layer(body,tiles->get_physics_layer_collision_layer(layer)); ps->body_set_collision_mask(body,tiles->get_physics_layer_collision_mask(layer));
                    chunk.bodies.push_back(body); physics_entities_2d[body]=id;
                }
            }
            for(int layer=0;layer<chunk.bodies.size();layer++) {
                RID body=chunk.bodies[layer];
                for(int polygon=0;polygon<tile->get_collision_polygons_count(layer);polygon++) {
                    for(int n=0;n<tile->get_collision_polygon_shapes_count(layer,polygon);n++) {
                        Ref<ConvexPolygonShape2D> shape=tile->get_collision_polygon_shape(layer,polygon,n,flip_h,flip_v,transpose);
                        if(shape.is_null()) { continue; } int shape_index=ps->body_get_shape_count(body);
                        ps->body_add_shape(body,shape->get_rid(),Transform2D(0,position));
                        ps->body_set_shape_as_one_way_collision(body,shape_index,tile->is_collision_polygon_one_way(layer,polygon),tile->get_collision_polygon_one_way_margin(layer,polygon));
                    }
                }
            }
        }
        if(navigation) {
            for(int layer=0;layer<tiles->get_navigation_layers_count();layer++) {
                Ref<NavigationPolygon> polygon=tile->get_navigation_polygon(layer,flip_h,flip_v,transpose); if(polygon.is_null()) { continue; }
                RID region=ns->region_create(); ns->region_set_navigation_polygon(region,polygon); ns->region_set_navigation_layers(region,tiles->get_navigation_layer_layers(layer));
                chunk.regions.push_back(region); chunk.region_transforms.push_back(Transform2D(0,position));
            }
        }
    }
}
void ECSWorld::sync_tilemaps_2d(RID viewport) {
    ERR_FAIL_COND(Thread::get_caller_id()!=owner_thread);
    if(tilemaps_2d.is_empty()) { return; } auto *rs=RenderingServer::get_singleton();
    if(!tilemap_canvas.is_valid()) { tilemap_canvas=rs->canvas_create(); }
    if(tilemap_viewport!=viewport) { if(tilemap_viewport.is_valid()) { rs->viewport_remove_canvas(tilemap_viewport,tilemap_canvas); } rs->viewport_attach_canvas(viewport,tilemap_canvas); tilemap_viewport=viewport; }
    for(auto &entry:tilemaps_2d) {
        uint64_t id=(uint64_t(slots[entry.key].generation)<<32)|entry.key; if(entry.value.dirty) { rebuild_tilemap(id); }
        Transform2D transform=calculate_transform_2d(id); bool active=is_active_in_hierarchy(id) && !Math::is_zero_approx(transform.determinant());
        if(entry.value.submitted && entry.value.submitted_active==active && entry.value.submitted_transform==transform) { continue; }
        entry.value.submitted=true; entry.value.submitted_active=active; entry.value.submitted_transform=transform;
        for(auto &pair:entry.value.chunks) {
            for(RID item:pair.value.items) { rs->canvas_item_set_transform(item,transform); rs->canvas_item_set_visible(item,active); }
            for(RID body:pair.value.bodies) { PhysicsServer2D::get_singleton()->body_set_state(body,PhysicsServer2D::BODY_STATE_TRANSFORM,transform); PhysicsServer2D::get_singleton()->body_set_space(body,active?physics_space_2d:RID()); }
            for(int i=0;i<pair.value.regions.size();i++) { auto *ns=NavigationServer2D::get_singleton(); ns->region_set_transform(pair.value.regions[i],transform*pair.value.region_transforms[i]); ns->region_set_map(pair.value.regions[i],active?tilemap_navigation_map:RID()); }
        }
    }
}
Dictionary ECSWorld::tilemap_statistics() const {
    Dictionary result; int cells=0,chunks=0,items=0,bodies=0,regions=0;
    for(const auto &entry:tilemaps_2d) { cells+=PackedInt32Array(entry.value.definition["cells"]).size()/6; chunks+=entry.value.chunks.size(); for(const auto &chunk:entry.value.chunks) { items+=chunk.value.items.size(); bodies+=chunk.value.bodies.size(); regions+=chunk.value.regions.size(); } }
    result["layers"]=tilemaps_2d.size(); result["cells"]=cells; result["chunks"]=chunks; result["draw_items"]=items; result["physics_bodies"]=bodies; result["navigation_regions"]=regions; result["entity_nodes"]=0; return result;
}
PackedVector2Array ECSWorld::tilemap_navigation_path(Vector2 start,Vector2 end,uint32_t layers) const {
    ERR_FAIL_COND_V(Thread::get_caller_id()!=owner_thread,PackedVector2Array());
    if(!tilemap_navigation_map.is_valid() || !start.is_finite() || !end.is_finite() || NavigationServer2D::get_singleton()->map_get_iteration_id(tilemap_navigation_map)==0) { return PackedVector2Array(); }
    return NavigationServer2D::get_singleton()->map_get_path(tilemap_navigation_map,start,end,true,layers);
}

bool ECSWorld::tilemap_2d_self_test(const String &path) {
#define TILE_CHECK(c) ERR_FAIL_COND_V_MSG(!(c),false,"ECS TileMap check failed: " #c)
    Ref<TileSet> tiles; tiles.instantiate(); tiles->set_tile_size(Size2i(32,32));
    Ref<Image> image=Image::create_empty(64,32,false,Image::FORMAT_RGBA8); image->fill(Color(.18,.62,.32)); image->fill_rect(Rect2i(32,0,32,32),Color(.24,.38,.82));
    Ref<TileSetAtlasSource> atlas; atlas.instantiate(); atlas->set_texture(ImageTexture::create_from_image(image)); atlas->set_texture_region_size(Size2i(32,32)); tiles->add_source(atlas,0);
    tiles->add_physics_layer(); tiles->set_physics_layer_collision_layer(0,1); tiles->add_navigation_layer(); tiles->set_navigation_layer_layers(0,1);
    PackedVector2Array square({Vector2(-16,-16),Vector2(16,-16),Vector2(16,16),Vector2(-16,16)});
    for(int x=0;x<2;x++) { atlas->create_tile(Vector2i(x,0)); TileData *tile=atlas->get_tile_data(Vector2i(x,0),0); tile->add_collision_polygon(0); tile->set_collision_polygon_points(0,0,square); Ref<NavigationPolygon> polygon; polygon.instantiate(); polygon->set_vertices(square); polygon->add_polygon(PackedInt32Array({0,1,2,3})); tile->set_navigation_polygon(0,polygon); }
    Ref<ECSWorld> world; world.instantiate(); uint64_t parent=world->create_entity(),id=world->create_entity(); TILE_CHECK(world->set_parent(id,parent));
    Dictionary definition; definition["tile_set"]=tiles; PackedInt32Array cells;
    for(int y=0;y<4;y++) { for(int x=-1;x<18;x++) { cells.append_array(PackedInt32Array({x,y,0,(x+y)&1,0,0})); } }
    definition["cells"]=cells; TILE_CHECK(world->set_tilemap_2d(id,definition));
    Dictionary invalid=definition.duplicate(true); PackedInt32Array bad=cells; bad.append_array(PackedInt32Array({-1,0,0,0,0,0})); invalid["cells"]=bad; TILE_CHECK(!world->set_tilemap_2d(id,invalid));
    invalid["cells"]=PackedInt32Array({0,0,999,0,0,0}); TILE_CHECK(!world->set_tilemap_2d(id,invalid)); TILE_CHECK(PackedInt32Array(world->get_tilemap_2d(id)["cells"])==cells);
    auto *rs=RenderingServer::get_singleton(); RID viewport=rs->viewport_create(); rs->viewport_set_size(viewport,800,600); world->sync_tilemaps_2d(viewport);
    Dictionary stats=world->tilemap_statistics(); TILE_CHECK(int(stats["cells"])==76 && int(stats["chunks"])==3 && int(stats["draw_items"])==3 && int(stats["physics_bodies"])==3 && int(stats["navigation_regions"])==76);
    TILE_CHECK(world->set_vector(parent,"position",Vector3(80,40,0))); world->sync_tilemaps_2d(viewport);
    for(const auto &chunk:world->tilemaps_2d[uint32_t(id)].chunks) { for(RID body:chunk.value.bodies) { Transform2D t=PhysicsServer2D::get_singleton()->body_get_state(body,PhysicsServer2D::BODY_STATE_TRANSFORM); TILE_CHECK(t.get_origin().is_equal_approx(Vector2(80,40))); } }
    TILE_CHECK(world->set_active(parent,false)); world->sync_tilemaps_2d(viewport);
    for(const auto &chunk:world->tilemaps_2d[uint32_t(id)].chunks) { for(RID body:chunk.value.bodies) { TILE_CHECK(!PhysicsServer2D::get_singleton()->body_get_space(body).is_valid()); } }
    TILE_CHECK(world->set_active(parent,true)); world->sync_tilemaps_2d(viewport);
    NavigationServer2D::get_singleton()->map_set_use_async_iterations(world->tilemap_navigation_map,false);
    NavigationServer2D::get_singleton()->map_force_update(world->tilemap_navigation_map);
    TILE_CHECK(world->tilemap_navigation_path(Vector2(100,56),Vector2(550,56),1).size()>=2);
    Ref<ECSScene> scene; scene.instantiate(); TILE_CHECK(scene->capture(world)); TILE_CHECK(ResourceSaver::save(scene,path,ResourceSaver::FLAG_BUNDLE_RESOURCES)==OK);
    Ref<ECSScene> reload=ResourceLoader::load(path,"ECSScene",ResourceLoader::CACHE_MODE_IGNORE); TILE_CHECK(reload.is_valid() && reload->instantiate().is_valid());
    atlas->remove_tile(Vector2i(1,0)); world->sync_tilemaps_2d(viewport); TILE_CHECK(int(world->tilemap_statistics()["draw_items"])==3);
    TILE_CHECK(world->destroy_entity(id)); TILE_CHECK(int(world->tilemap_statistics()["layers"])==0); world.unref(); rs->free_rid(viewport);
    print_line("ECS_TILEMAP_PASS atlas chunks collision navigation parent_transform activation invalid_atomic save_reload resource_edit deletion"); return true;
#undef TILE_CHECK
}
