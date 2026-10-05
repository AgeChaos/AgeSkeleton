#pragma once
#ifdef TOOLS_ENABLED
#include "core/io/resource.h"
#include "scene/resources/texture.h"

// Authoring only. Exported games use ordinary AtlasTexture resources.
class SpriteAtlas : public Resource {
	GDCLASS(SpriteAtlas, Resource);
	PackedStringArray sources;
	Dictionary settings, platform_overrides;
	bool enabled = true;
protected:
	static void _bind_methods();
public:
	void set_sources(const PackedStringArray &p_value) { sources = p_value; emit_changed(); }
	PackedStringArray get_sources() const { return sources; }
	void set_settings(const Dictionary &p_value) { settings = p_value.duplicate(true); emit_changed(); }
	Dictionary get_settings() const { return settings.duplicate(true); }
	void set_platform_overrides(const Dictionary &p_value) { platform_overrides = p_value.duplicate(true); emit_changed(); }
	Dictionary get_platform_overrides() const { return platform_overrides.duplicate(true); }
	void set_enabled(bool p_value) { enabled = p_value; emit_changed(); }
	bool is_enabled() const { return enabled; }
	Dictionary resolve_settings(const String &p_platform) const;
	SpriteAtlas();
};

struct SpriteAtlasBuild {
	Dictionary sprites; // original res:// path -> generated AtlasTexture resource path
	Dictionary sprite_pages;
	Vector<String> pages;
	Vector<Ref<Image>> previews;
	String error;
	int sprite_count = 0;
	uint64_t pixel_bytes = 0;
};

struct SpriteAtlasImages {
	Vector<Ref<Image>> pages;
	Vector<Rect2i> regions;
	Vector<int> page_indices;
	String error;
};

class SpriteAtlasBuilder {
public:
	// In-memory sources use the same atlas resource workflow as file-backed sprites.
	static Error pack_images(const Vector<Ref<Image>> &p_images, SpriteAtlasImages &r_result, int p_limit = 2048, int p_padding = 2);
	static Error collect(const PackedStringArray &p_sources, Vector<String> &r_files, String &r_error);
	static Error build(const Ref<SpriteAtlas> &p_atlas, const String &p_platform, SpriteAtlasBuild &r_result, bool p_preview = false);
	static bool is_image(const String &p_path);
};
#endif
