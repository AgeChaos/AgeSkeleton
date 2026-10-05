#ifdef TOOLS_ENABLED
#include "ecs_sprite_atlas.h"
#include "core/object/class_db.h"
#include "core/math/math_funcs_binary.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image_loader.h"
#include "core/io/resource_saver.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/portable_compressed_texture.h"

SpriteAtlas::SpriteAtlas() {
	settings["max_size"] = 2048;
	settings["padding"] = 2;
	settings["trim"] = true;
	settings["compression"] = 0; // Lossless, lossy, Basis Universal.
	settings["quality"] = 0.9;
}
void SpriteAtlas::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sources", "sources"), &SpriteAtlas::set_sources);
	ClassDB::bind_method(D_METHOD("get_sources"), &SpriteAtlas::get_sources);
	ClassDB::bind_method(D_METHOD("set_settings", "settings"), &SpriteAtlas::set_settings);
	ClassDB::bind_method(D_METHOD("get_settings"), &SpriteAtlas::get_settings);
	ClassDB::bind_method(D_METHOD("set_platform_overrides", "overrides"), &SpriteAtlas::set_platform_overrides);
	ClassDB::bind_method(D_METHOD("get_platform_overrides"), &SpriteAtlas::get_platform_overrides);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &SpriteAtlas::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &SpriteAtlas::is_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "sources"), "set_sources", "get_sources");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "settings"), "set_settings", "get_settings");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "platform_overrides"), "set_platform_overrides", "get_platform_overrides");
}
Dictionary SpriteAtlas::resolve_settings(const String &p_platform) const {
	Dictionary result = settings.duplicate(true);
	if (platform_overrides.has(p_platform)) {
		Dictionary overrides = platform_overrides[p_platform];
		result.merge(overrides, true);
	}
	return result;
}
bool SpriteAtlasBuilder::is_image(const String &p_path) {
	String ext = p_path.get_extension().to_lower();
	return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "svg" || ext == "tga" || ext == "bmp";
}
static Error atlas_collect_path(const String &p_path, HashSet<String> &r_files, String &r_error, int p_depth = 0) {
	if (!p_path.begins_with("res://") || p_path.contains("..") || p_depth > 64) {
		r_error = "Invalid atlas source: " + p_path;
		return ERR_INVALID_PARAMETER;
	}
	if (DirAccess::dir_exists_absolute(p_path)) {
		Ref<DirAccess> dir = DirAccess::open(p_path);
		if (dir.is_null()) { r_error = "Cannot open " + p_path; return ERR_CANT_OPEN; }
		dir->list_dir_begin();
		for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
			if (name.begins_with(".") || dir->is_link(name)) { continue; }
			if (dir->current_is_dir() || SpriteAtlasBuilder::is_image(name)) {
				Error err = atlas_collect_path(p_path.path_join(name), r_files, r_error, p_depth + 1);
				if (err != OK) { return err; }
			}
		}
	} else if (FileAccess::exists(p_path) && SpriteAtlasBuilder::is_image(p_path)) {
		r_files.insert(p_path);
	} else {
		r_error = "Missing or unsupported atlas source: " + p_path;
		return ERR_FILE_NOT_FOUND;
	}
	return OK;
}
Error SpriteAtlasBuilder::collect(const PackedStringArray &p_sources, Vector<String> &r_files, String &r_error) {
	HashSet<String> unique;
	for (const String &source : p_sources) {
		Error err = atlas_collect_path(source.simplify_path(), unique, r_error);
		if (err != OK) { return err; }
	}
	for (const String &path : unique) { r_files.push_back(path); }
	r_files.sort();
	return OK;
}
Error SpriteAtlasBuilder::build(const Ref<SpriteAtlas> &p_atlas, const String &p_platform, SpriteAtlasBuild &r_result, bool p_preview) {
	r_result = SpriteAtlasBuild();
	ERR_FAIL_COND_V(p_atlas.is_null(), ERR_INVALID_PARAMETER);
	Dictionary settings = p_atlas->resolve_settings(p_platform);
	int limit = settings.get("max_size", 2048), padding = settings.get("padding", 2), compression = settings.get("compression", 0);
	double quality = settings.get("quality", 0.9);
	bool trim = settings.get("trim", true);
	if (limit < 32 || limit > 8192 || (limit & (limit - 1)) || padding < 1 || padding > 32 || compression < 0 || compression > 2 || quality < 0 || quality > 1) {
		r_result.error = "Invalid settings: max_size must be a power of two (32..8192), padding 1..32, compression 0..2, quality 0..1.";
		return ERR_INVALID_PARAMETER;
	}
	Vector<String> files;
	Error err = collect(p_atlas->get_sources(), files, r_result.error);
	if (err != OK) { return err; }
	struct Entry { String path; Ref<Image> image; Rect2i crop; Vector2i original, position; int page = -1; };
	Vector<Entry> entries;
	String fingerprint = "spriteatlas-v1|" + p_atlas->get_path() + "|" + String(Variant(settings));
	for (const String &path : files) {
		Entry entry;
		entry.path = path;
		entry.image.instantiate();
		err = ImageLoader::load_image(path, entry.image);
		if (err != OK || entry.image->is_empty()) { r_result.error = "Cannot decode " + path; return ERR_FILE_CORRUPT; }
		entry.image->convert(Image::FORMAT_RGBA8);
		entry.original = entry.image->get_size();
		entry.crop = trim ? entry.image->get_used_rect() : Rect2i(Vector2i(), entry.original);
		if (entry.crop.size.x == 0 || entry.crop.size.y == 0) { entry.crop = Rect2i(0, 0, 1, 1); }
		if (entry.crop.size.x + padding * 2 > limit || entry.crop.size.y + padding * 2 > limit) {
			r_result.error = "Image exceeds page size including padding: " + path;
			return ERR_PARAMETER_RANGE_ERROR;
		}
		fingerprint += "|" + path + ":" + FileAccess::get_md5(path);
		entries.push_back(entry);
	}
	// Stable descending-area packing; ties use source path so rebuilds are deterministic.
	struct EntrySort { bool operator()(const Entry &a, const Entry &b) const {
		int aa = a.crop.size.x * a.crop.size.y, ba = b.crop.size.x * b.crop.size.y;
		return aa != ba ? aa > ba : a.path < b.path;
	} };
	entries.sort_custom<EntrySort>();
	struct Page { Vector<Rect2i> free; Vector2i used; };
	Vector<Page> pages;
	for (Entry &entry : entries) {
		Vector2i size = entry.crop.size + Vector2i(padding * 2, padding * 2);
		int best_page = -1, best_rect = -1, score = INT_MAX;
		for (int p = 0; p < pages.size(); ++p) {
			for (int r = 0; r < pages[p].free.size(); ++r) {
				Vector2i available = pages[p].free[r].size;
				int waste = available.x * available.y - size.x * size.y;
				if (size.x <= available.x && size.y <= available.y && waste < score) { best_page = p; best_rect = r; score = waste; }
			}
		}
		if (best_page < 0) { Page page; page.free.push_back(Rect2i(0, 0, limit, limit)); pages.push_back(page); best_page = pages.size() - 1; best_rect = 0; }
		Page &page = pages.write[best_page];
		Rect2i space = page.free[best_rect];
		page.free.remove_at(best_rect);
		// Non-overlapping guillotine split; never rotates sprites (nine-patch safe).
		if (space.size.x > size.x) { page.free.push_back(Rect2i(space.position + Vector2i(size.x, 0), Vector2i(space.size.x - size.x, size.y))); }
		if (space.size.y > size.y) { page.free.push_back(Rect2i(space.position + Vector2i(0, size.y), Vector2i(space.size.x, space.size.y - size.y))); }
		entry.position = space.position + Vector2i(padding, padding);
		entry.page = best_page;
		page.used.x = MAX(page.used.x, space.position.x + size.x);
		page.used.y = MAX(page.used.y, space.position.y + size.y);
	}
	String directory = ProjectSettings::get_singleton()->get_project_data_path().path_join("sprite_atlases").path_join(fingerprint.md5_text());
	err = DirAccess::make_dir_recursive_absolute(directory);
	if (err != OK) { r_result.error = "Cannot create cache " + directory; return err; }
	for (int p = 0; p < pages.size(); ++p) {
		Vector2i size(Math::next_power_of_2(uint32_t(pages[p].used.x)), Math::next_power_of_2(uint32_t(pages[p].used.y)));
		Ref<Image> image = Image::create_empty(size.x, size.y, false, Image::FORMAT_RGBA8);
		image->fill(Color(0, 0, 0, 0));
		for (const Entry &entry : entries) {
			if (entry.page != p) { continue; }
			image->blit_rect(entry.image, entry.crop, entry.position);
			for (int y = -padding; y < entry.crop.size.y + padding; ++y) {
				for (int x = -padding; x < entry.crop.size.x + padding; ++x) {
					if (x >= 0 && y >= 0 && x < entry.crop.size.x && y < entry.crop.size.y) { continue; }
					image->set_pixel(entry.position.x + x, entry.position.y + y, entry.image->get_pixel(entry.crop.position.x + CLAMP(x, 0, entry.crop.size.x - 1), entry.crop.position.y + CLAMP(y, 0, entry.crop.size.y - 1)));
				}
			}
		}
		Ref<PortableCompressedTexture2D> texture;
		texture.instantiate();
		texture->set_keep_compressed_buffer(true);
		texture->create_from_image(image, PortableCompressedTexture2D::CompressionMode(compression), false, quality);
		if (texture->get_width() != size.x || texture->get_height() != size.y) { r_result.error = "Texture compression failed"; return ERR_CANT_CREATE; }
		String page_path = directory.path_join("page_" + itos(p) + ".res");
		err = ResourceSaver::save(texture, page_path);
		if (err != OK) { r_result.error = "Cannot save " + page_path; return err; }
		texture->set_path(page_path, true);
		r_result.pages.push_back(page_path);
		if (p_preview) { r_result.previews.push_back(image); }
		r_result.pixel_bytes += uint64_t(size.x) * size.y * 4;
		for (const Entry &entry : entries) {
			if (entry.page != p) { continue; }
			Ref<AtlasTexture> sprite;
			sprite.instantiate();
			sprite->set_atlas(texture);
			sprite->set_region(Rect2(entry.position, entry.crop.size));
			sprite->set_margin(Rect2(entry.crop.position, entry.original - entry.crop.size));
			sprite->set_filter_clip(true);
			String sprite_path = directory.path_join(entry.path.md5_text() + ".res");
			err = ResourceSaver::save(sprite, sprite_path);
			if (err != OK) { r_result.error = "Cannot save " + sprite_path; return err; }
			r_result.sprites[entry.path] = sprite_path;
			r_result.sprite_pages[entry.path] = page_path;
		}
	}
	r_result.sprite_count = entries.size();
	return OK;
}
#endif
