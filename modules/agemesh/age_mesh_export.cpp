// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "age_mesh_export.h"
#ifdef TOOLS_ENABLED
#include "core/object/class_db.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_plugin.h"

class AgeMeshExportPlugin : public EditorExportPlugin {
    GDCLASS(AgeMeshExportPlugin, EditorExportPlugin);
protected:
    static void _bind_methods() {}
    void _get_export_options(const Ref<EditorExportPlatform> &p_platform, List<EditorExportPlatform::ExportOption> *r_options) const override {
        r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::INT,
                "agemesh/mode", PROPERTY_HINT_ENUM, "Follow Project,Disabled,CPU Batches,GPU (Forward+)"), 0));
    }
    PackedStringArray _get_export_features(const Ref<EditorExportPlatform> &p_platform, bool p_debug) const override {
        PackedStringArray features;
        int mode = int(get_option("agemesh/mode"));
        if (mode == 1) { features.push_back("agemesh_export_disabled"); }
        if (mode == 2) { features.push_back("agemesh_export_cpu"); }
        if (mode == 3) { features.push_back("agemesh_export_gpu"); }
        return features;
    }
public:
    String get_name() const override { return "AgeMesh"; }
    bool supports_platform(const Ref<EditorExportPlatform> &p_platform) const override { return true; }
};

static Ref<AgeMeshExportPlugin> plugin;
void initialize_agemesh_export() {
    GDREGISTER_INTERNAL_CLASS(AgeMeshExportPlugin);
    plugin.instantiate();
    EditorExport::get_singleton()->add_export_plugin(plugin);
}
void uninitialize_agemesh_export() {
    if (EditorExport::get_singleton() && plugin.is_valid()) { EditorExport::get_singleton()->remove_export_plugin(plugin); }
    plugin.unref();
}
#endif
