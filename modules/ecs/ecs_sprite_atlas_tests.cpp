#ifdef TOOLS_ENABLED
#include "ecs_sprite_atlas.h"
#include "ecs_sprite_atlas_editor.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/resources/atlas_texture.h"

// Explicit opt-in, on a disposable test project: -- --sprite-atlas-self-test.
bool sprite_atlas_self_test() {
#define ATLAS_CHECK(condition) if (!(condition)) { ERR_PRINT("SpriteAtlas check failed: " #condition); return false; }
	const String root = "res://atlas_test";
	ATLAS_CHECK(DirAccess::make_dir_recursive_absolute(root) == OK);
	Ref<Image> red = Image::create_empty(40, 40, false, Image::FORMAT_RGBA8);
	red->fill(Color(0, 0, 0, 0)); red->fill_rect(Rect2i(7, 9, 24, 24), Color(1, 0, 0, 1));
	ATLAS_CHECK(red->save_png(root.path_join("red.png")) == OK);
	Ref<Image> green = Image::create_empty(24, 24, false, Image::FORMAT_RGBA8); green->fill(Color(0, 1, 0, 1));
	ATLAS_CHECK(green->save_png(root.path_join("green.png")) == OK);
	Ref<Image> blank = Image::create_empty(16, 12, false, Image::FORMAT_RGBA8); blank->fill(Color(0, 0, 0, 0));
	ATLAS_CHECK(blank->save_png(root.path_join("blank.png")) == OK);
	Ref<SpriteAtlas> atlas; atlas.instantiate();
	PackedStringArray sources; sources.push_back(root); sources.push_back(root.path_join("red.png")); atlas->set_sources(sources);
	Dictionary settings = atlas->get_settings(); settings["max_size"] = 32; atlas->set_settings(settings);
	ATLAS_CHECK(ResourceSaver::save(atlas, "res://TestAtlas.tres") == OK); atlas->set_path("res://TestAtlas.tres");
	SpriteAtlasBuild result;
	ATLAS_CHECK(SpriteAtlasBuilder::build(atlas, "default", result, true) == OK);
	ATLAS_CHECK(result.sprite_count == 3 && result.pages.size() == 3);
	Ref<AtlasTexture> packed = ResourceLoader::load(result.sprites[root.path_join("red.png")]);
	ATLAS_CHECK(packed.is_valid() && packed->get_size() == Vector2(40, 40));
	ATLAS_CHECK(packed->get_margin() == Rect2(7, 9, 16, 16));
	ATLAS_CHECK(packed->get_region().size == Vector2(24, 24));
	Ref<Image> page = result.previews[result.pages.find(result.sprite_pages[root.path_join("red.png")])];
	Vector2i position = packed->get_region().position;
	ATLAS_CHECK(page->get_pixel(position.x - 2, position.y - 2) == Color(1, 0, 0, 1));
	ATLAS_CHECK(!page->has_mipmaps());
	Ref<AtlasTexture> transparent = ResourceLoader::load(result.sprites[root.path_join("blank.png")]);
	ATLAS_CHECK(transparent->get_size() == Vector2(16, 12));
	Ref<Image> transparent_page = result.previews[result.pages.find(result.sprite_pages[root.path_join("blank.png")])];
	Vector2i transparent_origin = transparent->get_region().position;
	ATLAS_CHECK(transparent_page->get_pixel(transparent_origin.x, transparent_origin.y).a == 0);
	String old_page = result.pages[0], old_hash = FileAccess::get_md5(old_page);
	SpriteAtlasBuild repeated;
	ATLAS_CHECK(SpriteAtlasBuilder::build(atlas, "default", repeated) == OK);
	ATLAS_CHECK(repeated.pages == result.pages && FileAccess::get_md5(old_page) == old_hash);
	Dictionary overrides, web; web["max_size"] = 128; overrides["web"] = web; atlas->set_platform_overrides(overrides);
	ATLAS_CHECK(SpriteAtlasBuilder::build(atlas, "web", repeated) == OK && repeated.pages.size() == 1);
	ATLAS_CHECK(int(atlas->resolve_settings("default")["max_size"]) == 32);
	red->fill_rect(Rect2i(7, 9, 24, 24), Color(0, 0, 1, 1)); red->save_png(root.path_join("red.png"));
	ATLAS_CHECK(SpriteAtlasBuilder::build(atlas, "default", repeated) == OK && repeated.pages[0] != old_page);
	settings["max_size"] = 33; atlas->set_settings(settings);
	ATLAS_CHECK(SpriteAtlasBuilder::build(atlas, "default", repeated) != OK && !repeated.error.is_empty());
	settings["max_size"] = 32; settings["trim"] = false; atlas->set_settings(settings);
	ATLAS_CHECK(SpriteAtlasBuilder::build(atlas, "default", repeated) != OK);
	HashSet<String> features; features.insert("web"); features.insert("wechat");
	ATLAS_CHECK(SpriteAtlasExportPlugin::platform_name(features) == "wechat");
	// Leave a usable test atlas for export validation.
	settings["max_size"] = 64; settings["trim"] = true; atlas->set_settings(settings);
	ATLAS_CHECK(ResourceSaver::save(atlas, "res://TestAtlas.tres") == OK);
	return true;
#undef ATLAS_CHECK
}
#endif
