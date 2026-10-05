// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "register_types.h"
#include "age_mesh.h"
#include "age_mesh_instance.h"
#include "age_mesh_gpu.h"
#include "age_mesh_page_set.h"
#include "age_mesh_streaming.h"
#include "age_mesh_page_pool.h"
#include "age_mesh_preparation.h"
#include "scene/main/scene_tree.h"
#include "age_mesh_export.h"
#include "core/object/callable_mp.h"
#include "servers/rendering/rendering_server.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/import/3d/resource_importer_scene.h"

class AgeMeshImporter : public EditorScenePostImportPlugin {
    GDCLASS(AgeMeshImporter, EditorScenePostImportPlugin);
    void convert(Node *p_node, Node *p_scene, int p_size, HashMap<Ref<Mesh>, Ref<AgeMeshData>> &p_cache) {
        for (int i = 0; i < p_node->get_child_count(); i++) { convert(p_node->get_child(i), p_scene, p_size, p_cache); }
        auto *instance = Object::cast_to<MeshInstance3D>(p_node);
        if (!instance) { return; }
        Ref<Mesh> mesh = instance->get_mesh();
        String reason = AgeMeshData::get_unsupported_reason(mesh);
        if (mesh.is_null() || mesh->get_surface_count() == 0) { return; }
        if (!reason.is_empty()) { print_verbose("AgeMesh native rendering for " + String(instance->get_name()) + ": " + reason); }
        Ref<AgeMeshData> data;
        if (p_cache.has(mesh)) { data = p_cache[mesh]; }
        else { data = AgeMeshData::build(mesh, p_size); p_cache[mesh] = data; }
        if (data.is_null()) { return; }
        auto *helper = memnew(AgeMeshInstance3D);
        helper->set_name("_AgeMesh");
        helper->set_data(data);
        instance->add_child(helper, true);
        helper->set_owner(p_scene);
        print_verbose("AGEMESH_IMPORT clusters=" + itos(data->get_cluster_count()) + " triangles=" + itos(data->get_source_triangles()));
    }
protected:
    static void _bind_methods() {}
public:
    void get_import_options(const String &p_path, List<ResourceImporter::ImportOption> *r_options) override {
        r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "meshes/agemesh/enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), false));
        r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "meshes/agemesh/triangles_per_cluster", PROPERTY_HINT_RANGE, "32,256,4"), 128));
    }
    Variant get_option_visibility(const String &p_path, const String &p_scene_import_type, const String &p_option, const HashMap<StringName, Variant> &p_options) const override {
        if (p_option.begins_with("meshes/agemesh/")) {
            if (p_scene_import_type != "PackedScene") { return false; }
            if (p_option.ends_with("triangles_per_cluster")) { const Variant *enabled = p_options.getptr("meshes/agemesh/enabled"); return enabled && bool(*enabled); }
        }
        return Variant();
    }
    void post_process(Node *p_scene, const HashMap<StringName, Variant> &p_options) override {
        const Variant *enabled = p_options.getptr("meshes/agemesh/enabled");
        if (!enabled || !bool(*enabled)) { return; }
        int size = p_options.has("meshes/agemesh/triangles_per_cluster") ? int(p_options["meshes/agemesh/triangles_per_cluster"]) : 128;
        HashMap<Ref<Mesh>, Ref<AgeMeshData>> cache;
        convert(p_scene, p_scene, size, cache);
    }
};
static Ref<AgeMeshImporter> importer;
#endif

void initialize_agemesh_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(AgeMeshData);
        GDREGISTER_CLASS(AgeMeshPageSet);
        GDREGISTER_CLASS(AgeMeshStreamingInstance3D);
        GDREGISTER_CLASS(AgeMeshInstance3D);
        SceneTree::add_idle_callback(AgeMeshPagePool::poll);
        SceneTree::add_idle_callback(AgeMeshPreparation::poll_retired);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/streaming_shared_page_memory_mb", PROPERTY_HINT_RANGE, "1,4096,1"), 1024);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/streaming_page_changes_per_frame", PROPERTY_HINT_RANGE, "16,1024,1"), 32);
        GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/agemesh/streaming_selection_budget_ms", PROPERTY_HINT_RANGE, "0.1,100,0.1"), 2.0);
        GLOBAL_DEF("rendering/agemesh/enabled", false);
        GLOBAL_DEF("rendering/agemesh/gpu_culling", false);
        GLOBAL_DEF("rendering/agemesh/dynamic_gpu_culling", false);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/dynamic_gpu_policy", PROPERTY_HINT_ENUM, "Conservative,Force"), 0);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/dynamic_gpu_alpha_min_triangles", PROPERTY_HINT_RANGE, "0,16777216,1"), 1048576);
        GLOBAL_DEF("rendering/agemesh/gpu_occlusion", false);
        GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/agemesh/gpu_lod_pixels", PROPERTY_HINT_RANGE, "0,16,0.1"), 0.0);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/gpu_working_memory_mb", PROPERTY_HINT_RANGE, "1,4096,1"), 256);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/upload_mb_per_frame", PROPERTY_HINT_RANGE, "1,64,1"), 4);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/clusters_per_batch", PROPERTY_HINT_RANGE, "1,64,1"), 8);
        GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/agemesh/prototype_cluster_limit", PROPERTY_HINT_RANGE, "1,65536,1"), 4096);
    }
#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        EditorNode::add_init_callback(initialize_agemesh_export);
        GDREGISTER_INTERNAL_CLASS(AgeMeshImporter);
        importer.instantiate();
        ResourceImporterScene::add_post_importer_plugin(importer);
    }
#endif
}
void uninitialize_agemesh_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        AgeMeshPagePool::shutdown();
        AgeMeshPreparation::shutdown_retired();
        RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&AgeMeshGpu::shutdown));
    }
#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        uninitialize_agemesh_export();
        ResourceImporterScene::remove_post_importer_plugin(importer);
        importer.unref();
    }
#endif
}
