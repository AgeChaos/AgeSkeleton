#include "ecs_tool_adapters.h"
#include "ecs_ui_components.h"
#include "scene/resources/animation.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/3d/mesh_library.h"
#include "core/templates/hash_set.h"
#include "scene/resources/sprite_frames.h"
#include "scene/resources/theme.h"
#include "ecs_scene.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/box_shape_3d.h"

static const Basis _ortho_bases[24] = {
	Basis(1, 0, 0, 0, 1, 0, 0, 0, 1),
	Basis(0, -1, 0, 1, 0, 0, 0, 0, 1),
	Basis(-1, 0, 0, 0, -1, 0, 0, 0, 1),
	Basis(0, 1, 0, -1, 0, 0, 0, 0, 1),
	Basis(1, 0, 0, 0, 0, -1, 0, 1, 0),
	Basis(0, 0, 1, 1, 0, 0, 0, 1, 0),
	Basis(-1, 0, 0, 0, 0, 1, 0, 1, 0),
	Basis(0, 0, -1, -1, 0, 0, 0, 1, 0),
	Basis(1, 0, 0, 0, -1, 0, 0, 0, -1),
	Basis(0, 1, 0, 1, 0, 0, 0, 0, -1),
	Basis(-1, 0, 0, 0, 1, 0, 0, 0, -1),
	Basis(0, -1, 0, -1, 0, 0, 0, 0, -1),
	Basis(1, 0, 0, 0, 0, 1, 0, -1, 0),
	Basis(0, 0, -1, 1, 0, 0, 0, -1, 0),
	Basis(-1, 0, 0, 0, 0, -1, 0, -1, 0),
	Basis(0, 0, 1, -1, 0, 0, 0, -1, 0),
	Basis(0, 0, 1, 0, 1, 0, -1, 0, 0),
	Basis(0, -1, 0, 0, 0, 1, -1, 0, 0),
	Basis(0, 0, -1, 0, -1, 0, -1, 0, 0),
	Basis(0, 1, 0, 0, 0, -1, -1, 0, 0),
	Basis(0, 0, 1, 0, -1, 0, 1, 0, 0),
	Basis(0, 1, 0, 0, 0, 1, 1, 0, 0),
	Basis(0, 0, -1, 0, 1, 0, 1, 0, 0),
	Basis(0, -1, 0, 0, 0, -1, 1, 0, 0)
};

