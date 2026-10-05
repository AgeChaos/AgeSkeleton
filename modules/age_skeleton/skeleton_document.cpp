#include "skeleton_document.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_format_binary.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "scene/resources/image_texture.h"
#ifdef WINDOWS_ENABLED
#include <windows.h>
#else
#include <cstdio>
#endif

namespace SkeletonDocument {
namespace {
const char *IMAGE_KEY = "ageskeleton_image";
struct Codec {
    String directory, error;
    bool reading = false;
    HashMap<ObjectID, Ref<Resource>> resources;
    HashMap<String, Ref<Texture2D>> images;

    Variant convert(const Variant &value, String hint = "Image", int depth = 0) {
        if (!error.is_empty()) { return Variant(); }
        if (depth > 128) { error = TTR("Project nesting limit exceeded."); return Variant(); }
        if (value.get_type() == Variant::DICTIONARY) {
            Dictionary source = value, result = source.duplicate();
            hint = source.get("name", hint);
            for (const Variant &key : source.keys()) { result[key] = convert(source[key], hint, depth + 1); }
            return result;
        }
        if (value.get_type() == Variant::ARRAY) {
            Array source = value, result = source.duplicate();
            for (int i = 0; i < source.size(); ++i) { result[i] = convert(source[i], hint, depth + 1); }
            return result;
        }
        if (value.get_type() != Variant::OBJECT) { return value; }
        Ref<Resource> resource = value;
        if (resource.is_null()) { return value; }
        if (resources.has(resource->get_instance_id())) { return resources[resource->get_instance_id()]; }
        if (reading && resource->has_meta(IMAGE_KEY)) {
            String relative = resource->get_meta(IMAGE_KEY);
            if (!relative.begins_with("images/") || relative.contains("..") || relative.contains(":") || relative.contains("\\")) {
                error = TTR("Invalid image path: ") + relative; return Variant();
            }
            if (!images.has(relative)) {
                Ref<Image> image = Image::load_from_file(directory.path_join(relative));
                if (image.is_null() || image->is_empty()) { error = TTR("Missing or invalid image: ") + relative; return Variant(); }
                Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
                texture->set_meta(IMAGE_KEY, relative);
                images[relative] = texture;
            }
            resources[resource->get_instance_id()] = images[relative];
            return images[relative];
        }
        Ref<Texture2D> texture = resource;
        if (!reading && texture.is_valid()) {
            Ref<Image> image = texture->get_image();
            if (image.is_null() || image->is_empty()) { error = TTR("Cannot read image: ") + hint; return Variant(); }
            if (image->is_compressed() && image->decompress() != OK) { error = TTR("Cannot decompress image: ") + hint; return Variant(); }
            PackedByteArray bytes = image->save_png_to_buffer();
            if (bytes.is_empty()) { error = TTR("Cannot encode image: ") + hint; return Variant(); }
            String original = texture->get_meta(IMAGE_KEY, texture->get_path());
            String name = (original.is_empty() ? hint : original.get_file()).get_basename().validate_filename();
            if (name.is_empty()) { name = "Image"; }
            String relative = "images/" + name + ".png";
            int suffix = 2;
            while (FileAccess::exists(directory.path_join(relative)) && FileAccess::get_file_as_bytes(directory.path_join(relative)) != bytes) {
                relative = "images/" + name + "_" + itos(suffix++) + ".png";
            }
            if (!FileAccess::exists(directory.path_join(relative))) {
                Ref<FileAccess> file = FileAccess::open(directory.path_join(relative), FileAccess::WRITE);
                if (file.is_null()) { error = TTR("Cannot write image: ") + relative; return Variant(); }
                file->store_buffer(bytes); file->flush();
                if (file->get_error() != OK) { error = TTR("Cannot write image: ") + relative; return Variant(); }
            }
            Ref<ImageTexture> reference = ImageTexture::create_from_image(Image::create_empty(1, 1, false, Image::FORMAT_RGBA8));
            reference->set_meta(IMAGE_KEY, relative);
            resources[resource->get_instance_id()] = reference;
            return reference;
        }
        Ref<Resource> copy = resource->duplicate(false);
        if (copy.is_null()) { error = TTR("Cannot copy project resource."); return Variant(); }
        resources[resource->get_instance_id()] = copy;
        List<PropertyInfo> properties; resource->get_property_list(&properties);
        for (const PropertyInfo &property : properties) {
            if ((property.usage & PROPERTY_USAGE_STORAGE) && property.name != "resource_path") {
                copy->set(property.name, convert(resource->get(property.name), hint, depth + 1));
            }
        }
        return copy;
    }
};

Error replace_file(const String &from, const String &to) {
#ifdef WINDOWS_ENABLED
    return MoveFileExW((LPCWSTR)from.utf16().get_data(), (LPCWSTR)to.utf16().get_data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? OK : ERR_FILE_CANT_WRITE;
#else
    return ::rename(from.utf8().get_data(), to.utf8().get_data()) == 0 ? OK : ERR_FILE_CANT_WRITE;
#endif
}
}

Error save(const Ref<ECSScene> &scene, const String &path, String &error) {
    error = String();
    if (scene.is_null() || path.get_extension().to_lower() != "ageskeleton") { error = TTR("Expected an .ageskeleton project."); return ERR_INVALID_PARAMETER; }
    String absolute = ProjectSettings::get_singleton()->globalize_path(path).simplify_path();
    Codec codec; codec.directory = absolute.get_base_dir();
    Error result = DirAccess::make_dir_recursive_absolute(codec.directory.path_join("images"));
    if (result != OK) { error = TTR("Cannot create images directory."); return result; }
    Ref<ECSScene> snapshot = codec.convert(scene);
    if (!codec.error.is_empty()) { error = codec.error; return ERR_FILE_CANT_WRITE; }
    snapshot->set_meta("ageskeleton_version", 1);
    String temporary = absolute + "." + itos(OS::get_singleton()->get_process_id()) + ".tmp";
    ResourceFormatSaverBinaryInstance saver;
    result = saver.save(temporary, snapshot, ResourceSaver::FLAG_BUNDLE_RESOURCES | ResourceSaver::FLAG_COMPRESS);
    if (result == OK) { result = replace_file(temporary, absolute); }
    if (result != OK) { DirAccess::remove_absolute(temporary); error = TTR("Could not save project; the previous project file was kept."); }
    return result;
}

Ref<ECSScene> load(const String &path, String &error) {
    error = String();
    String absolute = ProjectSettings::get_singleton()->globalize_path(path).simplify_path();
    Ref<ResourceFormatLoaderBinary> loader; loader.instantiate();
    Error result;
    Ref<ECSScene> stored = loader->load(absolute, absolute, &result, false, nullptr, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
    if (result != OK || stored.is_null() || int(stored->get_meta("ageskeleton_version", 0)) != 1) { error = TTR("Invalid or unsupported AgeSkeleton project."); return Ref<ECSScene>(); }
    Codec codec; codec.directory = absolute.get_base_dir(); codec.reading = true;
    Ref<ECSScene> scene = codec.convert(stored);
    if (!codec.error.is_empty()) { error = codec.error; return Ref<ECSScene>(); }
    if (scene.is_null() || scene->instantiate().is_null()) { error = TTR("Invalid skeletal project data."); return Ref<ECSScene>(); }
    scene->set_path(absolute, true);
    return scene;
}
}
