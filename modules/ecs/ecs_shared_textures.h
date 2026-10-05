// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#ifdef TOOLS_ENABLED
#include "ecs_scene.h"
#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/portable_compressed_texture.h"

// Externalize texture pages without changing polygon UVs, atlas regions or the
// source scene. Readable page names are shared by content within the same folder.
class ECSSharedTextures {
	String directory;
	String atlas_name;
	HashMap<String, String> content_paths;
	HashMap<ObjectID, Ref<Texture2D>> replacements;
	Error error = OK;
	String message;

	static String image_key(const Ref<Image> &p_image) {
		PackedByteArray data = p_image->get_data();
		unsigned char digest[32];
		if (CryptoCore::sha256(data.ptr(), data.size(), digest) != OK) {
			return String();
		}
		return (itos(p_image->get_width()) + ":" + itos(p_image->get_height()) + ":" + itos(p_image->get_format()) + ":" + itos(p_image->has_mipmaps()) + ":" + String::hex_encode_buffer(digest, 32)).sha256_text();
	}
	Ref<Texture2D> texture(const Ref<Texture2D> &p_texture) {
		if (replacements.has(p_texture->get_instance_id())) {
			return replacements[p_texture->get_instance_id()];
		}
		Ref<AtlasTexture> atlas = p_texture;
		if (atlas.is_valid()) {
			Ref<AtlasTexture> copy = atlas->duplicate();
			copy->set_path(String());
			replacements[p_texture->get_instance_id()] = copy;
			if (atlas->get_atlas().is_valid()) {
				copy->set_atlas(texture(atlas->get_atlas()));
			}
			return copy;
		}
		Ref<Image> image = p_texture->get_image();
		if (image.is_null() || image->is_empty()) {
			error = ERR_INVALID_DATA; message = "Texture has no readable image"; return p_texture;
		}
		image = image->duplicate();
		if (image->is_compressed() && image->decompress() != OK) {
			error = ERR_INVALID_DATA; message = "Cannot decompress texture"; return p_texture;
		}
		image->convert(Image::FORMAT_RGBA8);
		String key = image_key(image);
		if (key.is_empty()) { error = ERR_CANT_CREATE; return p_texture; }
		String path;
		if (content_paths.has(key)) {
			path = content_paths[key];
		} else {
			path = directory.path_join(atlas_name + ".res");
			int suffix = 1;
			while (FileAccess::exists(path) || DirAccess::dir_exists_absolute(path)) {
				path = directory.path_join(atlas_name + vformat("_%02d.res", suffix++));
			}
		}
		Ref<Texture2D> shared;
		if (FileAccess::exists(path)) {
			shared = ResourceLoader::load(path);
			Ref<Image> existing = shared.is_valid() ? shared->get_image() : Ref<Image>();
			if (existing.is_null() || image_key(existing) != key) {
				error = ERR_FILE_CORRUPT; message = "Invalid shared texture: " + path; return p_texture;
			}
		} else {
			Ref<PortableCompressedTexture2D> compressed;
			compressed.instantiate();
			compressed->set_keep_compressed_buffer(!PortableCompressedTexture2D::is_keeping_all_compressed_buffers());
			compressed->create_from_image(image, PortableCompressedTexture2D::COMPRESSION_MODE_LOSSLESS);
			if (compressed->get_size() != image->get_size()) { error = ERR_CANT_CREATE; return p_texture; }
			compressed->set_path(ProjectSettings::get_singleton()->localize_path(path));
			error = ResourceSaver::save(compressed, path, ResourceSaver::FLAG_COMPRESS);
			if (error != OK) { message = "Cannot save shared texture: " + path; return p_texture; }
			shared = compressed;
		}
		content_paths[key] = path;
		replacements[p_texture->get_instance_id()] = shared;
		return shared;
	}
	Variant rewrite(const Variant &p_value, int p_depth = 0) {
		if (error != OK) { return p_value; }
		if (p_depth > 128) { error = ERR_INVALID_DATA; message = "Cyclic or too deeply nested data"; return p_value; }
		if (p_value.get_type() == Variant::ARRAY) {
			Array result = Array(p_value).duplicate();
			for (int i = 0; i < result.size(); i++) { result[i] = rewrite(result[i], p_depth + 1); }
			return result;
		}
		if (p_value.get_type() == Variant::DICTIONARY) {
			Dictionary result = Dictionary(p_value).duplicate();
			for (const Variant &key : result.keys()) { result[key] = rewrite(result[key], p_depth + 1); }
			return result;
		}
		if (p_value.get_type() == Variant::OBJECT) {
			Ref<Texture2D> value = p_value;
			if (value.is_valid()) { return texture(value); }
		}
		return p_value;
	}

public:
	static Error save(const Ref<ECSScene> &p_scene, const String &p_path, String &r_error) {
		ECSSharedTextures export_state;
		const String destination = ProjectSettings::get_singleton()->globalize_path(p_path);
		export_state.directory = destination.get_base_dir().path_join("SharedTextures");
		Error err = DirAccess::make_dir_recursive_absolute(export_state.directory);
		if (err != OK) { r_error = "Cannot create shared texture directory"; return err; }
		export_state.atlas_name = destination.get_file().get_basename().validate_filename() + "_SkinAtlas";
		// Do not rename legacy hash pages: older scenes can still reference them.
		// Index readable pages by actual pixels, not names or unverified metadata.
		Ref<DirAccess> pages = DirAccess::open(export_state.directory);
		if (pages.is_null()) { r_error = "Cannot read shared texture directory"; return ERR_CANT_OPEN; }
		PackedStringArray files = pages->get_files();
		files.sort();
		for (const String &file : files) {
			if (file.get_extension() != "res" || !file.get_basename().contains("_SkinAtlas")) continue;
			String path = export_state.directory.path_join(file);
			Ref<Texture2D> texture = ResourceLoader::load(path, "", ResourceLoader::CACHE_MODE_IGNORE);
			Ref<Image> image = texture.is_valid() ? texture->get_image() : Ref<Image>();
			if (image.is_null() || image->is_empty()) continue;
			image = image->duplicate();
			if (image->is_compressed() && image->decompress() != OK) continue;
			image->convert(Image::FORMAT_RGBA8);
			String key = image_key(image);
			if (!key.is_empty() && !export_state.content_paths.has(key)) export_state.content_paths[key] = path;
		}
		Ref<ECSScene> output = p_scene->duplicate();
		output->set_entities(export_state.rewrite(p_scene->get_entities()));
		if (export_state.error != OK) { r_error = export_state.message; return export_state.error; }
		// Do not BUNDLE_RESOURCES: that would embed the shared page again.
		// res:// paths are already portable within the destination project. Use
		// relative paths only when exporting outside the current project.
		const bool external = !ProjectSettings::get_singleton()->localize_path(destination).begins_with("res://");
		err = ResourceSaver::save(output, destination, ResourceSaver::FLAG_COMPRESS | (external ? ResourceSaver::FLAG_RELATIVE_PATHS : 0));
		if (err != OK) { r_error = "Cannot save scene: " + p_path; }
		return err;
	}
};
#endif