bool ECSToolAdapters::compile(Array &entities, String &error) {
	Array result=entities.duplicate(true);
	const int original_count=result.size();
	for(int index=0;index<original_count;index++) {
		if(result[index].get_type()!=Variant::DICTIONARY) { error="Entity must be a dictionary."; return false; }
		Dictionary entity=result[index];
		if(entity.has("sprite_frames")) {
			if(entity["sprite_frames"].get_type()!=Variant::DICTIONARY || entity.has("animation")) { error="SpriteFrames owns its animation component."; return false; }
			Dictionary settings=entity["sprite_frames"]; Ref<SpriteFrames> frames=settings.get("frames",Variant()); StringName name=settings.get("animation",StringName("default"));
			if(frames.is_null() || !frames->has_animation(name) || frames->get_frame_count(name)==0 || frames->get_frame_count(name)>65536 || frames->get_animation_speed(name)<=0) { error="SpriteFrames animation is empty or invalid."; return false; }
			String space=settings.get("space",String("ui")); if(space!="ui" && space!="world") { error="SpriteFrames space must be ui or world."; return false; }
			bool world_sprite=space=="world";
			if(world_sprite && (entity.has("polygon_2d") || ECSUIComponents::has_ui(entity))) { error="World sprite owns polygon rendering and cannot also be UI."; return false; }
			Ref<Animation> clip; clip.instantiate(); int track=clip->add_track(Animation::TYPE_VALUE); clip->track_set_path(track,NodePath(world_sprite?":polygon:frame":":ui:texture")); clip->value_track_set_update_mode(track,Animation::UPDATE_DISCRETE);
			double time=0, fps=frames->get_animation_speed(name);
			for(int frame=0;frame<frames->get_frame_count(name);frame++) {
				Ref<Texture2D> texture=frames->get_frame_texture(name,frame); double duration=frames->get_frame_duration(name,frame);
				if(texture.is_null() || !Math::is_finite(duration) || duration<=0 || !Math::is_finite(fps)) { error="Invalid sprite frame."; return false; }
				if(world_sprite) {
					Variant raw_offset=settings.get("offset",Vector2()),raw_centered=settings.get("centered",true),raw_flip_h=settings.get("flip_h",false),raw_flip_v=settings.get("flip_v",false);
					if(raw_offset.get_type()!=Variant::VECTOR2 || !Vector2(raw_offset).is_finite() || raw_centered.get_type()!=Variant::BOOL || raw_flip_h.get_type()!=Variant::BOOL || raw_flip_v.get_type()!=Variant::BOOL) { error="Invalid sprite offset or flags."; return false; }
					Vector2 size=texture->get_size(),origin=Vector2(raw_offset)-(bool(raw_centered)?size*.5:Vector2());
					Rect2 destination(origin,size),source(Vector2(),size); Ref<Texture2D> image=texture;
					while(Ref<AtlasTexture>(image).is_valid()) { Ref<AtlasTexture> atlas=image; Rect2 next_destination,next_source; if(!atlas->get_rect_region(destination,source,next_destination,next_source) || atlas->get_atlas().is_null()) { error="Invalid sprite atlas region."; return false; } destination=next_destination; source=next_source; image=atlas->get_atlas(); }
					if(bool(raw_flip_h)) { destination.position.x=origin.x+size.x-(destination.position.x-origin.x)-destination.size.x; }
					if(bool(raw_flip_v)) { destination.position.y=origin.y+size.y-(destination.position.y-origin.y)-destination.size.y; }
					origin=destination.position; size=destination.size;
					Dictionary frame_data; frame_data["polygon"]=PackedVector2Array({origin,origin+Vector2(size.x,0),origin+size,origin+Vector2(0,size.y)});
					PackedVector2Array uv({Vector2(),Vector2(1,0),Vector2(1,1),Vector2(0,1)}); for(int k=0;k<4;k++) { Vector2 point=uv[k]; if(bool(raw_flip_h)) { point.x=1-point.x; } if(bool(raw_flip_v)) { point.y=1-point.y; } uv.set(k,(source.position+point*source.size)/image->get_size()); }
					frame_data["uv"]=uv; frame_data["texture"]=image; frame_data["color"]=settings.get("modulate",Color(1,1,1,1)); frame_data["z_index"]=settings.get("z_index",0);
					clip->track_insert_key(track,time,frame_data); if(frame==0) { Dictionary polygon=frame_data.duplicate(); polygon["skeleton"]=-1; entity["polygon_2d"]=polygon; }
				} else { clip->track_insert_key(track,time,texture); } time+=duration/fps;
			}
			clip->set_length(time); clip->set_loop_mode(frames->get_animation_loop(name)?Animation::LOOP_LINEAR:Animation::LOOP_NONE);
			if(!world_sprite) {
			if(!entity.has("ui_layout")) { Dictionary layout=ECSUIComponents::defaults("ui_layout"); layout["rect"]=settings.get("rect",Rect2(Vector2(),frames->get_frame_texture(name,0)->get_size())); entity["ui_layout"]=layout; }
			Dictionary image=entity.get("ui_image",ECSUIComponents::defaults("ui_image")); image["texture"]=frames->get_frame_texture(name,0); image["background"]=Color(0,0,0,0); entity["ui_image"]=image;
			}
			Dictionary animation; animation["clip"]=clip; animation["targets"]=PackedInt64Array({index}); animation["playing"]=settings.get("playing",true); animation["speed"]=settings.get("speed",1.0); entity["animation"]=animation;
		}
		if(entity.has("theme")) {
			if(entity["theme"].get_type()!=Variant::DICTIONARY) { error="Theme settings must be a dictionary."; return false; }
			Dictionary settings=entity["theme"]; Ref<Theme> theme=settings.get("resource",Variant()); StringName type=settings.get("type",StringName("Button"));
			if(theme.is_null() || !ECSUIComponents::has_ui(entity)) { error="Theme requires a Theme resource and UI layout."; return false; }
			Dictionary ui; if(!ECSUIComponents::compose(entity,ui)) { error="Invalid themed UI."; return false; }
			Dictionary styles,colors,icons,constants; List<StringName> names; Vector<StringName> dependencies;
			theme->get_type_dependencies(type,type,dependencies);
			if(dependencies.is_empty()) { dependencies.push_back(type); }
			for(int d=dependencies.size()-1;d>=0;d--) {
				StringName dependency=dependencies[d]; names.clear();
				theme->get_stylebox_list(dependency,&names); for(const StringName &name:names) { styles[name]=theme->get_stylebox(name,dependency); }
				names.clear(); theme->get_color_list(dependency,&names); for(const StringName &name:names) { colors[name]=theme->get_color(name,dependency); }
				names.clear(); theme->get_icon_list(dependency,&names); for(const StringName &name:names) { icons[name]=theme->get_icon(name,dependency); }
				names.clear(); theme->get_constant_list(dependency,&names); for(const StringName &name:names) { constants[name]=theme->get_constant(name,dependency); }
				if(theme->has_font("font",dependency)) { ui["font"]=theme->get_font("font",dependency); }
				if(theme->has_font_size("font_size",dependency)) { ui["font_size"]=theme->get_font_size("font_size",dependency); }
			}
			ui["theme_styles"]=styles; ui["theme_colors"]=colors; ui["theme_icons"]=icons; ui["theme_constants"]=constants; if(constants.has("outline_size")) { ui["outline_size"]=CLAMP(int(constants["outline_size"]),0,64); }
			// Keep compiled runtime UI distinct from the authoring component schema.
			for(const String &key:{String("ui_layout"),String("ui_image"),String("ui_text"),String("ui_button"),String("ui_toggle"),String("ui_input_field"),String("ui_slider"),String("ui_progress"),String("ui_scroll")}) { entity.erase(key); }
			entity["ui"]=ui;
		}
		if(entity.has("gridmap_3d")) {
			if(entity["gridmap_3d"].get_type()!=Variant::DICTIONARY) { error="GridMap settings must be a dictionary."; return false; }
			Dictionary settings=entity["gridmap_3d"]; Ref<MeshLibrary> library=settings.get("library",Variant()); Vector3 size=settings.get("cell_size",Vector3(2,2,2)); Variant raw=settings.get("cells",Array());
			if(library.is_null() || !size.is_finite() || size.x<=0 || size.y<=0 || size.z<=0 || raw.get_type()!=Variant::ARRAY) { error="Invalid GridMap library or cell size."; return false; }
			Array cells=raw; if(cells.size()>100000 || result.size()+cells.size()>1000000) { error="GridMap cell budget exceeded."; return false; }
			HashSet<Vector3i> occupied;
			for(const Variant &value:cells) {
				if(value.get_type()!=Variant::DICTIONARY) { error="GridMap cell must be a dictionary."; return false; }
				Dictionary cell=value; Variant coordinate=cell.get("position",Variant()); int item=cell.get("item",-1),orientation=cell.get("orientation",0);
				if(coordinate.get_type()!=Variant::VECTOR3I || !library->has_item(item) || library->get_item_mesh(item).is_null() || orientation<0 || orientation>=24 || occupied.has(coordinate)) { error="Invalid or duplicate GridMap cell."; return false; }
				occupied.insert(coordinate); Basis rotation = _ortho_bases[orientation];
				Transform3D transform=Transform3D(rotation,Vector3(Vector3i(coordinate))*size)*library->get_item_mesh_transform(item);
				Dictionary child; child["name"]="Grid cell "+itos(item); child["parent"]=index; child["mesh"]=library->get_item_mesh(item); child["position"]=transform.origin; child["rotation"]=transform.basis.get_euler(); child["scale"]=transform.basis.get_scale(); result.push_back(child);
#ifndef PHYSICS_3D_DISABLED
				auto shapes=library->get_item_shapes(item);
				if(!shapes.is_empty()) {
					Dictionary body,physics; Array entries;
					for(const auto &shape:shapes) { Dictionary entry; entry["shape"]=shape.shape; entry["transform"]=shape.local_transform; entries.push_back(entry); }
					physics["shapes"]=entries; physics["mode"]="static"; body["physics"]=physics;
					body["parent"]=index; body["name"]="Grid collision"; body["position"]=Vector3(Vector3i(coordinate))*size; body["rotation"]=rotation.get_euler(); result.push_back(body);
				}
#endif
				Ref<NavigationMesh> nav=library->get_item_navigation_mesh(item);
				if(nav.is_valid()) {
					Dictionary region,settings; settings["mesh"]=nav; settings["layers"]=int64_t(library->get_item_navigation_layers(item)); region["navigation"]=settings;
					Transform3D nav_transform=Transform3D(rotation,Vector3(Vector3i(coordinate))*size)*library->get_item_navigation_mesh_transform(item);
					region["parent"]=index; region["name"]="Grid navigation"; region["position"]=nav_transform.origin; region["rotation"]=nav_transform.basis.get_euler(); region["scale"]=nav_transform.basis.get_scale(); result.push_back(region);
				}
				if(result.size()>1000000) { error="GridMap expansion budget exceeded."; return false; }
			}
		}
		result[index]=entity;
	}
	entities=result; return true;
}

bool ECSToolAdapters::test() {
	Array invalid; Dictionary entity,grid; grid["library"]=Variant(); entity["gridmap_3d"]=grid; invalid.push_back(entity); Array before=invalid.duplicate(true); String error;
	bool ok=!compile(invalid,error) && invalid==before;
	Ref<SpriteFrames> frames; frames.instantiate(); Ref<GradientTexture2D> first,second; first.instantiate(); second.instantiate(); frames->add_frame("default",first); frames->add_frame("default",second); frames->set_animation_speed("default",2);
	Dictionary sprite; sprite["frames"]=frames; entity.clear(); entity["sprite_frames"]=sprite;
	Ref<ECSScene> scene; scene.instantiate(); scene->set_entities(Array({entity})); Ref<ECSWorld> world=scene->instantiate();
	if(world.is_null()) { return false; } auto ids=world->query(PackedStringArray()); world->step(.6); ok &= Ref<Texture2D>(world->get_ui(ids[0])["texture"])==second;
	sprite["space"]="world"; sprite["flip_h"]=true; entity.clear(); entity["sprite_frames"]=sprite; entity["position"]=Vector3(20,30,0); scene->set_entities(Array({entity})); world=scene->instantiate(); if(world.is_null()) { return false; } ids=world->query(PackedStringArray()); world->step(.6);
	ok &= world->get_ui(ids[0]).is_empty() && Ref<Texture2D>(world->get_polygon_2d(ids[0])["texture"])==second && PackedVector2Array(world->get_polygon_2d(ids[0])["uv"])[0]==Vector2(1,0) && world->get_global_transform(ids[0]).origin==Vector3(20,30,0);
	Ref<MeshLibrary> library; library.instantiate(); library->create_item(7); Ref<BoxMesh> mesh; mesh.instantiate(); library->set_item_mesh(7,mesh); grid["library"]=library; Dictionary cell; cell["position"]=Vector3i(2,0,0); cell["item"]=7; grid["cells"]=Array({cell}); entity.clear(); entity["gridmap_3d"]=grid; scene->set_entities(Array({entity})); world=scene->instantiate();
	if(world.is_null()) { return false; } ids=world->query(PackedStringArray()); ok &= ids.size()==2 && world->get_mesh(ids[1])==mesh && world->get_global_transform(ids[1]).origin==Vector3(4,0,0);
	Ref<BoxShape3D> shape; shape.instantiate(); MeshLibrary::ShapeData shape_data; shape_data.shape=shape; shape_data.local_transform.origin=Vector3(0,1,0); Vector<MeshLibrary::ShapeData> shapes; shapes.push_back(shape_data); library->set_item_shapes(7,shapes);
	Ref<NavigationMesh> nav; nav.instantiate(); nav->set_vertices(PackedVector3Array({Vector3(),Vector3(1,0,0),Vector3(0,0,1)})); nav->add_polygon(PackedInt32Array({0,1,2})); library->set_item_navigation_mesh(7,nav); library->set_item_navigation_mesh_transform(7,Transform3D(Basis(),Vector3(0,2,0))); library->set_item_navigation_layers(7,3);
	world=scene->instantiate(); if(world.is_null()) { return false; } ids=world->query(PackedStringArray());
	ok &= ids.size()==4 && !world->get_physics(ids[2]).is_empty() && Ref<NavigationMesh>(world->get_navigation(ids[3]).get("mesh",Variant()))==nav && world->get_global_transform(ids[3]).origin==Vector3(4,2,0);
	Dictionary physics=world->get_physics(ids[2]); Array body_shapes=physics.get("shapes",Array()); ok &= body_shapes.size()==1 && Transform3D(Dictionary(body_shapes[0])["transform"]).origin==Vector3(0,1,0);
	world->set_active(ids[0],false); ok &= !world->is_active_in_hierarchy(ids[2]) && !world->is_active_in_hierarchy(ids[3]); world->set_active(ids[0],true); ok &= world->is_active_in_hierarchy(ids[3]);
	Ref<Theme> theme; theme.instantiate(); theme->set_color("font_color","Button",Color(.2,.3,.4)); theme->set_icon("icon","Button",first); theme->set_constant("h_separation","Button",12); Dictionary themed; themed["resource"]=theme; themed["type"]="Button"; entity=ECSUIComponents::preset("button"); entity["theme"]=themed; scene->set_entities(Array({entity})); world=scene->instantiate(); if(world.is_null()) { return false; } ids=world->query(PackedStringArray()); ok &= Color(Dictionary(world->get_ui(ids[0])["theme_colors"])["font_color"]).is_equal_approx(Color(.2,.3,.4));
	ok &= Ref<Texture2D>(Dictionary(world->get_ui(ids[0])["theme_icons"])["icon"])==first && int(Dictionary(world->get_ui(ids[0])["theme_constants"])["h_separation"])==12;
	return ok;
}
